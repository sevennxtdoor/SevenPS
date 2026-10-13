#pragma once

/* @note consumable items used on a tile or on yourself (from GTOS' list, rewritten):
 * grow sprays, fortune cookies, gifts & chests, seed packs, surgery gatcha, fragments that combine into an item,
 * Green Beer, g-Virus & Antidote. @return true if the item was handled (it takes the item itself) */
extern bool use_consumable(ENetEvent &event, ::world &world, ::block &block, const ::pos &tile, const ::item &item);

/* @note wrench on a GrowScan 9000: world statistics */
extern void growscan_dialog(ENetEvent &event, ::world &world);
