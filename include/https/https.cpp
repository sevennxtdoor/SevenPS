#include "pch.hpp"
#include "https.hpp"
#include "server_data.hpp"
#include "tools/web_login.hpp"
#include "automate/holiday.hpp" // @note localtime()
#include <fstream>
#include <filesystem>
#include <mutex>
#include <chrono>
#include <thread>

#include <openssl/err.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <csignal>
    #include <unistd.h>
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h> // @note TCP_DEFER_ACCEPT
    #include <sys/socket.h>
    #include <sys/select.h>
    #include <fcntl.h>

    #define SOCKET int
    #define INVALID_SOCKET	(SOCKET)(~0)
    #define SOCKET_ERROR	(-1)

#endif

/* cross-platform socket close */
static void cross_close(SOCKET fd)
{
    int ret =
#ifdef _WIN32
    closesocket(fd)
#else // @note unix
    close(fd)
#endif
    ; // ending of ret. hehe some silly code ;)

    if (ret == SOCKET_ERROR) printf("socket close error.\n");
}

/* cross-platform WSA error log, fallback on linux with strerror() */
static void cross_log(const std::string &message)
{
#ifdef _WIN32
    std::fprintf(stderr, "%s: %d\n", message.c_str(), WSAGetLastError());
#else // @note unix
    std::fprintf(stderr, "%s: %s\n", message.c_str(), strerror(errno));
#endif
}

namespace
{
    struct request { std::string method, path, query, body, line, headers; };

    /* @note each connection has its own thread (see listener): a slow or silent client only holds its own one */
    constexpr int MAX_CONNECTIONS = 32, MAX_PER_IP = 4;
    constexpr auto REQUEST_TIME = std::chrono::seconds(10); // @note handshake + whole request, however slowly it trickles in
    std::mutex connection_mutex;
    int connections{};
    std::unordered_map<std::string, int> connections_by_ip{};

    constexpr std::uintmax_t WEB_LOG_MAX = 5 * 1024 * 1024; // @note then web.log becomes web.log.old: 404 spam can't fill the disk
    std::mutex log_mutex;

    /* @note web.log: every request line + answer code (never the body, which can hold a password). headers only for
     * unknown addresses, to see what the game asked for */
    void log_web(const std::string &ip, const request &req, const std::string &reply)
    {
        std::lock_guard lock(log_mutex);
        if (std::error_code ec; std::filesystem::file_size("web.log", ec) > WEB_LOG_MAX && !ec)
            std::filesystem::rename("web.log", "web.log.old", ec); // @note replaces the older one
        std::ofstream log("web.log", std::ios::app);
        const std::tm t = localtime();
        const std::string code = reply.size() > 12 ? reply.substr(9, 3) : "?";
        log << std::format("{:02}:{:02}:{:02} {} {} -> {}\n", t.tm_hour, t.tm_min, t.tm_sec, ip, req.line, code);
        if (code == "404") log << req.headers << "\n\n";
    }

