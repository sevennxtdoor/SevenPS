#include "pch.hpp"
#include "tools/playmods.hpp"
#include "tools/buffs.hpp"
#include "SetClothing.hpp"

/* @note OnSetClothing of `target` to `viewer`. it used to reach only the player themselves, so everyone else saw them
 * without skin color or clothes (unnoticed while testing alone).
 */
static void send_clothing(ENetPeer &viewer, ::peer &target)
{
    send_varlist(&viewer, {
        "OnSetClothing",
        CL_Vec3f{target.clothing[clothing::HAIR], target.clothing[clothing::SHIRT], target.clothing[clothing::LEGS]},
        CL_Vec3f{target.clothing[clothing::FEET], target.clothing[clothing::FACE], target.clothing[clothing::HAND]},
        CL_Vec3f{target.clothing[clothing::BACK], target.clothing[clothing::HEAD], target.clothing[clothing::CHARM]},
        (target.state & S_GHOST) ? -140 : buff_skin(target).value_or(target.skin_color), // @note Blueberry, Egg, Spray Tan...
        CL_Vec3f{target.clothing[clothing::ANCES], 0.0f, 0.0f}
    }, target.netid);
}

/* @note each connection's last movement packet in its current world (netid set), see ShowClothing */
static std::unordered_map<const ENetPeer*, ::gamePacket> last_moves{};

static ::gamePacket character_state(::peer &target)
{
    return ::gamePacket {
        /* @note PACKET_SET_CHARACTER_STATE (0x14): byte 1 punch effect, byte 2 build range, byte 3 punch range.
         * 128 = normal reach, 129 = +1 tile... (GTopia's SendCharacterState and GrowServer agree) */
        .type = static_cast<int>(0x14u | (static_cast<u_int>(target.punch_effect) << 8) | ((0x80u + playmod_range(target, false)) << 16) | ((0x80u + playmod_range(target, true)) << 24)),
        .netid = target.netid,
        .count = 125.0f, // @note gtnoob has this as 'waterspeed'
        .id = target.state, // @note 04 invisible (only eyes/mouth), 08 no arms, 16 no face, 32 invisible (only legs/arms), 64 devil horns, 128 angel halo, 2048 frozen, 4096 gray skin?,8192 ducttape, 16384 Onion effect, 32768 stars effect, 65536 zombie, 131072 hit by lava, 262144 shadow effect, 524288 irradiated effect, 1048576 spotlight, 2097152 pineapple thingy
        .pos = ::pos{ 1200.0f, 200.0f }, // @todo magic numbers
        .speed = player_physics(target), // @note {speed, gravity}: playmods, zombie, Antigravity Generator
        .punch = ::pos{ target.hair_color, 0x00000000u } // @todo can this even be unsigned?
    };
}

void on::SetClothing(ENetPeer &peer)
{
    ::peer *pPeer = static_cast<::peer*>(peer.data);

    if (pPeer->netid == 0) // @note not in a world: nobody else to show
    {
        send_clothing(peer, *pPeer);
        return;
    }
    peers(pPeer->recent_worlds.back(), PEER_SAME_WORLD, [&](ENetPeer &p) { send_clothing(p, *pPeer); });
    state_visuals(peer, character_state(*pPeer)); // @todo handle for 'p'
}

void on::ShowClothing(ENetPeer &viewer, ENetPeer &target)
{
    ::peer &t = *static_cast<::peer*>(target.data);
    send_clothing(viewer, t);
    send_data(viewer, compress_state(character_state(t)));
    /* @note and their last movement, sent again as it was: someone standing still stayed invisible to whoever entered until
     * they moved (OnSpawn alone didn't show them). a movement packet made up here swapped both players' clothes, so only the game's own */
    if (auto it = last_moves.find(&target); it != last_moves.end() && it->second.netid == t.netid)
        send_data(viewer, compress_state(it->second));
}

void on::RememberMove(ENetPeer &peer, const ::gamePacket &gamePacket)
{
    last_moves[&peer] = gamePacket;
}

void on::ForgetMove(ENetPeer &peer)
{
    last_moves.erase(&peer);
}
