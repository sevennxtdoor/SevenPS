#include "pch.hpp"
#include "tools/area_lock.hpp"
#include "tools/password_door.hpp"

#include "gateway_edit.hpp"

void gateway_edit(ENetEvent& event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    const short tilex = atoi(hPipe["tilex"].c_str());
    const short tiley = atoi(hPipe["tiley"].c_str());

    auto world = std::ranges::find(worlds, pPeer->recent_worlds.back(), &::world::name);
    if (world == worlds.end()) return;
    if (!can_build(*pPeer, *world, ::pos{ tilex, tiley })) return; // @note never trust the dialog: bounds + who may edit (a faked dialog could rename START's doors or write outside the world)

    block &block = world->blocks[cord(tilex, tiley)];

    /* @note the dialog caps these, a proxy doesn't: the world saves lengths as i16, so 32 KB of text went negative and
     * everything saved after it was lost on the next load */
    const auto capped = [&hPipe](const char *key, std::size_t max) { return hPipe[key].substr(0, max); };

    if (hPipe["dialog_name"] == "sign_edit") 
    {
        auto sign = std::ranges::find(world->signs, ::pos{tilex, tiley}, &::sign::pos);
        if (sign != world->signs.end()) 
        {
            sign->label = capped("sign_text", 128);
        }
        else {
            world->signs.emplace_back(::sign(capped("sign_text", 128), ::pos{tilex, tiley}));
        }
    }
    else if (hPipe["dialog_name"] == "door_edit") 
    {
        const u_char fg_type = id_to_item(block.fg).type;
        if (fg_type != type::DOOR && fg_type != type::PORTAL) return; // @note a faked dialog could put a door entry on any tile

        auto door = std::ranges::find(world->doors, ::pos{tilex, tiley}, &::door::pos);
        if (door == world->doors.end())
            door = world->doors.emplace(world->doors.end(), "", "", "", ::pos{ tilex, tiley });

        door->label = capped("door_name", 100);
        door->dest = capped("door_target", 24);
        door->id = capped("door_id", 11);
        door->password = (block.fg == PASSWORD_DOOR) ? clean_door_password(hPipe["door_password"]) : "";
    }
    else if (hPipe["dialog_name"] == "gateway_edit") 
    {
        block.state[2] &= ~(S_PUBLIC | S_LOCKED);
        block.state[2] |= atoi(hPipe["checkbox_public"].c_str()) ? S_PUBLIC : S_LOCKED;
    }

    send_tile_update(event, {
        .id = block.fg,
        .punch = { tilex, tiley }
    }, block, *world);
}