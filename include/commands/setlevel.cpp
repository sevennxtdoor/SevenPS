#include "pch.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "onVariant/CountryState.hpp"
#include "tools/economy.hpp" // @note staff_log()

#include "setlevel.hpp"

void setlevel(ENetEvent &event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (pPeer->role < OWNER) return send_action(*event.peer, "log", "msg|`4Unknown command.`` Enter `$/?`` for a list of valid commands.");

    std::vector<std::string> args = readch(std::string{ text }, ' ');
    if (args.size() < 3) return on::ConsoleMessage(event.peer, "`oUsage: /setlevel {growid} {1-125}``");
    const int level = atoi(args[2].c_str());
    if (level < 1 || level > 125) return on::ConsoleMessage(event.peer, "`4Level must be 1-125.``");

    auto lower = [](std::string s) { for (char &c : s) c = static_cast<char>(std::tolower(static_cast<u_char>(c))); return s; };
    for (ENetPeer *p : peers())
        if (::peer *pp = static_cast<::peer*>(p->data); pp && !pp->growid.empty() && lower(pp->growid) == lower(args[1]))
        {
            pp->level.front() = static_cast<u_int>(level);
            pp->level.back() = 0; // @note xp starts over at the new level
            ENetEvent theirs{ .peer = p };
            on::CountryState(theirs); // @note the level 125 badge
            send_varlist(p, { "OnPlayerLeveledUp", level });
            staff_log(*pPeer, "/setlevel", pp->growid, 0, level);
            on::ConsoleMessage(event.peer, std::format("`oSet {}'s level to `w{}``.``", pp->display_growid, level));
            if (p != event.peer) on::ConsoleMessage(p, std::format("`oYour level was set to `w{}``.``", level));
            return;
        }
    on::ConsoleMessage(event.peer, "`4That player is offline.``");
}
