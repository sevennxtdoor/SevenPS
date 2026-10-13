#include "pch.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "onVariant/SetBux.hpp"
#include "onVariant/SetClothing.hpp"
#include "tools/random.hpp"
#include "tools/time.hpp"
#include "tools/jammer.hpp"
#include "tools/playmods.hpp"
#include "tools/profile.hpp"

#include "consumables.hpp"

namespace
{
    constexpr short GEMS = 112; // @note a loot entry with this id gives gems instead of an item

    struct loot { short id; int min, max, weight; };

    /* @note fragments that combine into one item when you have enough (GTOS' amounts) */
    struct combine { short id; int needed; short result; short rare_result; int rare_percent; std::string_view text; };
    constexpr combine combines[]{
        { 1234,   4, 1206, 0,    0,  "The shards form into `wDevil Wings``!" },
        { 3122,  16, 3120, 0,    0,  "The fragments form into `wTeeny Devil Wings``!" },
        { 3110,  25, 1166, 3112, 15, "The tatters form into a `wVampire Cape``!" },
        { 1212,  25, 1190, 0,    0,  "The fur forms into a cuddly `wBlack Cat``!" },
        { 2000,  20, 1998, 0,    0,  "The bones form into a `wSkeletal Dragon Claw``!" },
        { 1976,  10, 1974, 0,    0,  "The skulls form into a `wNightmare Magnifying Glass``!" },
        { 2410, 200, 2408, 0,    0,  "The power of the shards is compressed into an `2Emerald Lock``!" },
        { 4426, 200, 4428, 0,    0,  "The power of the shards is compressed into a `4Ruby Lock``!" },
        { 5662, 200, 5642, 0,    0,  "The dust stirs and swirls... `5Cupid`` appears before you!" },
    };

    constexpr std::array<std::string_view, 12> fortunes{
        "You will only get what you have the courage to pursue.",
        "A pleasant surprise is waiting for you.",
        "Your hard work will soon pay off.",
        "A friend asks only for your time, not your money.",
        "Today is a good day to build something new.",
        "The best world lock is a good friend.",
        "Luck favors the ones who keep punching.",
        "Someone in MikeTopia thinks you are awesome.",
        "Do not trade with strangers in a hurry.",
        "Big things come from small seeds.",
        "Your next drop will make you smile.",
        "Patience is the best fertilizer.",
    };
}

static ::peer &me(ENetEvent &event) { return *static_cast<::peer*>(event.peer->data); }

static void bubble(ENetEvent &event, const std::string &text)
{
    send_varlist(event.peer, { "OnTalkBubble", me(event).netid, text, 0u, 1u });
}

static bool has(const ::peer &p, short id, int count)
{
    auto slot = std::ranges::find(p.slots, id, &::slot::id);
    return slot != p.slots.end() && slot->count >= count;
}

/* @note into the backpack, what doesn't fit is dropped at the player's feet */
static void give(ENetEvent &event, ::world &world, short id, int count)
{
    if (id == GEMS)
    {
        me(event).gems += count;
        on::SetBux(event);
        return;
    }
    for (int left = count; left > 0; )
    {
        const int now = std::min(left, 200);
        left -= now;
        if (const u_short rest = modify_item_inventory(event, ::slot(id, now)); rest > 0)
            add_drop(event, ::slot(id, rest), me(event).pos, world);
    }
}

static void take(ENetEvent &event, short id, int count = 1)
{
    modify_item_inventory(event, ::slot(id, -count));
}

static std::string name_of(short id, int count)
{
    return (id == GEMS) ? std::format("`w{}`` gems", count) : std::format("`w{} {}``", count, id_to_item(id).raw_name);
}

/* @note one weighted roll from a loot table, given straight away. @return what was won, for the message */
static std::string roll(ENetEvent &event, ::world &world, std::span<const loot> table)
{
    int total = 0;
    for (const loot &l : table) total += l.weight;
    int pick = RandomRange(0, total - 1);
    for (const loot &l : table)
    {
        if ((pick -= l.weight) >= 0) continue;
        const int count = RandomRange(l.min, l.max);
        give(event, world, l.id, count);
        return name_of(l.id, count);
    }
    return "nothing";
}

static void effect(ENetEvent &event, std::string_view sound)
{
    send_action(*event.peer, "play_sfx", std::format("file|{}\ndelayMS|0\n", sound));
    send_particle_effect(event, me(event).pos, { 0.0f, 44.0f });
}

/* ---------- the items ---------- */

