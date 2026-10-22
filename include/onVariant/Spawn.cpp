#include "pch.hpp"

#include "Spawn.hpp"

/* @note posXY in pixels, as every caller passes it. it was divided by 32 (tiles): someone already in the world stood near the
 * world's top left corner for whoever entered, out of sight until they moved (tested with 2 PCs) */
void on::Spawn(ENetPeer &peer, signed netID, signed userID, ::pos posXY, std::string name, std::string country, bool mstate, bool smstate, bool local) 
{
    send_varlist(&peer, { 
        "OnSpawn", 
        std::format(
            "spawn|avatar\nnetID|{}\nuserID|{}\ncolrect|0|0|20|30\nposXY|{}|{}\nname|{}``\ncountry|{}\ninvis|0\nmstate|{}\nsmstate|{}\nonlineID|\n{}",
            netID, userID, posXY.x_int(), posXY.y_int(), name, country, to_char(mstate), to_char(smstate), (local) ? "type|local\n" : ""
        )
    }, -1, 0xffffffff);
}