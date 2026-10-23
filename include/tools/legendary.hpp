#pragma once

/* @note Legendary Wizard (1790): long Legendary Quests with an untradeable reward, like real Growtopia.
 * steps & texts from GTOS (zKaan/GTOSLatest "Quest For Honor"); its PvP step is left out (SevenPS has no PvP), so 19 steps.
 * the last step is a Legendary Orb (1794) from the top of LEGENDARYMOUNTAIN (tools/system_worlds.cpp).
 * the Wizard vanishes when somebody turns in their final step to it. progress is kept in peer::profile (lq, lq_step, lq_prog, lq_base, lq_done) */

constexpr u_short LEGENDARY_WIZARD = 1790;
constexpr u_short LEGENDARY_ORB = 1794;

extern void legendary_wizard_dialog(ENetEvent &event, ::world &world, const ::pos &tile);
extern void legendary_wizard_return(ENetEvent &event, const ::hPipe &hPipe); // @note dialog_return "mt_wizard"
