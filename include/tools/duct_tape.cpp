#include "pch.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "onVariant/SetClothing.hpp"
#include "tools/profile.hpp"

#include "duct_tape.hpp"

int taped_left(const ::peer &p) { return std::max<int>(0, pget(p, "tape_until") - static_cast<int>(std::time(nullptr))); }

void duct_tape(ENetPeer &target, int seconds)
{
    ::peer &p = *static_cast<::peer*>(target.data);
    pset(p, "tape_until", static_cast<int>(std::time(nullptr)) + seconds);
    p.state |= S_DUCT_TAPE;
    on::SetClothing(target);
    on::ConsoleMessage(&target, "`4Duct tape has covered your mouth!``"); // @note Growtopia's text
}

void duct_tape_tick(ENetPeer &peer)
{
    ::peer &p = *static_cast<::peer*>(peer.data);
    const bool taped = taped_left(p) > 0;
    if (taped == static_cast<bool>(p.state & S_DUCT_TAPE)) return;

    if (taped) p.state |= S_DUCT_TAPE; // @note back in the game (or another world) while still taped
    else
    {
        p.state &= ~S_DUCT_TAPE;
        pset(p, "tape_until", 0);
        on::ConsoleMessage(&peer, "`2Duct tape removed. OUCH!``"); // @note Growtopia's text
    }
    on::SetClothing(peer);
}

bool tape_blocks(ENetPeer &peer)
{
    const ::peer &p = *static_cast<::peer*>(peer.data);
    if (taped_left(p) == 0) return false;
    on::ConsoleMessage(&peer, std::format("`4Your mouth is covered with duct tape`` for {} more minutes.", (taped_left(p) + 59) / 60));
    return true;
}
