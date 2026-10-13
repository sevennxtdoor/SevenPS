#include "pch.hpp"
#include "onVariant/ConsoleMessage.hpp"

#include "jammer.hpp"

bool is_jammer(short id)
{
    return id == jammer::SIGNAL || id == jammer::PUNCH || id == jammer::ZOMBIE || id == jammer::BALLOON || id == jammer::ANTIGRAVITY;
}

bool jammer_on(const ::world &world, short id)
{
    return std::ranges::any_of(world.blocks, [id](const ::block &b) { return b.fg == id && (b.state[2] & S_TOGGLE); });
}

bool world_jammed(const std::string &world_name)
{
    auto world = std::ranges::find(worlds, world_name, &::world::name);
    return world != worlds.end() && jammer_on(*world, jammer::SIGNAL);
}

std::string world_display(const std::string &world_name, const ::peer &viewer)
{
    if (viewer.role < MODERATOR && world_jammed(world_name)) return "`4JAMMED!``";
    return world_name;
}

std::string jammer_tags(const ::world &world)
{
    std::string tags{};
    const auto add = [&](short id, std::string_view tag) { if (jammer_on(world, id)) tags += std::format("{}{}", tags.empty() ? "" : "``, ", tag); };
    add(jammer::SIGNAL, "`4JAMMED");
    add(jammer::PUNCH, "`2NOPUNCH");
    add(jammer::ZOMBIE, "`2IMMUNE");
    add(jammer::BALLOON, "`2NOBALLOON");
    add(jammer::ANTIGRAVITY, "`2ANTIGRAVITY");
    return tags.empty() ? "" : std::format(" `0[{}```0]``", tags);
}

void jammer_toggled(ENetEvent &event, ::world &world, ::block &block, const ::pos &tile)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const bool on = block.state[2] & S_TOGGLE;

    std::string_view what{};
    switch (block.fg)
    {
        case jammer::SIGNAL:      what = on ? "This world is now `4hidden`` from the universe." : "This world is `2visible`` again."; break;
        case jammer::PUNCH:       what = on ? "Nobody can punch other players here." : "Players can punch each other again."; break;
        case jammer::ZOMBIE:      what = on ? "Zombie bites can't infect anyone here." : "Zombies can bite again!"; break;
        case jammer::BALLOON:     what = on ? "Balloons can't be thrown here." : "Balloons can be thrown again."; break;
        case jammer::ANTIGRAVITY: what = on ? "Gravity is reduced, jump as much as you want!" : "Gravity is back to normal."; break;
    }
    const std::string message = std::format("`w{}`` {} by {}``. {}", id_to_item(block.fg).raw_name, on ? "`2enabled" : "`4disabled", pPeer->display_growid, what);

    ::gamePacket update{};
    update.id = block.fg;
    update.punch = tile;
    send_tile_update(event, update, block, world); // @note everyone sees it switch on/off
    send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, std::format("`w{}`` {}``", id_to_item(block.fg).raw_name, on ? "`2ON" : "`4OFF"), 0u, 1u });
    peers(world.name, PEER_SAME_WORLD, [&message](ENetPeer &p) { on::ConsoleMessage(&p, message); });
}
