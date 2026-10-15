#pragma once

/* @note casino: the Roulette Wheel (758) like real Growtopia, plus protection against fake spin messages.
 * the spin message comes 2 s later (while the wheel turns), like GTOS. red/black follow the real roulette table,
 * "reme" (digit sum, last digit) is shown for the reme game players run in GTPS casinos */

extern void roulette_spin(ENetEvent &event); // @note punching a Roulette Wheel

/* @note chat that pretends to be a wheel result ("[Mike spun the wheel and got 0!]") is how GTPS casino scams work.
 * @return true if the text looks like one (color codes and case ignored) */
extern bool fake_spin(std::string_view text);
