#include "pch.hpp"
#include <filesystem>
#include <fstream>
#include <set>
#include "tools/random.hpp"
#include "tools/string.hpp"
#include "tools/area_lock.hpp"
#include "tools/system_worlds.hpp"
#include "tools/magplant.hpp"

#include "special_blocks.hpp"

namespace
{
    constexpr short GEMS = 112;
    constexpr short ROCK_HAMMER = 3932, ROCK_CHISEL = 3934, AMBER_NECKLACE = 6844;
    constexpr short FOSSIL_ITEM = 3936, FOSSIL_BRUSH = 4132, POLISHED_FOSSIL = 4134;

    struct prize { short id; int min, max, weight, percent; bool rare; };
    struct loot_block { int picks{1}; std::vector<prize> prizes{}; };
    struct prep_prize { short id; int weight_fossil, weight_polished; };

    /* ---------- resources/loot.txt & resources/fossils.txt, read again whenever they change ---------- */

    std::unordered_map<short, loot_block> loot{}, providers{};
    std::filesystem::file_time_type loot_at{}, providers_at{};

    std::unordered_map<short, short> fossil_to_rock{}, rock_to_fossil{}, pinata_change{};
    std::vector<prep_prize> prep_prizes{};
    int spawn_percent = 10, hammer_percent = 20, prep_min = 3600, prep_max = 86400;
    std::filesystem::file_time_type fossils_at{};

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

    /* @note resources/loot.txt & resources/providers.txt */
    void load_table(const char *path, std::unordered_map<short, loot_block> &table, std::filesystem::file_time_type &at)
    {
        if (!changed(path, at)) return;
        table.clear();
        loot_block *current = nullptr;
        for (std::vector<std::string> &v : lines_of(path))
        {
            if (v[0].starts_with('='))
            {
                current = &table[static_cast<short>(atoi(v[0].c_str() + 1))];
                current->picks = (v.size() > 1) ? std::max(1, atoi(v[1].c_str())) : 1;
                continue;
            }
            if (current == nullptr || v.size() < 4) continue;
            prize p{ .id = static_cast<short>(atoi(v[0].c_str())), .min = atoi(v[1].c_str()), .max = atoi(v[2].c_str()) };
            if (v[3].starts_with('%')) p.percent = atoi(v[3].c_str() + 1);
            else p.weight = atoi(v[3].c_str());
            p.rare = v.size() > 4 && v[4] == "rare";
            if (p.id > 0 && p.max >= p.min && p.min > 0) current->prizes.emplace_back(p);
        }
        std::printf("%s: %zu block(s)\n", path, table.size());
    }
    void load_loot() { load_table("resources/loot.txt", loot, loot_at); }

    void load_fossils()
    {
        if (!changed("resources/fossils.txt", fossils_at)) return;
        fossil_to_rock.clear(), rock_to_fossil.clear(), pinata_change.clear(), prep_prizes.clear();
        for (std::vector<std::string> &v : lines_of("resources/fossils.txt"))
        {
            const auto num = [&v](std::size_t i) { return (i < v.size()) ? atoi(v[i].c_str()) : 0; };
            if (v[0] == "rock" && v.size() >= 3)
            {
                fossil_to_rock[static_cast<short>(num(1))] = static_cast<short>(num(2));
                rock_to_fossil[static_cast<short>(num(2))] = static_cast<short>(num(1));
            }
            else if (v[0] == "pinata" && v.size() >= 3) pinata_change[static_cast<short>(num(1))] = static_cast<short>(num(2));
            else if (v[0] == "prep" && v.size() >= 4) prep_prizes.emplace_back(prep_prize{ static_cast<short>(num(1)), num(2), num(3) });
            else if (v[0] == "spawn") spawn_percent = num(1);
            else if (v[0] == "hammer") hammer_percent = num(1);
            else if (v[0] == "prep_time" && v.size() >= 3) prep_min = num(1), prep_max = std::max(num(1), num(2));
        }
        std::printf("fossils: %zu rock(s), %zu prep prize(s), %zu pinata change(s)\n", fossil_to_rock.size(), prep_prizes.size(), pinata_change.size());
    }

    /* ---------- state ---------- */

    using tile_key = std::pair<int, int>;
    tile_key key_of(const ::pos &tile) { return { tile.x_int(), tile.y_int() }; }

