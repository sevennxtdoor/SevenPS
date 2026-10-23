#pragma once

/* @note Telephone (3898): wrench it and dial a number, like real Growtopia (wiki: Telephone, Sales-Man, Growtoken).
 * 12345 = Crazy Jim's Daily Quest: bring 2 items (resources/crazy_jim.txt), get 1 Growtoken, once a day
 * 53785 = Sales-Man: 1 Growtoken for 100 Caducei / 20 Wolf Tickets / 300 Zombie Brains / 100 Buckazoids */

constexpr u_short TELEPHONE = 3898;

extern void telephone_dialog(ENetEvent &event, ::world &world, const ::pos &tile);
extern void telephone_return(ENetEvent &event, const ::hPipe &hPipe); // @note dialog_return "mt_phone"

/* @note today's 2 items: { {id, count}, {id, count} }, the same for everyone on the same day */
extern std::array<std::pair<short, short>, 2> crazy_jim_quest(int day);
