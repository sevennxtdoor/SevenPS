#include "pch.hpp"
#include <cstdio>
#include <fstream>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include "guard.hpp"
#include "auth.hpp"

namespace
{
    constexpr int ROUNDS = 100000;
    constexpr int SALT_BYTES = 16, HASH_BYTES = 32;
    constexpr int MAX_FAILS = 5, LOCK_SECONDS = 30;

    struct fails { int count{}; std::time_t locked_until{}; };
    std::unordered_map<std::string, fails> failed{}; // @note lower-case growid

    std::string lower(std::string s) { for (char &c : s) c = static_cast<char>(std::tolower(static_cast<u_char>(c))); return s; }

    std::string to_hex(const u_char *data, std::size_t size)
    {
        static constexpr char digits[] = "0123456789abcdef";
        std::string out(size * 2, '0');
        for (std::size_t i = 0; i < size; ++i) { out[i * 2] = digits[data[i] >> 4]; out[i * 2 + 1] = digits[data[i] & 15]; }
        return out;
    }
    std::vector<u_char> from_hex(const std::string &hex)
    {
        std::vector<u_char> out{};
        for (std::size_t i = 0; i + 1 < hex.size(); i += 2) out.push_back(static_cast<u_char>(std::stoi(hex.substr(i, 2), nullptr, 16)));
        return out;
    }

    std::vector<u_char> pbkdf2(const std::string &password, const std::vector<u_char> &salt, int rounds)
    {
        std::vector<u_char> out(HASH_BYTES);
        PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()), salt.data(), static_cast<int>(salt.size()), rounds, EVP_sha256(), HASH_BYTES, out.data());
        return out;
    }

    /* @return the stored password, empty optional if the account doesn't exist */
    std::optional<std::string> stored_password(const std::string &growid)
    {
        ::hStmt hStmt{ "SELECT password FROM peer WHERE growid = ? LIMIT 1" };
        MYSQL_BIND param = make_bind_in(growid);
        hStmt.bind_param(&param);
        char buffer[160]{};
        u_long length{};
        my_bool is_null{};
        MYSQL_BIND result{};
        result.buffer_type = MYSQL_TYPE_STRING;
        result.buffer = buffer;
        result.buffer_length = sizeof(buffer);
        result.length = &length;
        result.is_null = &is_null;
        mysql_stmt_bind_result(hStmt.pStmt, &result);
        hStmt.execute();
        mysql_stmt_store_result(hStmt.pStmt);
        if (mysql_stmt_fetch(hStmt.pStmt) != 0) return std::nullopt;
        return is_null ? std::string{} : std::string(buffer, std::min<u_long>(length, sizeof(buffer)));
    }

    void store_password(const std::string &growid, const std::string &hashed)
    {
        ::hStmt hStmt{ "UPDATE peer SET password = ? WHERE growid = ?" };
        MYSQL_BIND params[2] = { make_bind_in(hashed), make_bind_in(growid) };
        hStmt.bind_param(params);
        hStmt.execute();
    }

    bool valid_growid(const std::string &growid)
    {
        return growid.size() >= 3 && growid.size() <= 18 && std::ranges::all_of(growid, [](char c) { return std::isalnum(static_cast<u_char>(c)); });
    }
}

std::optional<std::string> account_name(const std::string &growid)
{
    if (!valid_growid(growid)) return std::nullopt;
    ::hStmt hStmt{ "SELECT growid FROM peer WHERE growid = ? LIMIT 1" };
    MYSQL_BIND param = make_bind_in(growid);
    hStmt.bind_param(&param);
    char buffer[32]{};
    u_long length{};
    MYSQL_BIND result{};
    result.buffer_type = MYSQL_TYPE_STRING;
    result.buffer = buffer;
    result.buffer_length = sizeof(buffer);
    result.length = &length;
    mysql_stmt_bind_result(hStmt.pStmt, &result);
    hStmt.execute();
    mysql_stmt_store_result(hStmt.pStmt);
    if (mysql_stmt_fetch(hStmt.pStmt) != 0) return std::nullopt;
    return std::string(buffer, std::min<u_long>(length, sizeof(buffer)));
}

