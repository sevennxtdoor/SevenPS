#pragma once

/* @note commands MikeTopia took from GTOS' list (our own code):
 * players: /pay /go /rules /mods /radio /info /status
 * moderator+: /warpto /mute /unmute /curse /uncurse /warn, developer+: /kickall */

extern void pay(ENetEvent &event, const std::string_view text);
extern void go(ENetEvent &event, const std::string_view text);
extern void rules(ENetEvent &event, const std::string_view text);
extern void mods(ENetEvent &event, const std::string_view text);
extern void radio(ENetEvent &event, const std::string_view text);
extern void info(ENetEvent &event, const std::string_view text);
extern void status(ENetEvent &event, const std::string_view text);
extern void warpto(ENetEvent &event, const std::string_view text);
extern void mute(ENetEvent &event, const std::string_view text);
extern void unmute(ENetEvent &event, const std::string_view text);
extern void curse(ENetEvent &event, const std::string_view text);
extern void uncurse(ENetEvent &event, const std::string_view text);
extern void warn(ENetEvent &event, const std::string_view text);
extern void kickall(ENetEvent &event, const std::string_view text);

/* @note seconds left, 0 = not muted / not cursed */
extern int muted_left(const ::peer &p);
extern int cursed_left(const ::peer &p);

extern void set_last_sb_world(const std::string &world); // @note /go goes there
extern bool radio_off(const ::peer &p);                   // @note doesn't want to see player /sb
