#include "pch.hpp"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include "tools/random.hpp"
#include "tools/string.hpp"
#include "tools/area_lock.hpp"
#include "tools/magplant.hpp"
#include "tools/profile.hpp"
#include "automate/holiday.hpp"

#include "special_tiles.hpp"

namespace
{
    constexpr short RED_CRYSTAL = 2242, BLACK_CRYSTAL = 2250;
    constexpr short CHI_HARMONIZER = 5258, HARMONIC_LOCK = 5260, RAINBOW_CRYSTAL_BLOCK = 3146, WATER_BUCKET = 822;
    constexpr short BUNNY_EGG = 610, MAGIC_EGG_ITEM = 611;
    constexpr int CLUSTER = 5;

    struct recipe { short id; int count; bool valentine; };
    struct egg_prize { int from, to; short id; int count, weight; };

    /* ---------- resources/crystals.txt, chi.txt & bunny_egg.txt, read again whenever they change ---------- */

    std::unordered_map<std::string, recipe> recipes{}; // @note sorted crystals ("11224") -> prize
    int chi_min = 20, chi_max = 120, accuracy_normal = 6, accuracy_harmonizer = 30, accuracy_lock = 15, rainbow_percent = 17;
    std::filesystem::file_time_type crystals_at{};

    std::unordered_map<short, u_char> chi_of{}; // @note item -> 0 Earth, 1 Fire, 2 Air, 3 Water
    std::filesystem::file_time_type chi_at{};

    std::vector<egg_prize> egg_prizes{};
    int egg_max = 2001;
    std::filesystem::file_time_type eggs_at{};

    bool changed(const char *path, std::filesystem::file_time_type &at)
    {
        std::error_code ec;
        const auto now = std::filesystem::last_write_time(path, ec);
        if (ec || now == at) return false;
        at = now;
        return true;
    }

