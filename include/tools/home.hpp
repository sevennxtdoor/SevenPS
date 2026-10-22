#pragma once

/* @note home world, like Growtopia (wiki: Commands, World Lock): /sethome saves the world you're in (only one, a new
 * one replaces it), /home or /gethome takes you there. the World Lock's "Set as Home World" checkbox does the same as
 * /sethome. kept in the profile ("home") */

extern std::string home_world(const ::peer &p);
extern void set_home(ENetEvent &event, const std::string &world_name);

extern void sethome_command(ENetEvent &event, const std::string_view text);
extern void home_command(ENetEvent &event, const std::string_view text);
