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

/* @note text for /status: "Speedy, Antigravity" or "none" */
extern std::string playmod_names(const ::peer &p);
