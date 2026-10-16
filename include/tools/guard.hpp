#pragma once

/* @note protection against bots and floods. everything is kept in memory (a restart clears it) and logged as "[guard] ...".
 * - packets: more than 40 text packets or 400 packets in one second = flooding -> kicked, IP blocked for 5 minutes
 * - connections: more than 20 new connections from one IP in a minute -> IP blocked for 5 minutes
 *   (ENet itself allows at most 4 connections at once from one IP, see host->duplicatePeers in main.cpp)
 * - logins: 10 wrong passwords from one IP within 15 minutes -> that IP can't log in for 15 minutes
 *   (stops a bot trying many accounts; auth.cpp still locks a single account after 5 wrong tries)
 * - new accounts: at most 3 per IP per day, so a bot can't fill the database with accounts
 * this computer (127.x.x.x) is never blocked by IP, so the owner can't lock himself out. if the server is ever put behind a
 * tunnel/proxy every player would look like 127.0.0.1 and the IP limits would stop working: then this needs the real IP */

extern std::string peer_ip(const ENetPeer &p);

/* @return false = refuse this new connection (blocked IP or too many new connections) */
extern bool guard_connect(ENetPeer &p);

/* @return false = this peer is flooding and was disconnected. `text` = action packet (type 2/3) */
extern bool guard_packet(ENetPeer &p, bool text);

/* @return empty if this IP may try to log in, else the reason */
extern std::string guard_login_blocked(const std::string &ip);
extern void guard_login_failed(const std::string &ip);

/* @return false if this IP made too many accounts today. guard_registered() counts one */
extern bool guard_may_register(const std::string &ip);
extern void guard_registered(const std::string &ip);

/* @note console logging: hides passwords/tokens in a packet before it is printed */
extern std::string guard_redact(const std::string &header);
