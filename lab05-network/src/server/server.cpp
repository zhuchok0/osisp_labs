#include "../common/net_common.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using namespace net;

static const char* ROLE = "SERVER";
static std::atomic<bool> g_stop{false};
static std::atomic<int> g_active{0};
static fs::path g_root; 
static int g_idle_ms = 30000; // тайм-аут простоя клиента
static int g_delay_ms = 0;  // тестовая задержка ответа

static const size_t kMaxBody = 60000;
static const unsigned long long kMaxLines = 1000;
static const int kMaxClients = 64;

struct Reply {
    bool ok;
    int code;
    std::string body;
};
static Reply ok_reply(std::string body) { return Reply{true, 200, std::move(body)}; }
static Reply err_reply(int code, std::string msg) { return Reply{false, code, std::move(msg)}; }

static bool is_reserved(std::string n) {
    size_t dot = n.find('.');
    if (dot != std::string::npos) n.resize(dot);
    for (auto& c : n) c = (char)std::toupper((unsigned char)c);
    if (n == "CON" || n == "PRN" || n == "AUX" || n == "NUL") return true;
    if (n.size() == 4 && (n.compare(0, 3, "COM") == 0 || n.compare(0, 3, "LPT") == 0) && n[3] >= '1' && n[3] <= '9')
        return true;
    return false;
}

static bool valid_name(const std::string& n) {
    if (n.empty() || n.size() > 100) return false;
    for (unsigned char c : n)
        if (!(std::isalnum(c) || c == '.' || c == '_' || c == '-')) return false;
    if (n.front() == '.' || n.back() == '.') return false;  // ".", "..", скрытые, завершающая точка
    if (n.find("..") != std::string::npos) return false;
    return !is_reserved(n);
}

static bool resolve(const std::string& name, fs::path& out, Reply& e) {
    if (!valid_name(name)) { e = err_reply(403, "BAD_PATH invalid file name"); return false; }
    std::error_code ec;
    fs::path p = g_root / name;
    fs::file_status st = fs::symlink_status(p, ec);
    if (ec || st.type() == fs::file_type::not_found) { e = err_reply(404, "NOT_FOUND"); return false; }
    if (fs::is_symlink(st)) { e = err_reply(403, "BAD_PATH symlink not allowed"); return false; }
    fs::path c = fs::canonical(p, ec);
    if (ec) { e = err_reply(404, "NOT_FOUND"); return false; }
    bool inside = fs::equivalent(c.parent_path(), g_root, ec);
    if (ec || !inside) { e = err_reply(403, "BAD_PATH outside sandbox"); return false; }
    if (!fs::is_regular_file(c, ec)) { e = err_reply(403, "BAD_PATH not a regular file"); return false; }
    out = c;
    return true;
}

static bool parse_uint(const std::string& s, unsigned long long& v) {
    if (s.empty() || s.size() > 18) return false;
    for (char c : s) if (c < '0' || c > '9') return false;
    v = std::stoull(s);
    return true;
}

// команды
static Reply cmd_help() {
    return ok_reply(
        "Commands:\n"
        "  LIST                        list files in sandbox\n"
        "  SIZE <file>                 file size in bytes\n"
        "  COUNT_LINES <file>          number of lines\n"
        "  READ_FIRST <file> <n>       first n lines (1..1000, at most 60000 bytes)\n"
        "  HELP                        this text\n");
}

