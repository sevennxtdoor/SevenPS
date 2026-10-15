#include "pch.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "onVariant/SetBux.hpp"
#include "tools/cps.hpp"
#include "tools/economy.hpp"
#include "tools/profile.hpp"
#include "tools/rank.hpp"
#include "tools/random.hpp"

#include "item_hunt.hpp"

namespace
{
    constexpr short CHEST = 596;             // @note Treasure Chest, only a marker: finders get gems
    constexpr int CHESTS = 5;
    constexpr int HUNT_LENGTH = 20 * 60;     // @note seconds
    constexpr int HUNT_GAP = 3 * 60 * 60;    // @note a hunt every 3 hours
    constexpr int PRIZE_MIN = 750, PRIZE_MAX = 1500; // @note ~half a World Lock, a few per hunt: a treat, not a farm
    const std::string WORLD = "START";

    struct chest { ::pos tile; u_int uid{}; }; // @note uid 0 = not on the ground right now (world not loaded)

    struct
    {
        bool running{};
        std::time_t ends{};
        std::vector<chest> chests{};
        std::vector<int> finders{};          // @note user_id: one chest each
        std::unordered_map<int, std::time_t> warned{}; // @note "you already found one" at most every 10 s
    } hunt;

    std::time_t next_hunt = std::time(nullptr) + 60 * 60; // @note the first one an hour after start

    ::world *start_world()
    {
        auto it = std::ranges::find(worlds, WORLD, &::world::name);
        return (it != worlds.end()) ? &*it : nullptr;
    }

    ::pos pixel_of(const ::pos &tile) { return ::pos{ tile.x * 32.0f + 8.0f, tile.y * 32.0f + 8.0f }; }

    void send_world(const ::gamePacket &packet)
    {
        const ::blob blob = compress_state(packet);
        peers(WORLD, PEER_SAME_WORLD, [&blob](ENetPeer &p) { send_data(p, blob); });
    }

    /* @note a chest on the ground for everyone in START (PACKET_ITEM_CHANGE_OBJECT, same as add_object) */
    void place(::world &world, chest &c)
    {
        c.uid = ++world.last_object_uid;
        world.objects.emplace_back(::object(CHEST, 1, pixel_of(c.tile), c.uid));
        send_world(::gamePacket{ .type = 0x0e, .netid = -1, .uid = static_cast<int>(c.uid), .count = 1.0f, .id = CHEST, .pos = pixel_of(c.tile) });
    }

    /* @note take a chest off the ground without anyone picking it up: netid -2, uid -1, id = drop uid (GTOS RemoveDroppedItem) */
    void unplace(::world &world, chest &c)
    {
        if (c.uid == 0) return;
        std::erase_if(world.objects, [&c](const ::object &o) { return o.uid == c.uid; });
        send_world(::gamePacket{ .type = 0x0e, .netid = -2, .uid = -1, .id = static_cast<int>(c.uid) });
        c.uid = 0;
    }

    /* @note empty tiles with room to stand and a solid block or platform underneath */
    std::vector<::pos> spots(const ::world &world)
    {
        std::vector<::pos> out{};
        const int rows = static_cast<int>(world.blocks.size()) / 100;
        for (int y = 1; y < rows - 1; ++y)
            for (int x = 1; x < 99; ++x)
            {
                const ::block &here = world.blocks[cord(x, y)], &above = world.blocks[cord(x, y - 1)], &below = world.blocks[cord(x, y + 1)];
                if (here.fg != 0 || above.fg != 0 || below.fg == 0) continue;
                const u_char c = id_to_item(below.fg).collision;
                if (c != collision::FULL && c != collision::ON_TOP) continue;
                if (::pos{ static_cast<float>(x), static_cast<float>(y) } == world.spawn.by_32(true)) continue;
                out.emplace_back(static_cast<float>(x), static_cast<float>(y));
            }
        return out;
    }

    std::string where(const ::pos &tile, int rows)
    {
        const std::string_view h = (tile.x < 33) ? "left" : (tile.x < 66) ? "middle" : "right";
        const std::string_view v = (tile.y < rows / 3) ? "top" : (tile.y < rows * 2 / 3) ? "middle" : "bottom";
        return (h == v) ? std::string{ "the very middle" } : std::format("the {} {}", v, h);
    }

    void stop_hunt(const std::string &message)
    {
        if (::world *w = start_world())
            for (chest &c : hunt.chests) unplace(*w, c);
        hunt = {};
        next_hunt = std::time(nullptr) + HUNT_GAP;
        if (!message.empty()) system_broadcast(message);
    }

    /* @return the reason it couldn't start, empty when it did */
    std::string start_hunt()
    {
        if (hunt.running) return "A hunt is already running.";
        ::world *w = start_world();
        if (w == nullptr)
        {
            worlds.emplace_back(WORLD); // @note load START (the ctor reads it from the database)
            w = start_world();
        }
        std::vector<::pos> free = spots(*w);
        if (free.empty()) return "START has no free spots for chests.";

        hunt = {};
        for (int tries = 0; tries < 500 && static_cast<int>(hunt.chests.size()) < CHESTS; ++tries)
        {
            const ::pos tile = free[RandomRange(0, static_cast<int>(free.size()))];
            const bool spread = std::ranges::none_of(hunt.chests, [&tile](const chest &c) {
                return std::abs(c.tile.x - tile.x) + std::abs(c.tile.y - tile.y) < 10; // @note not all in one corner
            });
            if (spread || tries > 400) hunt.chests.push_back(chest{ tile });
        }
        for (chest &c : hunt.chests) place(*w, c);
        hunt.running = true;
        hunt.ends = std::time(nullptr) + HUNT_LENGTH;
        system_broadcast(std::format("`9** ITEM HUNT! `w{}`` Treasure Chests are hidden in `wSTART``! Walk over one for up to `w{}`` gems "
            "(one per player). Type `$/hunt`` for hints. Ends in {} minutes.``", hunt.chests.size(), PRIZE_MAX, HUNT_LENGTH / 60), true);
        return {};
    }

