#include "pch.hpp"
#include "commands/__command.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "tools/time.hpp"
#include "tools/random.hpp"
#include "tools/social.hpp"
#include "tools/cps.hpp"
#include "commands/gtos.hpp"
#include "tools/casino.hpp"
#include "tools/duct_tape.hpp"
#include "input.hpp"

void action::input(ENetEvent& event, const std::string& header)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    ::hPipe hPipe{ header };
    std::string text = hPipe["text"];

    if (text.empty() || text.front() == '\r' || std::ranges::all_of(text, ::isspace)) return;
    text.erase(text.begin(), std::find_if_not(text.begin(), text.end(), ::isspace));
    text.erase(std::find_if_not(text.rbegin(), text.rend(), ::isspace).base(), text.end());
    if (text.empty()) return; // @note we recheck if empty since we did trimming.
    if (text.size() > 120 && !text.starts_with('/')) text.resize(120); // @note the game caps chat at 120: a proxy sent kilobytes to everyone in the world
    
    duct_tape_tick(*event.peer);
    u_int now = ticks();
    pPeer->messages.push_back(now);
    if (pPeer->messages.size() > 5) pPeer->messages.pop_front();
    if (pPeer->messages.size() == 5 && now - pPeer->messages.front() < 6)
    {
        on::ConsoleMessage(event.peer,
            "`6>>`4Spam detected! ``Please wait a bit before typing anything else.  "  
            "Please note, any form of bot/macro/auto-paste will get all your accounts banned, so don't do it!");
    }
    else if (muted_left(*pPeer) > 0 && (!text.starts_with('/') || text.starts_with("/sb") || text.starts_with("/msg") || text.starts_with("/gc") || text.starts_with("/me ")))
    {
        on::ConsoleMessage(event.peer, std::format("`4You are muted for {} more minutes.``", (muted_left(*pPeer) + 59) / 60));
    }
    else if ((text.starts_with("/sb") || text.starts_with("/msg") || text.starts_with("/gc")) && tape_blocks(*event.peer)) {} // @note duct tape: no broadcasts or messages
    else if (pPeer->role < MODERATOR && fake_spin(text)) // @note chat, /me, /sb, /msg: no fake "spun the wheel" messages (casino scams)
    {
        on::ConsoleMessage(event.peer, "`4Fake Roulette Wheel results aren't allowed.`` Only a real spin shows that message.");
    }
    else if (text.starts_with('/')) 
    {
        send_action(*event.peer, "log", std::format("msg| `6{}``", text));
        std::string command = text.substr(1, text.find(' ') - 1);
        
        if (auto it = cmd_pool.find(command); it != cmd_pool.end()) 
        {
            if (std::ranges::find(cmd_requires_arg, command) != cmd_requires_arg.end() && text.length() <= command.length()+1)
            {
                send_action(*event.peer, "log", "msg|`4Unknown command.`` Enter `$/?`` for a list of valid commands.");
            }
            else it->second(std::ref(event), std::move(text.substr(1)/* remove the '/' */));
        }
        else 
        {
            send_action(*event.peer, "log", "msg|`4Unknown command.`` Enter `$/?`` for a list of valid commands.");
        }
    }
    else 
    {
        if (pPeer->state & S_DUCT_TAPE)
        {
            for (char &c : text)
            {
                if (c != ' ') c = (RandomRange(1, 3) == 1) ? 'm' : 'f';
            }
        }
        /* @note staff chat color: owner `9 (gold), developer `5 (pink), moderator `^ (lime) */
        const std::string_view staff_color = !pPeer->nick.empty() ? "" : // @note /nick = undercover, chat like a normal player
            (pPeer->role >= OWNER) ? "`9" : (pPeer->role >= DEVELOPER) ? "`5" : (pPeer->role >= MODERATOR) ? "`^" : "";
        const std::string &player_chat = std::format("CP:0_PL:0_OID:_player_chat={}{}", staff_color, text);
        const std::string &message = std::format("CP:0_PL:0_OID:_CT:[W]_ `6<{}>`` {}{}````", pPeer->display_growid, staff_color.empty() ? "`$`$" : staff_color, text);
        peers(pPeer->recent_worlds.back(), PEER_SAME_WORLD, [&event, &pPeer, player_chat, message](ENetPeer& p) 
        {
            if (is_ignoring(*static_cast<::peer*>(p.data), pPeer->growid)) return; // @note they ignored us
            send_varlist(&p, { "OnTalkBubble", pPeer->netid, player_chat, 0u });
            on::ConsoleMessage(&p, message, 2);
        });
        check_quiz(event, text); // @note chat quiz answer
    }
}