static bool grow_spray(ENetEvent &event, ::world &world, ::block &block, const ::pos &tile, const ::item &item)
{
    if (id_to_item(block.fg).type != type::SEED) { bubble(event, "Use this on a growing tree to speed up its growth."); return true; }
    auto tree = std::ranges::find(world.trees, tile, &::tree::pos);
    if (tree == world.trees.end()) return true;

    const u_int grow_time = id_to_item(block.fg).tick;
    const u_int age = ticks() - tree->tick;
    if (age >= grow_time) { bubble(event, "This tree has already bloomed, don't waste your spray on it!"); return true; }

    const u_int skip = (item.id == 1778) ? 24 * 3600 : 3600; // @note Deluxe Grow Spray: a day, Grow Spray Fertilizer: an hour
    tree->tick -= std::min(skip, grow_time - age);
    take(event, item.id);

    ::gamePacket update{};
    update.id = block.fg;
    update.punch = tile;
    send_tile_update(event, update, block, world);
    send_particle_effect(event, tile.by_32(), { 0.0f, 44.0f });
    bubble(event, std::format("`2The tree grows {}!``", (item.id == 1778) ? "a whole day" : "an hour"));
    return true;
}

static bool on_self(ENetEvent &event, const ::pos &tile)
{
    if (tile == me(event).pos.by_32(true)) return true;
    bubble(event, "Must be used on yourself.");
    return false;
}

static void fortune_cookie(ENetEvent &event, ::world &world, const ::item &item)
{
    static constexpr loot cookie[]{
        { GEMS, 50, 500, 60 }, { 202, 1, 2, 12 }, { 204, 1, 1, 7 }, { 206, 1, 1, 3 }, { 5766, 3, 3, 6 },
        { 5768, 4, 4, 6 }, { 9286, 1, 1, 6 },
    };
    static constexpr loot lucky[]{
        { GEMS, 300, 2000, 50 }, { 202, 2, 4, 10 }, { 204, 1, 2, 10 }, { 206, 1, 1, 8 }, { 4994, 1, 1, 6 },
        { 242, 1, 1, 8 }, { 7688, 1, 1, 4 }, { 5754, 1, 1, 1 }, { 7696, 1, 1, 1 },
    };
    take(event, item.id);
    const std::string won = roll(event, world, (item.id == 9286) ? std::span<const loot>{ lucky } : std::span<const loot>{ cookie });
    const std::string fortune = std::format("`5Fortune: {}``", fortunes[RandomRange(0, fortunes.size() - 1)]);
    bubble(event, fortune);
    on::ConsoleMessage(event.peer, fortune);
    on::ConsoleMessage(event.peer, std::format("You received {} from the {}.", won, item.raw_name));
    effect(event, "audio/cash_register.wav");
}

static void open_box(ENetEvent &event, ::world &world, const ::item &item)
{
    static constexpr loot winter_gift[]{
        { 242, 1, 1, 10 }, { 828, 1, 5, 15 }, { 68, 1, 1, 12 }, { 82, 1, 1, 12 }, { 98, 1, 1, 10 },
        { 404, 1, 1, 12 }, { 400, 1, 1, 12 }, { 3402, 1, 1, 5 }, { 3400, 1, 2, 12 },
    };
    static constexpr loot bronze[]{ { GEMS, 500, 3000, 80 }, { 202, 1, 3, 10 }, { 204, 1, 1, 6 }, { 2978, 1, 1, 4 } };
    static constexpr loot silver[]{ { GEMS, 2000, 7000, 75 }, { 204, 1, 2, 10 }, { 206, 1, 1, 8 }, { 242, 1, 2, 6 }, { 5078, 1, 1, 1 } };
    static constexpr loot gold[]{ { GEMS, 5000, 15000, 70 }, { 206, 1, 2, 10 }, { 242, 2, 5, 12 }, { 4994, 1, 1, 6 }, { 7912, 1, 1, 1 }, { 5078, 1, 1, 1 } };
    static constexpr loot winter_chest[]{ { GEMS, 3000, 12000, 75 }, { 242, 1, 3, 15 }, { 1360, 2, 5, 8 }, { 8834, 1, 1, 1 }, { 7912, 1, 1, 1 } };
    static constexpr loot spring_chest[]{ { GEMS, 4000, 15000, 75 }, { 242, 2, 4, 15 }, { 3402, 1, 2, 8 }, { 8834, 1, 1, 1 }, { 5078, 1, 1, 1 } };

    std::span<const loot> table{};
    switch (item.id)
    {
        case 1360: table = winter_gift; break;
        case 6204: table = bronze; break;
        case 6202: table = silver; break;
        case 6200: table = gold; break;
        case 7484: table = winter_chest; break;
        case 7954: table = spring_chest; break;
    }
    take(event, item.id);
    const std::string won = roll(event, world, table);
    bubble(event, std::format("You received {} from the {}!", won, item.raw_name));
    on::ConsoleMessage(event.peer, std::format("You received {} from the `w{}``!", won, item.raw_name));
    effect(event, "audio/cash_register.wav");
}

