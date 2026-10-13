#pragma once

/* @note friends (`friends` table, both directions), ignore list (peer::profile "ignored"),
 * reports (`reports` table + online staff are told) and trade history (`trade_log` table) */

extern bool are_friends(const std::string &a, const std::string &b);
extern int friends_online(const std::string &growid);
extern bool is_ignoring(const ::peer &listener, const std::string &growid); // @note listener ignores growid

/* @note wrench someone else: "Add as friend" / "Remove friend", "Ignore" / "Unignore", "Report" */
extern void friend_button(ENetEvent &event, ::peer &target);
extern void ignore_button(ENetEvent &event, ::peer &target);
extern void send_report_dialog(ENetEvent &event, ::peer &target);

extern void send_friends(ENetEvent &event, bool show_offline); // @note social portal "Show Friends"
extern void send_trade_history(ENetEvent &event);             // @note wrench "Trade-Scan", social portal "Trade History"
extern void log_trade(const std::string &a, const std::string &b, const std::string &a_items, const std::string &b_items);

extern void friends_return(ENetEvent &event, const ::hPipe &hPipe);        // @note dialog_return "friends"
extern void friend_request_return(ENetEvent &event, const ::hPipe &hPipe); // @note dialog_return "mt_friend"
extern void report_return(ENetEvent &event, const ::hPipe &hPipe);         // @note dialog_return "mt_report"
