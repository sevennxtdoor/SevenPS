#include "pch.hpp"
#include "action/quit_to_exit.hpp"
#include "tools/discord.hpp"
#include "quit.hpp"

void action::quit(ENetEvent& event, const std::string& header) 
{
    action::quit_to_exit(event, "", true);
    
    if (event.peer == nullptr) return;
    if (event.peer->data != nullptr)
    {
        const ::peer *pPeer = static_cast<::peer*>(event.peer->data);
        if (pPeer->loaded && pPeer->login_time != 0) // @note a real game session (not the login hand-over or a refused login)
            discord_post("staff", std::format("{} left (played {} min)", pPeer->growid, (std::time(nullptr) - pPeer->login_time) / 60));
        delete static_cast<::peer*>(event.peer->data);
        event.peer->data = nullptr;
    }
    enet_peer_reset(event.peer);
}