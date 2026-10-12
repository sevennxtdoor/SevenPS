#include "pch.hpp"
#include <sstream>
#include "action/quit_to_exit.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "tools/worldban.hpp"

#include "moderation.hpp"

static std::string to_lower(std::string str)
{
    for (char &c : str) c = std::tolower(c);
    return str;
}

/* "kick Budi" -> "Budi", "nuke" -> "" */
static std::string argument(const std::string_view text)
{
    const std::size_t space = text.find(' ');
    if (space == std::string_view::npos) return "";
    std::string arg{ text.substr(space + 1) };
    arg.erase(0, arg.find_first_not_of(' '));
    return arg;
}

static bool has_role(ENetEvent& event, u_char role)
{
    if (static_cast<::peer*>(event.peer->data)->role >= role) return true;
    send_action(*event.peer, "log", "msg|`4Unknown command.`` Enter `$/?`` for a list of valid commands.");
    return false;
}

static ENetPeer *find_online(const std::string &growid)
{
    for (ENetPeer *p : peers())
    {
        ::peer *pOthers = static_cast<::peer*>(p->data);
        if (pOthers && to_lower(pOthers->growid) == to_lower(growid)) return p;
    }
    return nullptr;
}

/* @return false if no such growid */
static bool set_banned(const std::string &growid, signed banned)
{
    {
        ::hStmt hStmt{ "SELECT 1 FROM peer WHERE growid = ? LIMIT 1" };
        MYSQL_BIND param = make_bind_in(growid);
        hStmt.bind_param(&param);
        hStmt.execute();
        if (mysql_stmt_store_result(hStmt.pStmt) || mysql_stmt_num_rows(hStmt.pStmt) == 0) return false;
    }
    ::hStmt hStmt{ "UPDATE peer SET banned = ? WHERE growid = ?" };
    MYSQL_BIND params[2] = { make_bind_in(banned), make_bind_in(growid) };
    hStmt.bind_param(params);
    hStmt.execute();
    return true;
}

bool is_nuked(const std::string &world_name)
{
    ::hStmt hStmt{ "SELECT 1 FROM nuked WHERE name = ? LIMIT 1" };
    MYSQL_BIND param = make_bind_in(world_name);
    hStmt.bind_param(&param);
    hStmt.execute();
    return (!mysql_stmt_store_result(hStmt.pStmt) && mysql_stmt_num_rows(hStmt.pStmt) > 0);
}

/* @note target must have a lower role than you, so mods can't kick devs */
static bool can_punish(ENetEvent& event, ENetPeer *target)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    ::peer *pTarget = static_cast<::peer*>(target->data);
    if (pTarget->role < pPeer->role) return true;
    send_action(*event.peer, "log", "msg|`4You can't do that to someone with an equal or higher role.``");
    return false;
}

void kick(ENetEvent& event, const std::string_view text)
{
    if (!has_role(event, MODERATOR)) return;

    const std::string growid = argument(text);
    ENetPeer *target = find_online(growid);
    if (!target)
    {
        send_action(*event.peer, "log", std::format("msg|`4Player `w{}`` `4is not online.``", growid));
        return;
    }
    if (!can_punish(event, target)) return;

    on::ConsoleMessage(target, "`4You have been kicked from the server by a moderator.``");
    enet_peer_disconnect_later(target, 0); // @note disconnect event saves their inventory
    send_action(*event.peer, "log", std::format("msg|`2Kicked `w{}``.", growid));
}

void ban(ENetEvent& event, const std::string_view text)
{
    if (!has_role(event, MODERATOR)) return;

    const std::string growid = argument(text);
    ENetPeer *target = find_online(growid);
    if (target && !can_punish(event, target)) return;
    if (to_lower(growid) == to_lower(static_cast<::peer*>(event.peer->data)->growid))
    {
        send_action(*event.peer, "log", "msg|`4You can't ban yourself.``");
        return;
    }
    if (!set_banned(growid, 1))
    {
        send_action(*event.peer, "log", std::format("msg|`4Player `w{}`` `4not found.``", growid));
        return;
    }
    if (target)
    {
        static_cast<::peer*>(target->data)->banned = true;
        on::ConsoleMessage(target, "`4Your account has been banned from MikeTopia.``");
        enet_peer_disconnect_later(target, 0);
    }
    peers("", PEER_ALL, [&growid](ENetPeer &p)
    {
        on::ConsoleMessage(&p, std::format("`4** `w{}`` `4has been banned from MikeTopia **``", growid));
    });
}

