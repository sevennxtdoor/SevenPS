#include "pch.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "tools/economy.hpp"
#include "tools/profile.hpp"
#include "tools/rank.hpp"
#include "tools/random.hpp"
#include "onVariant/SetBux.hpp"
#include "onVariant/SetClothing.hpp"

#include "fishing.hpp"

namespace
{
    constexpr short WIGGLY_WORM = 2914, FISHING_FLY = 3016, SHRIMP_LURE = 3018;
    constexpr int BITE_MIN = 12, BITE_MAX = 30; // @note seconds until a fish bites
    constexpr int BITE_WINDOW = 3;              // @note seconds to punch after the splash
    constexpr int REACH = 2;                    // @note tiles from you to the water

    struct rod { short id; int max_lb; };
    constexpr rod RODS[] = {
        { 2912, 25 },  // @note Fishing Rod
        { 3008, 40 },  // @note E-Z Rod
        { 3010, 60 },  // @note Licorice Rod
        { 3100, 150 }, // @note Cursed Fishing Rod
        { 3040, 200 }, // @note Goldenrod
    };

    /* @note all TYPE_FISH in items.dat. worms catch common fish, the fly adds nicer ones, the lure the big ones */
    constexpr short COMMON[] = { 3000, 3026, 3030, 3032, 3036, 3038, 3452, 3458, 3460 };
    constexpr short NICE[]   = { 3034, 3434, 3438, 3450, 3544, 3550, 3620, 5100 };
    constexpr short RARE[]   = { 3024, 3454, 3746, 3814, 3820, 4958, 5448, 5450 };

    ::peer &me(ENetEvent &event) { return *static_cast<::peer*>(event.peer->data); }
    bool fishing(const ::peer &p) { return p.fish_tile.x >= 0; }

    int rod_max(const ::peer &p)
    {
        for (const rod &r : RODS) if (p.clothing[clothing::HAND] == r.id) return r.max_lb;
        return 0;
    }

    int held(const ::peer &p, short id)
    {
        auto it = std::ranges::find(p.slots, id, &::slot::id);
        return (it != p.slots.end()) ? it->count : 0;
    }

    void talk(ENetEvent &event, const std::string &text)
    {
        send_varlist(event.peer, { "OnTalkBubble", me(event).netid, text, 0u, 1u });
    }

    void sfx_world(const std::string &world, std::string_view file)
    {
        peers(world, PEER_SAME_WORLD, [file](ENetPeer &p) {
            send_action(p, "play_sfx", std::format("file|{}\ndelayMS|0\n", file));
        });
    }

    /* @note everyone in the world sees the line go out */
    void send_line(ENetEvent &event, const ::pos &tile)
    {
        ::peer &p = me(event);
        const ::blob blob = compress_state(::gamePacket{ .type = 0x1f/*GONE_FISHIN*/, .netid = p.netid, .punch = tile });
        peers(p.recent_worlds.back(), PEER_SAME_WORLD, [&blob](ENetPeer &other) { send_data(other, blob); });
    }

    /* @note pull the line in. the client ends the fishing pose with a fresh character state */
    void stop(ENetEvent &event, const std::string &why)
    {
        ::peer &p = me(event);
        fishing_reset(p);
        on::SetClothing(*event.peer); // @note also sends the character state (0x14), like GTOS send_state()
        send_varlist(event.peer, { "OnSetPos", CL_Vec2f{ p.pos.x, p.pos.y } }, p.netid); // @note GTOS follows it with OnSetPos to snap out of the pose
        if (!why.empty()) talk(event, why);
    }

    short pick_fish(short bait)
    {
        const int roll = RandomRange(0, 100);
        if (bait == SHRIMP_LURE && roll < 15) return RARE[RandomRange(0, std::size(RARE))];
        if ((bait == SHRIMP_LURE && roll < 50) || (bait == FISHING_FLY && roll < 30)) return NICE[RandomRange(0, std::size(NICE))];
        return COMMON[RandomRange(0, std::size(COMMON))];
    }

    bool is_rare(short fish) { return std::ranges::find(RARE, fish) != std::end(RARE); }

    void catch_fish(ENetEvent &event)
    {
        ::peer &p = me(event);
        const int max_lb = rod_max(p);
        const short fish = pick_fish(p.fish_bait);
        const bool rare = is_rare(fish);
        const int lb = std::clamp(rare ? RandomRange(max_lb / 2, max_lb + 1) : RandomRange(1, max_lb + 1), 1, 200);
        const ::item &item = id_to_item(fish);
        const std::string world = p.recent_worlds.back();
        auto w = std::ranges::find(worlds, world, &::world::name);

        /* @note one fish of a kind per backpack (its amount is the weight), like real Growtopia: a second one lands next to you */
        const bool has_slot = std::ranges::find(p.slots, fish, &::slot::id) != p.slots.end();
        if (held(p, fish) == 0 && (has_slot || static_cast<int>(p.slots.size()) < p.slot_size))
            modify_item_inventory(event, ::slot(fish, lb));
        else if (w != worlds.end())
        {
            add_drop(event, ::slot(fish, lb), p.pos, *w);
            on::ConsoleMessage(event.peer, std::format("`oYou already have a {} (or no room), so it flops onto the ground.``", item.raw_name));
        }

        const int gems = gem_bonus(p, 3 + lb / 2 + (rare ? 25 : 0));
        p.gems += gems;
        econ("fishing", gems);
        on::SetBux(event);
        p.add_xp(event, rare ? 5 : 2);
        add_stat(event, "fish_caught");
        if (lb > pget(p, "fish_best")) pset(p, "fish_best", lb);

        talk(event, std::format("`wYou caught a `2{} lb. {}``!", lb, item.raw_name));
        on::ConsoleMessage(event.peer, std::format("`oYou caught a `2{} lb. {}`` and got `w{}`` gems.``", lb, item.raw_name, gems));
        sfx_world(world, "audio/getpoint.wav");
        if (rare || lb >= 100)
            peers(world, PEER_SAME_WORLD, [&](ENetPeer &other) {
                on::ConsoleMessage(&other, std::format("`9** {} `9caught a `w{} lb. {}``!``", p.display_growid, lb, item.raw_name));
            });
    }