    bool someone_online()
    {
        bool any = false;
        peers("", PEER_ALL, [&any](ENetPeer &p) {
            const ::peer *pp = static_cast<::peer*>(p.data);
            if (pp != nullptr && !pp->growid.empty()) any = true;
        });
        return any;
    }
}

bool is_hunt_drop(const std::string &world, u_int uid)
{
    return hunt.running && world == WORLD && uid != 0 &&
        std::ranges::find(hunt.chests, uid, &chest::uid) != hunt.chests.end();
}

bool hunt_pickup(ENetEvent &event, ::world &world, u_int uid)
{
    if (!is_hunt_drop(world.name, uid)) return false;
    ::peer &p = *static_cast<::peer*>(event.peer->data);
    const std::time_t now = std::time(nullptr);

    if (std::ranges::find(hunt.finders, p.user_id) != hunt.finders.end())
    {
        if (now - hunt.warned[p.user_id] >= 10)
        {
            hunt.warned[p.user_id] = now;
            send_varlist(event.peer, { "OnTalkBubble", p.netid, "You already found a treasure this hunt. Leave this one for someone else!", 0u, 1u });
        }
        return true; // @note it stays on the ground
    }
    hunt.finders.push_back(p.user_id);

    auto it = std::ranges::find(hunt.chests, uid, &chest::uid);
    hunt.chests.erase(it);
    std::erase_if(world.objects, [uid](const ::object &o) { return o.uid == uid; });
    remove_object(event, static_cast<int>(uid)); // @note the chest flies to the finder

    const int gems = gem_bonus(p, RandomRange(PRIZE_MIN, PRIZE_MAX + 1));
    p.gems += gems;
    econ("item hunt", gems);
    on::SetBux(event);
    add_stat(event, "hunt_found");
    send_action(*event.peer, "play_sfx", "file|audio/cash_register.wav\ndelayMS|0\n");
    on::ConsoleMessage(event.peer, std::format("`9You found a hidden treasure and got `w{}`` gems!``", gems));

    if (hunt.chests.empty())
        stop_hunt(std::format("`9** ``{} `9found the last treasure (`w{}`` gems)! The Item Hunt is over, thanks for playing!``", p.display_growid, gems));
    else
        system_broadcast(std::format("`9** ``{} `9found a treasure in START (`w{}`` gems)! `w{}`` left.``", p.display_growid, gems, hunt.chests.size()));
    return true;
}

void hunt_tick()
{
    static std::time_t last{};
    const std::time_t now = std::time(nullptr);
    if (now == last) return; // @note once a second is plenty
    last = now;

    if (!hunt.running)
    {
        if (now < next_hunt) return;
        if (!someone_online()) { next_hunt = now + 10 * 60; return; } // @note nobody to play: try again later
        if (!start_hunt().empty()) next_hunt = now + HUNT_GAP;
        return;
    }
    if (now >= hunt.ends)
        return stop_hunt(std::format("`9** The Item Hunt is over! `w{}`` treasure(s) stayed hidden.``", hunt.chests.size()));

    /* @note START was unloaded (everyone left) and loaded again: its chests weren't saved, put them back */
    ::world *w = start_world();
    if (w == nullptr) { for (chest &c : hunt.chests) c.uid = 0; return; }
    for (chest &c : hunt.chests)
        if (c.uid == 0 || std::ranges::find(w->objects, c.uid, &::object::uid) == w->objects.end()) place(*w, c);
}

void hunt_command(ENetEvent &event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const std::vector<std::string> args = readch(std::string{ text }, ' ');

    if (args.size() >= 2 && pPeer->role >= DEVELOPER)
    {
        if (args[1] == "start")
        {
            const std::string why = start_hunt();
            if (!why.empty()) on::ConsoleMessage(event.peer, std::format("`4{}``", why));
            return;
        }
        if (args[1] == "stop")
        {
            if (!hunt.running) return on::ConsoleMessage(event.peer, "`4No hunt is running.``");
            return stop_hunt("`9** The Item Hunt was stopped by staff.``");
        }
    }
    if (!hunt.running)
        return on::ConsoleMessage(event.peer, std::format("`oNo Item Hunt right now. The next one starts in about `w{}`` minutes.``",
            std::max<long long>(1, (next_hunt - std::time(nullptr) + 59) / 60)));

    const ::world *w = start_world();
    const int rows = (w != nullptr) ? static_cast<int>(w->blocks.size()) / 100 : 60;
    std::string hints{};
    for (const chest &c : hunt.chests) hints += std::format("\n`o- somewhere in {}``", where(c.tile, rows));
    on::ConsoleMessage(event.peer, std::format("`9** Item Hunt in START: `w{}`` chest(s) left, `w{}`` minutes to go.``{}{}",
        hunt.chests.size(), (hunt.ends - std::time(nullptr) + 59) / 60, hints,
        (std::ranges::find(hunt.finders, pPeer->user_id) != hunt.finders.end()) ? "\n`oYou already found yours this hunt.``" : ""));
}
