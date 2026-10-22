#include "pch.hpp"

#include "combustible.hpp"

namespace
{
    struct recipe { std::string_view in; short in_count; std::string_view out; };

    /* @note growtopiawiki.com/w/Guide:Highly_Combustible_Box. found by name in items.dat */
    constexpr recipe wiki[]{
        { "Blade Fragment", 1, "Tempered Steel Fragment" }, { "Axe Fragment", 1, "Tempered Axe Fragment" },
        { "Steel Spike", 1, "Tempered Steel Spike" }, { "Deep Iron Ore", 1, "Deep Iron Ingot" },
        { "Lazy Cobra", 1, "Roasted Cobra" }, { "Star Iron", 1, "Star Iron Ingot" },
        { "Amber Block", 1, "Amber Resin" }, { "Magic Ore", 1, "Magic Ingot" },
        { "Party Tunes", 1, "Tempered Party Tunes" }, { "Howling Wolf Emblem", 1, "Charbroiled Wolf Emblem" },
        { "Vampire Cape", 1, "Flaming Cape" },
        { "Firefighter Jacket - Yellow", 1, "Firefighter Jacket - Red" }, { "Firefighter Helmet - Yellow", 1, "Firefighter Helmet - Red" },
        { "Firefighter Pants - Yellow", 1, "Firefighter Pants - Red" },
        { "Pickaxe", 1, "Fire Ax" }, { "Pet Slime", 1, "Strawberry Slime" }, { "Flame Tee", 1, "Ultra Flame Tee" },
        { "Super Utility Helmet - Yellow", 1, "Super Utility Helmet - Red" }, { "Super Utility Shirt - Yellow", 1, "Super Utility Shirt - Red" },
        { "Super Utility Tights - Yellow", 1, "Super Utility Tights - Red" }, { "Super Utility Boots - Yellow", 1, "Super Utility Boots - Red" },
        { "Fedora", 200, "Flare Fedora" }, // @note the only amount the wiki gives
        { "Highly Combustible Box", 1, "Combusted Box" }, { "Aqua Cave Crystal", 1, "Purple Cave Crystal" },
        { "Cow", 1, "Burnt Leather" }, { "Surgical Pins", 1, "Limb Connector" }, { "Stick Horse", 1, "Flaming Horse Chunk" },
        { "Chocolate Block", 1, "Dark Chocolate Block" }, { "Dead Thanksgiving Turkey", 1, "Roasted Turkey" },
        { "Obsidian", 1, "Dragon Glass" }
    };

    short id_of(std::string_view name)
    {
        auto it = std::ranges::find(items, name, &::item::raw_name);
        return (it == items.end()) ? 0 : static_cast<short>(it->id);
    }

    struct known { short in, in_count, out; };
    const std::vector<known> &recipes()
    {
        static const std::vector<known> list = []
        {
            std::vector<known> out{};
            for (const recipe &r : wiki)
            {
                const short in = id_of(r.in), result = id_of(r.out);
                if (in == 0 || result == 0) { std::printf("[combustible] recipe \"%.*s\" -> \"%.*s\" not in items.dat\n", (int)r.in.size(), r.in.data(), (int)r.out.size(), r.out.data()); continue; }
                out.push_back({ in, r.in_count, result });
            }
            return out;
        }();
        return list;
    }
}

void burn_box(ENetEvent &event, ::world &world, const ::pos &tile)
{
    /* @note collect first: add_object() below can move the vector */
    std::vector<::slot> inside{};
    std::vector<u_int> burnt{};
    for (const ::object &o : world.objects)
        if (o.pos.by_32(true) == tile && o.count > 0)
            inside.emplace_back(static_cast<short>(o.id), static_cast<short>(o.count)), burnt.push_back(o.uid);

    for (u_int uid : burnt) remove_object(event, static_cast<signed>(uid));
    std::erase_if(world.objects, [&burnt](const ::object &o) { return std::ranges::find(burnt, o.uid) != burnt.end(); });

    for (const ::slot &s : inside)
    {
        auto r = std::ranges::find(recipes(), s.id, &known::in);
        if (r == recipes().end()) continue; // @note not fireproof: burnt away
        const int made = s.count / r->in_count; // @note leftovers below the recipe amount burn too
        for (int left = made; left > 0; left -= 200)
            add_object(event, ::slot{ r->out, static_cast<short>(std::min(left, 200)) }, tile.by_32(), world);
    }
}