    std::vector<std::vector<std::string>> lines_of(const char *path)
    {
        std::vector<std::vector<std::string>> out{};
        std::ifstream file(path);
        for (std::string line; std::getline(file, line); )
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line.starts_with('#')) continue;
            out.emplace_back(readch(line, '|'));
        }
        return out;
    }

    void load_crystals()
    {
        if (!changed("resources/crystals.txt", crystals_at)) return;
        recipes.clear();
        for (std::vector<std::string> &v : lines_of("resources/crystals.txt"))
        {
            const auto num = [&v](std::size_t i) { return (i < v.size()) ? atoi(v[i].c_str()) : 0; };
            if (v[0] == "recipe" && v.size() >= 4)
            {
                std::string key = v[1];
                std::ranges::sort(key);
                if (key.size() != CLUSTER || std::ranges::any_of(key, [](char c) { return c < '1' || c > '5'; })) continue;
                recipes[key] = recipe{ static_cast<short>(num(2)), std::max(1, num(3)), v.size() > 5 && v[5] == "valentine" };
            }
            else if (v[0] == "chi" && v.size() >= 3) chi_min = num(1), chi_max = std::max(num(1), num(2));
            else if (v[0] == "accuracy" && v.size() >= 4) accuracy_normal = num(1), accuracy_harmonizer = num(2), accuracy_lock = num(3);
            else if (v[0] == "rainbow") rainbow_percent = num(1);
        }
        std::printf("crystals: %zu recipe(s)\n", recipes.size());
    }

    void load_chi()
    {
        if (!changed("resources/chi.txt", chi_at)) return;
        chi_of.clear();
        for (std::vector<std::string> &v : lines_of("resources/chi.txt"))
            if (v.size() >= 2 && atoi(v[1].c_str()) >= 0 && atoi(v[1].c_str()) <= 3)
                chi_of[static_cast<short>(atoi(v[0].c_str()))] = static_cast<u_char>(atoi(v[1].c_str()));
        std::printf("chi: %zu item(s)\n", chi_of.size());
    }

    void load_eggs()
    {
        if (!changed("resources/bunny_egg.txt", eggs_at)) return;
        egg_prizes.clear();
        for (std::vector<std::string> &v : lines_of("resources/bunny_egg.txt"))
        {
            const auto num = [&v](std::size_t i) { return (i < v.size()) ? atoi(v[i].c_str()) : 0; };
            if (v[0] == "max") egg_max = std::max(1, num(1));
            else if (v[0] == "prize" && v.size() >= 6 && num(3) > 0 && num(5) > 0)
                egg_prizes.emplace_back(egg_prize{ num(1), num(2), static_cast<short>(num(3)), std::max(1, num(4)), num(5) });
        }
        std::printf("bunny egg: %zu prize(s)\n", egg_prizes.size());
    }

    /* ---------- state ---------- */

    struct special_tile
    {
        ::pos pos{};
        std::string crystals{}; // @note crystal cluster: "1".."5" per crystal, sorted once it has 5
        int chi[4]{};           // @note what the cluster wants: Earth, Fire, Air, Water
        u_int eggs{};           // @note bunny egg: Magic Eggs in it
    };
    /* @note world name -> tiles. never erased, same reason as vending machines (world copies) */
    std::unordered_map<std::string, std::vector<special_tile>> tiles{};

    ::peer &me(ENetEvent &event) { return *static_cast<::peer*>(event.peer->data); }

    void bubble(ENetEvent &event, const std::string &text)
    {
        send_varlist(event.peer, { "OnTalkBubble", me(event).netid, text, 0u, 1u });
    }

    bool is_crystal(short id) { return id >= RED_CRYSTAL && id <= BLACK_CRYSTAL && id % 2 == 0; }
    char crystal_code(short id) { return static_cast<char>('1' + (id - RED_CRYSTAL) / 2); }
    short crystal_id(char code) { return static_cast<short>(RED_CRYSTAL + (code - '1') * 2); }

    short fg_at(::world &world, const ::pos &tile)
    {
        return in_world(world, tile.x_int(), tile.y_int()) ? world.blocks[cord(tile.x_int(), tile.y_int())].fg : 0;
    }

    special_tile *find(::world &world, const ::pos &tile)
    {
        auto &list = tiles[world.name];
        auto it = std::ranges::find(list, tile, &special_tile::pos);
        return (it != list.end()) ? &*it : nullptr;
    }

    void forget(::world &world, const ::pos &tile)
    {
        std::erase_if(tiles[world.name], [&tile](const special_tile &t) { return t.pos == tile; });
    }

    special_tile &new_cluster(::world &world, const ::pos &tile, short first)
    {
        load_crystals();
        forget(world, tile);
        special_tile &t = tiles[world.name].emplace_back(special_tile{ .pos = tile, .crystals = std::string(1, crystal_code(first)) });
        for (int &c : t.chi) c = RandomRange(chi_min, chi_max + 1);
        return t;
    }

    /* @note found again after a restart without its data (or placed before this update): a single crystal / egg */
    special_tile *find_or_make(::world &world, const ::pos &tile)
    {
        if (special_tile *t = find(world, tile)) return t;
        const short fg = fg_at(world, tile);
        if (is_crystal(fg)) return &new_cluster(world, tile, fg);
        if (fg == BUNNY_EGG) return &tiles[world.name].emplace_back(special_tile{ .pos = tile, .eggs = 1 });
        return nullptr;
    }

    void update_tile(ENetEvent &event, ::world &world, const ::pos &tile)
    {
        ::gamePacket update{};
        update.punch = tile;
        send_tile_update(event, update, world.blocks[cord(tile.x_int(), tile.y_int())], world);
    }

    void take(ENetEvent &event, ::world &world, short id, bool from_remote)
    {
        if (from_remote) magplant_remote_used(event, world);
        else modify_item_inventory(event, ::slot(id, -1));
    }

    /* ---------- Harmonic Crystals ---------- */

    const recipe *recipe_of(const std::string &crystals)
    {
        load_crystals();
        auto it = recipes.find(crystals);
        if (it == recipes.end()) return nullptr;
        if (it->second.valentine && holiday != H_VALENTINES) return nullptr; // @note Heartstone: Valentine's Week only
        return &it->second;
    }

    /* @note the Chi around a cluster: the backgrounds of the 9 tiles, the other 8 foregrounds and water on them (GTopia World.cpp) */
    std::array<int, 4> chi_around(::world &world, const ::pos &tile)
    {
        load_chi();
        std::array<int, 4> total{};
        const auto add = [&](short id) {
            if (id == 0) return;
            auto it = chi_of.find(id);
            if (it == chi_of.end()) return;
            const short rarity = id_to_item(id).rarity;
            if (rarity > 0 && rarity < 999) total[it->second] += rarity;
        };
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
            {
                const int x = tile.x_int() + dx, y = tile.y_int() + dy;
                if (!in_world(world, x, y)) continue;
                const ::block &b = world.blocks[cord(x, y)];
                add(b.bg);
                if (dx == 0 && dy == 0) continue;
                add(b.fg);
                if (b.state[3] & S_WATER) total[3] += id_to_item(WATER_BUCKET).rarity;
            }
        return total;
    }

    int accuracy(::world &world, const ::pos &tile)
    {
        load_crystals();
        if (world.blocks[cord(tile.x_int(), tile.y_int())].bg == CHI_HARMONIZER) return accuracy_harmonizer;
        if (std::ranges::any_of(world.blocks, [](const ::block &b) { return b.fg == HARMONIC_LOCK; })) return accuracy_lock;
        return accuracy_normal;
    }

    bool in_harmony(::world &world, const ::pos &tile, const special_tile &t)
    {
        const std::array<int, 4> have = chi_around(world, tile);
        const int acc = accuracy(world, tile);
        for (int i = 0; i < 4; ++i)
            if (std::abs(t.chi[i] - have[i]) > acc) return false;
        return true;
    }

    /* @note wiki (May 2022): crystals can't be placed in public worlds nor worlds where you're only an admin */
    void check_crystal_world(ENetEvent &event, ::world &world)
    {
        const ::peer &p = me(event);
        if (world.owner == 0 || world.owner != p.user_id || world.is_public)
            throw std::runtime_error("Crystals can only be placed in a World Locked world you own that isn't public.");
    }

    bool add_crystal(ENetEvent &event, ::world &world, ::block &block, const ::pos &tile, const ::item &item, bool from_remote)
    {
        if (!is_crystal(block.fg)) throw std::runtime_error("You can only use crystals on blank tiles or existing crystals.");
        check_crystal_world(event, world);
        special_tile *t = find_or_make(world, tile);
        if (t->crystals.size() >= CLUSTER) throw std::runtime_error("That crystal's energy is already at its highest vibration!");

        take(event, world, item.id, from_remote);
        t->crystals += crystal_code(item.id);
        if (t->crystals.size() == CLUSTER)
        {
            std::ranges::sort(t->crystals);
            if (recipe_of(t->crystals) == nullptr)
            {
                block.fg = 0, block.hits[0] = 0;
                forget(world, tile);
                bubble(event, "`4The crystals clashed and shattered!``");
            }
            else bubble(event, "`2The crystals have reached their highest vibration!``");
        }
        update_tile(event, world, tile);
        return true;
    }

    void harmonize(ENetEvent &event, ::world &world, ::block &block, const ::pos &tile, special_tile &t, const recipe &r)
    {
        const std::string crystals = t.crystals;
        magplant_drop(event, ::slot(r.id, r.count), tile.by_32(), world);
        if (fg_at(world, ::pos{ tile.x_int(), tile.y_int() + 1 }) == RAINBOW_CRYSTAL_BLOCK && RandomRange(0, 100) < rainbow_percent)
            magplant_drop(event, ::slot(crystal_id(crystals[RandomRange(0, CLUSTER)]), 1), tile.by_32(), world);

        block.fg = 0, block.hits[0] = 0;
        forget(world, tile);
        if (block.bg == CHI_HARMONIZER)
        {
            block.bg = 0, block.hits[1] = 0;
            bubble(event, std::format("`2Your Chi Harmonizer helped to resonate the elements! `wThe crystals became {}``!", (r.count > 1) ? std::format("{} {}", r.count, id_to_item(r.id).raw_name) : id_to_item(r.id).raw_name));
        }
        else bubble(event, std::format("`2The crystals reveal their true essence: `w{}``!", (r.count > 1) ? std::format("{} {}", r.count, id_to_item(r.id).raw_name) : id_to_item(r.id).raw_name));
        send_action(*event.peer, "play_sfx", "file|audio/success.wav\ndelayMS|0\n");
        me(event).add_xp(event, 20);
        update_tile(event, world, tile);
    }

    std::string chi_advice(::world &world, const ::pos &tile, const special_tile &t)
    {
        const std::array<int, 4> have = chi_around(world, tile);
        const int acc = accuracy(world, tile);
        constexpr std::array<const char*, 4> names{ "Earth", "Fire", "Air", "Water" };
        std::string advice{};
        for (int i = 0; i < 4; ++i)
        {
            const int diff = t.chi[i] - have[i];
            if (std::abs(diff) <= acc) continue;
            const int a = std::abs(diff);
            const char *how = (a > acc * 5) ? "vastly " : (a > acc * 3) ? "far " : (a > acc * 2) ? "" : "slightly ";
            advice += std::format("{}{}{} {}", advice.empty() ? "" : ", ", how, (diff > 0) ? "more" : "less", names[i]);
        }
        if (advice.empty()) return "The crystals are in perfect harmonic resonance with the elements. A single tap should reveal their true essence.";
        return "The crystals are out of alignment with the elements. They need to be surrounded by " + advice + " to seek a higher form.";
    }

    void crystal_dialog(ENetEvent &event, ::world &world, const ::pos &tile, const ::item &item, const special_tile &t)
    {
        std::string text = "This is a cluster of ";
        constexpr std::array<const char*, 5> colors{ "red", "green", "blue", "white", "black" };
        constexpr std::array<const char*, 5> amounts{ "a few ", "several ", "lots of ", "tons of ", "nothing but " };
        std::string sorted = t.crystals;
        std::ranges::sort(sorted);
        for (std::size_t i = 0; i < sorted.size(); )
        {
            const char c = sorted[i];
            std::size_t n = 0;
            while (i < sorted.size() && sorted[i] == c) ++n, ++i;
            text += std::format("{}{}{}", amounts[std::min<std::size_t>(n, 5) - 1], colors[c - '1'], (i < sorted.size()) ? ", " : "");
        }
        text += " crystals.";
        std::string more{};
        if (t.crystals.size() < CLUSTER)
            more = std::format("add_textbox|It can hold `w{}`` more crystal{}.|left|\n", CLUSTER - t.crystals.size(), (CLUSTER - t.crystals.size() == 1) ? "" : "s");
        else if (can_build(me(event), world, tile))
            more = std::format("add_textbox|{}|left|\n", chi_advice(world, tile, t));
        send_varlist(event.peer, {
            "OnDialogRequest",
            std::format(
                "set_default_color|`o\n"
                "add_label_with_icon|big|`wCrystal Cluster``|left|{}|\n"
                "add_textbox|{}|left|\n"
                "{}"
                "end_dialog|popup||Cool, man|\n",
                item.id, text, more
            )
        });
    }

    /* ---------- Bunny Egg ---------- */

    std::string egg_size(u_int eggs) { return std::format("{}.{}%", eggs / 10, eggs % 10); }

    void egg_prize_drop(ENetEvent &event, ::world &world, const ::pos &tile, u_int eggs)
    {
        load_eggs();
        int total = 0;
        for (const egg_prize &e : egg_prizes) if (static_cast<int>(eggs) >= e.from && static_cast<int>(eggs) <= e.to) total += e.weight;
        if (total == 0)
        {
            bubble(event, "`4SPLAT!`` An egg hit you in the face! (grow it past `w10%`` for a prize)");
            return;
        }
        int pick = RandomRange(0, total);
        for (const egg_prize &e : egg_prizes)
        {
            if (static_cast<int>(eggs) < e.from || static_cast<int>(eggs) > e.to || (pick -= e.weight) >= 0) continue;
            magplant_drop(event, ::slot(e.id, e.count), tile.by_32(), world);
            bubble(event, std::format("`wThe {} Bunny Egg cracks open: `2{}``!", egg_size(eggs), (e.count > 1) ? std::format("{} {}", e.count, id_to_item(e.id).raw_name) : id_to_item(e.id).raw_name));
            return;
        }
    }

    bool grow_egg(ENetEvent &event, ::world &world, ::block &block, const ::pos &tile, bool from_remote)
    {
        load_eggs();
        special_tile *t = find_or_make(world, tile);
        if (static_cast<int>(t->eggs) >= egg_max) throw std::runtime_error("That egg can't get any bigger!");

        take(event, world, MAGIC_EGG_ITEM, from_remote);
        ++t->eggs;
        if (t->eggs > 1000)
        {
            const double burst = (t->eggs - 1000) / 100000.0; // @note GTopia: every egg past 100% may burst it
            if (RandomRange(0, 100000) < static_cast<int>(burst * 100000))
            {
                const u_int eggs = t->eggs;
                block.fg = 0, block.hits[0] = 0;
                forget(world, tile);
                bubble(event, "`4The oversized egg burst!``");
                egg_prize_drop(event, world, tile, eggs);
                update_tile(event, world, tile);
                return true;
            }
        }
        bubble(event, (t->eggs >= 1801) ? std::format("`4This oversized egg is so big it could burst at any moment! ({})``", egg_size(t->eggs)) :
                      (t->eggs >= 1601) ? std::format("`8This oversized egg has a huge chance to burst! ({})``", egg_size(t->eggs)) :
                      (t->eggs >= 1400) ? std::format("`6This oversized egg has a good chance to burst! ({})``", egg_size(t->eggs)) :
                                          std::format("`wThe Bunny Egg grew to `2{}``.", egg_size(t->eggs)));
        update_tile(event, world, tile);
        return true;
    }
}

