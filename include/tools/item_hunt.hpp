#pragma once

/* @note Item Hunt event: every few hours the server hides Treasure Chests (596) on the ground in START. walking over
 * one gives gems instead of the chest; each player finds at most one per hunt, so everyone gets a chance.
 * /hunt shows the hints, staff start/stop it with /hunt start | /hunt stop.
 * the chests only live in memory: they are never saved with the world, and come back if START is reloaded */

extern void hunt_tick(); // @note main loop, about once a second

extern void hunt_command(ENetEvent &event, const std::string_view text); // @note /hunt, /hunt start, /hunt stop

/* @note item_activate_object, before the item is given. @return true if it was a hunt chest (handled here) */
extern bool hunt_pickup(ENetEvent &event, ::world &world, u_int uid);

/* @note world save: hunt chests are skipped */
extern bool is_hunt_drop(const std::string &world, u_int uid);
