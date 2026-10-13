#pragma once

/* @note Surg-E surgery. dialog format & item ids from GTOS (dialog "surge"), the game logic is our own:
 * 6 diagnoses and all 13 surgical tools work. wrench a Surg-E, pick tools from the tray until the patient
 * is fixed and stitched up. the robot is destroyed whether you succeed or fail. */

/* @note wrench on a Surg-E: "are you sure?" dialog, or resume the surgery that is already going on */
extern void send_surgery_dialog(ENetEvent &event, ::world &world, const ::pos &pos);
extern void surgery_return(ENetEvent &event, const ::hPipe &hPipe); // @note dialog_return "surge"

/* @note leaving the world (or disconnecting) mid surgery counts as giving up */
extern void abandon_surgery(ENetEvent &event);

/* @note someone is operating on this Surg-E (it can't be broken meanwhile) */
extern bool surgery_in_use(const ::world &world, const ::pos &pos);