    /* @note the socket is non-blocking: after an SSL call wants more data (or room to write), wait for it until the deadline.
     * a blocking read restarted its timeout on every byte, so a client sending one byte every few seconds never timed out */
    bool wait_ready(SSL *ssl, int ret, std::chrono::steady_clock::time_point deadline)
    {
        const int err = SSL_get_error(ssl, ret);
        if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) return false;
        const auto left = std::chrono::duration_cast<std::chrono::microseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (left <= 0) return false;
        const SOCKET fd = static_cast<SOCKET>(SSL_get_fd(ssl));
        fd_set set;
        FD_ZERO(&set);
        FD_SET(fd, &set);
        timeval tv{ static_cast<long>(left / 1000000), static_cast<long>(left % 1000000) };
        return select(static_cast<int>(fd) + 1, err == SSL_ERROR_WANT_READ ? &set : nullptr, err == SSL_ERROR_WANT_WRITE ? &set : nullptr, nullptr, &tv) > 0;
    }

    /* @return bytes read, or <= 0 when the connection ended, broke or ran out of time */
    int read_some(SSL *ssl, char *buf, int size, std::chrono::steady_clock::time_point deadline)
    {
        for (;;)
        {
            const int n = SSL_read(ssl, buf, size);
            if (n > 0 || !wait_ready(ssl, n, deadline)) return n;
        }
    }

    /* @note reads one HTTP request: headers up to 8 KB, a body up to 8 KB (Content-Length). anything bigger or broken is dropped.
     * the old code read exactly 213 bytes and printed them without an ending 0 (read past the buffer) */
    std::optional<request> read_request(SSL *ssl, std::chrono::steady_clock::time_point deadline)
    {
        std::string raw{};
        char buf[2048];
        std::size_t header_end{};
        while ((header_end = raw.find("\r\n\r\n")) == std::string::npos)
        {
            if (raw.size() > 8 * 1024 || std::chrono::steady_clock::now() > deadline) return std::nullopt; // @note one byte every few seconds kept a connection for hours
            const int n = read_some(ssl, buf, sizeof(buf), deadline);
            if (n <= 0) return std::nullopt;
            raw.append(buf, n);
        }
        request req{};
        const std::string first = raw.substr(0, raw.find("\r\n"));
        const std::size_t sp1 = first.find(' '), sp2 = first.find(' ', sp1 + 1);
        if (sp1 == std::string::npos || sp2 == std::string::npos) return std::nullopt;
        req.line = first.substr(0, std::min(first.find('?'), first.rfind(' '))) + (first.find('?') != std::string::npos ? "?(hidden)" : ""); // @note the query can hold a login token
        req.headers = raw.substr(0, std::min<std::size_t>(header_end, 2000));
        req.method = first.substr(0, sp1);
        req.path = first.substr(sp1 + 1, sp2 - sp1 - 1);
        if (const std::size_t q = req.path.find('?'); q != std::string::npos) req.query = req.path.substr(q + 1), req.path.resize(q);
        /* @note tolerant: "https://host/path", upper case letters, a trailing / all mean the same page */
        if (req.path.starts_with("http")) if (const std::size_t at = req.path.find('/', req.path.find("://") + 3); at != std::string::npos) req.path.erase(0, at);
        for (char &c : req.path) c = static_cast<char>(std::tolower(static_cast<u_char>(c)));
        while (req.path.size() > 1 && req.path.back() == '/') req.path.pop_back();

        std::size_t length{};
        std::string headers = raw.substr(0, header_end);
        for (char &c : headers) c = static_cast<char>(std::tolower(static_cast<u_char>(c)));
        if (const std::size_t at = headers.find("\r\ncontent-length:"); at != std::string::npos)
        {
            length = std::strtoul(headers.c_str() + at + 17, nullptr, 10);
            if (length > 8 * 1024) return std::nullopt;
        }
        req.body = raw.substr(header_end + 4);
        while (req.body.size() < length)
        {
            if (std::chrono::steady_clock::now() > deadline) return std::nullopt;
            const int n = read_some(ssl, buf, sizeof(buf), deadline);
            if (n <= 0) return std::nullopt;
            req.body.append(buf, n);
        }
        req.body.resize(std::min(req.body.size(), length));
        return req;
    }

    std::string http(const std::string &status, const std::string &type, const std::string &body)
    {
        return std::format(
            "HTTP/1.1 {}\r\n"
            "Content-Type: {}\r\n"
            "Content-Length: {}\r\n"
            "Cache-Control: no-store\r\n"
            "X-Content-Type-Options: nosniff\r\n"
            "X-Frame-Options: DENY\r\n"
            "Content-Security-Policy: default-src 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; img-src data:; connect-src 'self'; form-action 'self'\r\n"
            "Connection: close\r\n\r\n"
            "{}",
            status, type, body.size(), body);
    }

    std::string route(const request &req, const std::string &ip)
    {
        std::printf("[web] %s %s %s\n", ip.c_str(), req.method.c_str(), req.path.c_str()); // @note never the body (password)

        if (req.path == "/growtopia/server_data.php")
            return http("200 OK", "text/plain",
                std::format(
                    "server|{}\n"
                    "port|{}\n"
                    "type|{}\n"
                    "type2|{}\n" // @todo remove for older clients
                    "#maint|{}\n"
                    "loginurl|{}\n"
                    "meta|{}\n"
                    "RTENDMARKERBS1001",
                    gServer_data.server, gServer_data.port, gServer_data.type, gServer_data.type2, gServer_data.maint, gServer_data.loginurl, gServer_data.meta
                ));

        auto login_page = []()
        {
            std::ifstream file("resources/login.html", std::ios::binary); // @note read every time: edit the page without a restart
            if (!file) return http("500 Internal Server Error", "text/plain", "resources/login.html is missing");
            return http("200 OK", "text/html; charset=utf-8", std::string(std::istreambuf_iterator<char>(file), {}));
        };
        /* @note the login window the game opens */
        if (req.path == "/player/login/dashboard") return login_page();
        /* @note the page posts here: GrowID + password -> token */
        if (req.path == "/player/login/validate" && req.method == "POST")
            return http("200 OK", "application/json", web_login_request(req.body, ip));

        /* @note the page then opens this address; the game reads the token out of the answer */
        if (req.path == "/player/growid/login/validate")
        {
            const std::string token = form_value(req.query, "token");
            if (session_growid(token).empty()) return http("401 Unauthorized", "text/plain", "Unauthorized");
            return http("200 OK", "text/html", token_json(token));
        }
        /* @note the game asks if a saved token still works (otherwise it shows the login page again) */
        if (req.path == "/player/growid/checktoken")
        {
            const std::string token = form_value(req.body, "refreshToken");
            /* @note an old/expired token (e.g. from the old vercel login): the game shows whatever we answer, so answer
             * with the login page instead of a bare "Unauthorized" */
            if (session_growid(token).empty()) return login_page();
            /* @note the game opens this in its login window and only reads the token on /player/growid/login/validate:
             * answering the JSON here just showed it as a page and the login hung (Bos, logging in again after quitting).
             * 303 = the window follows it with a GET, the same address a normal login ends on. the token is 64 hex chars */
            return std::format(
                "HTTP/1.1 303 See Other\r\n"
                "Location: /player/growid/login/validate?token={}\r\n"
                "Content-Length: 0\r\n"
                "Cache-Control: no-store\r\n"
                "Connection: close\r\n\r\n", token);
        }
        return http("404 Not Found", "text/plain", "Not Found");
    }
}

