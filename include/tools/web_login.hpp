#pragma once

/* @note MikeTopia's own login page (resources/login.html), served by https/https.cpp on www.growtopia1.com:443.
 * before, loginurl was login-gurotopia.vercel.app: the password was typed on someone else's website and travelled back
 * to us inside the token (base64 of "growId=..&password=.."), so that site could read every password.
 * now: the page posts the GrowID + password to our own server, we check it here and hand out a session token
 * (64 random hex characters). the game only ever sees that token (ltoken, then tankIDPass), never the password.
 * tokens live in memory for 7 days (a server restart logs everyone out of the page, nothing else), at most 5 per account,
 * and only their SHA-256 is kept, so even a memory dump doesn't give working tokens.
 * threads: the web server runs on its own thread, but the database and auth.cpp aren't thread safe, so a login is queued
 * and answered by the main loop (web_login_tick). sessions themselves are behind a mutex */

/* @note https thread: blocks until the main loop answered (at most ~15 s). `form` = the url-encoded POST body. @return JSON */
extern std::string web_login_request(const std::string &form, const std::string &ip);

/* @note main loop: answers queued logins */
extern void web_login_tick();

/* @note main thread: what web_login_tick does for one request (also used by the tests). @return JSON */
extern std::string web_login_handle(const std::string &form, const std::string &ip);

/* @return a new token for this account */
extern std::string session_new(const std::string &growid);

/* @return the account this token belongs to, empty if unknown/expired. any thread */
extern std::string session_growid(const std::string &token);

/* @return the JSON the client expects when a token is good */
extern std::string token_json(const std::string &token);

/* @note "a=1&b=x%20y" (POST body or ?query) -> value of `key`, url-decoded */
extern std::string form_value(const std::string &form, const std::string &key);
