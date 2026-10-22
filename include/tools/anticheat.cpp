#include "pch.hpp"
#include "tools/playmods.hpp"

#include "anticheat.hpp"

bool anticheat_on = true;

namespace
{
    using clock = std::chrono::steady_clock;

    /* @note tuning. sideways and up: 1.5x the walking speed. down: falling gets fast. SLACK = what may come at once
     * after a lag spike, on top of up to one second of saved-up movement */
    constexpr float SIDE_FACTOR = 1.5f, UP_SPEED = 1200.0f, DOWN_SPEED = 2500.0f;
    constexpr float SIDE_SLACK = 3 * 32.0f, UP_SLACK = 5 * 32.0f, DOWN_SLACK = 8 * 32.0f;
    constexpr float MAX_SAVED_SECONDS = 1.0f;
    constexpr float PUNCH_GAP_MS = 200.0f, PUNCH_BURST = 3.0f; // @note Growtopia's Fist: 300 punches a minute (wiki), a few may come at once after lag

    float seconds_since(clock::time_point &last, clock::time_point now)
    {
        const float s = std::chrono::duration<float>(now - last).count();
        last = now;
        return std::clamp(s, 0.0f, MAX_SAVED_SECONDS);
    }

    void refill(::peer &p, float side_rate, float s)
    {
        p.move_side = std::min(p.move_side + side_rate * s, side_rate * MAX_SAVED_SECONDS + SIDE_SLACK);
        p.move_up   = std::min(p.move_up   + UP_SPEED * s,  UP_SPEED * MAX_SAVED_SECONDS + UP_SLACK);
        p.move_down = std::min(p.move_down + DOWN_SPEED * s, DOWN_SPEED * MAX_SAVED_SECONDS + DOWN_SLACK);
    }
}

void moved_by_server(::peer &p, const ::pos &to)
{
    p.pos = to;
    p.move_at = clock::time_point{}; // @note "long ago": the next move gets a full budget
}

bool move_allowed(ENetPeer &peer, const ::pos &to)
{
    ::peer &p = *static_cast<::peer*>(peer.data);
    if (!anticheat_on || p.role >= MODERATOR) return true;

    const float side_rate = std::max(player_physics(p).x, NORMAL_SPEED) * SIDE_FACTOR;
    refill(p, side_rate, seconds_since(p.move_at, clock::now()));

    const float dx = std::abs(to.x - p.pos.x), dy = to.y - p.pos.y; // @note y grows downwards
    const bool ok = dx <= p.move_side && ((dy >= 0) ? dy <= p.move_down : -dy <= p.move_up);
    if (!ok)
    {
        if (++p.move_strikes == 1 || p.move_strikes % 50 == 0)
            std::printf("[guard] %s moved too fast (%.0f, %.0f px), put back (%u times)\n", p.growid.c_str(), dx, dy, p.move_strikes);
        send_varlist(&peer, { "OnSetPos", CL_Vec2f{ p.pos.x, p.pos.y } }, p.netid);
        return false;
    }
    p.move_side -= dx;
    if (dy >= 0) p.move_down -= dy; else p.move_up += dy;
    return true;
}

bool punch_allowed(::peer &p)
{
    if (!anticheat_on || p.role >= MODERATOR) return true;

    const float ms = std::chrono::duration<float, std::milli>(clock::now() - p.punch_at).count();
    p.punch_at = clock::now();
    p.punch_budget = std::min(p.punch_budget + ms / PUNCH_GAP_MS, PUNCH_BURST);
    if (p.punch_budget < 1.0f) return false;
    p.punch_budget -= 1.0f;
    return true;
}
