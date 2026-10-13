#include "pch.hpp"
#include "tools/social.hpp"
#include "socialportal.hpp"

void socialportal(ENetEvent& event, const ::hPipe &hPipe)
{
    const std::string clicked = hPipe["buttonClicked"];
    if (clicked == "showfriend") send_friends(event, false);
    else if (clicked == "trade_history") send_trade_history(event);
    else if (!clicked.empty())
    {
        send_varlist(event.peer, { "OnDialogRequest",
            "set_default_color|`o\n"
            "add_label|big|`wSocial Portal``|left|\n"
            "add_spacer|small|\n"
            "add_textbox|Guilds, apprentices and the community hub are not on `9MikeTopia`` yet.|left|\n"
            "end_dialog|popup||OK|\n" });
    }
}
