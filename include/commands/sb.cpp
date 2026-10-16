#include "pch.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "onVariant/SetBux.hpp"
#include "tools/rank.hpp"
#include "tools/wrench_menu.hpp"
#include "tools/jammer.hpp"
#include "commands/gtos.hpp"
#include "tools/economy.hpp"

#include "sb.hpp"

/* @param megaphone paid with a Megaphone item instead of gems */
static void super_broadcast(ENetEvent& event, const std::string_view text, bool megaphone)
{
    if (text.size() <= sizeof("sb ")-1) return; // @note "/sb" with no message: substr() would throw and crash the server
    const std::string message = clean_text(std::string{ text.substr(sizeof("sb ")-1) }, 120);
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    auto world = std::ranges::find(worlds, pPeer->recent_worlds.back(), &::world::name);
    if (world == worlds.end()) return;

    std::string display = jammer_on(*world, jammer::SIGNAL) ? "`4JAMMED!``" : pPeer->recent_worlds.back(); // @note Signal Jammer
    set_last_sb_world(pPeer->recent_worlds.back()); // @note /go

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

    /* @note like CreativePS, a super broadcast costs gems (free for ranks with the perk) */
    constexpr int SB_COST = 1000;
    if (megaphone)
    {
        auto it = std::ranges::find(pPeer->slots, static_cast<short>(2480), &::slot::id);
        if (it == pPeer->slots.end() || it->count < 1) return;
        modify_item_inventory(event, ::slot(2480, -1));
    }
    else if (const ::rank *r = get_rank(*pPeer); !(r && r->sb_free))
    {
        if (pPeer->gems < SB_COST)
        {
            on::ConsoleMessage(event.peer, std::format("`4A super broadcast costs `w{}`` gems.`` Get a rank (`$/rank``) for free broadcasts!", SB_COST));
            return;
        }
        pPeer->gems -= SB_COST;
        econ("super broadcasts", -SB_COST);
        on::SetBux(event);
    }
    peers("", PEER_ALL, [&event, &pPeer, message, display](ENetPeer& peer)
    {
        if (::peer *pp = static_cast<::peer*>(peer.data); pp && pp != pPeer && radio_off(*pp)) return; // @note /radio off
        on::ConsoleMessage(&peer,
            std::format(
                "CP:0_PL:0_OID:_CT:[SB]_ `5** from ({}```5) in [```${}```5] ** : ```${}``",
                pPeer->display_growid, display, message
            )
        );
    });
}

void sb(ENetEvent& event, const std::string_view text) { super_broadcast(event, text, false); }

void sb_megaphone(ENetEvent& event, const std::string_view text) { super_broadcast(event, text, true); }
