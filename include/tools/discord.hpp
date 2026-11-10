#pragma once

/* @note SevenPS Discord bot (discord_bot/ folder): the server never talks to Discord itself. it writes messages to the
 * `discord_outbox` table and its status to `server_status`, the bot reads both. if the bot is off nothing breaks */

/* @param channel "announce", "sb" or "staff" (the bot maps them to Discord channels). Growtopia color codes may stay in */
extern void discord_post(std::string_view channel, std::string_view text);

/* @note main loop: every 30 s the online count + time to `server_status`, and outbox rows older than an hour are dropped
 * (the bot was off: an old pile of messages is noise) */
extern void discord_tick();

/* @note what the bot shows in /online and /world: one "shown name<TAB>world<TAB>growid" line per player (world empty in the
 * menu, growid empty for staff under /nick) */
extern std::string discord_players();

/* @brief /link {code}: links `growid` to the Discord account that asked the bot for that code
 * @return the message for the player */
extern std::string discord_link(const std::string &growid, std::string_view typed);

extern void link_command(ENetEvent &event, const std::string_view text);   // @note /link {code}
extern void unlink_command(ENetEvent &event, const std::string_view text); // @note /unlink
