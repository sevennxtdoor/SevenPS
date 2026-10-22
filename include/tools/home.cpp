#include "pch.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "commands/warp.hpp"
#include "tools/profile.hpp"

#include "home.hpp"

std::string home_world(const ::peer &p) { return pget_str(p, "home"); }

void set_home(ENetEvent &event, const std::string &world_name)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    pset_str(*pPeer, "home", world_name);
    on::ConsoleMessage(event.peer, std::format("`oYour home world is now `w{}``. Type `$/home`` to go there.``", world_name));
}

void sethome_command(ENetEvent &event, const std::string_view)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (pPeer->netid == 0) return on::ConsoleMessage(event.peer, "`4Enter a world first.``");
    set_home(event, pPeer->recent_worlds.back());
}

void home_command(ENetEvent &event, const std::string_view)
{
    const std::string home = home_world(*static_cast<::peer*>(event.peer->data));
    if (home.empty()) return on::ConsoleMessage(event.peer, "`oYou don't have a home world yet. Type `$/sethome`` in a world to make it your home.``");
    warp(event, "warp " + home);
}
