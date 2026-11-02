#include "../common/net_common.h"

#include <iostream>
#include <string>

using namespace net;

static const char* ROLE = "CLIENT";

struct Resp {
    bool got = false; // получен корректный ответ
    bool ok = false;   // статус OK
    int code = 0;
    double server_us = 0; // время обработки на сервере
    double rtt_us = 0;  // полное время запрос-ответ на клиенте
    size_t sent = 0, recvd = 0;
    std::string body;
    int fail = 0; // 0 - нет, 1 - тайм-аут, 2 - ошибка/обрыв/протокол
};

static SOCKET connect_to(const char* host, int port, int timeout_ms, double& connect_us) {
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons((u_short)port);
    if (inet_pton(AF_INET, host, &a.sin_addr) != 1) {
        log(ROLE, "ERROR bad host '%s'", host);

        return INVALID_SOCKET;
    }

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        log_wsa(ROLE, "socket", WSAGetLastError());

        return INVALID_SOCKET;
    }

    double t0 = now_us();
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
    if (connect(s, (sockaddr*)&a, sizeof a) == SOCKET_ERROR) {
        int e = WSAGetLastError();
        if (e != WSAEWOULDBLOCK) {
            log_wsa(ROLE, "connect", e);
            closesocket(s);
            return INVALID_SOCKET;
        }

        fd_set w, x;
        FD_ZERO(&w); FD_ZERO(&x);
        FD_SET(s, &w); FD_SET(s, &x);
        timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        int sr = select(0, nullptr, &w, &x, &tv);
        if (sr == 0) {
            log(ROLE, "CONNECT_TIMEOUT after %dms", timeout_ms);
            closesocket(s);

            return INVALID_SOCKET;
        }
        if (sr == SOCKET_ERROR) {
            log_wsa(ROLE, "select(connect)", WSAGetLastError());
            closesocket(s);

            return INVALID_SOCKET;
        }
        if (FD_ISSET(s, &x)) {
            int err = 0, len = sizeof err;
            getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&err, &len);
            log_wsa(ROLE, "connect", err);  // WSAECONNREFUSED - сервер недоступен
            closesocket(s);

            return INVALID_SOCKET;
        }
    }

    nb = 0;
    ioctlsocket(s, FIONBIO, &nb);
    BOOL nd = TRUE;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&nd, sizeof nd);

    connect_us = now_us() - t0;

    return s;
}

// Разбор "V1 <id> <OK|ERR> <code> <server_us>\n<body>"
static bool parse_response(const std::string& m, unsigned long long expect, Resp& r) {
    size_t nl = m.find('\n');
    std::string head = m.substr(0, nl == std::string::npos ? m.size() : nl);
    unsigned long long id = 0;
    char st[8] = {0};
    int code = 0;
    double us = 0;
    if (sscanf(head.c_str(), "V1 %llu %7s %d %lf", &id, st, &code, &us) != 4) return false;

    if (id != expect && !(id == 0 && code == 413)) return false;  // id==0: ответ на некорректный кадр

    r.ok = (std::string(st) == "OK");
    r.code = code;
    r.server_us = us;
    r.body = (nl == std::string::npos) ? "" : m.substr(nl + 1);

    return true;
}

