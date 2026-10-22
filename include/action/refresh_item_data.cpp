#include "pch.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "tools/guard.hpp" // @note peer_ip()
#include "refresh_item_data.hpp"

void action::refresh_item_data(ENetEvent& event, const std::string& header)
{
    std::printf("[items] %s asked for items.dat (%zu bytes)\n", peer_ip(*event.peer).c_str(), im_data.size());
    on::ConsoleMessage(event.peer, "One moment, updating item data...");
    /* @note 5.7 MB over a slow link (a VPN like Radmin) took longer than ENet's 30 s timeout and the player was cut off at "Updating items (8/10)".
     * give this peer up to 3 minutes; ENet's defaults are 32 / 5 s / 30 s */
    enet_peer_timeout(event.peer, 32, 15000, 180000);
    ENetPacket *packet = enet_packet_create(im_data.data(), im_data.size(), ENET_PACKET_FLAG_RELIABLE);
    if (send_packet(event.peer, packet)) enet_packet_destroy(packet); // @note 5.7 MB: plain enet_peer_send refused it (64 KB limit)
}
