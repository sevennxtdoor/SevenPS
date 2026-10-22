#include "pch.hpp"
#include "action/join_request.hpp"
#include "action/quit_to_exit.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "tools/anticheat.hpp"

#include "password_door.hpp"

namespace
{
    constexpr int MAX_FAILS = 5;     // @note wrong passwords in a row before the door makes you wait
    constexpr int FAIL_WAIT = 30;    // @note seconds. stops guessing a password with a bot
    constexpr float NEAR_TILES = 3.0f; // @note how close you must stand to a door to use it

    ::door *door_at(::world &world, const ::pos &tile)
    {
        auto door = std::ranges::find(world.doors, tile, &::door::pos);
        return (door != world.doors.end()) ? &*door : nullptr;
    }

    ::door *door_with_id(::world &world, const std::string &id)
    {
        if (id.empty()) return nullptr;
        auto door = std::ranges::find(world.doors, id, &::door::id);
        return (door != world.doors.end()) ? &*door : nullptr;
    }

    /* @note un-freeze the player and put them on a tile (door enter animation freezes the client) */
    void put_at(ENetEvent &event, const ::pos &pixel)
    {
        ::peer *pPeer = static_cast<::peer*>(event.peer->data);
        moved_by_server(*pPeer, pixel);
        send_varlist(event.peer, { "OnSetPos", CL_Vec2f{ pixel.x, pixel.y } }, pPeer->netid);
        send_varlist(event.peer, { "OnZoomCamera", 10000.0f, 1000u });
        send_varlist(event.peer, { "OnSetFreezeState", 0u }, pPeer->netid);
    }

    /* @note go through a door: "" = main door, "WORLD", "WORLD:ID" or ":ID" */
    void go_through(ENetEvent &event, const std::string world_name, const std::string dest)
    {
        ::peer *pPeer = static_cast<::peer*>(event.peer->data);

        std::string target_world = dest, target_id{};
        if (const std::size_t colon = dest.find(':'); colon != std::string::npos)
        {
            target_world = dest.substr(0, colon);
            target_id = dest.substr(colon + 1);
        }
        for (char &c : target_world) c = std::toupper(c);

        if (target_world.empty() || target_world == world_name) // @note stays in this world
        {
            auto world = std::ranges::find(worlds, world_name, &::world::name);
            if (world == worlds.end()) return;

            const ::door *door = door_with_id(*world, target_id);
            put_at(event, door ? door->pos.by_32() : pPeer->rest_pos); // @note no such ID -> main door, like a door without destination
            return;
        }

        /* @note copies of the names: quit_to_exit/join_request can move the worlds vector, so no references across them */
        action::quit_to_exit(event, "", true);
        action::join_request(event, "", target_world);

        if (target_id.empty() || pPeer->recent_worlds.back() != target_world) return; // @note no ID, or the world refused us
        auto world = std::ranges::find(worlds, target_world, &::world::name);
        if (world == worlds.end()) return;
        if (const ::door *door = door_with_id(*world, target_id))
            put_at(event, door->pos.by_32());
    }

    void ask_password(ENetEvent &event, const ::door &door)
    {
        const std::string label = door.label.empty() ? id_to_item(PASSWORD_DOOR).raw_name : door.label;
        send_varlist(event.peer, {
            "OnDialogRequest",
            std::format(
                "set_default_color|`o\n"
                "add_label_with_icon|big|`w{}``|left|{}|\n"
                "add_textbox|The door requires a password.|left|\n"
                "add_text_input|password|Password||23|\n"
                "end_dialog|mt_passdoor|Cancel|OK|",
                label, PASSWORD_DOOR
            )
        });
    }
}

std::string clean_door_password(const std::string &text)
{
    std::string out{};
    for (char c : text)
        if (std::isalnum(static_cast<u_char>(c)) && out.size() < 23)
            out += std::toupper(static_cast<u_char>(c));
    return out;
}

void door_activate(ENetEvent &event, ::world &world, const ::pos &tile)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    ::door *door = door_at(world, tile);

    if (door == nullptr) // @note a door that was never edited: goes to the main door
    {
        put_at(event, pPeer->rest_pos);
        return;
    }
    if (world.blocks[cord(tile.x_int(), tile.y_int())].fg == PASSWORD_DOOR && !door->password.empty())
    {
        /* @note stay on the door while typing. the server remembers WHICH door, the dialog doesn't carry it,
         * so a faked dialog can't open some other door from across the world */
        put_at(event, tile.by_32());
        pPeer->pass_door = tile;
        ask_password(event, *door);
        return;
    }
    go_through(event, world.name, door->dest);
}

void password_door_return(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const ::pos tile = pPeer->pass_door;
    pPeer->pass_door = ::pos{ -1, -1 }; // @note one answer per prompt
    if (tile.x < 0) return;

    auto world = std::ranges::find(worlds, pPeer->recent_worlds.back(), &::world::name);
    if (world == worlds.end()) return;
    if (world->blocks[cord(tile.x_int(), tile.y_int())].fg != PASSWORD_DOOR) return; // @note broken while we typed
    ::door *door = door_at(*world, tile);
    if (door == nullptr || door->password.empty()) return;

    const ::pos me = pPeer->pos.by_32(true);
    if (std::abs(me.x - tile.x) > NEAR_TILES || std::abs(me.y - tile.y) > NEAR_TILES) return; // @note walked away

    const std::time_t now = std::time(nullptr);
    if (pPeer->pass_door_wait > now)
    {
        send_varlist(event.peer, { "OnTalkBubble", pPeer->netid,
            std::format("`4Too many wrong passwords!`` Wait `w{}`` seconds.", pPeer->pass_door_wait - now) });
        return;
    }

    if (clean_door_password(hPipe["password"]) != door->password)
    {
        if (++pPeer->pass_door_fails >= MAX_FAILS)
        {
            pPeer->pass_door_fails = 0;
            pPeer->pass_door_wait = now + FAIL_WAIT;
        }
        send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, "`4Wrong password!``" });
        return;
    }

    pPeer->pass_door_fails = 0;
    send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, "`2The door opens!``" });
    go_through(event, world->name, door->dest);
}
