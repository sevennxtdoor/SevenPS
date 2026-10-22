#pragma once

namespace on
{
    /* @note `peer`'s clothes, skin and character state, to everyone in their world (only to them outside a world) */
    extern void SetClothing(ENetPeer &peer);
    /* @note `target`'s clothes, skin, character state and last movement, to `viewer` only (someone just entered the world) */
    extern void ShowClothing(ENetPeer &viewer, ENetPeer &target);
    /* @note movement.cpp: the packet as everyone in the world got it, for ShowClothing */
    extern void RememberMove(ENetPeer &peer, const ::gamePacket &gamePacket);
    extern void ForgetMove(ENetPeer &peer); // @note left the world
}
