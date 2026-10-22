#pragma once

/* @note Highly Combustible Box, like Growtopia (wiki: Highly Combustible Box, Guide:Highly Combustible Box): open it
 * (punch), drop items in and set it on fire with a Pocket Lighter. it bursts into a Combusted Box: a few
 * items are tempered by the fire (the recipes), everything else inside burns away.
 * the wiki lists the recipes but no amounts (only Fedora: 200), so every other recipe is 1 in = 1 out (guessed) */

/* @note burns what was dropped on the box at `tile` (the caller turns the box into a Combusted Box) */
extern void burn_box(ENetEvent &event, ::world &world, const ::pos &tile);
