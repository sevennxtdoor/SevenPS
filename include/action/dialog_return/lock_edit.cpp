#include "pch.hpp"
#include "tools/area_lock.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "tools/economy.hpp"
#include "commands/titles.hpp"
#include "tools/world_key.hpp"
#include "tools/home.hpp"
#include "tools/guild.hpp"

#include "lock_edit.hpp"

void lock_edit(ENetEvent& event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    auto world = std::ranges::find(worlds, pPeer->recent_worlds.back(), &::world::name);
    if (world == worlds.end()) return;
    if (world->owner == 0 || pPeer->user_id != world->owner) return; // @note never trust the dialog: anyone can send a fake lock_edit
    if (hPipe["buttonClicked"] == "getKey") return world_key_get(event, *world); // @note tools/world_key.cpp

    ::pos pos{};
    pos.x = atoi(hPipe["tilex"].c_str());
    pos.y = atoi(hPipe["tiley"].c_str());
    if (!in_world(*world, pos.x_int(), pos.y_int())) return;
    guild_lock_edit(*world, hPipe); // @note guild ranks with access (only a guild's home world)

    /* @note access list: unticked names are removed, the player picker adds someone in this world (like Small/Big/Huge Locks) */
    std::vector<int> changed{};
    for (int &uid : world->access)
        if (uid > 0 && hPipe[std::format("access_{}", uid)] == "0") changed.push_back(uid), uid = 0;
    if (const std::string picked = hPipe["playerNetID"]; !picked.empty())
    {
        const int netid = atoi(picked.c_str());
        for (ENetPeer *p : peers(world->name, PEER_SAME_WORLD))
        {
            ::peer *other = static_cast<::peer*>(p->data);
            if (other->netid != netid) continue;
            if (other->user_id == world->owner || std::ranges::find(world->access, other->user_id) != world->access.end()) break;
            auto empty = std::ranges::find(world->access, 0);
            if (empty == world->access.end())
            {
                on::ConsoleMessage(event.peer, std::format("The access list is full ({} players).", world->access.size()));
                break;
            }
            *empty = other->user_id;
            changed.push_back(other->user_id);
            on::ConsoleMessage(event.peer, std::format("`w{}`` now has access to `w{}``.", other->growid, world->name));
            on::ConsoleMessage(p, std::format("`w{}`` gave you access to `w{}``.", pPeer->growid, world->name));
            break;
        }
    }
    for (ENetPeer *p : peers(world->name, PEER_SAME_WORLD)) // @note cyan name on/off for the ones added/removed
        if (std::ranges::find(changed, static_cast<::peer*>(p->data)->user_id) != changed.end()) refresh_display_name(*p);

    if (hPipe["checkbox_set_as_home_world"] == "1" && home_world(*pPeer) != world->name) set_home(event, world->name); // @note like /sethome

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