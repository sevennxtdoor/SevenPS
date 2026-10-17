#pragma once

extern void tile_change(ENetEvent& event, ::gamePacket gamePacket);

/* @note how far (in tiles, both directions) a player may punch/build from where they stand. staff and wrenches skip it */
extern float max_reach;
