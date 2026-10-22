#pragma once

/* @note World Key, like Growtopia (wiki: World Key): sells a world safely through the trade window.
 * - the owner wrenches the World Lock > Get World Key (not on a My First World Lock, not while anyone else has access,
 *   other locks are in the world or items are lying around): "You got a World Key! You can now trade this World to other Players!"
 * - traded away by the owner, in their own world: the other player becomes the owner (the key is used up)
 * - leaving the world takes every World Key out of your backpack */

/* @note "Get World Key" in the World Lock's wrench dialog (dialog_return/lock_edit.cpp, owner already checked) */
extern void world_key_get(ENetEvent &event, ::world &world);

/* @note after a trade: `from` gave `count` World Keys to `to` (action/trade.cpp) */
extern void world_key_traded(ENetPeer &from, ENetPeer &to);

/* @note leaving a world (action/quit_to_exit.cpp) */
extern void world_key_left(ENetEvent &event);

/* @return the World Key's item id, 0 if items.dat has none */
extern short world_key_id();