int main(int argc, char** argv) {
    const char* host = arg_value(argc, argv, "--host", "127.0.0.1");
    int port = atoi(arg_value(argc, argv, "--port", "20000"));
    const char* id = arg_value(argc, argv, "--id", "1");
    int timeout_ms = atoi(arg_value(argc, argv, "--timeout", "5000"));
    int count = atoi(arg_value(argc, argv, "--count", "1"));
    const char* cmd = arg_value(argc, argv, "--cmd", nullptr);
    const char* csv = arg_value(argc, argv, "--csv", nullptr);
    const char* label = arg_value(argc, argv, "--label", "-");
    bool quiet = has_flag(argc, argv, "--quiet");

    if (port < 1 || port > 65535 || timeout_ms < 1 || count < 1) {
        fprintf(stderr, "usage: client --host <ip> --port <p> --id <n> [--cmd \"CMD\" --count N] [--timeout ms] "
                        "[--quiet] [--csv file] [--label name]\n");
        return 2;
    }

    WSADATA wsa;
    int rc = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (rc != 0) {
        log_wsa(ROLE, "WSAStartup", rc);

        return 2;
    }

    double t_start = now_us();
    double connect_us = 0;
    SOCKET s = connect_to(host, port, timeout_ms, connect_us);
    if (s == INVALID_SOCKET) {
        log(ROLE, "SERVER_UNAVAILABLE host=%s port=%d", host, port);
        WSACleanup();

        return 2;
    }
    log(ROLE, "CONNECTED id=%s host=%s port=%d connect=%.3fms", id, host, port, connect_us / 1000.0);

    unsigned long long next_id = 1;
    unsigned long long n_sent = 0, n_recv = 0, n_ok = 0, n_err = 0, n_timeout = 0;
    unsigned long long bytes_out = 0, bytes_in = 0;
    double sum_rtt = 0, sum_srv = 0;
    bool broken = false;

    auto do_request = [&](const std::string& command) {
        Resp r;
        unsigned long long rid = next_id++;
        std::string payload = "V1 " + std::to_string(rid) + " " + id + " " + command;
        double t0 = now_us();
        ++n_sent;
        if (!send_message(s, payload)) {
            log_wsa(ROLE, "send", WSAGetLastError());
            ++n_err;
            broken = true;
            return;
        }
        bytes_out += 4 + payload.size();
        std::string msg;
        int rr = recv_message(s, msg, timeout_ms, nullptr);
        r.rtt_us = now_us() - t0;
        if (rr == RECV_TIMEOUT) {
            log(ROLE, "TIMEOUT id=%s requestId=%llu after %dms", id, rid, timeout_ms);
            ++n_timeout;
            broken = true;  // поздний ответ
            return;
        }
        if (rr == RECV_CLOSED) {
            log(ROLE, "SERVER_CLOSED id=%s requestId=%llu", id, rid);
            ++n_err;
            broken = true;
            return;
        }
        if (rr != RECV_OK) {
            if (rr == RECV_ERROR) log_wsa(ROLE, "recv", WSAGetLastError());
            else log(ROLE, "PROTOCOL_ERROR id=%s requestId=%llu recv=%d", id, rid, rr);
            ++n_err;
            broken = true;
            return;
        }
        bytes_in += 4 + msg.size();

        if (!parse_response(msg, rid, r)) {
            log(ROLE, "PROTOCOL_ERROR id=%s requestId=%llu malformed response", id, rid);
            ++n_err;
            broken = true;
            return;
        }
        ++n_recv;
        sum_rtt += r.rtt_us;
        sum_srv += r.server_us;
        if (r.ok) ++n_ok; else ++n_err;

        if (!quiet) {
            printf("REQ %llu %s %d rtt=%.3fms server=%.0fus resp=%zuB\n", rid, r.ok ? "OK" : "ERR", r.code,
                   r.rtt_us / 1000.0, r.server_us, msg.size() + 4);
            fwrite(r.body.data(), 1, r.body.size(), stdout);
            if (!r.body.empty() && r.body.back() != '\n') printf("\n");
            fflush(stdout);
        }
    };

    if (cmd) {
        for (int i = 0; i < count && !broken; ++i) do_request(cmd);
    } else {
        std::string line;
        printf("Type commands (HELP, LIST, SIZE f, COUNT_LINES f, READ_FIRST f n), QUIT to exit.\n");
        while (!broken && std::getline(std::cin, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (line.empty()) continue;
            if (line == "QUIT" || line == "quit" || line == "EXIT" || line == "exit") break;
            do_request(line);
        }
    }

    shutdown(s, SD_SEND);
    closesocket(s);
    double total_us = now_us() - t_start;
    double avg_rtt = n_recv ? sum_rtt / n_recv / 1000.0 : 0;
    double avg_srv = n_recv ? sum_srv / n_recv / 1000.0 : 0;

    log(ROLE, "SUMMARY id=%s sent=%llu received=%llu ok=%llu err=%llu timeouts=%llu connect=%.3fms total=%.3fms "
              "avg_rtt=%.3fms avg_server=%.3fms bytes_out=%llu bytes_in=%llu",
        id, n_sent, n_recv, n_ok, n_err, n_timeout, connect_us / 1000.0, total_us / 1000.0, avg_rtt, avg_srv,
        bytes_out, bytes_in);

    if (csv) {
        FILE* f = fopen(csv, "a");
        if (f) {
            fprintf(f, "%s,%s,\"%s\",%llu,%llu,%llu,%llu,%llu,%.3f,%.3f,%.3f,%.3f,%llu,%llu\n", label, id,
                    cmd ? cmd : "interactive", n_sent, n_recv, n_ok, n_err, n_timeout, connect_us / 1000.0,
                    total_us / 1000.0, avg_rtt, avg_srv, bytes_out, bytes_in);
            fclose(f);
        } else {
            log(ROLE, "ERROR cannot open csv '%s'", csv);
        }
    }

    WSACleanup();
    return (n_timeout || broken) ? 1 : 0;
}
