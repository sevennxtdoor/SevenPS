#include "pch.hpp"
#include <chrono>
#include <unordered_map>
#include "action/quit_to_exit.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "tools/anticheat.hpp"

#include "worldban.hpp"

namespace
{
    using steady = std::chrono::steady_clock;

    /* world name -> { user_id -> ban expiry } */
    std::unordered_map<std::string, std::unordered_map<int, steady::time_point>> bans{};
}

void world_ban(const std::string &world_name, int user_id, int minutes)
{
    bans[world_name][user_id] = steady::now() + std::chrono::minutes(minutes);
}

int world_ban_left(const std::string &world_name, int user_id)
{
    auto world = bans.find(world_name);
    if (world == bans.end()) return 0;

    auto ban = world->second.find(user_id);
    if (ban == world->second.end()) return 0;

    const auto left = std::chrono::duration_cast<std::chrono::seconds>(ban->second - steady::now()).count();
    if (left <= 0)
    {
        world->second.erase(ban); // @note expired
        return 0;
    }
    return static_cast<int>((left + 59) / 60);
}

void world_unban_all(const std::string &world_name)
{
    bans.erase(world_name);
}

bool can_world_punish(const ::peer &actor, const ::peer &target, const ::world &world)
{
    if (actor.user_id == target.user_id || target.user_id == world.owner) return false;

    const bool is_owner = (world.owner != 0 && actor.user_id == world.owner);
    if (!is_owner && actor.role < MODERATOR) return false;

    return (target.role < MODERATOR || actor.role > target.role);
}

bool world_punish(ENetEvent& event, const std::string &act, ENetPeer *target, int minutes)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (pPeer->netid == 0) return false;

    auto world = std::ranges::find(worlds, pPeer->recent_worlds.back(), &::world::name);
    if (world == worlds.end()) return false;

    ::peer *pTarget = static_cast<::peer*>(target->data);
    if (pTarget->netid == 0 || pTarget->recent_worlds.back() != world->name) return false;
    if (!can_world_punish(*pPeer, *pTarget, *world)) return false; // @note check again, dialog replies can be faked

    const std::string world_name = world->name; // @note copy, quit_to_exit() may erase the world
    std::string message{};
    if (act == "pull")
    {
        moved_by_server(*pTarget, pPeer->pos);
        send_varlist(target, { "OnSetPos", CL_Vec2f{pPeer->pos.x, pPeer->pos.y} }, pTarget->netid);
        message = std::format("{} `5pulls`` {}``!", pPeer->display_growid, pTarget->display_growid);
    }
    else if (act == "kick")
    {
        moved_by_server(*pTarget, world->spawn);
        send_varlist(target, { "OnSetPos", CL_Vec2f{world->spawn.x, world->spawn.y} }, pTarget->netid);
        message = std::format("{} `4kicks`` {}``!", pPeer->display_growid, pTarget->display_growid);
    }
    else if (act == "worldban")
    {
        world_ban(world_name, pTarget->user_id, minutes);
        message = std::format("{} `4world bans`` {} from `w{}``!", pPeer->display_growid, pTarget->display_growid, world_name);
    }
    else return false;

    peers(world_name, PEER_SAME_WORLD, [&message](ENetPeer &p) { on::ConsoleMessage(&p, message); });
    send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, message, 0u });

    if (act == "worldban")
    {
        on::ConsoleMessage(target, std::format("`4Oh no! ``You've been banned from `w{}`` for `w{}`` minute{}.", world_name, minutes, (minutes == 1) ? "" : "s"));
        ENetEvent other{ .peer = target };
        action::quit_to_exit(other, "", false);
    }
    return true;
}