    std::unordered_map<std::string, std::set<tile_key>> unearthed{}; // @note world -> fossil rocks a hammer has loosened
    std::unordered_map<std::string, std::set<u_int>> fresh_fossils{}; // @note world -> drop uids of fossils nobody picked up yet

    struct prep { ::pos pos{}; short fossil{}; long long ready{}; };
    /* @note world name -> prep stations in use. never erased, same reason as vending machines (world copies) */
    std::unordered_map<std::string, std::vector<prep>> preps{};

    ::peer &me(ENetEvent &event) { return *static_cast<::peer*>(event.peer->data); }

    void bubble(ENetEvent &event, const std::string &text)
    {
        send_varlist(event.peer, { "OnTalkBubble", me(event).netid, text, 0u, 1u });
    }

    long long now() { return static_cast<long long>(std::time(nullptr)); }

    bool wearing(const ::peer &p, short id)
    {
        return std::ranges::find(p.clothing, static_cast<float>(id)) != p.clothing.end();
    }

    ::pos drop_spot(const ::pos &tile) // @note same spread as add_drop()
    {
        const ::pos pixel = tile.by_32();
        return ::pos{ pixel.x + RandomRange(0, 16), pixel.y + RandomRange(0, 16) };
    }

    /* @note gems drop as 100/50/10/5/1 gem pieces (the pickup counts them for /economy) */
    void drop_gems(ENetEvent &event, ::world &world, const ::pos &tile, int gems)
    {
        for (short piece : {100, 50, 10, 5, 1})
            for (; gems >= piece; gems -= piece)
                magplant_drop(event, ::slot(GEMS, piece), tile.by_32(), world);
    }

    void drop_item(ENetEvent &event, ::world &world, const ::pos &tile, short id, int count)
    {
        if (id == GEMS) return drop_gems(event, world, tile, count);
        for (int left = count; left > 0; )
        {
            const int now = std::min(left, 200);
            left -= now;
            magplant_drop(event, ::slot(id, now), tile.by_32(), world);
        }
    }

    /* @note into the backpack, what doesn't fit drops on the tile */
    void give_item(ENetEvent &event, ::world &world, const ::pos &tile, short id, int count)
    {
        if (const u_short rest = modify_item_inventory(event, ::slot(id, count)); rest > 0)
            add_drop(event, ::slot(id, rest), tile.by_32(), world);
    }

    std::string name_of(short id, int count)
    {
        return (id == GEMS) ? std::format("{} gems", count) : (count > 1) ? std::format("{} {}", count, id_to_item(id).raw_name) : id_to_item(id).raw_name;
    }

    void announce_rare(ENetEvent &event, short id, const ::item &from)
    {
        const std::string message = std::format("msg|`9WOW! `2{} found a `#{}`2 in a `#{}`2!", me(event).growid, id_to_item(id).raw_name, from.raw_name);
        peers("", PEER_ALL, [&message](ENetPeer &p) { send_action(p, "log", message); });
    }

    /* ---------- loot blocks ---------- */

    /* @note rolls the prizes of `lb` onto the tile. `twice_id`: that prize counts double (Sheep Shears) */
    std::vector<std::string> roll_prizes(ENetEvent &event, ::world &world, const ::pos &tile, const ::item &item, const loot_block &lb, short twice_id = 0)
    {
        int total = 0;
        for (const prize &p : lb.prizes) total += p.weight;
        std::vector<std::string> got{};
        const auto award = [&](const prize &p) {
            const int count = RandomRange(p.min, p.max + 1) * (p.id == twice_id ? 2 : 1); // @note RandomRange(a, b) never returns b
            drop_item(event, world, tile, p.id, count);
            got.emplace_back(name_of(p.id, count));
            if (p.rare) announce_rare(event, p.id, item);
        };
        for (int i = 0; i < lb.picks && total > 0; ++i)
        {
            int pick = RandomRange(0, total);
            for (const prize &p : lb.prizes)
            {
                if (p.weight == 0 || (pick -= p.weight) >= 0) continue;
                award(p);
                break;
            }
        }
        for (const prize &p : lb.prizes)
            if (p.percent > 0 && RandomRange(0, 100) < p.percent) award(p);
        return got;
    }

    std::string list_of(const std::vector<std::string> &got)
    {
        std::string text{};
        for (const std::string &g : got) text += (text.empty() ? "" : ", ") + g;
        return text;
    }