bool special_place(ENetEvent &event, ::world &world, ::block &block, const ::pos &tile, const ::item &item, bool from_remote)
{
    if (is_crystal(item.id))
    {
        if (block.fg != 0) return add_crystal(event, world, block, tile, item, from_remote);
        check_crystal_world(event, world);
        return false; // @note an empty tile: placed the normal way, special_placed() starts the cluster
    }
    if (item.id == MAGIC_EGG_ITEM)
    {
        if (block.fg == BUNNY_EGG) return grow_egg(event, world, block, tile, from_remote);
        if (block.fg != 0) return false; // @note on a tree it splices like any seed
        block.fg = BUNNY_EGG, block.hits[0] = 0;
        forget(world, tile);
        tiles[world.name].emplace_back(special_tile{ .pos = tile, .eggs = 1 });
        take(event, world, MAGIC_EGG_ITEM, from_remote);
        add_stat(event, "placed");
        bubble(event, "`wA Bunny Egg! Use more `2Magic Eggs`` on it to grow it, break it for a surprise.");
        update_tile(event, world, tile);
        return true;
    }
    return false;
}

void special_placed(::world &world, const ::pos &tile, const ::item &item)
{
    if (is_crystal(item.id)) new_cluster(world, tile, item.id);
}

bool special_tile_punch(ENetEvent &event, ::world &world, ::block &block, const ::pos &tile, const ::item &item)
{
    if (!is_crystal(item.id) || block.fg != item.id) return false;
    special_tile *t = find_or_make(world, tile);
    if (t == nullptr || t->crystals.size() < CLUSTER) return false;
    const recipe *r = recipe_of(t->crystals);
    if (r == nullptr || !in_harmony(world, tile, *t)) return false; // @note out of tune: it breaks like any block
    if (!can_build(me(event), world, tile)) return false;
    harmonize(event, world, block, tile, *t, *r);
    return true;
}

