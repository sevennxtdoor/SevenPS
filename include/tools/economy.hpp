#pragma once

/* @note MikeTopia's economy: the World Lock stays the price reference (2000 gems in the store).
 * every gem that enters or leaves the game is counted per source, so staff can watch inflation with /economy */

/* @note `gems` > 0 = created (blocks, quiz, online, goals...), < 0 = destroyed (store, ranks, /sb...) */
extern void econ(std::string_view source, long long gems);

extern void economy_command(ENetEvent &event, const std::string_view text); // @note /economy (developer+)
