#pragma once

/* @note goes to a world, no questions asked: used by /go, /warpto, /home, /parkour... */
extern void warp(ENetEvent& event, const std::string_view text);

/* @note the /warp command, like Growtopia (wiki: Commands): Super Supporters only (staff in SevenPS), 15 second cooldown */
extern void warp_command(ENetEvent& event, const std::string_view text);

/* @note /rgo, like Growtopia: to the world of the last private message you got, if possible */
extern void rgo(ENetEvent& event, const std::string_view text);

/* @note a private message reached `to`: remember where `from` was, for /rgo */
extern void got_private_message(::peer &to, const ::peer &from);
