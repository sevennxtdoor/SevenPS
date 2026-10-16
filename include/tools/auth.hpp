#pragma once

/* @note login security. Gurotopia never checked passwords (any password opened any account) and kept them as plain text.
 * now: passwords are stored as PBKDF2-SHA256 (100000 rounds, random 16 byte salt) "pbkdf2$rounds$salt$hash", checked on
 * both login steps (ltoken and tankIDName/tankIDPass, like GrowServer), compared in constant time, and 5 wrong tries lock
 * the account for 30 seconds. old plain text passwords still work once and are upgraded to a hash on that login */

extern std::string hash_password(const std::string &password);
extern bool check_password(const std::string &stored, const std::string &password);

/* @return empty if the login is allowed, else the reason to show. `may_register`: an unknown GrowID becomes a new account */
/* @note forgotten password: put "growid|new password" in reset_password.txt next to main.exe and start the server.
 * only someone with access to the server's files can do this; the file is deleted right after */
extern void apply_password_reset();

/* `ip`: the player's IP, for the per-IP limits in tools/guard.hpp */
extern std::string authenticate(const std::string &growid, const std::string &password, bool may_register, const std::string &ip);
