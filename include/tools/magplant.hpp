#pragma once

/* @note Magplant 5000 (5638), Gaia's Beacon (6946, seeds only) and Unstable Tesseract (6948, no seeds).
 * the machine collects one chosen item when it drops from blocks/trees/providers in its world, the owner adds or
 * takes items with the wrench, and the Magplant 5000 Remote (5640) builds/plants straight from the machine.
 * kept in the `world_magplant` table. tile extra layout (type 0x3e) from GTopia (keichira/GTopia, MIT) */

constexpr u_short MAGPLANT_REMOTE_ID = 5640; // @note Magplant 5000 Remote

/* @note tile extra after the 0x3e type byte: item, count, collecting, building mode, 0 */
extern void magplant_extra(::blob &blob, ::world &world, const ::pos &pos);

extern void magplant_placed(::world &world, const ::pos &pos);
extern bool magplant_in_use(::world &world, const ::pos &pos); // @note still holds items (it can't be broken then)
extern void magplant_removed(ENetEvent &event, ::world &world, const ::pos &pos);

/* @note punching it (first hit) turns building mode on/off, only one machine per world builds at a time */
extern void magplant_punched(ENetEvent &event, ::world &world, const ::pos &pos);

/* @note use instead of add_drop() for things that drop from the world: a collecting machine takes it if there's room */
extern void magplant_drop(ENetEvent &event, ::slot slot, ::pos pos, ::world &world);

/* @note the player placed with the remote: @return the item to place. throws std::runtime_error (talk bubble) when it can't */
extern short magplant_remote_item(ENetEvent &event, ::world &world);
extern void magplant_remote_used(ENetEvent &event, ::world &world); // @note one item left the machine

/* @note entering a world: the remote only works where you got it, and show which machine is building */
extern void magplant_on_join(ENetEvent &event, ::world &world);

extern void magplant_dialog(ENetEvent &event, ::world &world, const ::pos &pos); // @note wrench
extern void magplant_return(ENetEvent &event, const ::hPipe &hPipe); // @note dialog_return "mt_magplant"

extern void load_magplants(::world &world);
extern void save_magplants(::world &world);
