#pragma once

extern int get_weather_id(u_int item_id);

/* @note weather to show when entering a world: /weather override first, otherwise a toggled weather machine, otherwise sunny */
extern int current_weather(const ::world &world);
extern void clear_weather_override(const std::string &world_name); // @note a weather machine was punched

extern void weather(ENetEvent& event, const std::string_view text);
