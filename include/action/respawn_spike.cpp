#include "pch.hpp"
#include "tools/anticheat.hpp"
#include "respawn_spike.hpp"

void action::respawn_spike(ENetEvent& event, const std::string& header)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    moved_by_server(*pPeer, pPeer->rest_pos); // @note back at the checkpoint/door: not a teleport hack (respawn.cpp)
    send_varlist(event.peer, { "OnSetPos", CL_Vec2f{pPeer->rest_pos.x, pPeer->rest_pos.y} }, pPeer->netid, 0x000007d0/*2000ms*/);
    /* audio/teleport.wav */
    send_varlist(event.peer, { "OnSetFreezeState", 0u }, pPeer->netid, 0x000007d0/*2000ms*/);
}