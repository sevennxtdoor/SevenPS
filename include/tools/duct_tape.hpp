#pragma once

/* @note Duct Tape, like Growtopia (wiki: Duct Tape): a moderator's tool for players who say offensive things.
 * 10 minutes of the Duct Tape mod: chat comes out as m's and f's, and no broadcasts, private messages, door/sign
 * editing or writing on bulletin boards and mailboxes. kept in the profile ("tape_until"), so leaving doesn't end it */

constexpr int DUCT_TAPE_SECONDS = 10 * 60;

/* @note seconds left, 0 = not taped */
extern int taped_left(const ::peer &p);

/* @note tape this player's mouth for `seconds` */
extern void duct_tape(ENetPeer &target, int seconds = DUCT_TAPE_SECONDS);

/* @note shows/takes off the tape once the time is up (call often: movement, chat) */
extern void duct_tape_tick(ENetPeer &p);

/* @return true (and tells them) if a taped player tries to broadcast, message or write */
extern bool tape_blocks(ENetPeer &p);
