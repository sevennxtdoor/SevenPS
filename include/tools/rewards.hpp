#pragma once

/* @note in-game rewards like the hosted GTPS servers have:
 * - redeem codes: the owner makes them in the /code window (no typing of options), players use /redeem CODE or the store's Redeem Code button.
 *   each account can use a code once; codes have a use limit and an end date. tables `redeem_code` & `redeem_used`
 * - Daily Spin: /spin once a day (resources/daily_spin.txt), rare prizes are announced */

extern void redeem_command(ENetEvent &event, const std::string_view text); // @note /redeem CODE (no code: opens the dialog)
extern void code_command(ENetEvent &event, const std::string_view text);   // @note /code (owner): a window to make & delete codes
extern void code_return(ENetEvent &event, const ::hPipe &hPipe);           // @note dialog_return "mt_code"
extern void redeem_dialog(ENetEvent &event);                               // @note store button "Redeem Code"
extern void redeem_return(ENetEvent &event, const ::hPipe &hPipe);         // @note dialog_return "mt_redeem"

extern void spin_command(ENetEvent &event, const std::string_view text);   // @note /spin
