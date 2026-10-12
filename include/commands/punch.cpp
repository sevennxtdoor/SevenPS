#include "pch.hpp"
#include "onVariant/SetClothing.hpp"
#include "onVariant/ConsoleMessage.hpp"

#include "punch.hpp"

u_char get_punch_id(u_int item_id)
{
    switch (item_id)
    {
        case 138: case 2976: return 1; // @note https://growtopia.fandom.com/wiki/Mods/Eye_Beam
        case 366: return 2;
        case 472: return 3;
        case 594: return 4;
        case 768: return 5;
        case 900: return 6;

        case 930: return 8; // @todo or 12

        case 1204: return 10;
        case 1738: return 11;
        case 1484: return 12;

        case 3066: case 5206: case 7504: case 10288: return 17; // @note https://growtopia.fandom.com/wiki/Mods/Fire_Hose
        case 2636: case 2908: case 3070: case 3108: case 3466: case 2952: return 29; // @note https://growtopia.fandom.com/wiki/Mods/Slasher

        /* @note ported from GTOS (github.com/zKaan/GTOSLatest, equip_item: effect - 8421376). items above win if they differ */
        case 1512: return 14;
        case 1542: return 15;
        case 1576: return 16;
        case 1676: return 17;
        case 1780: return 20;
        case 1782: return 21;
        case 1804: case 7912: return 22;
        case 1868: return 23;
        case 1874: return 24;
        case 1946: return 25;
        case 3932: case 9430: case 9448: case 9452: case 9454: case 9456: case 9458: case 9488: case 9492: case 9494: case 9496: case 9508: case 9510: case 9512: case 9514: case 9520: return 30;
        case 2212: return 32;
        case 2220: return 34;
        case 2266: return 35;
        case 8452: return 36;
        case 2388: return 37;
        case 2450: return 38;
        case 2476: return 39;
        case 2572: return 42;
        case 2592: return 43;
        case 2720: return 44;
        case 2754: return 46;
        case 2756: return 47;
        case 2866: return 50;
        case 2876: return 51;
        case 2906: return 53;
        case 2890: return 55;
        case 2910: return 56;
        case 3124: return 58;
        case 3168: return 59;
        case 3214: return 60;
        case 3300: return 64;
        case 3418: return 65;
        case 3686: return 68;
        case 4474: return 72;
        case 4778: case 6026: case 9164: return 76;
        case 4996: return 77;
        case 5480: return 80;
        case 6308: return 82;
        case 7044: return 86;
        case 7136: return 92;
        case 7196: return 95;
        case 7488: return 103;
        case 7586: return 104;
        case 8910: return 129;
        case 6782: return 133;
        case 9006: return 135;
        case 9058: case 9484: case 9486: case 9502: return 136;
        case 9236: return 145;
        case 9376: return 149;
        case 6312: return 153;
        case 10128: return 171;
        case 10426: return 183;
        case 10618: return 187;
        case 10652: return 188;

        default: return 0;
    }
}

void punch(ENetEvent& event, const std::string_view text) 
{
    const std::string id{ text.substr(sizeof("punch ")-1) };
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    pPeer->punch_effect = (u_char)atoi(id.c_str());
    on::SetClothing(*event.peer);
}