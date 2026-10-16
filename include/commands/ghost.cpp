#include "pch.hpp"
#include "onVariant/SetClothing.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "ghost.hpp"

void ghost(ENetEvent& event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    /* @note staff only: a ghost walks through walls (past locked doors, parkour, other people's areas) */
    if (pPeer->role < MODERATOR) return on::ConsoleMessage(event.peer, "`4/ghost is for moderators and above.``");

    pPeer->state ^= S_GHOST;
    on::ConsoleMessage(event.peer, (pPeer->state & S_GHOST) ? "`oGhost mode `2on``. Type `w/ghost`` again to turn it off.``" : "`oGhost mode `4off``.``");

    on::SetClothing(*event.peer);
}