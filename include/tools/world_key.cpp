#include "pch.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "commands/titles.hpp" // @note refresh_display_name()
#include "tools/auth.hpp"      // @note owner_display()

#include "world_key.hpp"

namespace
{
    constexpr short MY_FIRST_WORLD_LOCK = 9640;

    short held(const ::peer &p, short id)
    {
        auto it = std::ranges::find(p.slots, id, &::slot::id);
        return (it == p.slots.end()) ? 0 : it->count;
    }

    /* @return the World Lock's tile (not Small/Big/Huge/Builder's Locks), {-1, -1} if none */
    ::pos world_lock_tile(const ::world &world)
    {
        const int width = static_cast<int>(world.blocks.size()) / 60;
        for (int i = 0; i < static_cast<int>(world.blocks.size()); ++i)
        {
            const short fg = world.blocks[i].fg;
            if (fg != 0 && id_to_item(fg).type == type::LOCK && !is_tile_lock(fg)) return ::pos{ i % width, i / width };
        }
        return ::pos{ -1, -1 };
    }
}

short world_key_id()
{
    static const short id = []
    {
        auto it = std::ranges::find(items, std::string_view{ "World Key" }, &::item::raw_name);
        return (it == items.end()) ? short{} : static_cast<short>(it->id);
    }();
    return id;
}

void world_key_get(ENetEvent &event, ::world &world)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const short key = world_key_id();
    if (key == 0) return;

    const ::pos lock = world_lock_tile(world);
    if (lock.x < 0) return;
    /* @note the rules from the wiki (World Key) */
    if (world.blocks[cord(lock.x_int(), lock.y_int())].fg == MY_FIRST_WORLD_LOCK)
        return on::ConsoleMessage(event.peer, "`4You can't get a World Key for a My First World Lock.``");
    if (std::ranges::any_of(world.access, [](int uid) { return uid > 0; }))
        return on::ConsoleMessage(event.peer, "`4Remove everyone from the access list first.``");
    if (std::ranges::any_of(world.blocks, [](const ::block &b) { return b.fg != 0 && is_tile_lock(b.fg); }))
        return on::ConsoleMessage(event.peer, "`4Remove the other locks in this world first.``");
    if (!world.objects.empty())
        return on::ConsoleMessage(event.peer, "`4Pick up the items lying around first.``");
    if (held(*pPeer, key) > 0) return on::ConsoleMessage(event.peer, "`4You already have a World Key.``");
    if (std::ranges::find(pPeer->slots, key, &::slot::id) == pPeer->slots.end() && static_cast<int>(pPeer->slots.size()) >= pPeer->slot_size)
        return on::ConsoleMessage(event.peer, "`4Your backpack is full!``");

    modify_item_inventory(event, ::slot{ key, 1 });
    on::ConsoleMessage(event.peer, "`2You got a World Key! You can now trade this World to other Players!``"); // @note Growtopia's text
}

void world_key_traded(ENetPeer &from, ENetPeer &to)
{
    ::peer *pFrom = static_cast<::peer*>(from.data), *pTo = static_cast<::peer*>(to.data);
    const short key = world_key_id();
    if (key == 0 || held(*pTo, key) < 1) return;

    ENetEvent theirs{ .peer = &to };
    modify_item_inventory(theirs, ::slot{ key, static_cast<short>(-held(*pTo, key)) }); // @note the key is used up either way

    auto world = std::ranges::find(worlds, pFrom->recent_worlds.back(), &::world::name);
    if (world == worlds.end() || pFrom->netid == 0 || world->owner != pFrom->user_id) return; // @note only the owner, in that world, sells it

    world->owner = pTo->user_id;
    world->access.fill(0);
    if (auto mine = std::ranges::find(pFrom->my_worlds, world->name); mine != pFrom->my_worlds.end()) mine->clear();
    if (std::ranges::find(pTo->my_worlds, world->name) == pTo->my_worlds.end())
    {
        std::ranges::rotate(pTo->my_worlds, pTo->my_worlds.begin() + 1);
        pTo->my_worlds.back() = world->name;
    }

    if (const ::pos lock = world_lock_tile(*world); lock.x >= 0)
    {
        ::gamePacket update{};
        update.punch = lock;
        send_tile_update(theirs, update, world->blocks[cord(lock.x_int(), lock.y_int())], *world);
    }
    refresh_display_name(from);
    refresh_display_name(to);
    const std::string message = std::format("`5[```w{}`` has been `$World Locked`` by {}`5]``", world->name, owner_display(pTo->user_id));
    peers(world->name, PEER_SAME_WORLD, [&message](ENetPeer &p) { on::ConsoleMessage(&p, message); });
}

void world_key_left(ENetEvent &event)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const short key = world_key_id();
    if (key != 0 && held(*pPeer, key) > 0) modify_item_inventory(event, ::slot{ key, static_cast<short>(-held(*pPeer, key)) });
}
