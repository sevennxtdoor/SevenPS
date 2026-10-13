#pragma once

/* @note world jammers: punch one to turn it on or off (the tile's S_TOGGLE flag, saved with the world).
 * Signal Jammer hides the world from Top Worlds, /sb and friend lists; the others are shown on entering
 * and their effects (punching players, zombie bites, balloons, gravity) are checked with jammer_on() */

namespace jammer
{
    constexpr short SIGNAL = 226, PUNCH = 1276, ZOMBIE = 1278, BALLOON = 4884, ANTIGRAVITY = 4992;
}

extern bool is_jammer(short id);
extern bool jammer_on(const ::world &world, short id);
extern bool world_jammed(const std::string &world_name); // @note Signal Jammer on in that (loaded) world

/* @note "`4JAMMED!``" for players when `world_name` has a Signal Jammer on, else the name. staff always see the name */
extern std::string world_display(const std::string &world_name, const ::peer &viewer);

/* @note " `0[`4JAMMED``, `2NOPUNCH```0]" for the "World X entered" message, empty if nothing is on */
extern std::string jammer_tags(const ::world &world);

/* @note a jammer was punched and S_TOGGLE flipped: tell everyone in the world and update the tile */
extern void jammer_toggled(ENetEvent &event, ::world &world, ::block &block, const ::pos &tile);