void unban(ENetEvent& event, const std::string_view text)
{
    if (!has_role(event, MODERATOR)) return;

    const std::string growid = argument(text);
    if (!set_banned(growid, 0))
    {
        send_action(*event.peer, "log", std::format("msg|`4Player `w{}`` `4not found.``", growid));
        return;
    }
    send_action(*event.peer, "log", std::format("msg|`2Unbanned `w{}``.", growid));
}

/* /nuke [world] - defaults to the world you are in */
void nuke(ENetEvent& event, const std::string_view text)
{
    if (!has_role(event, MODERATOR)) return;
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    std::string world_name = argument(text);
    if (world_name.empty() && pPeer->netid != 0) world_name = pPeer->recent_worlds.back();
    for (char &c : world_name) c = std::toupper(c);
    if (world_name.empty() || !alnum(world_name))
    {
        send_action(*event.peer, "log", "msg|`4Usage:`` /nuke {world} - or use /nuke inside the world.");
        return;
    }
    {
        ::hStmt hStmt{ "INSERT IGNORE INTO nuked (name) VALUES (?)" };
        MYSQL_BIND param = make_bind_in(world_name);
        hStmt.bind_param(&param);
        hStmt.execute();
    }

    /* @note collect first: quit_to_exit() may erase the world while we loop */
    for (ENetPeer *p : peers(world_name, PEER_SAME_WORLD))
    {
        ::peer *pOthers = static_cast<::peer*>(p->data);
        on::ConsoleMessage(p, "`4This world has been nuked!``");
        if (pOthers->role >= MODERATOR) continue; // @note staff may stay
        ENetEvent other{ .peer = p };
        action::quit_to_exit(other, "", false);
    }
    peers("", PEER_ALL, [&world_name](ENetPeer &p)
    {
        on::ConsoleMessage(&p, std::format("`o>>`4{}`` `owas nuked from orbit. It's the only way to be sure. Play nice, everybody!``", world_name));
    });
}

void unnuke(ENetEvent& event, const std::string_view text)
{
    if (!has_role(event, MODERATOR)) return;
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    std::string world_name = argument(text);
    if (world_name.empty() && pPeer->netid != 0) world_name = pPeer->recent_worlds.back();
    for (char &c : world_name) c = std::toupper(c);

    ::hStmt hStmt{ "DELETE FROM nuked WHERE name = ?" };
    MYSQL_BIND param = make_bind_in(world_name);
    hStmt.bind_param(&param);
    hStmt.execute();
    send_action(*event.peer, "log", std::format("msg|`2World `w{}`` `2is no longer nuked.``", world_name));
}

void uba(ENetEvent& event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    auto world = std::ranges::find(worlds, pPeer->recent_worlds.back(), &::world::name);
    if (pPeer->netid == 0 || world == worlds.end()) return;

    if ((world->owner == 0 || world->owner != pPeer->user_id) && pPeer->role < MODERATOR)
    {
        send_action(*event.peer, "log", "msg|`4Only the world owner can do that.``");
        return;
    }
    world_unban_all(world->name);
    on::ConsoleMessage(event.peer, "`oYou've unbanned everybody from this world.``");
}

/* /wban {growid} [minutes] - world owner (always 10 minutes) or staff (1-60 minutes). target must be in your world */
void wban(ENetEvent& event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    std::istringstream args{ argument(text) };
    std::string growid{};
    int minutes{ 10 };
    args >> growid;
    if (int n{}; args >> n && pPeer->role >= MODERATOR) minutes = std::clamp(n, 1, 60); // @note only staff choose the length

    ENetPeer *target = find_online(growid);
    if (!target || pPeer->netid == 0 || static_cast<::peer*>(target->data)->recent_worlds.back() != pPeer->recent_worlds.back()
        || static_cast<::peer*>(target->data)->netid == 0)
    {
        send_action(*event.peer, "log", std::format("msg|`4Player `w{}`` `4is not in this world.``", growid));
        return;
    }
    if (!world_punish(event, "worldban", target, minutes))
        send_action(*event.peer, "log", "msg|`4You can't world ban that player.`` (only the world owner or staff can, and not on the owner or higher staff)");
}
