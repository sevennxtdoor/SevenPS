#include "pch.hpp"
#include "action/quit.hpp"
#include "tools/guard.hpp"
#include "connect.hpp"

void _connect(ENetEvent& event, int status)
{
    if (!guard_connect(*event.peer))
    {
        enet_peer_reset(event.peer); // @note blocked IP or connection flood: drop it before anything is allocated
        return;
    }
    ENetPacket *packet = enet_packet_create(nullptr, sizeof(status)+1, ENET_PACKET_FLAG_RELIABLE);

    if (peers().size() > host->peerCount)
    {
        send_action(*event.peer, "log",
            std::format(
                "msg|`4SERVER OVERLOADED`` : Sorry, our servers are currently at max capacity with {} online, please try later. We are working to improve this!",
                host->peerCount
            ));
        send_action(*event.peer, "logon_fail", ""); // @note triggers action|quit on client.
        status = 0;
    }
    event.peer->data = new peer();

    memcpy(packet->data, &status, sizeof(status));
    if (enet_peer_send(event.peer, 0, packet)) enet_packet_destroy(packet);
}