static Reply cmd_list() {
    std::vector<std::pair<std::string, unsigned long long>> v;
    std::error_code ec;
    for (fs::directory_iterator it(g_root, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (it->is_regular_file(e2) && !e2) {
            auto sz = it->file_size(e2);
            if (!e2) v.emplace_back(it->path().filename().string(), (unsigned long long)sz);
        }
    }

    std::sort(v.begin(), v.end());

    std::string body;
    for (auto& f : v) {
        std::string line = f.first + "\t" + std::to_string(f.second) + "\n";
        if (body.size() + line.size() > kMaxBody) { body += "[truncated]\n"; break; }
        body += line;
    }

    if (body.empty()) body = "(empty)\n";

    return ok_reply(body);
}

static Reply cmd_size(const std::string& name) {
    fs::path p;
    Reply e{};
    if (!resolve(name, p, e)) return e;

    std::error_code ec;
    auto sz = fs::file_size(p, ec);

    if (ec) return err_reply(500, "CANNOT_STAT");

    return ok_reply(std::to_string((unsigned long long)sz) + "\n");
}

static Reply cmd_count_lines(const std::string& name) {
    fs::path p;
    Reply e{};
    if (!resolve(name, p, e)) return e;

    std::ifstream f(p, std::ios::binary);
    if (!f) return err_reply(500, "CANNOT_OPEN");

    std::vector<char> buf(1 << 16);
    unsigned long long lines = 0;
    char last = '\n';
    bool any = false;
    while (f.read(buf.data(), (std::streamsize)buf.size()) || f.gcount() > 0) {
        std::streamsize n = f.gcount();
        any = true;
        lines += (unsigned long long)std::count(buf.data(), buf.data() + n, '\n');
        last = buf[(size_t)n - 1];
    }

    if (any && last != '\n') ++lines;  // последняя строка без перевода строки

    return ok_reply(std::to_string(lines) + "\n");
}

static Reply cmd_read_first(const std::string& name, const std::string& nstr) {
    unsigned long long n = 0;
    if (!parse_uint(nstr, n) || n < 1 || n > kMaxLines)
        return err_reply(400, "BAD_ARGS n must be an integer in 1..1000");

    fs::path p;
    Reply e{};
    if (!resolve(name, p, e)) return e;

    std::ifstream f(p, std::ios::binary);
    if (!f) return err_reply(500, "CANNOT_OPEN");

    std::string body;
    unsigned long long lines = 0;
    bool truncated = false;
    char c;
    while (lines < n && f.get(c)) {
        if (body.size() >= kMaxBody) { truncated = true; break; }
        body.push_back(c);
        if (c == '\n') ++lines;
    }

    if (truncated) body += "\n[truncated at 60000 bytes]\n";

    return ok_reply(body);
}

// разбор запроса
struct Request {
    bool valid = false;
    unsigned long long id = 0;
    std::string client = "?";
    std::string cmd;
    std::vector<std::string> args;
};

static Request parse_request(const std::string& s) {
    Request r;
    std::istringstream in(s);
    std::vector<std::string> t;
    std::string w;
    while (in >> w) {
        t.push_back(w);
        if (t.size() > 12) break;
    }

    if (!t.empty() && t.size() >= 2) {
        unsigned long long id = 0;
        if (parse_uint(t[1], id)) r.id = id;
    }

    if (t.size() < 4 || t[0] != "V1" || r.id == 0) return r;  // requestId начинается с 1

    r.client = t[2].substr(0, 16);
    r.cmd = t[3];
    for (auto& c : r.cmd) c = (char)std::toupper((unsigned char)c);

    r.args.assign(t.begin() + 4, t.end());
    r.valid = true;

    return r;
}

static Reply execute(const Request& r) {
    const auto& a = r.args;
    auto need = [&](size_t n) { return a.size() == n; };
    if (r.cmd == "HELP")        return need(0) ? cmd_help() : err_reply(400, "BAD_ARGS usage: HELP");
    if (r.cmd == "LIST")        return need(0) ? cmd_list() : err_reply(400, "BAD_ARGS usage: LIST");
    if (r.cmd == "SIZE")        return need(1) ? cmd_size(a[0]) : err_reply(400, "BAD_ARGS usage: SIZE <file>");
    if (r.cmd == "COUNT_LINES") return need(1) ? cmd_count_lines(a[0]) : err_reply(400, "BAD_ARGS usage: COUNT_LINES <file>");
    if (r.cmd == "READ_FIRST")  return need(2) ? cmd_read_first(a[0], a[1]) : err_reply(400, "BAD_ARGS usage: READ_FIRST <file> <n>");
   
    return err_reply(400, "UNKNOWN_COMMAND (try HELP)");
}

static bool send_reply(SOCKET s, unsigned long long id, const Reply& rep, double server_us) {
    char head[96];
    snprintf(head, sizeof head, "V1 %llu %s %d %.0f\n", id, rep.ok ? "OK" : "ERR", rep.code, server_us);
    return send_message(s, std::string(head) + rep.body);
}

// обслуживание клиента
static void handle_client(SOCKET s, std::string peer, int conn) {
    std::string cid = "?";
    for (;;) {
        std::string msg;
        int r = recv_message(s, msg, g_idle_ms, &g_stop);
        if (r == RECV_OK) {
            Request rq = parse_request(msg);
            if (rq.valid) cid = rq.client;

            log(ROLE, "RECV id=%s peer=%s requestId=%llu size=%zu cmd=%s", cid.c_str(), peer.c_str(), rq.id,
                msg.size(), rq.valid ? rq.cmd.c_str() : "<invalid>");

            double t0 = now_us();
            Reply rep;
            if (!rq.valid) {
                rep = err_reply(400, "BAD_REQUEST expected: V1 <requestId> <clientId> <COMMAND> [args]");
            } else {
                try {
                    rep = execute(rq);
                } catch (const std::exception& ex) {
                    rep = err_reply(500, std::string("INTERNAL ") + ex.what());
                }
            }
            double server_us = now_us() - t0;
            if (g_delay_ms > 0) Sleep((DWORD)g_delay_ms);

            bool sent = send_reply(s, rq.id, rep, server_us);
            if (!sent) {
                log_wsa(ROLE, "send", WSAGetLastError());
                break;
            }

            log(ROLE, "SEND id=%s requestId=%llu status=%s code=%d size=%zu server_us=%.0f", cid.c_str(), rq.id,
                rep.ok ? "OK" : "ERR", rep.code, rep.body.size(), server_us);
        } else if (r == RECV_CLOSED) {
            log(ROLE, "CLIENT_DISCONNECTED id=%s peer=%s conn=%d", cid.c_str(), peer.c_str(), conn);
            break;
        } else if (r == RECV_TIMEOUT) {
            log(ROLE, "TIMEOUT id=%s peer=%s idle>%dms, closing", cid.c_str(), peer.c_str(), g_idle_ms);
            break;
        } else if (r == RECV_BADLEN) {
            log(ROLE, "BAD_LENGTH id=%s peer=%s (allowed 1..%u), closing", cid.c_str(), peer.c_str(), kMaxMessage);
            send_reply(s, 0, err_reply(413, "BAD_LENGTH frame length out of range"), 0);
            break;
        } else if (r == RECV_STOPPED) {
            log(ROLE, "SHUTDOWN id=%s peer=%s", cid.c_str(), peer.c_str());
            break;
        } else {
            int e = WSAGetLastError();
            if (e == WSAECONNRESET) log(ROLE, "CLIENT_RESET id=%s peer=%s", cid.c_str(), peer.c_str());
            else log_wsa(ROLE, "recv/select", e);

            break;
        }
    }
    shutdown(s, SD_SEND);
    closesocket(s);

    --g_active;
}

static BOOL WINAPI ctrl_handler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT) {
        g_stop = true;
        return TRUE;
    }

    return FALSE;
}

