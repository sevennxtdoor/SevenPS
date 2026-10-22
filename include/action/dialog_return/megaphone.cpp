#include "pch.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "commands/sb.hpp"
#include "commands/gtos.hpp" // @note muted_left()
#include "tools/casino.hpp" // @note fake_spin()
#include "tools/duct_tape.hpp"

#include "megaphone.hpp"

void megaphone(ENetEvent& event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const std::string message = hPipe["message"];
    if (message.empty()) return;

    /* @note this dialog skipped the chat checks: muted players and fake roulette results could still go to everyone */
    if (muted_left(*pPeer) > 0)
        return on::ConsoleMessage(event.peer, std::format("`4You are muted for {} more minutes.``", (muted_left(*pPeer) + 59) / 60));
    if (tape_blocks(*event.peer)) return; // @note duct tape: no broadcasts
    if (pPeer->role < MODERATOR && fake_spin(message))
        return on::ConsoleMessage(event.peer, "`4Fake Roulette Wheel results aren't allowed.`` Only a real spin shows that message.");

    sb_megaphone(event, "sb " + message); // @note uses up the Megaphone (not gems). sb skips the first 3 chars ("sb ")
}
