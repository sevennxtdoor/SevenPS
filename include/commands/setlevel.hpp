#pragma once

/* @note /setlevel {growid} {1-125}: owner only, the player must be online (saved on logout like any level) */
extern void setlevel(ENetEvent &event, const std::string_view text);