    bool break_loot(ENetEvent &event, ::world &world, const ::pos &tile, const ::item &item)
    {
        load_loot();
        auto it = loot.find(item.id);
        if (it == loot.end()) return false;
        const std::vector<std::string> got = roll_prizes(event, world, tile, item, it->second);
        if (!got.empty()) bubble(event, std::format("`wThe {} breaks open: `2{}``!", item.raw_name, list_of(got)));
        me(event).add_xp(event, std::trunc(1.0f + item.rarity / 5.0f));
        return true;
    }

    /* @note Mystery Pinata: the items under it change into their festive versions */
    void mystery_pinata(ENetEvent &event, ::world &world, const ::pos &tile)
    {
        std::vector<::object> changed{};
        for (const ::object &o : world.objects)
            if (o.pos.by_32(true) == tile && pinata_change.contains(static_cast<short>(o.id))) changed.emplace_back(o);
        for (const ::object &o : changed)
        {
            std::erase_if(world.objects, [&o](const ::object &x) { return x.uid == o.uid; });
            /* @note take it off the ground without anyone picking it up: netid -2, uid -1, id = drop uid (GTOS RemoveDroppedItem) */
            const ::blob blob = compress_state(::gamePacket{ .type = 0x0e, .netid = -2, .uid = -1, .id = static_cast<int>(o.uid) });
            peers(world.name, PEER_SAME_WORLD, [&blob](ENetPeer &p) { send_data(p, blob); });
            add_object(event, ::slot(pinata_change[static_cast<short>(o.id)], o.count), o.pos, world);
        }
        if (!changed.empty()) bubble(event, "`5The Mystery Pinata's magic changed some of the prizes!``");
    }

    /* ---------- fossils ---------- */

    bool is_fossil_rock(short id) { load_fossils(); return fossil_to_rock.contains(id); }

    prep *prep_at(::world &world, const ::pos &tile)
    {
        auto &list = preps[world.name];
        auto it = std::ranges::find(list, tile, &prep::pos);
        return (it != list.end()) ? &*it : nullptr;
    }

    bool is_prep_station(::world &world, const ::pos &tile)
    {
        return in_world(world, tile.x_int(), tile.y_int()) && id_to_item(world.blocks[cord(tile.x_int(), tile.y_int())].fg).type == type::FOSSIL_PREP_STATION;
    }

    std::string wait_text(long long seconds)
    {
        if (seconds >= 3600) return std::format("{} hour{}", (seconds + 1800) / 3600, (seconds + 1800) / 3600 == 1 ? "" : "s");
        if (seconds >= 60) return std::format("{} minute{}", (seconds + 30) / 60, (seconds + 30) / 60 == 1 ? "" : "s");
        return std::format("{} second{}", seconds, seconds == 1 ? "" : "s");
    }

    void collect_prep(ENetEvent &event, ::world &world, const ::pos &tile, prep &p)
    {
        load_fossils();
        const bool polished = p.fossil == POLISHED_FOSSIL;
        int total = 0;
        for (const prep_prize &pp : prep_prizes) total += polished ? pp.weight_polished : pp.weight_fossil;
        short won = 0;
        if (total > 0)
        {
            int pick = RandomRange(0, total);
            for (const prep_prize &pp : prep_prizes)
            {
                const int w = polished ? pp.weight_polished : pp.weight_fossil;
                if (w == 0 || (pick -= w) >= 0) continue;
                won = pp.id;
                break;
            }
        }
        std::erase_if(preps[world.name], [&tile](const prep &x) { return x.pos == tile; });
        if (won == 0) { bubble(event, "The fossil dissolved into nothing..."); return; }
        give_item(event, world, tile, won, 1);
        bubble(event, std::format("`wThe fossil turned out to be a `2{}``!", id_to_item(won).raw_name));
        send_action(*event.peer, "play_sfx", "file|audio/success.wav\ndelayMS|0\n");
    }
}

