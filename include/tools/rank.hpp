#pragma once

/* @note CreativePS-style ranks (VIP ... GOD) bought with gems. separate from staff roles (@Mod, @Dev, @Owner).
 * configured in resources/ranks.txt (name, color, price, bonuses, perks), read at server start */

struct rank
{
    int level{};          // @note 1 = lowest
    std::string name{};   // @note "VIP"
    std::string color{};  // @note "`2"
    int price{};          // @note gems
    int gem_bonus{};      // @note % extra gems from breaking blocks & online rewards
    int xp_bonus{};       // @note % extra XP
    int slots{};          // @note extra backpack slots (total for this rank, not on top of the previous one)
    bool sb_free{}, titles{}, nick{};
};

extern void load_ranks();
extern const ::rank *get_rank(const ::peer &p); // @note nullptr = no rank
extern int gem_bonus(const ::peer &p, int gems); // @note gems with the rank bonus applied
extern int xp_bonus(const ::peer &p, int xp);

extern void rank_command(ENetEvent &event, const std::string_view text);     // @note /rank - the rank shop
extern void giverank(ENetEvent &event, const std::string_view text);         // @note /giverank {growid} {level} (developer+)
extern void rank_return(ENetEvent &event, const ::hPipe &hPipe);             // @note dialog_return "mt_rank"