bool special_tile_break(ENetEvent &event, ::world &world, const ::pos &tile, const ::item &item)
{
    if (is_crystal(item.id)) { forget(world, tile); return false; }
    if (item.id != BUNNY_EGG) return false;
    const special_tile *t = find(world, tile);
    const u_int eggs = t ? t->eggs : 1;
    forget(world, tile);
    egg_prize_drop(event, world, tile, eggs);
    return true;
}

bool special_tile_wrench(ENetEvent &event, ::world &world, const ::pos &tile, const ::item &item)
{
    if (is_crystal(item.id))
    {
        if (special_tile *t = find_or_make(world, tile)) crystal_dialog(event, world, tile, item, *t);
        return true;
    }
    if (item.id == BUNNY_EGG)
    {
        const special_tile *t = find_or_make(world, tile);
        const u_int eggs = t ? t->eggs : 1;
        send_varlist(event.peer, {
            "OnDialogRequest",
            std::format(
                "set_default_color|`o\n"
                "add_label_with_icon|big|`wBunny Egg``|left|{}|\n"
                "add_textbox|This egg is `2{}`` big (`w{}`` Magic Egg{}). Each Magic Egg grows it by 0.1%.|left|\n"
                "add_textbox|Break it for a surprise: the bigger the egg, the better the prize. Past 100% it may burst!|left|\n"
                "end_dialog|popup||OK|\n",
                BUNNY_EGG, egg_size(eggs), eggs, (eggs == 1) ? "" : "s"
            )
        });
        return true;
    }
    return false;
}

