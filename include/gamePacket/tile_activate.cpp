#include "pch.hpp"
#include "action/join_request.hpp"
#include "action/quit_to_exit.hpp"
#include "tools/system_worlds.hpp"
#include "tools/password_door.hpp"
#include "tile_activate.hpp"

void tile_activate(ENetEvent& event, ::gamePacket gamePacket)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    auto world = std::ranges::find(worlds, pPeer->recent_worlds.back(), &::world::name);
    if (world == worlds.end()) return;

    ::block &block = world->blocks[cord(gamePacket.punch.x, gamePacket.punch.y)];
    const ::item &item = id_to_item(block.fg);
    switch (item.type)
    {
        case type::MAIN_DOOR:
        {
            action::quit_to_exit(event, "", false);
            break;
        }
        case type::DOOR:
        case type::PORTAL:
        {
            const ::pos tile = gamePacket.punch;
            state_visuals(*event.peer, std::move(gamePacket)); // @note before we leave, so this world sees us go in
            door_activate(event, *world, tile); // @note WORLD, WORLD:ID, :ID and the Password Door
            return; // @note we may be in another world now, `world` is stale
        }
        case type::CHECKPOINT:
        {
            ::block &checkpoint = world->blocks[cord(pPeer->rest_pos.by_32(true).x, pPeer->rest_pos.by_32(true).y)]; // @note get previous checkpoint from respawn position

            checkpoint.state[2] &= ~S_TOGGLE;
            send_tile_update(event, ::gamePacket{.id = block.fg/*has to be 'block' or else iterfere with main door*/, .punch = pPeer->rest_pos.by_32(true)}, checkpoint, *world);

            pPeer->rest_pos = gamePacket.punch.by_32();
            block.state[2] |= S_TOGGLE; // @note toggle current checkpoint
            send_tile_update(event, ::gamePacket{.id = block.fg, .punch = gamePacket.punch}, block, *world);
            system_checkpoint(event, *world, gamePacket.punch); // @note PARKOUR finish
            break;
        }
    }

    state_visuals(*event.peer, std::move(gamePacket)); // finished.
}