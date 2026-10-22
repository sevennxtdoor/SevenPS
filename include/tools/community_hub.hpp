#pragma once

/* @note Community Hub (Social Portal), growtopiawiki.com/w/Community_Hub: Featured Worlds, World of the Day (current and past),
 * Top Worlds, the Official YouTube Channel and Content Creators. GT also has Top Rated worlds; SevenPS has no world ratings yet.
 * what's shown comes from resources/community_hub.txt (read every time it opens), Top Worlds from the worlds with people in them */

extern void send_community_hub(ENetEvent &event);               // @note social portal "Community Hub"
extern void community_hub_return(ENetEvent &event, const ::hPipe &hPipe); // @note dialog_return "mt_hub"