/* @note 5 random seeds, like GTOS: 4 of a low rarity and 1 rarer */
static void seed_pack(ENetEvent &event, ::world &world, const ::item &item)
{
    take(event, item.id);
    std::string got{};
    for (int i = 0; i < 5; ++i)
    {
        const int low = (i < 4) ? RandomRange(1, 9) : RandomRange(10, 14);
        for (int tries = 0; tries < 2000; ++tries)
        {
            const ::item &seed = id_to_item(static_cast<u_short>(RandomRange(2, static_cast<int>(items.size()) - 1)));
            if (seed.type != type::SEED || seed.raw_name.starts_with("null") || seed.rarity < low || seed.rarity > low + 1) continue;
            give(event, world, seed.id, 1);
            got += std::format("{}`w{}``", got.empty() ? "" : ", ", seed.raw_name);
            break;
        }
    }
    on::ConsoleMessage(event.peer, std::format("You opened a Small Seed Pack: {}", got));
    bubble(event, "You opened a `wSmall Seed Pack``!");
    effect(event, "audio/change_clothes.wav");
}

static void surgery_gatcha(ENetEvent &event, ::world &world, const ::item &item)
{
    take(event, item.id);
    for (short tool : { 1258, 1260, 1262, 1264, 1266, 1268, 4308, 4310, 4312, 4314, 4316, 4318 }) give(event, world, tool, 5);
    give(event, world, 1270, 1); // @note stitches
    give(event, world, 4296, 1); // @note Surg-E
    bubble(event, "You received a full set of `wSurgical Tools`` and a `wSurg-E``!");
    on::ConsoleMessage(event.peer, "You received 5 of every surgical tool, 1 Surgical Stitches and 1 Surg-E from the Surgery Gatcha.");
    effect(event, "audio/cash_register.wav");
}

static void gift_of_growganoth(ENetEvent &event, ::world &world, const ::item &item)
{
    static constexpr short prizes[]{ 1216, 1218, 1992, 1982, 1994, 1972, 1980, 1988, 1984, 3116, 3102, 3106, 3110, 4160, 4162, 4164,
        4154, 4158, 5224, 5222, 5232, 5240, 5248, 5238, 5256, 7116, 7108, 7110, 7128, 7112, 7114, 7130, 1198, 1208, 1222, 1200, 1220,
        1202, 1238, 1168, 1172, 1230, 1194, 1192, 1226, 1196, 1236, 1182, 1184, 1186, 1188, 1170, 10198, 10196, 10202, 10204, 10326,
        10324, 10322 }; // @note GTOS' list, all Halloween items
    take(event, item.id);
    const short prize = prizes[RandomRange(0, std::size(prizes) - 1)];
    give(event, world, prize, 1);
    const std::string text = std::format("You received `21 {}`` from the Gift of Growganoth.", id_to_item(prize).raw_name);
    bubble(event, text);
    on::ConsoleMessage(event.peer, text);
    effect(event, "audio/skel.wav");
}

static bool fragments(ENetEvent &event, ::world &world, const ::item &item)
{
    auto it = std::ranges::find(combines, item.id, &combine::id);
    if (it == std::end(combines)) return false;
    if (!has(me(event), item.id, it->needed))
    {
        bubble(event, std::format("`6You need `w{}`` {} to make something!``", it->needed, item.raw_name));
        return true;
    }
    take(event, item.id, it->needed);
    const short result = (it->rare_result && RandomRange(1, 100) <= it->rare_percent) ? it->rare_result : it->result;
    give(event, world, result, 1);
    bubble(event, std::format("`5SQUISH! ``{}", (result == it->rare_result) ? std::format("Lucky! You made an `w{}``!", id_to_item(result).raw_name) : std::string{ it->text }));
    on::ConsoleMessage(event.peer, std::format("You made `w1 {}``.", id_to_item(result).raw_name));
    effect(event, "audio/change_clothes.wav");
    return true;
}

