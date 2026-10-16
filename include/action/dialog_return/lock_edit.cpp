#include "pch.hpp"
#include "tools/area_lock.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "tools/economy.hpp"

#include "lock_edit.hpp"

void lock_edit(ENetEvent& event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    auto world = std::ranges::find(worlds, pPeer->recent_worlds.back(), &::world::name);
    if (world == worlds.end()) return;
    if (world->owner == 0 || pPeer->user_id != world->owner) return; // @note never trust the dialog: anyone can send a fake lock_edit

    ::pos pos{};
    pos.x = atoi(hPipe["tilex"].c_str());
    pos.y = atoi(hPipe["tiley"].c_str());
    if (!in_world(*world, pos.x_int(), pos.y_int())) return;
    
    world->is_public = atoi(hPipe["checkbox_public"].c_str());
    if (world->is_public && staff_locked(*pPeer)) // @note players would break the blocks a MOD/DEV spawned
    {
        world->is_public = false;
        on::ConsoleMessage(event.peer, "`4Staff accounts can't make their world public`` - spawned blocks would leak to players.");
    }
    if (atoi(hPipe["checkbox_disable_music"].c_str()) != 0)
        world->lock_state |= DISABLE_MUSIC;
    else world->lock_state &= ~DISABLE_MUSIC;
    world->minimum_entry_level = static_cast<u_char>(std::clamp(atoi(hPipe["minimum_entry_level"].c_str()), 1, 125)); // @note u_char: 300 became 44

    ::block &block = world->blocks[cord(pos.x, pos.y)];

    if (world->is_public) 
         block.state[2] |= S_PUBLIC;
    else block.state[2] &= ~S_PUBLIC;
    on::ConsoleMessage(event.peer, std::format("`2{}`` has set the `$World Lock`` to {}``", pPeer->growid, (block.state[2] & S_PUBLIC) ? "`$PUBLIC" : "`4PRIVATE"));

    send_tile_update(event, {
        .id = block.fg,
        .punch = pos
    }, block, *world);
}