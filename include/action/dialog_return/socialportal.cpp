#include "pch.hpp"
#include "tools/social.hpp"
#include "tools/guild.hpp"
#include "socialportal.hpp"

void socialportal(ENetEvent& event, const ::hPipe &hPipe)
{
    const std::string clicked = hPipe["buttonClicked"];
    if (clicked == "showfriend") send_friends(event, false);
    else if (clicked == "trade_history") send_trade_history(event);
    else if (clicked == "showguild") guild_command(event, "/guild"); // @note same menu as typing /guild
    else if (!clicked.empty())
    {
        send_varlist(event.peer, { "OnDialogRequest",
            "set_default_color|`o\n"
            "add_label|big|`wSocial Portal``|left|\n"
            "add_spacer|small|\n"
            "add_textbox|Apprentices and the community hub are not on `9SevenPS`` yet.|left|\n"
            "end_dialog|popup||OK|\n" });
    }
}
