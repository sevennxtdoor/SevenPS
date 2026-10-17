#pragma once

/* @note SevenPS's economy: the World Lock stays the price reference (2000 gems in the store).
 * every gem that enters or leaves the game is counted per source, so staff can watch inflation with /economy */

/* @note `gems` > 0 = created (blocks, quiz, online, goals...), < 0 = destroyed (store, ranks, /sb...) */
extern void econ(std::string_view source, long long gems);

extern void economy_command(ENetEvent &event, const std::string_view text); // @note /economy (developer+)

/* @note moderators & developers can spawn items (/find, /give), so their accounts can't move items or gems to
 * other players: no drop, trade, vending or /pay. the owner is free. players who want to play use another account */
extern bool staff_locked(const ::peer &p);
extern void staff_locked_message(ENetPeer &p);

/* @note every /find, /give, /givegems and /giverank is written to the `staff_log` table, the owner reads it with /stafflog */
extern void staff_log(const ::peer &staff, std::string_view action, std::string_view target, int item, int amount);
extern void stafflog_command(ENetEvent &event, const std::string_view text);