bool special_punch(ENetEvent &event, ::world &world, ::block &block, const ::pos &tile, const ::item &item)
{
    ::peer &p = me(event);
    if (item.type == type::FOSSIL_PREP_STATION && block.fg == item.id)
    {
        prep *pr = prep_at(world, tile);
        if (pr == nullptr) return false; // @note empty: breaks like any block
        if (!can_build(p, world, tile)) throw std::runtime_error("You don't have access to this Fossil Prep Station.");
        if (now() < pr->ready)
            throw std::runtime_error(std::format("There's a fossil being prepared in there. You'll have to wait to break this or you'd lose the fossil! (`w{}`` left)", wait_text(pr->ready - now())));
        collect_prep(event, world, tile, *pr);
        return true;
    }
    if (!is_fossil_rock(item.id) || block.fg != item.id) return false;

    if (p.clothing[clothing::HAND] == ROCK_HAMMER)
    {
        auto &loose = unearthed[world.name];
        if (loose.contains(key_of(tile))) // @note Rock Hammer: "If the fossil comes loose and you hit it again, you'll break it!"
        {
            loose.erase(key_of(tile));
            block.fg = fossil_to_rock[item.id];
            block.hits[0] = 0;
            ::gamePacket update{};
            update.punch = tile;
            send_tile_update(event, update, block, world);
            throw std::runtime_error("`4Oops! You hit it again and smashed the fossil!``");
        }
        if (RandomRange(0, 100) < hammer_percent)
        {
            loose.insert(key_of(tile));
            send_particle_effect(event, tile.by_32(), { 0.0f, 44.0f });
            bubble(event, "`2I unearthed a fossil!`` Now break the rock with a `wRock Chisel``.");
        }
        else bubble(event, "`w*tap tap*``");
        return true; // @note precise taps: the hammer never damages the rock
    }
    return false;
}

bool special_break(ENetEvent &event, ::world &world, ::block &block, const ::pos &tile, const ::item &item, bool &tile_update)
{
    if (item.id == 1584/*Mystery Pinata*/) { load_fossils(); mystery_pinata(event, world, tile); return false; }
    if (break_loot(event, world, tile, item)) return true;
    if (item.type == type::FOSSIL_PREP_STATION) std::erase_if(preps[world.name], [&tile](const prep &x) { return x.pos == tile; });
    if (!is_fossil_rock(item.id)) return false;

    ::peer &p = me(event);
    const bool loose = unearthed[world.name].erase(key_of(tile)) > 0;
    if (loose && p.clothing[clothing::HAND] == ROCK_CHISEL)
    {
        const int count = (wearing(p, AMBER_NECKLACE) && RandomRange(0, 100) < 5) ? 2 : 1;
        for (int i = 0; i < count; ++i)
            fresh_fossils[world.name].insert(static_cast<u_int>(add_object(event, ::slot(FOSSIL_ITEM, 1), drop_spot(tile), world)));
        bubble(event, count == 2 ? "`2Two fossils came loose!``" : "`2The fossil came loose!``");
        p.add_xp(event, std::trunc(1.0f + item.rarity / 5.0f));
        return true;
    }
    /* @note any other way smashes the fossil and leaves the normal rock (Rock Chisel wiki page) */
    block.fg = fossil_to_rock[item.id];
    tile_update = true;
    bubble(event, loose ? "`4You smashed the fossil! You need a `wRock Chisel`` to break it out." : "`4You smashed the fossil! Tap it with a `wRock Hammer`` first.");
    return true;
}