    void reel_in(ENetEvent &event)
    {
        ::peer &p = me(event);
        const std::time_t now = std::time(nullptr);
        const bool hooked = p.fish_bite_end != 0 && now <= p.fish_bite_end;
        if (hooked && rod_max(p) > 0) catch_fish(event);
        stop(event, hooked ? "" : "`wToo soon! The fish got away with your bait.``");
    }

    void cast(ENetEvent &event, ::world &world, const ::pos &tile, short bait)
    {
        ::peer &p = me(event);
        if (rod_max(p) == 0) return talk(event, "You need to wear a Fishing Rod to use bait.");
        if (tile.x < 0 || tile.y < 0 || tile.x >= 100 || cord(tile.x_int(), tile.y_int()) >= static_cast<int>(world.blocks.size())) return;
        if (!(world.blocks[cord(tile.x_int(), tile.y_int())].state[3] & S_WATER)) return talk(event, "You can only fish in water!");

        const ::pos stand = p.pos.by_32(true);
        const int dx = tile.x_int() - stand.x_int(), dy = tile.y_int() - stand.y_int();
        if (std::abs(dx) > REACH || dy < -1 || dy > REACH) return talk(event, "Too far away...");
        if (held(p, bait) < 1) return;

        modify_item_inventory(event, ::slot(bait, -1));
        p.fish_tile = tile;
        p.fish_stand = stand;
        p.fish_bait = bait;
        p.fish_bite = std::time(nullptr) + RandomRange(BITE_MIN, BITE_MAX + 1);
        p.fish_bite_end = 0;
        send_line(event, tile);
    }
}

bool is_bait(short id) { return id == WIGGLY_WORM || id == FISHING_FLY || id == SHRIMP_LURE; }

void fishing_reset(::peer &p)
{
    p.fish_tile = p.fish_stand = ::pos{ -1, -1 };
    p.fish_bait = 0;
    p.fish_bite = p.fish_bite_end = 0;
}

bool fishing_tile_change(ENetEvent &event, ::world &world, const ::gamePacket &gamePacket)
{
    ::peer &p = me(event);
    if (fishing(p))
    {
        if (gamePacket.id == 18) reel_in(event);                      // @note punch = reel in
        else if (is_bait(static_cast<short>(gamePacket.id))) talk(event, "You're already fishing!");
        else stop(event, "`wSit still while fishing!``");              // @note wrench/place pulls the line in
        return true;
    }
    if (!is_bait(static_cast<short>(gamePacket.id))) return false;
    cast(event, world, gamePacket.punch, static_cast<short>(gamePacket.id));
    return true; // @note bait is never "used up" any other way
}

void fishing_moved(ENetEvent &event)
{
    ::peer &p = me(event);
    if (!fishing(p)) return;
    const ::pos now = p.pos.by_32(true);
    if (std::abs(now.x_int() - p.fish_stand.x_int()) > 1 || std::abs(now.y_int() - p.fish_stand.y_int()) > 1)
        stop(event, "`wSit still while fishing!``");
}

void fishing_tick()
{
    static std::time_t last{};
    const std::time_t now = std::time(nullptr);
    if (now == last) return; // @note once a second is plenty
    last = now;

    peers("", PEER_ALL, [now](ENetPeer &peer)
    {
        ::peer *p = static_cast<::peer*>(peer.data);
        if (p == nullptr || !fishing(*p)) return;

        if (p->fish_bite_end != 0 && now > p->fish_bite_end) // @note too slow: it swims off, another one comes later
        {
            p->fish_bite_end = 0;
            p->fish_bite = now + RandomRange(BITE_MIN, BITE_MAX + 1);
            return;
        }
        if (p->fish_bite_end == 0 && now >= p->fish_bite) // @note splash! punch now
        {
            p->fish_bite_end = now + BITE_WINDOW;
            const std::string world = p->recent_worlds.back();
            const CL_Vec2f where{ p->fish_tile.x * 32.0f + 15.0f, p->fish_tile.y * 32.0f + 15.0f };
            peers(world, PEER_SAME_WORLD, [&where](ENetPeer &other) {
                send_varlist(&other, { "OnParticleEffect", 36u, where, 0.0f, 0.0f });
                send_action(other, "play_sfx", "file|audio/splash.wav\ndelayMS|0\n");
            });
        }
    });
}
