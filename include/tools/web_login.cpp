#include "pch.hpp"
#include <future>
#include <mutex>
#include <deque>
#include <functional>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include "auth.hpp"
#include "web_login.hpp"
#include "discord.hpp"

namespace
{
    constexpr std::time_t SESSION_SECONDS = 7 * 24 * 3600;
    constexpr std::size_t SESSIONS_PER_ACCOUNT = 5;

    struct session { std::string growid; std::time_t expires{}; std::time_t made{}; };
    std::mutex session_mutex;
    std::unordered_map<std::string, session> sessions{}; // @note key = SHA-256 of the token (hex)

    struct job { std::function<void()> work; std::promise<void> done; };
    std::mutex job_mutex;
    std::deque<std::shared_ptr<job>> jobs{};

    std::string hex(const u_char *data, std::size_t size)
    {
        static constexpr char digits[] = "0123456789abcdef";
        std::string out(size * 2, '0');
        for (std::size_t i = 0; i < size; ++i) { out[i * 2] = digits[data[i] >> 4]; out[i * 2 + 1] = digits[data[i] & 15]; }
        return out;
    }

    std::string sha256(const std::string &text)
    {
        u_char digest[32]{};
        u_int size{};
        EVP_Digest(text.data(), text.size(), digest, &size, EVP_sha256(), nullptr);
        return hex(digest, sizeof(digest));
    }

    bool lower_hex(const std::string &s, std::size_t size)
    {
        return s.size() == size && std::ranges::all_of(s, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
    }

    std::string url_decode(const std::string &in)
    {
        std::string out{};
        for (std::size_t i = 0; i < in.size(); ++i)
        {
            if (in[i] == '+') out += ' ';
            else if (in[i] == '%' && i + 2 < in.size() && std::isxdigit(static_cast<u_char>(in[i + 1])) && std::isxdigit(static_cast<u_char>(in[i + 2])))
            {
                out += static_cast<char>(std::stoi(in.substr(i + 1, 2), nullptr, 16));
                i += 2;
            }
            else out += in[i];
        }
        return out;
    }

    std::string json_text(const std::string &s)
    {
        std::string out{};
        for (char c : s)
        {
            if (c == '"' || c == '\\') out += '\\', out += c;
            else if (static_cast<u_char>(c) < 0x20) out += ' ';
            else if (c == '<') out += "\\u003c"; // @note never closes a <script> it ends up in
            else out += c;
        }
        return out;
    }

    std::string error_json(const std::string &message)
    {
        return std::format("{{\"status\":\"error\",\"message\":\"{}\"}}", json_text(message));
    }
}

std::string form_value(const std::string &form, const std::string &key)
{
    for (std::size_t start = 0; start <= form.size(); )
    {
        std::size_t end = form.find('&', start);
        if (end == std::string::npos) end = form.size();
        const std::string pair = form.substr(start, end - start);
        if (const std::size_t eq = pair.find('='); eq != std::string::npos && url_decode(pair.substr(0, eq)) == key)
            return url_decode(pair.substr(eq + 1));
        start = end + 1;
    }
    return {};
}

std::string session_new(const std::string &growid)
{
    u_char random[32]{};
    RAND_bytes(random, sizeof(random));
    const std::string token = hex(random, sizeof(random));
    const std::time_t now = std::time(nullptr);

    std::lock_guard lock(session_mutex);
    std::erase_if(sessions, [now](const auto &s) { return s.second.expires <= now; });
    /* @note keep the newest few per account: logging in over and over can't fill the memory */
    std::vector<std::unordered_map<std::string, session>::iterator> mine{};
    for (auto it = sessions.begin(); it != sessions.end(); ++it) if (it->second.growid == growid) mine.push_back(it);
    if (mine.size() >= SESSIONS_PER_ACCOUNT)
    {
        std::ranges::sort(mine, {}, [](const auto &it) { return it->second.made; });
        for (std::size_t i = 0; i + SESSIONS_PER_ACCOUNT - 1 < mine.size(); ++i) sessions.erase(mine[i]);
    }
    sessions[sha256(token)] = { .growid = growid, .expires = now + SESSION_SECONDS, .made = now };
    return token;
}

std::string session_growid(const std::string &token)
{
    if (!lower_hex(token, 64)) return {}; // @note also refuses the old base64 "growId=..&password=.." tokens
    std::lock_guard lock(session_mutex);
    const auto it = sessions.find(sha256(token));
    if (it == sessions.end() || it->second.expires <= std::time(nullptr)) return {};
    return it->second.growid;
}

void session_drop(const std::string &growid)
{
    const auto lower = [](std::string s) { for (char &c : s) c = static_cast<char>(std::tolower(static_cast<u_char>(c))); return s; };
    std::lock_guard lock(session_mutex);
    std::erase_if(sessions, [&](const auto &s) { return lower(s.second.growid) == lower(growid); }); // @note reset_password.txt may spell it differently
}

std::string token_json(const std::string &token)
{
    return std::format("{{\"status\":\"success\",\"message\":\"Account Validated.\",\"token\":\"{}\",\"url\":\"\",\"accountType\":\"growtopia\"}}", json_text(token));
}

namespace
{
    /* @note runs `work` on the main thread. @return empty when it ran, else the reason to show */
    using main_thread = std::function<std::string(std::function<void()>)>;

