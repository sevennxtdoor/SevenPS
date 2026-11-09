#pragma once

/* @note g-Virus (764): an infected player ("Infected!" mod) who punches another player infects them too, unless a
 * Zombie Jammer is on in the world. called for every punch (item 18), also on an empty tile */
extern void zombie_punch(ENetEvent &event, ::world &world, const ::pos &tile);
