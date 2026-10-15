#pragma once

/* @note doors & the Password Door (762).
 * a door's Destination can be "WORLD", "WORLD:ID" or ":ID" (the door with that ID in the current world), like real Growtopia.
 * a Password Door asks for its password before it lets you through. the password lives only on the server
 * (::door::password, `world_door` table) and is never part of the world blob, so a modded client can't read it. */

constexpr u_short PASSWORD_DOOR = 762;

/* @note letters and numbers only, upper case, max 23 (the same rules the owner's dialog uses) */
extern std::string clean_door_password(const std::string &text);

/* @note the player went into a door/portal (tile_activate). asks for the password first when it's a Password Door */
extern void door_activate(ENetEvent &event, ::world &world, const ::pos &tile);

extern void password_door_return(ENetEvent &event, const ::hPipe &hPipe); // @note dialog_return "mt_passdoor"
