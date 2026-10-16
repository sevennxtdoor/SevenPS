#include "pch.hpp"
#include <filesystem>
#include <fstream>
#include "tools/string.hpp"
#include "tools/profile.hpp"

#include "combiner.hpp"

namespace
{
    constexpr short MICROWAVE = 1472;

    struct need { short id; int amount; };
    struct recipe { short result; int count; std::array<need, 3> needs; bool microwave; };

    std::vector<recipe> recipes{};
    std::filesystem::file_time_type recipes_at{};

    void load()
    {
        std::error_code ec;
        const auto at = std::filesystem::last_write_time("resources/combiner.txt", ec);
        if (ec || at == recipes_at) return;
        recipes_at = at;
        recipes.clear();
        std::ifstream file("resources/combiner.txt");
        for (std::string line; std::getline(file, line); )
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line.starts_with('#')) continue;
            const std::vector<std::string> v = readch(line, '|');
            if (v.size() < 9 || v[0] != "recipe") continue;
            const auto num = [&v](std::size_t i) { return atoi(v[i].c_str()); };
            recipe r{ static_cast<short>(num(1)), std::max(1, num(2)), {}, v.size() > 9 && v[9] == "microwave" };
            bool ok = r.result > 0;
            for (int i = 0; i < 3; ++i)
            {
                r.needs[i] = need{ static_cast<short>(num(3 + i * 2)), num(4 + i * 2) };
                ok = ok && r.needs[i].id > 0 && r.needs[i].amount > 0;
            }
            if (ok) recipes.emplace_back(r);
        }
        std::printf("combiner: %zu recipe(s)\n", recipes.size());
    }

    ::peer &me(ENetEvent &event) { return *static_cast<::peer*>(event.peer->data); }

    void bubble(ENetEvent &event, const std::string &text)
    {
        send_varlist(event.peer, { "OnTalkBubble", me(event).netid, text, 0u, 1u });
    }

    /* @note takes `amount` of `id` from the drops on the tile (a part of a stack stays on the ground) */
    void take(ENetEvent &event, ::world &world, const ::pos &tile, short id, int amount)
    {
        std::vector<::object> stacks{};
        for (const ::object &o : world.objects)
            if (o.id == id && o.pos.by_32(true) == tile) stacks.emplace_back(o);
        for (const ::object &o : stacks)
        {
            if (amount <= 0) break;
            std::erase_if(world.objects, [&o](const ::object &x) { return x.uid == o.uid; });
            /* @note take it off the ground without anyone picking it up: netid -2, uid -1, id = drop uid (GTOS RemoveDroppedItem) */
            const ::blob blob = compress_state(::gamePacket{ .type = 0x0e, .netid = -2, .uid = -1, .id = static_cast<int>(o.uid) });
            peers(world.name, PEER_SAME_WORLD, [&blob](ENetPeer &p) { send_data(p, blob); });
            const int used = std::min<int>(amount, o.count);
            amount -= used;
            if (o.count > used) add_object(event, ::slot(id, static_cast<short>(o.count - used)), o.pos, world);
        }
    }
}

bool combiner_punch(ENetEvent &event, ::world &world, ::block &block, const ::pos &tile, const ::item &item)
{
    if (item.type != type::CHEMICAL_COMBINER || block.fg != item.id) return false;
    if (!(block.state[2] & S_TOGGLE)) return false; // @note closed: the punch opens it

    std::unordered_map<short, int> on_tile{};
    for (const ::object &o : world.objects)
        if (o.pos.by_32(true) == tile) on_tile[static_cast<short>(o.id)] += o.count;
    if (on_tile.empty()) return false;

    load();
    const auto fits = [&](const recipe &r) { return std::ranges::all_of(r.needs, [&](const need &n) { return on_tile.contains(n.id) && on_tile[n.id] >= n.amount; }); };
    /* @note a recipe made of exactly the items on it first, then one that only needs some of them */
    std::vector<const recipe*> order{};
    for (const recipe &r : recipes) if (fits(r) && on_tile.size() == 3) order.emplace_back(&r);
    for (const recipe &r : recipes) if (fits(r) && on_tile.size() != 3) order.emplace_back(&r);
    for (const recipe *pr : order)
    {
        const recipe &r = *pr;
        if (r.microwave && item.id != MICROWAVE) throw std::runtime_error("This recipe would melt the chocolate beyond recognition!");
        for (const need &n : r.needs) take(event, world, tile, n.id, n.amount);
        add_object(event, ::slot(r.result, static_cast<short>(r.count)), tile.by_32(), world);
        send_particle_effect(event, tile.by_32(), { 0.0f, 44.0f });
        bubble(event, std::format("`wThe {} made `2{}{}``!", item.raw_name, r.count > 1 ? std::format("{} ", r.count) : "", id_to_item(r.result).raw_name));
        me(event).add_xp(event, 3);
        add_stat(event, "combined");
        return true;
    }
    return false; // @note no recipe fits: the punch closes it (and the items are kept)
}