u_char special_extra_type(const ::item &item)
{
    /* @note the client reads extra tile data for these item types (GTopia TileExtra.cpp, MIT): without it the rest of the world is misread */
    switch (item.type)
    {
        case type::MAILBOX:             return 0x06;
        case type::BULLETIN:            return 0x07;
        case type::ACHIEVEMENT:         return 0x0a;
        case type::HEART_MONITOR:       return 0x0b;
        case type::DONATION_BOX:        return 0x0c;
        case type::MAGIC_EGG:           return 0x0f;
        case type::XENONITE:            return 0x12;
        case type::BOOTH:               return 0x13; // @note dress-up booth
        case type::CRYSTAL:             return 0x14;
        case type::SPOTLIGHT:           return 0x16;
        case type::BATTLE_PET_CAGE:     return 0x24;
        case type::SFX_WEATHER_MACHINE: return 0x28;
        default:                        return 0x00;
    }
}

void special_extra(::blob &blob, ::world &world, const ::pos &tile, u_char type)
{
    const auto str = [&blob](const std::string &s) { blob.i16(static_cast<short>(s.length())); for (char c : s) blob.u8(c); };
    switch (type)
    {
        case 0x08: // @note dice: the face it shows (GTopia TileExtra_Component)
        {
            auto random = std::ranges::find(world.random_blocks, tile, &::random_block::pos);
            blob.u8(random != world.random_blocks.end() ? random->value : 0);
            break;
        }
        case 0x09: blob.u32(0); break; // @note Chicken, Cow, ATM...: seconds since planted (providers aren't harvested yet)
        case 0x06: case 0x07: case 0x0c: str(""), str(""), str(""), blob.u8(0x00); break; // @note mailbox, bulletin, donation box: 3 strings + flags
        case 0x0a: blob.i32(-1), blob.u8(127); break; // @note achievement block: owner, achievement (127 = blank)
        case 0x0b: blob.i32(-1), str(""); break; // @note heart monitor: owner, name
        case 0x0f: // @note bunny egg: Magic Eggs in it
        {
            const special_tile *t = find(world, tile);
            blob.u32(t ? t->eggs : 1);
            break;
        }
        case 0x12: blob.u8(0x00), blob.u32(0); break; // @note xenonite: flags
        case 0x13: for (int i = 0; i < 9; ++i) blob.i16(0); break; // @note dress-up booth: 9 clothes
        case 0x14: // @note crystal cluster: the crystals in it
        {
            const special_tile *t = find(world, tile);
            str(t ? t->crystals : std::string(1, is_crystal(fg_at(world, tile)) ? crystal_code(fg_at(world, tile)) : '1'));
            break;
        }
        case 0x16: break; // @note spotlight: nothing
        case 0x24: str(""), blob.i32(0), blob.i32(0), blob.i32(0); break; // @note battle pet cage: name, 3 pets
        case 0x28: blob.i32(0); break; // @note Heatwave, Background... weather machines: item
    }
}

