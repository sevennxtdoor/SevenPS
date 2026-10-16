#pragma once

/* @note area locks: Small Lock (10 tiles), Big Lock (48), Huge Lock (200) and Builder's Lock (200).
 * the lock spreads from where it's placed over connected tiles (like real Growtopia), only the owner and
 * the access list may build or break there. kept in the `world_area_lock` table, the world blob stays the same */

enum area_lock_flag : int {
    AL_IGNORE_AIR  = 0x1, // @note "Ignore empty air": only blocks get locked, empty tiles are skipped
    AL_BUILD_ONLY  = 0x2, // @note Builder's Lock: people with access can only build
    AL_BREAK_ONLY  = 0x4  // @note Builder's Lock: people with access can only break
};

struct area_lock
{
    ::pos pos{};      // @note the lock itself
    short item{};     // @note 202, 204, 206 or 4994
    int owner{};      // @note user id
    std::string owner_name{};
    std::vector<std::pair<int, std::string>> access{}; // @note {user id, growid}
    int flags{};
    std::vector<u_short> tiles{}; // @note cord(x, y) of every locked tile, the lock's own tile not included
};

/* @return the area lock covering this tile (or the lock standing on it), nullptr if none */
extern ::area_lock *area_lock_at(::world &world, const ::pos &tile);

/* @note checks a punch (18), place or wrench (32) inside an area lock. @return false and tells the player why when it's not allowed */
extern bool area_lock_allows(ENetEvent &event, ::world &world, const ::pos &tile, short action);

/* @note a tile lock was placed / broken. place throws std::runtime_error when it can't be placed here */
extern void area_lock_place(ENetEvent &event, ::world &world, short item, const ::pos &pos);
extern void area_lock_removed(::world &world, const ::pos &pos);

/* @note a World Lock can't go in a world that has someone else's area lock. @return that owner's name, "" if none */
extern std::string area_lock_other_owner(::world &world, int user_id);

/* @note PACKET_SEND_LOCK for every area lock, sent to a player entering the world */
extern void send_area_locks(ENetPeer &peer, ::world &world);
extern void send_area_lock_visuals(ENetEvent &event, ::world &world, const ::pos &pos); // @note to everyone in the world

extern void area_lock_dialog(ENetEvent &event, ::world &world, const ::pos &pos); // @note wrench on the lock
extern void area_lock_return(ENetEvent &event, const ::hPipe &hPipe); // @note dialog_return "area_lock"

/* ---------- who may change a tile (used by every dialog that edits something in the world) ---------- */

/* @note x, y inside the world's blocks. dialogs send tilex/tiley back and they can be faked */
extern bool in_world(const ::world &world, int x, int y);
extern bool in_world(const ::world &world, const ::pos &tile); // @note for tiles straight from a packet (floats)

/* @note same rules as building: system worlds staff only, World Lock owner/access (or public), area lock owner/access, else anyone */
extern bool can_build(const ::peer &p, ::world &world, const ::pos &tile);

/* @note World Lock owner, else the area lock's owner, else 0 (nobody owns this tile) */
extern int tile_owner(::world &world, const ::pos &tile);

/* @note owner or on the access list of the lock that protects this tile. false when nobody owns it */
extern bool tile_access(const ::peer &p, ::world &world, const ::pos &tile);

extern void load_area_locks(::world &world);
extern void save_area_locks(::world &world);
