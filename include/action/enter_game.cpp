#include "pch.hpp"
#include "onVariant/RequestWorldSelectMenu.hpp"
#include "onVariant/RequestGazette.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "onVariant/SetBux.hpp"
#include "tools/create_dialog.hpp"
#include "automate/holiday.hpp"
#include "tools/cps.hpp"
#include "tools/social.hpp"
#include "action/join_request.hpp"

#include "enter_game.hpp"

void action::enter_game(ENetEvent& event, const std::string& header) 
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    pPeer->display_growid = pPeer->name_as("`w");
    const int friends = friends_online(pPeer->growid);
    on::ConsoleMessage(event.peer, (friends == 0) ?
        std::format("Welcome back, {}. No friends are online.", pPeer->display_growid) :
        std::format("Welcome back, {}. `w{}`` friend{} online.", pPeer->display_growid, friends, (friends == 1) ? " is" : "s are"));
    on::ConsoleMessage(event.peer, "`o>> Welcome to `2Seven Private Server``!");
    on::ConsoleMessage(event.peer, holiday_greeting().second);
    on::ConsoleMessage(event.peer, "`5Personal Settings active:`` `#Can customize profile``");
    const bool new_player = announce_join(event);
    if (pPeer->role >= MODERATOR)
        on::ConsoleMessage(event.peer, "`5Super Supporter`` status active! Perks: `$/nick {name}``, all titles (wrench yourself > `wTitle``)");
    
    send_inventory_state(event);
    on::SetBux(event);
    send_varlist(event.peer, { "SetHasGrowID", 1, pPeer->growid.c_str(), "" });
    {
        std::tm time = localtime();

        send_varlist(event.peer, {
            "OnTodaysDate",
            time.tm_mon + 1,
            time.tm_mday,
            0u, // @todo
            0u // @todo
        });
    } // @note delete time

    on::RequestWorldSelectMenu(event);
    on::RequestGazette(event);

    send_data(*event.peer, compress_state(::gamePacket{ .type = 0x16 /*PACKET_PING_REQUEST*/ }));
    /* for v5.47+ client */
    send_varlist(event.peer, {
        "OnSetFeatureEnableFlags",
        "EA8DEAcGAgEOBQgKCQ0MEQQ=" // @todo Dw0JEQQMEAMPAgYBDgUICg==
    });
    if (new_player) action::join_request(event, "", "START"); // @note a new player lands in the hub right away instead of the world menu
}