std::string hash_password(const std::string &password)
{
    std::vector<u_char> salt(SALT_BYTES);
    RAND_bytes(salt.data(), SALT_BYTES); // @note a new random salt per password: two equal passwords never look the same
    const std::vector<u_char> hash = pbkdf2(password, salt, ROUNDS);
    return std::format("pbkdf2${}${}${}", ROUNDS, to_hex(salt.data(), salt.size()), to_hex(hash.data(), hash.size()));
}

bool check_password(const std::string &stored, const std::string &password)
{
    if (!stored.starts_with("pbkdf2$")) // @note old plain text. the old column was VARCHAR(18), so a longer password was cut to 18
    {
        if (stored.empty() || password.size() < stored.size()) return false;
        if (password.size() > stored.size() && stored.size() != 18) return false;
        return CRYPTO_memcmp(stored.data(), password.data(), stored.size()) == 0;
    }

    const std::vector<std::string> parts = readch(stored, '$');
    if (parts.size() != 4) return false;
    const int rounds = atoi(parts[1].c_str());
    if (rounds < 1000 || rounds > 10000000) return false;
    const std::vector<u_char> expected = from_hex(parts[3]);
    const std::vector<u_char> actual = pbkdf2(password, from_hex(parts[2]), rounds);
    return expected.size() == actual.size() && CRYPTO_memcmp(expected.data(), actual.data(), actual.size()) == 0; // @note constant time: no timing hints
}

void apply_password_reset()
{
    std::string line{};
    {
        std::ifstream file("reset_password.txt");
        if (!file || !std::getline(file, line)) return;
    }
    std::remove("reset_password.txt"); // @note don't leave a password lying around in a text file
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const std::size_t bar = line.find('|');
    if (bar == std::string::npos) return (void)std::puts("[login] reset_password.txt: write it as growid|new password");
    const std::string growid = line.substr(0, bar), password = line.substr(bar + 1);
    if (!stored_password(growid) || password.size() < 4) return (void)std::printf("[login] reset_password.txt: unknown GrowID or password under 4 characters\n");
    store_password(growid, hash_password(password));
    failed.erase(lower(growid));
    std::printf("[login] password of %s was reset\n", growid.c_str());
}

std::string authenticate(const std::string &growid, const std::string &password, bool may_register, const std::string &ip)
{
    if (const std::string why = guard_login_blocked(ip); !why.empty()) return why; // @note before hashing: a bot can't burn our CPU either
    if (!valid_growid(growid)) return "GrowIDs are 3-18 letters or numbers.";
    if (password.empty() || password.size() > 64) return "Wrong password.";

    fails &f = failed[lower(growid)];
    const std::time_t now = std::time(nullptr);
    if (f.locked_until > now) return std::format("Too many wrong passwords. Try again in {} seconds.", f.locked_until - now);

    const std::optional<std::string> stored = stored_password(growid);
    if (!stored)
    {
        if (!may_register) return "That account doesn't exist.";
        if (password.size() < 4) return "Pick a password with at least 4 characters.";
        if (!guard_may_register(ip)) return "Too many new accounts from your connection today. Try again tomorrow.";
        guard_registered(ip);
        return {}; // @note peer::load() creates it with hash_password()
    }
    if (!check_password(*stored, password))
    {
        if (++f.count >= MAX_FAILS) { f.count = 0; f.locked_until = now + LOCK_SECONDS; } // @note slows down password guessing
        guard_login_failed(ip); // @note ...and a bot trying many accounts from one IP
        return "Wrong password.";
    }
    f = {};
    if (!stored->starts_with("pbkdf2$")) store_password(growid, hash_password(password)); // @note upgrade the old plain text
    return {};
}
