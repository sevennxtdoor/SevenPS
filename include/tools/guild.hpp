#pragma once

/* @note guilds: /guild create {name} (50000 gems, level 5+), invite/accept, ranks member < elder < co-leader < leader,
 * kick, promote/demote, leadership transfer, statement, flag (two items you own), disband, /gc guild chat.
 * saved in the `guild` table (members as "growid:rank,..."), kept in memory while the server runs.
 * the flag over the head uses OnGuildDataChanged + "|showGuild" in OnCountryState, as GTOS does (unverified in game) */

extern void load_guilds(); // @note startup

extern void guild_command(ENetEvent &event, const std::string_view text); // @note /guild ...
extern void gc_command(ENetEvent &event, const std::string_view text);    // @note /gc {message}

extern bool in_guild(const std::string &growid);

/* @note visuals: `viewer` sees `target`'s guild flag (nothing if target has no guild) */
extern void guild_show(ENetPeer &viewer, const ::peer &target);
/* @note everyone in target's world (target too) gets their current guild state */
extern void guild_broadcast(ENetPeer &target);
