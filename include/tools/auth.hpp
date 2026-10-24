#pragma once

/* @note login security. the original code never checked passwords (any password opened any account) and kept them as plain text.
 * now: passwords are stored as PBKDF2-SHA256 (100000 rounds, random 16 byte salt) "pbkdf2$rounds$salt$hash", checked on
 * both login steps (ltoken and tankIDName/tankIDPass, like GrowServer), compared in constant time, and 5 wrong tries lock
 * the account for 30 seconds. old plain text passwords still work once and are upgraded to a hash on that login */

extern std::string hash_password(const std::string &password);
extern bool check_password(const std::string &stored, const std::string &password);

/* @note forgotten password: put "growid|new password" in reset_password.txt next to main.exe and start the server.
 * only someone with access to the server's files can do this; the file is deleted right after */
extern void apply_password_reset();

/* @note a login in two main thread steps around the slow part (check_password()/hash_password(), any thread):
 * PBKDF2 took ~50-100 ms on the main loop per login and froze the game while players logged in.
 * `ip`: the player's IP, for the per-IP limits in tools/guard.hpp */
struct login_check
{
    std::string error{};                 // @note not empty: refused, the reason to show
    std::optional<std::string> stored{}; // @note the account's stored password; nullopt = no such account (a new one when registering)
};
extern login_check login_begin(const std::string &growid, const std::string &password, bool may_register, const std::string &ip);

/* @note after check_password(): `rehash` = hash_password() when the stored one was old plain text. @return empty if the login is allowed */
extern std::string login_end(const std::string &growid, bool password_ok, const std::string &rehash, const std::string &ip);

/* @note a new account is about to be made: counts it for the per-IP limit. @return empty if allowed */
extern std::string register_end(const std::string &ip);

/* @note every step at once, on the main thread (the tests). @return empty if the login is allowed, else the reason to show.
 * `may_register`: an unknown GrowID becomes a new account */
extern std::string authenticate(const std::string &growid, const std::string &password, bool may_register, const std::string &ip);

/* @return the GrowID as stored (its real upper/lower case), empty if there is no such account */
extern std::optional<std::string> account_name(const std::string &growid);

/* @return the GrowID of a user id (online first, else the database), "(unknown)" if gone */
extern std::string growid_of(int user_id);

/* @return "`9@Seven``" for staff (role color + @: moderator, developer, owner), "`2Name``" for everyone else */
extern std::string role_name(const std::string &growid, int role);

/* @return a user's name as the game shows it over their head (online), else role_name() from the database */
extern std::string owner_display(int user_id);

/* @return "[WORLD World Locked by @Seven]" (the owner's name in their own color / staff @ tag), what the game shows on entering, punching or building in someone else's world */
extern std::string world_locked_text(const std::string &world_name, int owner);
