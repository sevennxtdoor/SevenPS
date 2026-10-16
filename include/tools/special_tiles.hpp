#pragma once

/* @note Phase 3 part 2 of "every item works": tiles that keep their own data
 * - Harmonic Crystals (resources/crystals.txt, Chi of every item in resources/chi.txt): stack 5 crystals, surround them
 *   with the right Earth/Fire/Air/Water, punch once for the prize
 * - providers (resources/providers.txt): Cow, Chicken, ATM... grow for the item's own time, punch to harvest
 * - Bunny Egg (resources/bunny_egg.txt): Magic Eggs on an empty tile grow one egg, break it for a prize by size
 * - the tile data the client expects for these and a few other item types (mailbox, bulletin, achievement block...) */

/* @note an item used on a tile. @return true if handled (it takes the item itself), throws std::runtime_error for a talk bubble */
extern bool special_place(ENetEvent &event, ::world &world, ::block &block, const ::pos &tile, const ::item &item, bool from_remote);

/* @note a just placed block (the normal way): crystals start a cluster, providers start growing. @return true if the client needs a tile update */
extern bool special_placed(::world &world, const ::pos &tile, const ::item &item);

/* @note punched, before it takes damage. @return true if handled (no damage) */
extern bool special_tile_punch(ENetEvent &event, ::world &world, ::block &block, const ::pos &tile, const ::item &item);

/* @note the block just broke (block.fg is already cleared). @return true if it gave its own drops */
extern bool special_tile_break(ENetEvent &event, ::world &world, const ::pos &tile, const ::item &item);

/* @note wrenched. @return true if it showed its own dialog */
extern bool special_tile_wrench(ENetEvent &event, ::world &world, const ::pos &tile, const ::item &item);

/* @note get_type() of the item types handled here (0 = none) */
extern u_char special_extra_type(const ::item &item);

/* @note the tile data after the extra type byte, for the world and tile update packets */
extern void special_extra(::blob &blob, ::world &world, const ::pos &tile, u_char type);

/* @note crystal clusters and bunny eggs, kept in the `world_special_tile` table */
extern void load_special_tiles(::world &world);
extern void save_special_tiles(::world &world);
