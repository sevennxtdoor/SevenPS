#pragma once

/* @note SevenPS Discord bot (discord_bot/ folder): the server never talks to Discord itself. it writes messages to the
 * `discord_outbox` table and its status to `server_status`, the bot reads both. if the bot is off nothing breaks */

/* @param channel "announce", "sb" or "staff" (the bot maps them to Discord channels). Growtopia color codes may stay in */
extern void discord_post(std::string_view channel, std::string_view text);

/* @note main loop: every 30 s the online count + time to `server_status`, and outbox rows older than an hour are dropped
 * (the bot was off: an old pile of messages is noise) */
extern void discord_tick();
