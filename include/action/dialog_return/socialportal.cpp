#include "pch.hpp"
#include "tools/social.hpp"
#include "tools/guild.hpp"
#include "tools/mentor.hpp"
#include "tools/community_hub.hpp"
#include "socialportal.hpp"

void socialportal(ENetEvent& event, const ::hPipe &hPipe)
{
    const std::string clicked = hPipe["buttonClicked"];
    if (clicked == "showfriend") send_friends(event, false);
    else if (clicked == "trade_history") send_trade_history(event);
    else if (clicked == "show_apprentices") send_apprentices(event);
    else if (clicked == "communityhub") send_community_hub(event);
    else if (clicked == "showguild") guild_command(event, "/guild"); // @note same menu as typing /guild
}
