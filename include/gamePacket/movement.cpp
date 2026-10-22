#include "pch.hpp"
#include "action/respawn.hpp"
#include "tools/fishing.hpp"
#include "tools/area_lock.hpp"
#include "tools/playmods.hpp"
#include "tools/anticheat.hpp"

#include "movement.hpp"

void movement(ENetEvent& event, ::gamePacket gamePacket) 
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    
    /* @note the position comes from the client: a made-up one (NaN, negative, past the world) was later used as a tile index */
    if (auto world = std::ranges::find(worlds, pPeer->recent_worlds.back(), &::world::name);
        world == worlds.end() || !in_world(*world, gamePacket.pos.by_32(true))) return;
    if (!move_allowed(*event.peer, gamePacket.pos)) return; // @note teleport/speed hack: put back, nobody sees the jump

    pPeer->pos = gamePacket.pos;
    pPeer->facing_left = gamePacket.state & state::S_MOVE_LEFT;
    fishing_moved(event); // @note walking away pulls the line in

    /* add fireproof only take away 1 hp instead of 2 */
    if ((gamePacket.state & state::S_LAVA_HIT) && !(pPeer->state & STATE_FIREPROOF)) pPeer->pain_hp -= 2; // @note fireproof items (playmods.txt)
    if (pPeer->pain_hp <= 0) action::respawn(event, ""), pPeer->pain_hp = 10;
    
    gamePacket.netid = pPeer->netid;
    state_visuals(*event.peer, std::move(gamePacket)); // finished.
}
