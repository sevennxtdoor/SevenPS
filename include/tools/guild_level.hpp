#pragma once

/* @note guild levels 1-50. 1-15 are Growtopia's (growtopiawiki.com/w/Guilds), 16-50 are SevenPS's own (a guess that keeps GT's last step).
 * the guild's XP comes from the XP its members earn; the leader pays the gems to level up */
constexpr int GUILD_MAX_LEVEL = 50;

/* @note XP to go from `level` to the next one, 0 at the max level */
extern long long guild_xp_needed(int level);
/* @note gems to go from `level` to the next one, 0 at the max level */
extern int guild_gems_needed(int level);
/* @note GT: 5 members, 5 more per level */
extern std::size_t guild_max_members(int level);
