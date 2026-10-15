#pragma once

/* @note new player tutorial: 6 steps from "lock your own world" to "buy something in the Store". progress comes from
 * the profile stats (add_stat), so there is nothing to fake: each finished step gives a few gems, the last one a bonus.
 * the current step is shown on login, after each step and with /tutorial */

extern void tutorial_check(ENetEvent &event);   // @note called by add_stat()
extern void tutorial_on_join(ENetEvent &event); // @note reminder of the current step
extern void tutorial_command(ENetEvent &event, const std::string_view text); // @note /tutorial
