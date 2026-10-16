#include "pch.hpp"
#include "tools/playmods.hpp"
#include "SetClothing.hpp"

void on::SetClothing(ENetPeer &peer)
{
    ::peer *pPeer = static_cast<::peer*>(peer.data);

    send_varlist(&peer, {
        "OnSetClothing", 
        CL_Vec3f{pPeer->clothing[clothing::HAIR], pPeer->clothing[clothing::SHIRT], pPeer->clothing[clothing::LEGS]}, 
        CL_Vec3f{pPeer->clothing[clothing::FEET], pPeer->clothing[clothing::FACE], pPeer->clothing[clothing::HAND]}, 
        CL_Vec3f{pPeer->clothing[clothing::BACK], pPeer->clothing[clothing::HEAD], pPeer->clothing[clothing::CHARM]}, 
        (pPeer->state & S_GHOST) ? -140 : pPeer->skin_color,
        CL_Vec3f{pPeer->clothing[clothing::ANCES], 0.0f, 0.0f}
    }, pPeer->netid);

    ::gamePacket gamePacket {
        /* @note PACKET_SET_CHARACTER_STATE (0x14): byte 1 punch effect, byte 2 build range, byte 3 punch range.
         * 128 = normal reach, 129 = +1 tile... (GTopia's SendCharacterState and GrowServer agree) */
        .type = static_cast<int>(0x14u | (static_cast<u_int>(pPeer->punch_effect) << 8) | ((0x80u + playmod_range(*pPeer, false)) << 16) | ((0x80u + playmod_range(*pPeer, true)) << 24)),
        .netid = pPeer->netid,
        .count = 125.0f, // @note gtnoob has this as 'waterspeed'
        .id = pPeer->state, // @note 04 invisible (only eyes/mouth), 08 no arms, 16 no face, 32 invisible (only legs/arms), 64 devil horns, 128 angel halo, 2048 frozen, 4096 gray skin?,8192 ducttape, 16384 Onion effect, 32768 stars effect, 65536 zombie, 131072 hit by lava, 262144 shadow effect, 524288 irradiated effect, 1048576 spotlight, 2097152 pineapple thingy
        .pos = ::pos{ 1200.0f, 200.0f }, // @todo magic numbers
        .speed = player_physics(*pPeer), // @note {speed, gravity}: playmods, zombie, Antigravity Generator
        .punch = ::pos{ pPeer->hair_color, 0x00000000u } // @todo can this even be unsigned?
        
    };
    state_visuals(peer, std::move(gamePacket)); // @todo handle for 'p'
}