#pragma once

extern bool is_nuked(const std::string &world_name);

/* moderator+ */
extern void kick(ENetEvent& event, const std::string_view text);
extern void ban(ENetEvent& event, const std::string_view text);
extern void unban(ENetEvent& event, const std::string_view text);
extern void nuke(ENetEvent& event, const std::string_view text);
extern void unnuke(ENetEvent& event, const std::string_view text);

/* world owner (or staff): lift every world ban in the current world */
extern void uba(ENetEvent& event, const std::string_view text);

/* world owner (or staff): world ban a player in your world */
extern void wban(ENetEvent& event, const std::string_view text);
