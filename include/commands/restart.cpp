#include "pch.hpp"
#include <chrono>
#include <optional>
#include "onVariant/ConsoleMessage.hpp"

#include "restart.hpp"

namespace
{
    using steady = std::chrono::steady_clock;

    std::optional<steady::time_point> restart_at{};
    int last_notice{}; // @note seconds-left of the last announcement, so each one is sent once
    std::string restart_by{}; // @note display name of the developer who scheduled it
    constexpr int notices[]{ 600, 300, 240, 180, 120, 60, 30, 10, 5 };
}

static void announce(const std::string &message)
{
    peers("", PEER_ALL, [&message](ENetPeer &p)
    {
        on::ConsoleMessage(&p, message);
        send_varlist(&p, { "OnAddNotification", "interface/science_button.rttex", message, "audio/hub_open.wav", 0 });
    });
}

static void announce_time_left(int seconds)
{
    const std::string time_left = (seconds >= 60) ?
        std::format("`4{}`` minute{}", seconds / 60, (seconds / 60 == 1) ? "" : "s") :
        std::format("`4{}`` seconds", seconds);
    announce(std::format("`4Global System Message``: Restarting server for update in {} `o(Restart By {}`o)``", time_left, restart_by));
}

void restart(ENetEvent& event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (pPeer->role < OWNER)
    {
        send_action(*event.peer, "log", "msg|`4Unknown command.`` Enter `$/?`` for a list of valid commands.");
        return;
    }

    const std::string_view arg = (text.size() > sizeof("restart")-1) ? text.substr(sizeof("restart ")-1) : "";
    if (arg == "cancel")
    {
        if (!restart_at) return send_action(*event.peer, "log", "msg|No restart is scheduled.");
        restart_at.reset();
        announce(std::format("`4Global System Message``: Server restart has been `2cancelled`` `o(by {}`o)``", pPeer->display_growid));
        return;
    }

    int minutes = 1;
    if (!arg.empty())
    {
        try { minutes = std::stoi(std::string{ arg }); }
        catch (const std::logic_error &) { return send_action(*event.peer, "log", "msg|`4Usage:`` /restart {minutes} or /restart cancel"); }
    }
    minutes = std::clamp(minutes, 0, 60);

    const int seconds = std::max(minutes * 60, 10); // @note at least 10 seconds warning
    restart_by = pPeer->display_growid;
    restart_at = steady::now() + std::chrono::seconds(seconds);
    last_notice = seconds;
    announce_time_left(seconds);
}

bool restart_tick()
{
    if (!restart_at) return false;

    const auto left = std::chrono::duration_cast<std::chrono::seconds>(*restart_at - steady::now()).count();
    if (left <= 0)
    {
        announce(std::format("`4Global System Message``: Restarting server now! See you in a moment. `o(Restart By {}`o)``", restart_by));
        restart_at.reset();
        return true;
    }
    for (int notice : notices)
        if (left <= notice && last_notice > notice)
        {
            last_notice = notice;
            announce_time_left(notice);
            break;
        }
    return false;
}
