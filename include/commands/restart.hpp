#pragma once

/* /restart {minutes} | /restart cancel - owner only */
extern void restart(ENetEvent& event, const std::string_view text);

/* @note call about once a second from the main loop. returns true when it's time to shut down */
extern bool restart_tick();
