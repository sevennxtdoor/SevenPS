#pragma once

/* @note Display Block and the other TYPE_DISPLAY_BLOCK items (growtopiawiki.com/w/Display_Block).
 * the owner uses an item on it to put it on display, wrenches it to take the item back, and breaking it ejects the
 * item. only the block's owner (the lock's owner) touches it. untradeable items, Screen Doors, locks and Display
 * Blocks can't be displayed. kept in the `world_display` table.
 * @todo the wiki's 5 second warm up after placing it */

/* @note tile extra after the 0x17 type byte: the displayed item id (0 = empty) */
extern void display_extra(::blob &blob, ::world &world, const ::pos &pos);

/* @note something on it: only its owner may break it. @return why not, empty when allowed */
extern std::string display_refuse_break(const ::peer &p, ::world &world, const ::pos &pos);
extern void display_removed(ENetEvent &event, ::world &world, const ::pos &pos); // @note broken: the item drops out

/* @note an item was used on it: puts it on display */
extern void display_use_item(ENetEvent &event, ::world &world, const ::pos &pos, short item_id);

extern void display_wrench(ENetEvent &event, ::world &world, const ::pos &pos); // @note takes the item back

extern void load_displays(::world &world);
extern void save_displays(::world &world);
