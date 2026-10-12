#pragma once

/* @note world bans are temporary (like real growtopia), kept in memory only - a server restart clears them */

extern void world_ban(const std::string &world_name, int user_id, int minutes = 10);

/* @return minutes left (rounded up), 0 if not banned */
extern int world_ban_left(const std::string &world_name, int user_id);

extern void world_unban_all(const std::string &world_name);

/* @note owner of the world (or staff) acting on someone who isn't the owner and isn't staff of equal/higher role */
extern bool can_world_punish(const ::peer &actor, const ::peer &target, const ::world &world);

/* @param act "pull", "kick" or "worldban" - target must be in your world. @return false if not allowed */
extern bool world_punish(ENetEvent& event, const std::string &act, ENetPeer *target, int minutes = 10);
