#pragma once

extern void sb(ENetEvent& event, const std::string_view text);

/* @note the Megaphone item: same broadcast, uses up 1 Megaphone instead of gems */
extern void sb_megaphone(ENetEvent& event, const std::string_view text);
