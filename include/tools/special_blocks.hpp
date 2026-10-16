#pragma once

/* @note Phase 3 of "every item works": blocks that do something special when punched or broken.
 * - loot blocks (resources/loot.txt): Mystery Block, Pot O' Gold, treasure hoards, Locke's box, Summer Surprise, pinatas...
 * - fossils (resources/fossils.txt): fossil rocks, Rock Hammer & Rock Chisel, Fossil Brush, Fossil Prep Station
 * - Mystery Pinata: items lying under it change into festive ones when it breaks (resources/fossils.txt "pinata")
 * prize lists from the Growtopia wiki, chances are MikeTopia's own (the wiki has none) */

/* @note punched, before it takes damage. @return true if handled (no damage), throws std::runtime_error for a talk bubble */
extern bool special_punch(ENetEvent &event, ::world &world, ::block &block, const ::pos &tile, const ::item &item);

/* @note the block just broke (block.fg is already cleared). @return true if it gave its own drops instead of the normal
 * gems/seed/block. sets `tile_update` when it put another block back (a smashed fossil leaves the plain rock) */
extern bool special_break(ENetEvent &event, ::world &world, ::block &block, const ::pos &tile, const ::item &item, bool &tile_update);

/* @note Fossil Brush, Fossil & Polished Fossil used on a tile. @return true if it was one of them (it takes the item itself) */
extern bool special_use(ENetEvent &event, ::world &world, ::block &block, const ::pos &tile, const ::item &item);

/* @note a world just loaded: it may turn one rock into a fossil rock */
extern void fossil_spawn(::world &world);

/* @note fossils soaking in Fossil Prep Stations, kept in the `world_fossil_prep` table */
extern void load_fossil_preps(::world &world);
extern void save_fossil_preps(::world &world);