    struct login_state { login_check check{}; bool password_ok{}; std::string hashed{}, answer{}; };

    std::string handle(const std::string &form, const std::string &ip, const main_thread &on_main)
    {
        const std::string growid = form_value(form, "growId"), password = form_value(form, "password");
        const bool registering = form_value(form, "mode") == "register";
        if (growid.empty() || password.empty()) return error_json("Fill in your GrowID and password.");
        if (registering && password != form_value(form, "confirm")) return error_json("The two passwords are not the same.");

        /* @note shared: after a timeout the main thread may still run a step, the https thread's stack is gone by then */
        auto s = std::make_shared<login_state>();
        if (const std::string why = on_main([s, growid, password, ip, registering]
            {
                if (registering && account_name(growid)) s->check.error = "That GrowID is already taken.";
                else s->check = login_begin(growid, password, registering, ip);
            }); !why.empty()) return error_json(why);
        if (!s->check.error.empty())
        {
            if (!registering) std::printf("[web login] %s refused: %s\n", growid.c_str(), s->check.error.c_str()); // @note never the password
            return error_json(s->check.error);
        }

        /* @note the slow part, on this (https) thread: PBKDF2 */
        if (registering) s->hashed = hash_password(password);
        else if ((s->password_ok = s->check.stored && check_password(*s->check.stored, password)) && !s->check.stored->starts_with("pbkdf2$"))
            s->hashed = hash_password(password); // @note old plain text, upgraded in login_end()

        if (const std::string why = on_main([s, growid, password, ip, registering]
            {
                if (registering)
                {
                    if (account_name(growid)) return (void)(s->answer = error_json("That GrowID is already taken.")); // @note made meanwhile
                    if (const std::string why = register_end(ip); !why.empty()) return (void)(s->answer = error_json(why));
                    ::peer account{};
                    account.growid = growid;
                    account.load(growid, password, s->hashed); // @note makes the account (hashed password, starter backpack)
                    account.loaded = false; // @note nothing to save: it was just made
                    std::printf("[web login] new account %s from %s\n", growid.c_str(), ip.c_str());
                    discord_post("staff", std::format("New player registered: {}", growid)); // @note never the IP or password
                    s->answer = token_json(session_new(growid));
                    return;
                }
                if (const std::string why = login_end(growid, s->password_ok, s->hashed, ip); !why.empty())
                {
                    std::printf("[web login] %s refused: %s\n", growid.c_str(), why.c_str()); // @note never the password
                    s->answer = error_json(why);
                    return;
                }
                s->answer = token_json(session_new(account_name(growid).value_or(growid))); // @note the stored spelling: "mike" logs in as Mike
            }); !why.empty()) return error_json(why);
        return s->answer;
    }
}

std::string web_login_handle(const std::string &form, const std::string &ip)
{
    return handle(form, ip, [](std::function<void()> work) { work(); return std::string{}; });
}

std::string web_login_request(const std::string &form, const std::string &ip)
{
    return handle(form, ip, [](std::function<void()> work) -> std::string
    {
        auto j = std::make_shared<job>();
        j->work = std::move(work);
        std::future<void> done = j->done.get_future();
        {
            std::lock_guard lock(job_mutex);
            if (jobs.size() >= 20) return "The server is busy, try again in a moment.";
            jobs.push_back(j);
        }
        if (done.wait_for(std::chrono::seconds(15)) != std::future_status::ready) return "The server didn't answer, try again.";
        return {};
    });
}

void web_login_tick()
{
    for (;;)
    {
        std::shared_ptr<job> j{};
        {
            std::lock_guard lock(job_mutex);
            if (jobs.empty()) return;
            j = jobs.front();
            jobs.pop_front();
        }
        j->work();
        j->done.set_value();
    }
}
