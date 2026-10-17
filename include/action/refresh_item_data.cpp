#include "pch.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "refresh_item_data.hpp"

void action::refresh_item_data(ENetEvent& event, const std::string& header) 
{
    on::ConsoleMessage(event.peer, "One moment, updating item data...");
    ENetPacket *packet = enet_packet_create(im_data.data(), im_data.size(), ENET_PACKET_FLAG_RELIABLE);
    if (send_packet(event.peer, packet)) enet_packet_destroy(packet); // @note 5.7 MB: plain enet_peer_send refused it (64 KB limit)
}