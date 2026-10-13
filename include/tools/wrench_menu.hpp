#pragma once

/* @note the buttons in the wrench menu (yourself & others) that Gurotopia left empty.
 * notebook, world lock bank, online status, view worn clothes, send message. the rest say "not available yet" */

/* @return true if `buttonClicked` was a wrench menu button handled here (called from popup) */
extern bool wrench_menu_button(ENetEvent &event, const ::hPipe &hPipe);

extern void notebook_return(ENetEvent &event, const ::hPipe &hPipe);      // @note dialog_return "notebook"
extern void wl_bank_return(ENetEvent &event, const ::hPipe &hPipe);       // @note dialog_return "wl_bank"
extern void online_status_return(ENetEvent &event, const ::hPipe &hPipe); // @note dialog_return "online_status"
extern void sendpm_return(ENetEvent &event, const ::hPipe &hPipe);        // @note dialog_return "sendpm"

/* @note "`2Online``", "`3Busy``", "`8Away``" */
extern std::string online_status_label(const ::peer &peer);

/* @note text a player typed, made safe to put back in a dialog or message: no '|' or new lines (they would inject dialog lines) */
extern std::string clean_text(std::string text, std::size_t max_length);
