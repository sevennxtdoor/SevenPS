#pragma once

/* @note Chemical Combiners (item type "chemical combiner": Laboratory, E-Z Cook Oven, Microwave, Barbecue Grill, Vehicle
 * Replicator). like in Growtopia: punch it to open/close it, drop the three ingredients on the open combiner, punch it
 * and the result drops. recipes in resources/combiner.txt (Growtopia wiki "Guide:Combining") */

/* @note punched. @return true if it combined (no damage, no open/close) */
extern bool combiner_punch(ENetEvent &event, ::world &world, ::block &block, const ::pos &tile, const ::item &item);
