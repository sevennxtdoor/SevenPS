#include "pch.hpp"
#include "onVariant/ConsoleMessage.hpp"

#include "sb.hpp"

void sb(ENetEvent& event, const std::string_view text)
{
    if (text.size() <= sizeof("sb ")-1) return; // @note "/sb" with no message: substr() would throw and crash the server
    const std::string message{ text.substr(sizeof("sb ")-1) };
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    auto world = std::ranges::find(worlds, pPeer->recent_worlds.back(), &::world::name);
    if (world == worlds.end()) return;

    std::string display = pPeer->recent_worlds.back();

    /* @note owner broadcast: gold header + a pop-up notification for everyone */
    if (pPeer->role >= OWNER)
    {
        const std::string owner_sb = std::format(
            "CP:0_PL:0_OID:_CT:[SB]_ `9>> `4[`9OWNER BROADCAST`4]`` `9from {}`9 in [`${}`9] `9>>`` `9{}``",
            pPeer->display_growid, display, message
        );
        peers("", PEER_ALL, [&owner_sb, &pPeer, &message](ENetPeer& peer)
        {
            on::ConsoleMessage(&peer, owner_sb);
            send_varlist(&peer, { "OnAddNotification", "interface/atomic_button.rttex",
                std::format("`9{}``: `w{}``", pPeer->display_growid, message), "audio/hub_open.wav", 0 });
        });
        return;
    }
    /* @note developer broadcast: orange header, no pop-up */
    if (pPeer->role >= DEVELOPER)
    {
        const std::string dev_sb = std::format(
            "CP:0_PL:0_OID:_CT:[SB]_ `6>> [DEVELOPER BROADCAST]`` `6from {}`6 in [`${}`6] >>`` `6{}``",
            pPeer->display_growid, display, message
        );
        peers("", PEER_ALL, [&dev_sb](ENetPeer& peer) { on::ConsoleMessage(&peer, dev_sb); });
        return;
    }

    peers("", PEER_ALL, [&event, &pPeer, message, display](ENetPeer& peer)
    {
        on::ConsoleMessage(&peer,
            std::format(
                "CP:0_PL:0_OID:_CT:[SB]_ `5** from ({}```5) in [```${}```5] ** : ```${}``",
                pPeer->display_growid, display, message
            )
        );
    });
}