/* @note one connection, on its own thread: TLS handshake, one request, the answer. everything must finish within REQUEST_TIME */
static void serve(SSL_CTX *ctx, SOCKET fd, const std::string &ip)
{
#ifdef _WIN32
    u_long non_blocking = 1;
    ioctlsocket(fd, FIONBIO, &non_blocking);
#else
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
#endif
    const auto deadline = std::chrono::steady_clock::now() + REQUEST_TIME;

    /* https://docs.openssl.org/3.0/man3/SSL_new/#return-values */
    SSL *ssl = SSL_new(ctx);
    if (!ssl) { cross_close(fd); return; } // @note SSL_up_ref() here added a second reference that was never freed (leaked every request)

    /* https://docs.openssl.org/3.0/man3/SSL_set_fd/#return-values */
    /* https://docs.openssl.org/3.0/man3/SSL_accept/#return-values */
    bool ready = SSL_set_fd(ssl, static_cast<int>(fd)) == 1;
    for (int ret{}; ready && (ret = SSL_accept(ssl)) <= 0; )
        ready = wait_ready(ssl, ret, deadline); // @note a handshake sent one byte at a time ends at the deadline too
    if (!ready) ERR_print_errors_fp(stderr);
    else if (std::optional<request> req = read_request(ssl, deadline); req)
    {
        const std::string reply = route(*req, ip);
        log_web(ip, *req, reply);
        const auto write_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5); // @note after route(): a login can wait up to 15 s for the main loop
        for (int ret{}; (ret = SSL_write(ssl, reply.data(), static_cast<int>(reply.size()))) <= 0; ) // @note a non-blocking write is retried with the same data
            if (!wait_ready(ssl, ret, write_deadline)) { ERR_print_errors_fp(stderr); break; }
    }

    /* "It can also occur when not all data was read using SSL_read()." */
    SSL_shutdown(ssl); // @note non-blocking: a client that doesn't answer the close is fine
    SSL_free(ssl);
    shutdown(fd, 2);
    cross_close(fd);
}

