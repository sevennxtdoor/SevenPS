#pragma once

/* @note fishing, like real Growtopia: wear a Fishing Rod, use bait (Wiggly Worm, Fishing Fly, Shrimp Lure) on a water
 * tile next to you, wait for the splash, then punch quickly to reel the fish in. punching too early, moving away or
 * leaving the world loses the bait. the fish goes in your backpack with its weight as the amount (dropped next to you
 * when you already have one of that kind), plus a few gems.
 * GONE_FISHIN is game packet 31 (0x1f) with netid + the water tile (GTOS, GTopia and GrowServer agree). bite splash =
 * OnParticleEffect 36 (from GTOS). nothing here is saved except the profile stats fish_caught / fish_best */

/* @note call early in tile_change, before world lock checks (fishing doesn't change the world, so visitors may fish).
 * @return true when it was a fishing action and tile_change must stop */
extern bool fishing_tile_change(ENetEvent &event, ::world &world, const ::gamePacket &gamePacket);

extern void fishing_moved(ENetEvent &event); // @note movement: walking away pulls the line in
extern void fishing_reset(::peer &p);        // @note entering/leaving a world: no line, no message

extern void fishing_tick(); // @note main loop: bites and fish that got away

extern bool is_bait(short id);
