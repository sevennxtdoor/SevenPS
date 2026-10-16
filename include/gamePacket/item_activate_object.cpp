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

        /* @note how many fit: up to 200 a stack, and a new item needs a free backpack slot */
        auto slot = std::ranges::find(pPeer->slots, static_cast<short>(object->id), &::slot::id);
        const int fits = (slot != pPeer->slots.end()) ? 200 - slot->count : (static_cast<int>(pPeer->slots.size()) < pPeer->slot_size ? 200 : 0);
        const u_short collected = static_cast<u_short>(std::clamp<int>(fits, 0, object->count));
        if (collected == 0) return; // @note backpack full: the drop stays where it is

        on::ConsoleMessage(event.peer, (item.rarity >= 999) ?
            std::format("Collected `w{} {}``.",                collected, item.raw_name) :
            std::format("Collected `w{} {}``. Rarity: `w{}``", collected, item.raw_name, item.rarity)
        );
        if (collected < object->count)
        {
            /* @note only part of it fits. this used to "add back" the rest with add_object(), which merged into this
             * same drop: the drop kept more than was left (picking up duplicated items), and the add could move the
             * vector under the iterator we still used */
            modify_item_inventory(event, ::slot(static_cast<short>(object->id), static_cast<short>(collected)));
            object->count -= collected;
            item_change_object(event, ::gamePacket{
                .netid = (int)0xfffffffd, // @note "this drop's count changed" (like merge_object)
                .uid   = (int)object->uid,
                .count = static_cast<float>(object->count),
                .id    = object->id,
                .pos   = object->pos
            });
            return;
        }
        pPeer->emplace(::slot(static_cast<short>(object->id), static_cast<short>(collected))); // @note the client adds it itself when the drop is removed below
        object->count = 0;
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