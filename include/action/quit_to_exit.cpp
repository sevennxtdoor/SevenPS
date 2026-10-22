#include "pch.hpp"
#include "onVariant/RequestWorldSelectMenu.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "trade.hpp"
#include "tools/surgery.hpp"
#include "tools/world_key.hpp"
#include "onVariant/SetClothing.hpp"
#include "quit_to_exit.hpp"

void action::quit_to_exit(ENetEvent& event, const std::string& header, bool skip_selection = false) 
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (pPeer == nullptr) return; // @note connection that never got a player (refused by tools/guard)
    /* @note not in a world (already left it): recent_worlds still names the last one, so leaving to the menu and then quitting the game
     * took that world's visitor count down twice. it reached 0 with others still inside and was unloaded; the next one to enter got a
     * fresh netid counter, the same netid as someone there, and that player's game force closed (tested with 2 PCs) */
    if (pPeer->netid == 0) return;
    if (pPeer->trade_netid != 0) end_trade(*event.peer); // @note leaving the world cancels a trade
    abandon_surgery(event); // @note ...and fails a surgery

    auto world = std::ranges::find(worlds, pPeer->recent_worlds.back(), &::world::name);
    if (world == worlds.end()) return; // @note peer was not in a world, therefore nothing to exit from.
    world_key_left(event); // @note a World Key only works in the world it was made in

    std::string message = std::format("`5<{} left, `w{}`` others here>``", pPeer->display_growid, world->visitors-1);
    std::string netid = std::format("netID|{}\n", pPeer->netid);
    std::string pId = std::format("pId|{}\n", pPeer->user_id); // @note this is found during OnSpawn 'eid', the value is the same for user_id.
    peers(pPeer->recent_worlds.back(), PEER_SAME_WORLD, [&event, pPeer, message, netid, pId](ENetPeer& peer) 
    {
        ::peer *pOthers = static_cast<::peer*>(peer.data);
        if (pOthers->user_id == pPeer->user_id) return;

        on::ConsoleMessage(&peer, message);
        send_varlist(&peer, { "OnRemove", netid, pId }); // @todo
    });

    if (--world->visitors <= 0) // @note take 1, and if result is 0, delete memory copy of world.
    {
        /* @note erase() would copy the next worlds over this one and only destroy the last, so this world's
         * destructor (which saves it) never ran. move it to the back first so it is the one destroyed & saved */
        std::iter_swap(world, std::prev(worlds.end()));
        worlds.pop_back();
    }
    on::ForgetMove(*event.peer); // @note our last movement belonged to that world
    pPeer->netid = 0; // this will fix any packets being sent outside of world; this can also be used to check if peer is not in a world.

    pPeer->display_growid = pPeer->name_as("`w");
    if (!skip_selection) on::RequestWorldSelectMenu(event);
}