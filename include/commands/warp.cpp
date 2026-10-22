#include "pch.hpp"
#include <chrono>
#include <unordered_map>
#include "action/join_request.hpp"
#include "action/quit_to_exit.hpp"
#include "onVariant/ConsoleMessage.hpp"

#include "warp.hpp"

void warp(ENetEvent& event, const std::string_view text)
{
    std::string world_name{ text.substr(strlen("warp ")) };
    for (char &c : world_name) c = std::toupper(c); // @note start -> START

    send_action(*event.peer, "log", std::format("msg| `6/warp {}``", world_name));
    send_varlist(event.peer, { "OnSetFreezeState", 1 });
    send_action(*event.peer, "log", std::format("msg|Magically warping to world `5{}``...", world_name));

    action::quit_to_exit(event, "", true);
    action::join_request(event, "", world_name);
}

void warp_command(ENetEvent& event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (pPeer->role < MODERATOR) // @note Super Supporter = staff in SevenPS (peer::update_effects)
        return on::ConsoleMessage(event.peer, "`4/warp is a `5Super Supporter`` perk.``");

    using clock = std::chrono::steady_clock;
    static std::unordered_map<int, clock::time_point> last_warp{}; // @note user id -> last /warp. in memory only
    const auto now = clock::now();
    if (auto it = last_warp.find(pPeer->user_id); it != last_warp.end() && now - it->second < std::chrono::seconds(15))
        return on::ConsoleMessage(event.peer, std::format("`4Wait {} more seconds`` before warping again.",
            15 - std::chrono::duration_cast<std::chrono::seconds>(now - it->second).count()));
    last_warp[pPeer->user_id] = now;
    warp(event, text);
}

void got_private_message(::peer &to, const ::peer &from)
{
    to.last_pm_world = (from.netid != 0) ? from.recent_worlds.back() : "";
}

void rgo(ENetEvent& event, const std::string_view)
{
    const std::string world_name = static_cast<::peer*>(event.peer->data)->last_pm_world;
    if (world_name.empty()) return on::ConsoleMessage(event.peer, "`oNo private message to follow`` (or the sender wasn't in a world).");
    warp(event, "warp " + world_name);
}