bool use_consumable(ENetEvent &event, ::world &world, ::block &block, const ::pos &tile, const ::item &item)
{
    switch (item.id)
    {
        case 228: case 1778: return grow_spray(event, world, block, tile, item);
        case 5750: case 9286: if (on_self(event, tile)) fortune_cookie(event, world, item); return true;
        case 1360: case 6204: case 6202: case 6200: case 7484: case 7954: if (on_self(event, tile)) open_box(event, world, item); return true;
        case 5706: if (on_self(event, tile)) seed_pack(event, world, item); return true;
        case 9644: if (on_self(event, tile)) surgery_gatcha(event, world, item); return true;
        case 10386: if (on_self(event, tile)) gift_of_growganoth(event, world, item); return true;
        case 5664: // @note Valentine's Card -> Valentine's Dust
        {
            if (!on_self(event, tile)) return true;
            take(event, item.id);
            give(event, world, 5662, 1);
            bubble(event, "`5The card crumbles into Valentine's Dust...``");
            return true;
        }
        case 540: // @note Green Beer
        {
            take(event, item.id);
            peers(world.name, PEER_SAME_WORLD, [&](ENetPeer &p) { send_varlist(&p, { "OnTalkBubble", me(event).netid, "`2BURRRPPP...!``", 0u }); });
            return true;
        }
        case 764: // @note g-Virus: become a zombie (slow, green). Zombie Jammer only stops bites, not the virus itself
        {
            if (!on_self(event, tile)) return true;
            if (me(event).state & S_ZOMBIE) { bubble(event, "You are already a zombie!"); return true; }
            take(event, item.id);
            me(event).state |= S_ZOMBIE;
            on::SetClothing(*event.peer);
            send_varlist(event.peer, { "OnAddNotification", "interface/large/infected.rttex", "`4You are infected! Use an Antidote to get better.``", "audio/skel.wav", 0 });
            return true;
        }
        case 782: // @note Antidote
        {
            if (!on_self(event, tile)) return true;
            if (!(me(event).state & S_ZOMBIE)) { bubble(event, "You're not sick!"); return true; }
            take(event, item.id);
            me(event).state &= ~S_ZOMBIE;
            on::SetClothing(*event.peer);
            bubble(event, "`2You feel much better!``");
            return true;
        }
    }
    if (on_self(event, tile)) return fragments(event, world, item);
    return false;
}

/* ---------- GrowScan 9000 ---------- */

void growscan_dialog(ENetEvent &event, ::world &world)
{
    std::unordered_map<short, int> blocks{};
    int trees = 0, ready = 0, empty = 0;
    for (const ::block &b : world.blocks)
    {
        if (b.fg == 0 && b.bg == 0) ++empty;
        if (b.fg != 0) ++blocks[b.fg];
    }
    for (const ::tree &t : world.trees)
    {
        const ::block &b = world.blocks[cord(t.pos.x_int(), t.pos.y_int())];
        if (id_to_item(b.fg).type != type::SEED) continue;
        ++trees;
        if (ticks() - t.tick >= static_cast<u_int>(id_to_item(b.fg).tick)) ++ready;
    }
    std::vector<std::pair<short, int>> top(blocks.begin(), blocks.end());
    std::ranges::sort(top, std::greater{}, &std::pair<short, int>::second);

    int dropped = 0;
    for (const ::object &o : world.objects) dropped += o.count;

    std::string rows{};
    for (std::size_t i = 0; i < top.size() && i < 10; ++i)
        rows += std::format("add_label_with_icon|small|`w{}``: {}|left|{}|\n", id_to_item(top[i].first).raw_name, top[i].second, top[i].first);

    send_varlist(event.peer, { "OnDialogRequest", std::format(
        "set_default_color|`o\n"
        "add_label_with_icon|big|`wGrowScan 9000``|left|6016|\n"
        "add_spacer|small|\n"
        "add_textbox|World: `w{}``{}|left|\n"
        "add_textbox|Players here: `w{}`` - Empty tiles: `w{}``|left|\n"
        "add_textbox|Trees: `w{}`` (`2{}`` ready to harvest)|left|\n"
        "add_textbox|Dropped items: `w{}`` in `w{}`` piles|left|\n"
        "add_spacer|small|\n"
        "add_label|small|`wMost common blocks:``|left|\n"
        "{}"
        "end_dialog|growscan||OK|\n",
        world.name, jammer_tags(world), world.visitors, empty, trees, ready, dropped, world.objects.size(), rows) });
}
