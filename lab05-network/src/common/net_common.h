// Формат кадра TCP: [Length: 4 байта, uint32, сетевой порядок][Payload: Length байт]
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

#pragma comment(lib, "Ws2_32.lib")

namespace net {

constexpr uint32_t kMaxMessage = 65536;

enum RecvResult {
    RECV_OK = 1,
    RECV_CLOSED = 0, // соединение закрыто собеседником
    RECV_ERROR = -1,   // ошибка Winsock (код в WSAGetLastError)
    RECV_TIMEOUT = -2, // истекло время ожидания данных
    RECV_STOPPED = -3, // запрошено завершение работы
    RECV_BADLEN = -4 // некорректная длина в заголовке кадра
};

// ошибки
inline std::string wsa_text(int code) {
    char* buf = nullptr;
    DWORD n = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, (DWORD)code, 0, (LPSTR)&buf, 0, nullptr);
    std::string s = (n && buf) ? std::string(buf, n) : std::string("unknown error");
    if (buf) LocalFree(buf);

    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();

    return s;
}

// время
inline double now_us() {  // QueryPerformanceCounter, микросекунды
    static const LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1e6 / (double)freq.QuadPart;
}

// журнал
inline std::mutex& log_mutex() { static std::mutex m; return m; }

inline void log(const char* role, const char* fmt, ...) {
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);

    SYSTEMTIME st;
    GetLocalTime(&st);
    std::lock_guard<std::mutex> lk(log_mutex());
    printf("[%02d:%02d:%02d.%03d] %s %s\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, role, msg);
    fflush(stdout);
}

inline void log_wsa(const char* role, const char* func, int code) {
    log(role, "ERROR func=%s WSAGetLastError=%d (%s)", func, code, wsa_text(code).c_str());
}

// аргументы командной строки
inline const char* arg_value(int argc, char** argv, const char* name, const char* def = nullptr) {
    for (int i = 1; i < argc - 1; ++i)
        if (std::strcmp(argv[i], name) == 0) return argv[i + 1];
    return def;
}
inline bool has_flag(int argc, char** argv, const char* name) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], name) == 0) return true;
    return false;
}

// ввод-вывод
// Отправка всех байтов
inline bool send_all(SOCKET s, const char* data, int len) {
    int sent = 0;
    while (sent < len) {
        int n = send(s, data + sent, len - sent, 0);
        if (n == SOCKET_ERROR) return false;
        sent += n;
    }

    return true;
}



inline bool send_message(SOCKET s, const std::string& payload) {
    uint32_t len = (uint32_t)payload.size();
    std::string frame;
    frame.resize(4 + payload.size());
    frame[0] = (char)((len >> 24) & 0xFF);
    frame[1] = (char)((len >> 16) & 0xFF);
    frame[2] = (char)((len >> 8) & 0xFF);
    frame[3] = (char)(len & 0xFF);
    std::memcpy(&frame[4], payload.data(), payload.size());
    return send_all(s, frame.data(), (int)frame.size());
}

// Прием ровно len байт Ожидание select() шаг опроса 200 мс
inline int recv_all(SOCKET s, char* buf, int len, int idle_ms, const std::atomic<bool>* stop) {
    const int poll_ms = 200;
    int got = 0, idle = 0;
    while (got < len) {
        if (stop && stop->load()) return RECV_STOPPED;
        fd_set rs;
        FD_ZERO(&rs);
        FD_SET(s, &rs);
        timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = poll_ms * 1000;
        int r = select(0, &rs, nullptr, nullptr, &tv);
        if (r == SOCKET_ERROR) return RECV_ERROR;
        if (r == 0) {
            idle += poll_ms;
            if (idle >= idle_ms) return RECV_TIMEOUT;
            continue;
        }
        int n = recv(s, buf + got, len - got, 0);
        if (n > 0) { got += n; idle = 0; }
        else if (n == 0) return RECV_CLOSED;
        else return RECV_ERROR;
    }
    return RECV_OK;
}


inline int recv_message(SOCKET s, std::string& out, int idle_ms, const std::atomic<bool>* stop) {
    unsigned char h[4];
    int r = recv_all(s, (char*)h, 4, idle_ms, stop);
    if (r != RECV_OK) return r;
    uint32_t len = ((uint32_t)h[0] << 24) | ((uint32_t)h[1] << 16) | ((uint32_t)h[2] << 8) | (uint32_t)h[3];
    if (len == 0 || len > kMaxMessage) return RECV_BADLEN;
    out.resize(len);
    return recv_all(s, &out[0], (int)len, idle_ms, stop);
}


} 