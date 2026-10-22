#pragma once

/* @note Heartstone / Golden Booty Chest / Super GBC loot, the list from the Growtopia wiki (Golden Booty Chest: 69 items,
 * the Heartstone uses the same table). the wiki has no chances, so tile_change.cpp rolls the Golden Heart Crystal
 * itself (1%, guessed) and everything else comes from here, evenly */

/* @return a random item from the wiki list (never the Golden Heart Crystal), {id, amount} */
extern ::slot random_love_item();

/* @return how many of the wiki's names were found in items.dat (tests) */
extern std::size_t love_loot_size();
