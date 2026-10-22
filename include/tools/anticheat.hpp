#pragma once

/* @note anti-cheat for positions and punching. the client tells us where it is and when it punches, so a hacked client
 * teleported (taking drops from across the world, building past the reach check) and broke blocks instantly.
 * - moving: every player has a distance budget that fills at their walking speed (playmods/buffs included). a move
 *   bigger than the budget is refused and the player is put back where we last saw them ("rubberband", never a kick,
 *   so a laggy player only snaps back a bit). falling down may be fast, going up/sideways not
 * - punching: a few punches may come at once (lag), after that about one every PUNCH_GAP_MS; extra punches do nothing
 * staff (MODERATOR+) is never checked. logged as "[guard] ..." like tools/guard */

extern bool anticheat_on; // @note the offline tests turn it off (they move players around freely), [AC1] tests it

/* @note the server itself moved this player (spawn, respawn, a door, pull/kick): the next move starts from here */
extern void moved_by_server(::peer &p, const ::pos &to);

/* @return false = refuse this position (moved faster than possible); the player was already put back */
extern bool move_allowed(ENetPeer &peer, const ::pos &to);

/* @return false = punching faster than the game can, ignore this punch */
extern bool punch_allowed(::peer &p);