bool special_use(ENetEvent &event, ::world &world, ::block &block, const ::pos &tile, const ::item &item)
{
    ::peer &p = me(event);
    if (item.id == FOSSIL_BRUSH)
    {
        auto &fresh = fresh_fossils[world.name];
        auto o = std::ranges::find_if(world.objects, [&](const ::object &x) {
            return x.id == FOSSIL_ITEM && x.pos.by_32(true) == tile && fresh.contains(x.uid);
        });
        if (o == world.objects.end())
        {
            bubble(event, "Use this on a `wFossil`` lying on the ground that nobody has picked up yet.");
            return true;
        }
        const ::object old = *o;
        fresh.erase(old.uid);
        world.objects.erase(o);
        const ::blob blob = compress_state(::gamePacket{ .type = 0x0e, .netid = -2, .uid = -1, .id = static_cast<int>(old.uid) });
        peers(world.name, PEER_SAME_WORLD, [&blob](ENetPeer &peer) { send_data(peer, blob); });
        add_object(event, ::slot(POLISHED_FOSSIL, old.count), old.pos, world);
        modify_item_inventory(event, ::slot(FOSSIL_BRUSH, -1));
        send_particle_effect(event, tile.by_32(), { 0.0f, 44.0f });
        bubble(event, "`2You carefully brush it into a `wPolished Fossil``!");
        return true;
    }
    if (item.id != FOSSIL_ITEM && item.id != POLISHED_FOSSIL) return false;

    if (!is_prep_station(world, tile) || block.fg == 0)
    {
        bubble(event, "Put this in a `wFossil Prep Station`` to see what it is.");
        return true;
    }
    if (!can_build(p, world, tile)) { bubble(event, "You don't have access to this Fossil Prep Station."); return true; }
    if (prep *pr = prep_at(world, tile))
    {
        bubble(event, (now() < pr->ready) ? "There's already a fossil being prepared in there." : "The fossil is ready, punch the station to collect it first!");
        return true;
    }
    load_fossils();
    const long long seconds = RandomRange(prep_min, prep_max + 1);
    preps[world.name].emplace_back(prep{ .pos = tile, .fossil = static_cast<short>(item.id), .ready = now() + seconds });
    modify_item_inventory(event, ::slot(item.id, -1));
    bubble(event, std::format("`wThe solvents start dissolving the rock... come back in about `2{}``, then punch the station.", wait_text(seconds)));
    return true;
}

void fossil_spawn(::world &world)
{
    if (world.name.empty() || is_system_world(world.name)) return;
    load_fossils();
    if (RandomRange(0, 100) >= spawn_percent) return;

    std::vector<int> rocks{};
    for (int i = 0; i < static_cast<int>(world.blocks.size()); ++i)
        if (rock_to_fossil.contains(world.blocks[i].fg)) rocks.emplace_back(i);
    if (rocks.empty()) return; // @note wiki: any world with at least one rock, busy or not
    ::block &b = world.blocks[rocks[RandomRange(0, static_cast<int>(rocks.size()))]];
    b.fg = rock_to_fossil[b.fg];
    b.hits[0] = 0;
}

void load_fossil_preps(::world &world)
{
    if (preps.contains(world.name)) return; // @note already in memory (newer than the database)
    auto &list = preps[world.name];

    ::hStmt hStmt{ "SELECT x, y, fossil, ready FROM world_fossil_prep WHERE world = ?" };
    MYSQL_BIND param = make_bind_in(world.name);
    hStmt.bind_param(&param);

    signed x{}, y{}, fossil{};
    long long ready{};
    MYSQL_BIND result[4] = { make_bind_out(x), make_bind_out(y), make_bind_out(fossil), make_bind_out(ready) };
    mysql_stmt_bind_result(hStmt.pStmt, result);
    hStmt.execute();
    mysql_stmt_store_result(hStmt.pStmt);

    while (mysql_stmt_fetch(hStmt.pStmt) == 0)
        list.emplace_back(prep{ .pos = ::pos{ x, y }, .fossil = static_cast<short>(fossil), .ready = ready });
}

void save_fossil_preps(::world &world)
{
    {
        ::hStmt hStmt{ "DELETE FROM world_fossil_prep WHERE world = ?" };
        MYSQL_BIND param = make_bind_in(world.name);
        hStmt.bind_param(&param);
        hStmt.execute();
    }
    for (const prep &pr : preps[world.name])
    {
        if (!is_prep_station(world, pr.pos)) continue; // @note broken meanwhile
        const signed x = pr.pos.x_int(), y = pr.pos.y_int(), fossil = pr.fossil;
        ::hStmt hStmt{ "INSERT INTO world_fossil_prep (world, x, y, fossil, ready) VALUES (?, ?, ?, ?, ?)" };
        MYSQL_BIND params[5] = { make_bind_in(world.name), make_bind_in(x), make_bind_in(y), make_bind_in(fossil), make_bind_in(pr.ready) };
        hStmt.bind_param(params);
        hStmt.execute();
    }
}

bool provider_prize(ENetEvent &event, ::world &world, const ::pos &tile, const ::item &item, short twice_id)
{
    load_table("resources/providers.txt", providers, providers_at);
    auto it = providers.find(item.id);
    if (it == providers.end()) return false;
    const std::vector<std::string> got = roll_prizes(event, world, tile, item, it->second, twice_id);
    if (!got.empty()) bubble(event, std::format("`wYou harvested the {}: `2{}``!", item.raw_name, list_of(got)));
    return true;
}
