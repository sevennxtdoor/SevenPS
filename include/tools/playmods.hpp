#pragma once

/* @note movement playmods: speed & gravity sent in the character state packet (SetClothing).
 * worn items come from resources/playmods.txt, plus zombies (slow) and an active Antigravity Generator */

constexpr float NORMAL_SPEED = 250.0f, NORMAL_GRAVITY = 1000.0f;

/* @return {speed, gravity} for this player right now */
extern ::pos player_physics(const ::peer &p);

enum playmod_flag : int {
    PM_DOUBLE_JUMP = 0x1, // @note 'dj' in playmods.txt: double jump without a back item
    PM_DIG         = 0x2  // @note 'dig': Enhanced Digging, blocks need one hit less
};

/* @return true if a worn item gives this playmod_flag */
extern bool has_playmod(const ::peer &p, int flag);

/* @note every character state bit playmods.txt can give (update_effects clears these before adding the worn ones) */
constexpr int PLAYMOD_STATES = 0x4 | 0x40 | 0x80 | 0x100 | 0x200 | 0x400 | 0x4000 | 0x8000 | 0x40000 | 0x80000 | 0x100000;
constexpr int STATE_FIREPROOF = 0x200;

/* @return the character state bits from worn items ('eyes', 'halo', 'fireproof'... in playmods.txt) */
extern int playmod_states(const ::peer &p);

/* @return extra tiles of reach from worn items ('pr+N' punch, 'br+N' build), 0 = normal */
extern u_char playmod_range(const ::peer &p, bool punch);

/* @note text for /status: "Speedy, Antigravity" or "none" */
extern std::string playmod_names(const ::peer &p);
