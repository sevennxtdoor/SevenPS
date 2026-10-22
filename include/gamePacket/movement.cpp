#include "pch.hpp"
#include "action/respawn.hpp"
#include "tools/fishing.hpp"
#include "tools/area_lock.hpp"
#include "tools/playmods.hpp"
#include "tools/anticheat.hpp"
#include "tools/duct_tape.hpp"
#include "onVariant/SetClothing.hpp"

#include "movement.hpp"

void movement(ENetEvent& event, ::gamePacket gamePacket) 
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    
    /* @note the position comes from the client: a made-up one (NaN, negative, past the world) was later used as a tile index */
    if (auto world = std::ranges::find(worlds, pPeer->recent_worlds.back(), &::world::name);
        world == worlds.end() || !in_world(*world, gamePacket.pos.by_32(true))) return;
    if (pPeer->others_clothes_due && pPeer->netid != 0) // @note our game has loaded the world now (join_request.cpp)
    {
        pPeer->others_clothes_due = false;
        for (ENetPeer *other : peers(pPeer->recent_worlds.back(), PEER_SAME_WORLD))
            if (other != event.peer) on::ShowClothing(*event.peer, *other);
    }
    if (!move_allowed(*event.peer, gamePacket.pos)) return; // @note teleport/speed hack: put back, nobody sees the jump

    pPeer->pos = gamePacket.pos;
    pPeer->facing_left = gamePacket.state & state::S_MOVE_LEFT;
    fishing_moved(event); // @note walking away pulls the line in
    duct_tape_tick(*event.peer); // @note 10 minutes are up: take the tape off

    /* @note Growtopia (wiki: Lava): 5 lava hits in quick succession kill you, 10 with the Fireproof mod (playmods.txt),
     * and about 5 seconds without a hit heals you. fireproof used to make you never die at all */
    if (gamePacket.state & state::S_LAVA_HIT)
    {
        const std::time_t now = std::time(nullptr);
        if (now - pPeer->pain_at >= 5) pPeer->pain_hp = 10;
        pPeer->pain_at = now;
        pPeer->pain_hp -= (pPeer->state & STATE_FIREPROOF) ? 1 : 2;
    }
    if (pPeer->pain_hp <= 0) action::respawn(event, ""), pPeer->pain_hp = 10;
    
    gamePacket.netid = pPeer->netid;
    on::RememberMove(*event.peer, gamePacket); // @note shown again to whoever enters while we stand still
    state_visuals(*event.peer, std::move(gamePacket)); // finished.
}
