#include "pch.hpp"
#include "tools/guild.hpp"

#include "CountryState.hpp"

std::string on::country_state(const ::peer &peer)
{
    /* @todo add |showGuild when we add guild system; also add peer's real country */
    /* @note |maxLevel is what makes the client draw the blue name (level 125), the Blue Name title reuses it */
    const bool blue_name = peer.level[0] == 125 || (peer.titles_on & peer.owned_titles() & T_BLUENAME);
    return std::format("{}{}{}", peer.country, in_guild(peer.growid) ? "|showGuild" : "", blue_name ? "|maxLevel" : ""); // @note order as GTOS
}

void on::CountryState(ENetEvent& event)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const std::string state = country_state(*pPeer);

    /* @note everyone in the world must get it, not only this peer, or only they see their own blue name */
    if (pPeer->netid == 0)
    {
        send_varlist(event.peer, { "OnCountryState", state }, pPeer->netid);
        return;
    }
    peers(pPeer->recent_worlds.back(), PEER_SAME_WORLD, [&state, &pPeer](ENetPeer &p)
    {
        send_varlist(&p, { "OnCountryState", state }, pPeer->netid);
    });
}