void load_special_tiles(::world &world)
{
    if (tiles.contains(world.name)) return; // @note already in memory (newer than the database)
    auto &list = tiles[world.name];

    ::hStmt hStmt{ "SELECT x, y, data FROM world_special_tile WHERE world = ?" };
    MYSQL_BIND param = make_bind_in(world.name);
    hStmt.bind_param(&param);

    signed x{}, y{};
    char data[65]{};
    unsigned long length{};
    MYSQL_BIND result[3] = { make_bind_out(x), make_bind_out(y), {} };
    result[2].buffer_type = MYSQL_TYPE_STRING;
    result[2].buffer = data;
    result[2].buffer_length = sizeof(data) - 1;
    result[2].length = &length;
    mysql_stmt_bind_result(hStmt.pStmt, result);
    hStmt.execute();
    mysql_stmt_store_result(hStmt.pStmt);

    while (mysql_stmt_fetch(hStmt.pStmt) == 0)
    {
        /* @note "c|11224|50|60|70|80" crystals + chi, "e|123" bunny egg */
        const std::vector<std::string> v = readch(std::string(data, std::min<unsigned long>(length, sizeof(data) - 1)), '|');
        special_tile t{ .pos = ::pos{ x, y } };
        if (v.size() >= 6 && v[0] == "c")
        {
            t.crystals = v[1].substr(0, CLUSTER);
            for (int i = 0; i < 4; ++i) t.chi[i] = atoi(v[2 + i].c_str());
        }
        else if (v.size() >= 2 && v[0] == "e") t.eggs = static_cast<u_int>(std::max(1, atoi(v[1].c_str())));
        else continue;
        list.emplace_back(t);
    }
}

void save_special_tiles(::world &world)
{
    {
        ::hStmt hStmt{ "DELETE FROM world_special_tile WHERE world = ?" };
        MYSQL_BIND param = make_bind_in(world.name);
        hStmt.bind_param(&param);
        hStmt.execute();
    }
    for (const special_tile &t : tiles[world.name])
    {
        const short fg = fg_at(world, t.pos);
        std::string data{};
        if (is_crystal(fg) && !t.crystals.empty()) data = std::format("c|{}|{}|{}|{}|{}", t.crystals, t.chi[0], t.chi[1], t.chi[2], t.chi[3]);
        else if (fg == BUNNY_EGG) data = std::format("e|{}", t.eggs);
        else continue; // @note broken meanwhile
        const signed x = t.pos.x_int(), y = t.pos.y_int();
        ::hStmt hStmt{ "INSERT INTO world_special_tile (world, x, y, data) VALUES (?, ?, ?, ?)" };
        MYSQL_BIND params[4] = { make_bind_in(world.name), make_bind_in(x), make_bind_in(y), make_bind_in(data) };
        hStmt.bind_param(params);
        hStmt.execute();
    }
}
