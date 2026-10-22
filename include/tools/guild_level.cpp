#include "pch.hpp"

#include "guild_level.hpp"

namespace
{
    /* @note growtopiawiki.com/w/Guilds: XP and gems for 1 -> 2 ... 14 -> 15 */
    constexpr std::array<long long, 14> GT_XP{ 405000, 1215000, 2430000, 4050000, 7290000, 11430000, 16200000,
                                               21870000, 28350000, 38700000, 48600000, 60547500, 73710000, 91125000 };
    constexpr std::array<int, 14> GT_GEMS{ 100000, 125000, 150000, 200000, 250000, 300000, 350000,
                                           400000, 500000, 600000, 700000, 800000, 900000, 1000000 };
    /* @note SevenPS 15 -> 50: GT's last XP step (73,710,000 -> 91,125,000) and 100,000 more gems every level */
    constexpr long long XP_STEP = 91125000 - 73710000;
    constexpr int GEMS_STEP = 100000;
}

long long guild_xp_needed(int level)
{
    if (level < 1 || level >= GUILD_MAX_LEVEL) return 0;
    if (level <= static_cast<int>(GT_XP.size())) return GT_XP[level - 1];
    return GT_XP.back() + (level - static_cast<int>(GT_XP.size())) * XP_STEP;
}

int guild_gems_needed(int level)
{
    if (level < 1 || level >= GUILD_MAX_LEVEL) return 0;
    if (level <= static_cast<int>(GT_GEMS.size())) return GT_GEMS[level - 1];
    return GT_GEMS.back() + (level - static_cast<int>(GT_GEMS.size())) * GEMS_STEP;
}

std::size_t guild_max_members(int level)
{
    return static_cast<std::size_t>(5 * std::clamp(level, 1, GUILD_MAX_LEVEL));
}