void https::listener()
{
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();
    constexpr int enable = 1;

    /* https://docs.openssl.org/3.0/man3/SSL_CTX_new/#return-values */
    SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
    if (!ctx)
    {
        ERR_print_errors_fp(stderr);
    }

    /* https://docs.openssl.org/master/man3/SSL_CTX_use_certificate/#return-values */
    /* @note sevenps.crt is signed by our own CA "SevenPS Local CA" (installed in Windows, only valid for growtopia1/growtopia2.com),
     * so the game's login window trusts it. miketopia.crt is the one before the rename (old CA), used until sevenps.* is there.
     * server.crt (Gurotopia's, self-signed) was only accepted for server_data.php. our keys are git-ignored: never upload them */
    const char *crt = "resources/ctx/server.crt", *key = "resources/ctx/server.key";
    for (const char *name : { "sevenps", "miketopia" })
        if (const std::string c = std::format("resources/ctx/{}.crt", name), k = std::format("resources/ctx/{}.key", name);
            std::filesystem::exists(c) && std::filesystem::exists(k))
        {
            static std::string own_crt, own_key;
            own_crt = c, own_key = k;
            crt = own_crt.c_str(), key = own_key.c_str();
            break;
        }
    std::printf("[web] certificate: %s\n", crt);
    if (SSL_CTX_use_certificate_file(ctx, crt, SSL_FILETYPE_PEM) != 1 ||
        SSL_CTX_use_PrivateKey_file(ctx, key, SSL_FILETYPE_PEM)  != 1)
    {
        ERR_print_errors_fp(stderr);
    }

#ifdef SIGPIPE // @note unix
    std::signal(SIGPIPE, SIG_IGN);
#endif

    /* https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-socket */
    SOCKET socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket == INVALID_SOCKET)
    {
        cross_log("socket function failed");
    }

#ifdef SO_REUSEADDR
    setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, (char*)&enable, sizeof(enable));
#endif
#ifdef TCP_DEFER_ACCEPT // @note unix
    setsockopt(socket, IPPROTO_TCP, TCP_DEFER_ACCEPT, (char*)&enable, sizeof(enable));
#endif
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(443);
    socklen_t addrlen = sizeof(addr);

    /* https://learn.microsoft.com/en-us/windows/win32/api/winsock/nf-winsock-bind */
    if (bind(socket, (struct sockaddr*)&addr, addrlen) == SOCKET_ERROR)
    {
        cross_log("could not bind socket");
    }

    /* https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-listen */
    if (listen(socket, SOMAXCONN) == SOCKET_ERROR)
    {
        cross_log("failed to listen on socket");
    }
    else std::printf("listening on %s:%hu\n", gServer_data.server.c_str(), gServer_data.port);
    
    while (true)
    {
        /* https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-accept */
        SOCKET fd = accept(socket, reinterpret_cast<sockaddr*>(&addr), &addrlen);
        if (fd == INVALID_SOCKET) continue;

        const u_char *ip4 = reinterpret_cast<const u_char*>(&addr.sin_addr);
        const std::string ip = std::format("{}.{}.{}.{}", ip4[0], ip4[1], ip4[2], ip4[3]);

        /* @note one request at a time let a single client that sends nothing (or one byte now and then) block every login.
         * now each connection gets its own thread, at most MAX_PER_IP from one address and MAX_CONNECTIONS in all */
        {
            std::lock_guard lock(connection_mutex);
            if (connections >= MAX_CONNECTIONS || connections_by_ip[ip] >= MAX_PER_IP)
            {
                if (connections_by_ip[ip] == 0) connections_by_ip.erase(ip);
                cross_close(fd);
                continue;
            }
            ++connections, ++connections_by_ip[ip];
        }
        std::thread([ctx, fd, ip]()
        {
            serve(ctx, fd, ip);
            std::lock_guard lock(connection_mutex);
            --connections;
            if (--connections_by_ip[ip] <= 0) connections_by_ip.erase(ip);
        }).detach();
    }
}

#ifndef _WIN32
    #undef SOCKET
#endif
