#include "pch.hpp"
#include <sstream>

#include "onVariant/SetClothing.hpp"
#include "titles.hpp"
#include "setrole.hpp"

static std::string to_lower(std::string str)
{
    for (char &c : str) c = std::tolower(c);
    return str;
}

/* /setrole {growid} {player|mod|dev} - developer+. works on offline players too (saved to SQL)
 * @note you can only change someone whose role is below yours, and only give roles below yours:
 *       developer -> player/mod, owner -> player/mod/dev. owner is never given by command (set it in the database) */
void setrole(ENetEvent& event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (pPeer->role < DEVELOPER)
    {
        send_action(*event.peer, "log", "msg|`4Unknown command.`` Enter `$/?`` for a list of valid commands.");
        return;
    }

    std::istringstream args{ std::string{ text.substr(sizeof("setrole ")-1) } };
    std::string growid{}, role_name{};
    if (!(args >> growid >> role_name))
    {
        send_action(*event.peer, "log", "msg|`4Usage:`` /setrole {growid} {player|mod|dev}");
        return;
    }

    u_char role{};
    std::string label{};
    role_name = to_lower(role_name);
    if      (role_name == "player")                             { role = PLAYER;    label = "`wPlayer``"; }
    else if (role_name == "mod" || role_name == "moderator")    { role = MODERATOR; label = "`#Moderator``"; }
    else if (role_name == "dev" || role_name == "developer")    { role = DEVELOPER; label = "`6Developer``"; }
    else
    {
        send_action(*event.peer, "log", "msg|`4Unknown role.`` Use: player, mod, dev");
        return;
    }
    if (role >= pPeer->role)
    {
        send_action(*event.peer, "log", std::format("msg|`4You can't give the role`` {} `4- only roles below your own.``", label));
        return;
    }

    signed current = -1; // @note stays -1 when no such growid
    {
        ::hStmt hStmt{ "SELECT role FROM peer WHERE growid = ? LIMIT 1" };
        MYSQL_BIND param = make_bind_in(growid); // WHERE
        hStmt.bind_param(&param);
        MYSQL_BIND result = make_bind_out(current);
        mysql_stmt_bind_result(hStmt.pStmt, &result);
        hStmt.execute();
        hStmt.fetch();
    } // @note close statement
    if (current < 0)
    {
        send_action(*event.peer, "log", std::format("msg|`4Player `w{}`` `4not found.``", growid));
        return;
    }
    if (current >= pPeer->role)
    {
        send_action(*event.peer, "log", std::format("msg|`4You can't change `w{}``'s role.`` `4(equal or higher than yours)``", growid));
        return;
    }
    {
        const signed value = role;
        ::hStmt hStmt{ "UPDATE peer SET role = ? WHERE growid = ?" };
        MYSQL_BIND params[2] = {
            make_bind_in(value), // SET
            make_bind_in(growid) // WHERE
        };
        hStmt.bind_param(params);
        hStmt.execute();
    }

    /* if the player is online, apply it now instead of on next login */
    peers("", PEER_ALL, [&](ENetPeer &p)
    {
        ::peer *pOthers = static_cast<::peer*>(p.data);
        if (!pOthers || to_lower(pOthers->growid) != to_lower(growid)) return;

        pOthers->role = role;
        pOthers->update_effects(); // @note super supporter state follows the role
        if (pOthers->netid != 0) on::SetClothing(p);
        refresh_display_name(p);
        send_action(p, "log", std::format("msg|`oYour role is now {}`o.``", label));
    });

    send_action(*event.peer, "log", std::format("msg|`2Set `w{}`` `2to`` {}`2.``", growid, label));
}
