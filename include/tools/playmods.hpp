#pragma once

/* @note movement playmods: speed & gravity sent in the character state packet (SetClothing).
 * worn items come from resources/playmods.txt, plus zombies (slow) and an active Antigravity Generator */

constexpr float NORMAL_SPEED = 250.0f, NORMAL_GRAVITY = 1000.0f;

/* @return {speed, gravity} for this player right now */
extern ::pos player_physics(const ::peer &p);

/* @note text for /status: "Speedy, Antigravity" or "none" */
extern std::string playmod_names(const ::peer &p);
