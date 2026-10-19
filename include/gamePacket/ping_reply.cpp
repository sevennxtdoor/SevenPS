#include "pch.hpp"
#include "tools/cps.hpp"
#include "ping_reply.hpp"

void ping_reply(ENetEvent& event, ::gamePacket gamePacket)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    
    std::printf("ping requested from %s\n", pPeer->growid.c_str());
    start_join_ready(*event.peer); // @note enter_game's ping comes back after the world menu: a new player joins START right then
}