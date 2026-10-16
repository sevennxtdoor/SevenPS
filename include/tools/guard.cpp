#include "pch.hpp"
#include "guard.hpp"

namespace
{
    constexpr int MAX_TEXT_PER_SECOND = 40, MAX_PACKETS_PER_SECOND = 400;
    constexpr int MAX_CONNECTS_PER_MINUTE = 20;
    constexpr int MAX_LOGIN_FAILS = 10, LOGIN_FAIL_WINDOW = 15 * 60, LOGIN_BLOCK = 15 * 60;
    constexpr int MAX_ACCOUNTS_PER_DAY = 3;
    constexpr int FLOOD_BLOCK = 5 * 60;

    struct ip_state
    {
        std::time_t blocked_until{};
        std::time_t connect_minute{}; int connects{};
        std::time_t fail_start{}; int fails{}; std::time_t login_blocked_until{};
        std::time_t account_day{}; int accounts{};
    };
    std::unordered_map<std::string, ip_state> ips{};

    /* @note per connection packet counter, keyed by the ENet slot (reset when a new connection gets that slot) */
    struct rate { std::time_t second{}; int text{}, all{}; };
    std::unordered_map<const ENetPeer*, rate> rates{};

    bool local(const std::string &ip) { return ip.starts_with("127."); }

    void block_ip(const std::string &ip, int seconds, const char *why)
    {
        if (local(ip)) return;
        ips[ip].blocked_until = std::time(nullptr) + seconds;
        std::printf("[guard] %s blocked for %d minutes: %s\n", ip.c_str(), seconds / 60, why);
    }
}

std::string peer_ip(const ENetPeer &p)
{
    const auto &v4 = p.address.host.v4;
    return std::format("{}.{}.{}.{}", v4[0], v4[1], v4[2], v4[3]);
}

bool guard_connect(ENetPeer &p)
{
    const std::string ip = peer_ip(p);
    const std::time_t now = std::time(nullptr);
    rates[&p] = {};
    if (local(ip)) return true;

    ip_state &s = ips[ip];
    if (s.blocked_until > now) return false;
    if (now - s.connect_minute >= 60) { s.connect_minute = now; s.connects = 0; }
    if (++s.connects > MAX_CONNECTS_PER_MINUTE)
    {
        block_ip(ip, FLOOD_BLOCK, "too many new connections");
        return false;
    }
    return true;
}

bool guard_packet(ENetPeer &p, bool text)
{
    rate &r = rates[&p];
    const std::time_t now = std::time(nullptr);
    if (r.second != now) r = { .second = now };
    ++r.all;
    if (text) ++r.text;
    if (r.text <= MAX_TEXT_PER_SECOND && r.all <= MAX_PACKETS_PER_SECOND) return true;

    const std::string ip = peer_ip(p);
    std::printf("[guard] %s flooding (%d text / %d packets in one second), kicked\n", ip.c_str(), r.text, r.all);
    block_ip(ip, FLOOD_BLOCK, "packet flood");
    enet_peer_disconnect_later(&p, 0); // @note the disconnect event still saves a logged in player
    r = { .second = now + 3600 }; // @note don't print this again for the rest of the connection
    return false;
}

std::string guard_login_blocked(const std::string &ip)
{
    if (local(ip)) return {};
    const std::time_t now = std::time(nullptr);
    const ip_state &s = ips[ip];
    if (s.login_blocked_until > now)
        return std::format("Too many wrong passwords from your connection. Try again in {} minutes.", (s.login_blocked_until - now + 59) / 60);
    return {};
}

void guard_login_failed(const std::string &ip)
{
    if (local(ip)) return;
    const std::time_t now = std::time(nullptr);
    ip_state &s = ips[ip];
    if (now - s.fail_start >= LOGIN_FAIL_WINDOW) { s.fail_start = now; s.fails = 0; }
    if (++s.fails >= MAX_LOGIN_FAILS)
    {
        s.fails = 0;
        s.login_blocked_until = now + LOGIN_BLOCK;
        std::printf("[guard] %s can't log in for %d minutes: %d wrong passwords\n", ip.c_str(), LOGIN_BLOCK / 60, MAX_LOGIN_FAILS);
    }
}

bool guard_may_register(const std::string &ip)
{
    if (local(ip)) return true;
    ip_state &s = ips[ip];
    const std::time_t now = std::time(nullptr);
    if (now - s.account_day >= 24 * 3600) { s.account_day = now; s.accounts = 0; }
    return s.accounts < MAX_ACCOUNTS_PER_DAY;
}

void guard_registered(const std::string &ip)
{
    if (local(ip)) return;
    ++ips[ip].accounts;
    std::printf("[guard] new account from %s (%d today)\n", ip.c_str(), ips[ip].accounts);
}

std::string guard_redact(const std::string &header)
{
    std::string out{};
    for (const std::string &line : readch(header, '\n'))
    {
        std::string key = line.substr(0, line.find('|'));
        for (char &c : key) c = static_cast<char>(std::tolower(static_cast<u_char>(c)));
        const bool secret = key.find("pass") != std::string::npos || key == "ltoken" || key == "token"; // @note ltoken = base64 of growId & password
        out += secret ? key + "|(hidden)\n" : line + "\n";
    }
    return out;
}
