#pragma once

/* @note guilds: created from Social Portal > Create Guild (200,000 gems, level 5+) in a World Locked world you own,
 * which becomes the guild's home world (its lock can't be broken, guild ranks can get access).
 * invite/accept (also by wrench), ranks member < elder < co-leader < leader, kick, promote/demote, leadership transfer (the home world goes along),
 * statement, flag (two items you own), disband, /gc guild chat.
 * saved in the `guild` table (members as "growid:rank,..."), kept in memory while the server runs.
 * the flag over the head uses OnGuildDataChanged + "|showGuild" in OnCountryState, as GTOS does (unverified in game) */

extern void load_guilds(); // @note startup

extern void guild_command(ENetEvent &event, const std::string_view text); // @note /guild ...
extern void gc_command(ENetEvent &event, const std::string_view text);    // @note /gc {message}
extern void guild_return(ENetEvent &event, const ::hPipe &hPipe);          // @note dialog_return "mt_guild" (Create Guild button)

extern bool in_guild(const std::string &growid);

/* @note wrench menu on another player: their guild and rank, plus Invite to Guild / Promote / Demote / Kick from Guild when you may */
extern std::string guild_wrench_lines(const ::peer &viewer, const ::peer &target);
extern bool guild_wrench_button(ENetEvent &event, const ::hPipe &hPipe); // @note popup buttons guild_*, true if handled
extern void guild_invite_return(ENetEvent &event, const ::hPipe &hPipe);  // @note dialog_return "mt_guild_invite" (Accept / Decline)

constexpr short GUILD_LOCK = 5814;

/* @note Create Guild turns the world lock into a Guild Lock (5814), as in GT. the Guild Lock tile carries 16 more bytes than a
 * World Lock (gtworld-r skips them for 5814; what's in them is unknown, SevenPS sends zeros). without them the game crashed.
 * still being tested in game: false keeps the world lock in place and turns Guild Lock tiles back on world load */
constexpr bool GUILD_LOCK_TILE = true;

extern bool is_guild_home(const ::world &world);
/* @note world load: a Guild Lock tile that isn't a guild's home (or GUILD_LOCK_TILE off) turns back into the lock it replaced */
extern void guild_lock_fix(::world &world);

/* @note a guild member whose rank the leader ticked may build in the guild's home world */
extern bool guild_access(const ::peer &p, const ::world &world);
/* @note user ids of those members that are online, for the lock data the game reads */
extern std::vector<int> guild_access_ids(const ::world &world);
/* @note extra lines for the Guild Lock wrench dialog ("" if this world is no guild's home), read back by guild_lock_edit() */
extern std::string guild_lock_section(const ::world &world);
extern void guild_lock_edit(const ::world &world, const ::hPipe &hPipe);

/* @note "|showGuild" in OnCountryState: every guild member (see SHOW_FLAG in guild.cpp) */
extern bool guild_flag_shown(const std::string &growid);
/* @note every XP a member earns goes to their guild (growtopiawiki.com/w/Guilds), the leader levels it up with gems */
extern void guild_add_xp(const std::string &growid, int xp);
extern void save_guilds(); // @note autosave: guilds whose XP changed
/* @note the guild line in your own wrench menu (name, rank, a button to the guild menu); "" without a guild */
extern std::string guild_self_lines(const ::peer &self);

/* @note visuals: `viewer` sees `target`'s guild flag (nothing if target has no guild) */
extern void guild_show(ENetPeer &viewer, const ::peer &target);
/* @note everyone in target's world (target too) gets their current guild state */
extern void guild_broadcast(ENetPeer &target);
