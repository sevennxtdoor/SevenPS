#include "pch.hpp"
#include "tools/auth.hpp"
#include "https/server_data.hpp"
#include "proton/Variant.hpp"

#include "protocol.hpp"

void action::protocol(ENetEvent& event, const std::string& header)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    try 
    {
        std::vector<std::string> pipes = readch(header, '|');
        if (pipes.size() < 4ull) throw std::runtime_error("");

        if (pipes[2ull] == "ltoken")
        {
            const std::string &decoded = base64_decode(pipes[3ull]);
            if (decoded.empty()) throw std::runtime_error("");

            if (std::size_t pos = decoded.find("growId="); pos != std::string::npos) 
            {
                pos += strlen("growId=");
                pPeer->growid = decoded.substr(pos, decoded.find('&', pos) - pos);
            }
            if (std::size_t pos = decoded.find("password="); pos != std::string::npos) 
            {
                pos += strlen("password=");
                pPeer->password = decoded.substr(pos, decoded.find('&', pos) - pos);
            }
        } // @note delete decoded
        if (pPeer->growid.empty() || pPeer->password.empty()) throw std::runtime_error("");
    }
    catch (...) { 
        send_action(*event.peer, "logon_fail", "");
        return; // @note stop processing invalid protocol data
    }
    /* @note the password was never checked: any password opened any account */
    if (const std::string why = authenticate(pPeer->growid, pPeer->password, true); !why.empty())
    {
        std::printf("[login] %s refused: %s\n", pPeer->growid.c_str(), why.c_str()); // @note never the password
        send_action(*event.peer, "log", std::format("msg|`4Failed`` logging in: {}", why));
        send_action(*event.peer, "logon_fail", "");
        pPeer->growid.clear(); // @note this peer is not that account
        return;
    }
    const std::string password = pPeer->password;
    pPeer->load(pPeer->growid, pPeer->password); // @todo i want remove this tbh. since we only are fetching user_id which seems a waste.

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