#pragma once

/* @note boxes that hold things for a world, all used with the wrench:
 *   Storage Box Xtreme (20/40/90 items), Untrade-a-Box (untradeable items only), Safe Vault (password),
 *   Donation Box (anyone gives items + a note), Mailbox (anyone leaves a private message), Bulletin Board (public messages).
 * the owner is the World Lock / area lock owner, so a box only works in a locked area.
 * kept in the `world_box` table, the world blob stays the same */

enum class box_kind : u_char { NONE, STORAGE, UNTRADE, VAULT, DONATION, MAIL, BULLETIN };

extern box_kind box_type(const ::item &item);

/* @note true while it still holds items (it can't be broken then) */
extern bool box_in_use(::world &world, const ::pos &pos);
extern void box_removed(::world &world, const ::pos &pos);

extern void box_dialog(ENetEvent &event, ::world &world, const ::pos &pos); // @note wrench
extern void box_return(ENetEvent &event, const ::hPipe &hPipe); // @note dialog_return "mt_box"

extern void load_boxes(::world &world);
extern void save_boxes(::world &world);
