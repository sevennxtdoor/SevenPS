#pragma once

/* @note sponsor items shop: /sponsor lists resources/sponsor.txt (renamed in items.dat by apply_sponsor_items()) with the
 * real item pictures, prices are checked on the server. one of each per player */

extern void sponsor_command(ENetEvent &event, const std::string_view text); // @note /sponsor
extern void sponsor_return(ENetEvent &event, const ::hPipe &hPipe);        // @note dialog mt_sponsor
