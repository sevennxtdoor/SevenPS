#include "pch.hpp"
#include "onVariant/ConsoleMessage.hpp"

#include "itemfavourite.hpp"

void action::itemfavourite(ENetEvent& event, const std::string& header)
{
    const std::vector<std::string> pipes = readch(header, '|');
    const int id = (pipes.size() > 4) ? atoi(pipes[4].c_str()) : 0; // @note [4] read past the end of a short packet, and stoi("abc") threw and crashed the server
    if (id <= 0 || id_to_item(id).id == 0) return;

    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    auto it = std::ranges::find(pPeer->fav, id);
    bool fav = it != pPeer->fav.end();
    if (pPeer->fav.size() >= 20 && !fav)
    {
        const std::string &message = "You cannot favorite any more items. Remove some from your list and try again.";

        send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, message, 0u, 1u });
        on::ConsoleMessage(event.peer, message);
        return;
    }
    send_varlist(event.peer, { "OnFavItemUpdated", id, (fav) ? 0 : 1 });
    if (fav) pPeer->fav.erase(it);
    else pPeer->fav.emplace_back(id);
}
