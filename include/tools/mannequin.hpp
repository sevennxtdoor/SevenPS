#pragma once

/* @note Mannequin (1420) and the other TYPE_MANNEQUIN items.
 * use a clothing item on it to dress it (a Yes/No dialog first), punch it to take one piece back, and wrench it to
 * write its sign or take off chosen/all clothes. only works in a World Locked / area locked spot, and only the owner
 * and their access list touch it. kept in the `world_mannequin` table.
 * tile extra layout (type 0x0e) from GTopia (keichira/GTopia, MIT): text, u8 flags, u32 hair color, 9 x i16 clothes
 * (same order as ::clothing: hat, shirt, pants, shoes, face, hand, back, hair, chest) */

/* @note tile extra after the 0x0e type byte */
extern void mannequin_extra(::blob &blob, ::world &world, const ::pos &pos);

extern void mannequin_placed(::world &world, const ::pos &pos);
extern void mannequin_removed(::world &world, const ::pos &pos);

/* @note punching it: takes the first piece of clothing back. @return true if it did (then the mannequin takes no damage) */
extern bool mannequin_punched(ENetEvent &event, ::world &world, const ::pos &pos);

/* @note a clothing item was used on it: asks "Do you really want to put ...?" */
extern void mannequin_use_item(ENetEvent &event, ::world &world, const ::pos &pos, short item_id);

extern void mannequin_dialog(ENetEvent &event, ::world &world, const ::pos &pos); // @note wrench
extern void mannequin_return(ENetEvent &event, const ::hPipe &hPipe); // @note dialog_return "mt_mannequin"

extern void load_mannequins(::world &world);
extern void save_mannequins(::world &world);
