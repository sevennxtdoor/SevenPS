#include "pch.hpp"
#include "https.hpp"
#include "server_data.hpp"
#include "tools/web_login.hpp"
#include <fstream>

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
    struct request { std::string method, path, query, body; };

    /* @note reads one HTTP request: headers up to 8 KB, a body up to 8 KB (Content-Length). anything bigger or broken is dropped.
     * the old code read exactly 213 bytes and printed them without an ending 0 (read past the buffer) */
    std::optional<request> read_request(SSL *ssl)
    {
        std::string raw{};
        char buf[2048];
        std::size_t header_end{};
        while ((header_end = raw.find("\r\n\r\n")) == std::string::npos)
        {
            if (raw.size() > 8 * 1024) return std::nullopt;
            const int n = SSL_read(ssl, buf, sizeof(buf));
            if (n <= 0) return std::nullopt;
            raw.append(buf, n);
        }
        request req{};
        const std::string first = raw.substr(0, raw.find("\r\n"));
        const std::size_t sp1 = first.find(' '), sp2 = first.find(' ', sp1 + 1);
        if (sp1 == std::string::npos || sp2 == std::string::npos) return std::nullopt;
        req.method = first.substr(0, sp1);
        req.path = first.substr(sp1 + 1, sp2 - sp1 - 1);
        if (const std::size_t q = req.path.find('?'); q != std::string::npos) req.query = req.path.substr(q + 1), req.path.resize(q);

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
            const int n = SSL_read(ssl, buf, sizeof(buf));
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

        /* @note the login window the game opens */
        if (req.path == "/player/login/dashboard")
        {
            std::ifstream file("resources/login.html", std::ios::binary); // @note read every time: edit the page without a restart
            if (!file) return http("500 Internal Server Error", "text/plain", "resources/login.html is missing");
            return http("200 OK", "text/html; charset=utf-8", std::string(std::istreambuf_iterator<char>(file), {}));
        }
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
            if (session_growid(token).empty()) return http("401 Unauthorized", "text/plain", "Unauthorized");
            return http("200 OK", "text/html", token_json(token));
        }
        return http("404 Not Found", "text/plain", "Not Found");
    }
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
    if (SSL_CTX_use_certificate_file(ctx, "resources/ctx/server.crt", SSL_FILETYPE_PEM) != 1 ||
        SSL_CTX_use_PrivateKey_file(ctx, "resources/ctx/server.key", SSL_FILETYPE_PEM)  != 1)
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

        /* @note one request at a time: a client that connects and sends nothing used to block every login forever */
#ifdef _WIN32
        const DWORD timeout = 5000; // @note ms
#else
        const timeval timeout{ .tv_sec = 5, .tv_usec = 0 };
#endif
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        const u_char *ip4 = reinterpret_cast<const u_char*>(&addr.sin_addr);
        const std::string ip = std::format("{}.{}.{}.{}", ip4[0], ip4[1], ip4[2], ip4[3]);

        /* https://docs.openssl.org/3.0/man3/SSL_new/#return-values */
        SSL *ssl = SSL_new(ctx);
        if (!ssl) { cross_close(fd); continue; } // @note SSL_up_ref() here added a second reference that was never freed (leaked every request)
        /* https://docs.openssl.org/3.0/man3/SSL_set_fd/#return-values */
        /* https://docs.openssl.org/3.0/man3/SSL_accept/#return-values */
        else if (SSL_set_fd(ssl, fd) != 1 || SSL_accept(ssl) <= 0) {
            //int ret = SSL_get_error(ssl, 3);
            ERR_print_errors_fp(stderr);
        }
        else if (std::optional<request> req = read_request(ssl); req)
        {
            const std::string reply = route(*req, ip);
            if (SSL_write(ssl, reply.data(), static_cast<int>(reply.size())) <= 0) ERR_print_errors_fp(stderr);
        }

        /* "It can also occur when not all data was read using SSL_read()." */
        if (SSL_shutdown(ssl) <0) 
        {
            //int ret = SSL_get_error(ssl, 3);
            ERR_print_errors_fp(stderr);
        }
        SSL_free(ssl);
        if (shutdown(fd, 2) == SOCKET_ERROR) cross_log("failed to shutdown socket"); // @todo unsure if WSA can handle this.
        cross_close(fd);
    }
}

#ifndef _WIN32
    #undef SOCKET
#endif
