#pragma once

/* @note recompute display_growid (world owner green / access cyan / titles / role) and show it to the world */
extern void refresh_display_name(ENetPeer &p);

/* wrench -> Title button, or /title */
extern void send_title_dialog(ENetEvent& event);
extern void title_edit(ENetEvent& event, const ::hPipe &hPipe); // @note dialog_return "title_edit"

extern void title(ENetEvent& event, const std::string_view text);
extern void givetitle(ENetEvent& event, const std::string_view text); // @note developer+
extern void nick(ENetEvent& event, const std::string_view text);      // @note moderator+ (super supporter perk)

/* @note e.g. "`9Owner`` `o|`` `5Super Supporter``" - shown in wrench menus. staff are super supporters */
extern std::string status_label(const ::peer &peer);

extern void hidestatus(ENetEvent& event, const std::string_view text); // @note moderator+ (super supporter)
