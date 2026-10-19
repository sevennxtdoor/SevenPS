#pragma once

/* @note things that make SevenPS feel alive, inspired by CreativePS (our own code, not copied):
 * automatic events, rotating broadcasts, join announcements, /top leaderboards, /start and /event,
 * and CreativePS' /find rule: most items are free, shop & special items are not */

enum class event_type : u_char { NONE, GEMS, XP, LUCKY, SURGERY };

extern event_type active_event();
extern bool event_on(event_type type);

/* @note a message to everyone logged in. notification = also the big popup on the right */
extern void system_broadcast(const std::string &message, bool notification = false);
extern void announce_level(const ::peer &p, int level);                  // @note every 10 levels, called on level up
extern void announce_achievement(const ::peer &p, std::string_view name); // @note called when an achievement unlocks

extern void cps_tick(); // @note main loop, about once a second: events & broadcasts
extern bool announce_join(ENetEvent &event); // @note welcome + "X joined" + current event, called on enter_game. true = a new player (first login ever)
extern void queue_start_join(ENetPeer &p);   // @note sends the player to START a moment later, once the client has shown the world menu

/* @note /find rule. @return empty if the item can be found, else the reason it can't */
extern std::string find_blocked(const ::peer &p, short id);

extern void event_command(ENetEvent &event, const std::string_view text); // @note /event, /event start {type} {minutes}, /event stop
extern void top(ENetEvent &event, const std::string_view text);           // @note /top (menu), /top {level|gems|rich|surgeon|played}
extern void top_return(ENetEvent &event, const ::hPipe &hPipe);           // @note dialog_return "mt_top"
extern void start_command(ENetEvent &event, const std::string_view text); // @note /start
extern void msg_command(ENetEvent &event, const std::string_view text);   // @note /msg {growid} {message}
extern void online_command(ENetEvent &event, const std::string_view text); // @note /online

/* @note chat quiz: @return true if `text` was the right answer (the player won) */
extern bool check_quiz(ENetEvent &event, const std::string &text);
