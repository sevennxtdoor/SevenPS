#include "pch.hpp"
#include "tools/economy.hpp"
#include "onVariant/SetBux.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "tools/item_hunt.hpp"

#include "item_activate_object.hpp"

void item_activate_object(ENetEvent& event, ::gamePacket gamePacket) 
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    auto world = std::ranges::find(worlds, pPeer->recent_worlds.back(), &::world::name);
    if (world == worlds.end()) return;

    auto object = std::ranges::find(world->objects, gamePacket.id, &::object::uid);
    if (object == world->objects.end()) return; // @note already taken, or a made-up uid (reading past the end crashed the server)

    /* @note the client asks for a drop it walked over; a cheat client can ask for any drop in the world. 5 tiles leaves room for lag */
    if (std::abs(object->pos.x - pPeer->pos.x) > 5 * 32 || std::abs(object->pos.y - pPeer->pos.y) > 5 * 32) return;

    if (hunt_pickup(event, *world, object->uid)) return; // @note Item Hunt chest: gems instead of the item
    if (object->id != 112/*gem*/)
    {
        const ::item &item = id_to_item(object->id);

        u_short remember = object->count;
        object->count = pPeer->emplace(::slot(object->id, object->count)); // @return remains after reaching 200
        if (object->count > 0)
        {
            add_object(event, ::slot(object->id, object->count), object->pos, *world);
        }
        u_short collected = remember - object->count;
        if (collected ==/*unsigned*/ 0) return; // @todo

        on::ConsoleMessage(event.peer, (item.rarity >= 999) ?
            std::format("Collected `w{} {}``.",                collected, item.raw_name) :
            std::format("Collected `w{} {}``. Rarity: `w{}``", collected, item.raw_name, item.rarity)
        );
    }
    else 
    {
        pPeer->gems += object->count;
        econ("gems from blocks", object->count);
        object->count = 0;
        on::SetBux(event);
    }
    remove_object(event, object->uid);

    if (object->count == 0) world->objects.erase(object);
}