#pragma once

/* @note SevenPS's own worlds: START (the hub) and PARKOUR are built with a ready-made design the first time
 * they are opened, only staff can edit them, and /rebuild puts the design back. */

extern bool is_system_world(std::string_view name);

/* @note called for a brand new world. @return true if `world` is a system world and got its design */
extern bool build_system_world(::world &world);

/* @note a checkpoint was touched: the PARKOUR finish gives a daily reward */
extern void system_checkpoint(ENetEvent &event, ::world &world, const ::pos &tile);

/* @note an already built START keeps its blocks, but the signs that teach the basics get today's text */
extern void refresh_system_signs(::world &world);

extern void rebuild_command(ENetEvent &event, const std::string_view text); // @note /rebuild {START|PARKOUR}, owner only

/* @note door destinations are not part of the blocks blob, they're kept in the `world_door` table */
extern void load_doors(::world &world);
extern void save_doors(::world &world);