int main(int argc, char** argv) {
    const char* host = arg_value(argc, argv, "--host", "127.0.0.1");
    int port = atoi(arg_value(argc, argv, "--port", "20000"));
    const char* sandbox = arg_value(argc, argv, "--sandbox", "sandbox");

    g_idle_ms = atoi(arg_value(argc, argv, "--timeout", "30000"));
    g_delay_ms = atoi(arg_value(argc, argv, "--delay", "0"));

    if (port < 1 || port > 65535 || g_idle_ms < 200) {
        fprintf(stderr, "usage: server --host <ip> --port <p> --sandbox <dir> [--timeout ms] [--delay ms]\n");
        return 2;
    }

    std::error_code ec;
    fs::create_directories(sandbox, ec);
    g_root = fs::canonical(sandbox, ec);
    if (ec) {
        log(ROLE, "ERROR sandbox '%s': %s", sandbox, ec.message().c_str());
        return 2;
    }

    SetConsoleCtrlHandler(ctrl_handler, TRUE);

    WSADATA wsa;
    int rc = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (rc != 0) {
        log_wsa(ROLE, "WSAStartup", rc);
        return 1;
    }

    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ls == INVALID_SOCKET) {
        log_wsa(ROLE, "socket", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        log(ROLE, "ERROR bad host '%s'", host);
        closesocket(ls);
        WSACleanup();
        return 2;
    }
    if (bind(ls, (sockaddr*)&addr, sizeof addr) == SOCKET_ERROR) {
        log_wsa(ROLE, "bind", WSAGetLastError()); 
        closesocket(ls);
        WSACleanup();
        return 1;
    }
    if (listen(ls, SOMAXCONN) == SOCKET_ERROR) {
        log_wsa(ROLE, "listen", WSAGetLastError());
        closesocket(ls);
        WSACleanup();
        return 1;
    }
    log(ROLE, "SERVER START host=%s port=%d sandbox=%s idle_timeout=%dms delay=%dms", host, port,
        g_root.string().c_str(), g_idle_ms, g_delay_ms);

    std::vector<std::thread> threads;
    int conn = 0;
    while (!g_stop) {
        fd_set rs;
        FD_ZERO(&rs);
        FD_SET(ls, &rs);
        timeval tv{0, 500 * 1000};
        int sr = select(0, &rs, nullptr, nullptr, &tv);
        if (sr == SOCKET_ERROR) {
            log_wsa(ROLE, "select", WSAGetLastError());
            break;
        }
        if (sr == 0) continue;

        sockaddr_in pa{};
        int pl = sizeof pa;
        SOCKET c = accept(ls, (sockaddr*)&pa, &pl);
        if (c == INVALID_SOCKET) {
            log_wsa(ROLE, "accept", WSAGetLastError());
            continue;
        }
        char ip[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &pa.sin_addr, ip, sizeof ip);
        char peer[64];
        snprintf(peer, sizeof peer, "%s:%u", ip, (unsigned)ntohs(pa.sin_port));

        if (g_active >= kMaxClients) {
            log(ROLE, "REJECT peer=%s too many clients", peer);
            send_reply(c, 0, err_reply(503, "BUSY too many clients"), 0);
            closesocket(c);
            continue;
        }
        ++g_active;
        ++conn;
        log(ROLE, "CLIENT_CONNECTED peer=%s conn=%d active=%d", peer, conn, g_active.load());
        threads.emplace_back(handle_client, c, std::string(peer), conn);
    }

    log(ROLE, "SERVER STOPPING");
    closesocket(ls);
    for (auto& t : threads) t.join();
    WSACleanup();
    log(ROLE, "SERVER STOP");
    return 0;
}
