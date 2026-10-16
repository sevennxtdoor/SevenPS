#include "pch.hpp"
#include "tools/auth.hpp"
#include "tools/web_login.hpp"
#include "https/server_data.hpp"
#include "proton/Variant.hpp"

#include "protocol.hpp"

void action::protocol(ENetEvent& event, const std::string& header)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (pPeer->loaded) return; // @note already in the game: a 2nd login on this connection would reload (and dupe) the backpack
    /* @note ltoken = the session token from our own login page (tools/web_login.cpp), never the password.
     * the old login page sent base64 "growId=..&password=.." here, so whoever ran that page could read every password */
    std::vector<std::string> pipes = readch(header, '|');
    std::string token{};
    for (std::size_t i = 0; i + 1 < pipes.size(); ++i)
        if (pipes[i] == "ltoken") token = pipes[i + 1];

    const std::string growid = session_growid(token);
    if (growid.empty())
    {
        std::printf("[login] refused: no valid login token (old or made-up)\n");
        send_action(*event.peer, "log", "msg|`4Please log in again`` on the MikeTopia login page.");
        send_action(*event.peer, "logon_fail", "");
        return;
    }
    pPeer->growid = growid;
    pPeer->password = token; // @note goes back to the client as tankIDPass (SetHasGrowID below), checked again in tankIDName
    const std::string password = pPeer->password;
    pPeer->load(pPeer->growid, pPeer->password); // @todo i want remove this tbh. since we only are fetching user_id which seems a waste.
    pPeer->loaded = false; // @note this first connection only hands over to the game connection: it must never save (stale data over the real session)

    /* @note the client keeps these and sends them back as tankIDName/tankIDPass on the next connection (GrowServer) */
    send_varlist(event.peer, { "SetHasGrowID", 1, pPeer->growid.c_str(), password.c_str() });

    send_varlist(event.peer, {
        "OnSendToServer", 
        (signed)gServer_data.port, 
        0, 
        pPeer->user_id, 
        std::format("{}|0|0", gServer_data.server), // @todo UUIDToken
        1, 
        pPeer->growid.c_str()
    }); // @note PACKET_DISCONNECT from client.
}