#pragma once

/* @note vending machines (ported from GTOS' SendVendDialog / SendBuyerVendDialog, rewritten).
 * stored in their own `vending` table so the world `blocks` format stays the same */

struct vending
{
    ::pos pos{};      // @note tile position
    int item{};       // @note item inside, 0 = empty
    int stock{};
    int price{};      // @note world locks per item, or items per world lock when per_lock
    bool per_lock{};  // @note "Items per World Lock"
    int earned{};     // @note world locks waiting to be withdrawn by the owner
};

extern ::vending *find_vending(::world &world, const ::pos &pos);

extern void load_vendings(::world &world);
extern void save_vendings(::world &world);

/* @note the 8 bytes after the 0x18 tile extra type: item id, price (negative = items per world lock) */
extern void vending_extra(::blob &blob, ::world &world, const ::pos &pos);

/* @note wrench on a vending machine: owner gets the edit dialog, everyone else the buy dialog */
extern void send_vending_dialog(ENetEvent &event, ::world &world, const ::pos &pos);
extern void vending_return(ENetEvent &event, const ::hPipe &hPipe); // @note dialog_return "vending"

/* @note "Empty the machine first!" when it still has stock or earnings */
extern bool vending_in_use(::world &world, const ::pos &pos);
