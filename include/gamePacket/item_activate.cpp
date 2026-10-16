#include "pch.hpp"
#include "onVariant/SetClothing.hpp"
#include "commands/punch.hpp"
#include "onVariant/ConsoleMessage.hpp"

#include "item_activate.hpp"

namespace
{
    short held(const ::peer &p, short id)
    {
        auto it = std::ranges::find(p.slots, id, &::slot::id);
        return (it == p.slots.end()) ? 0 : it->count;
    }

    /* @note room for "count" more of "id" (a stack that exists, even used up, keeps its slot) */
    bool room(const ::peer &p, short id, short count)
    {
        auto it = std::ranges::find(p.slots, id, &::slot::id);
        if (it != p.slots.end()) return it->count + count <= 200;
        return static_cast<int>(p.slots.size()) < p.slot_size;
    }

    void say(ENetEvent &event, const std::string &text)
    {
        ::peer *pPeer = static_cast<::peer*>(event.peer->data);
        send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, text, 0u, 1u });
        on::ConsoleMessage(event.peer, text);
    }

    /* @note "give" in exchange for "take": checked before anything changes, so nothing is lost or made up when the backpack is full */
    void convert(ENetEvent &event, ::slot take, ::slot give, const std::string &done)
    {
        ::peer *pPeer = static_cast<::peer*>(event.peer->data);
        if (!room(*pPeer, give.id, give.count))
            return say(event, std::format("You don't have room for {} `2{}``.", give.count, id_to_item(give.id).raw_name));
        modify_item_inventory(event, ::slot(take.id, static_cast<short>(-take.count)));
        modify_item_inventory(event, give);
        say(event, done);
    }
}

void item_activate(ENetEvent& event, ::gamePacket gamePacket)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    const ::item &item = id_to_item(gamePacket.id);
    if (item.cloth_type != clothing::NONE)
    {
        float &current_cloth = pPeer->clothing[item.cloth_type]; // @note ID of the current clothing being changed

        /* @note never trust the client: this packet could put on any item (and its powers) without owning it. taking off is always fine */
        if (current_cloth != gamePacket.id && held(*pPeer, static_cast<short>(gamePacket.id)) < 1) return;

        current_cloth = (current_cloth == gamePacket.id) ? 0 : gamePacket.id;

        pPeer->update_effects();

        /* @note this is so we can add the latest punch effect (if any) */
        u_short punch_id = get_punch_id(static_cast<u_int>(current_cloth));
        if (punch_id != 0)
            pPeer->punch_effect = punch_id;

        send_varlist(event.peer, { "OnEquipNewItem", gamePacket.id }, pPeer->netid);
        on::SetClothing(*event.peer); // @todo
        return;
    }

    /* @note copies: modify_item_inventory() can add a slot, which moved the vector and left the old iterator pointing at freed memory */
    const short id = static_cast<short>(gamePacket.id);
    const short have = held(*pPeer, id);
    if (have < 1) return;

    if (id == 242 && have >= 100)
        convert(event, {242, 100}, {1796, 1}, "You compressed 100 `2World Lock`` into a `2Diamond Lock``!");
    else if (id == 1796 && have >= 100) // @note 100 Diamond Locks -> a Blue Gem Lock
        convert(event, {1796, 100}, {7188, 1}, "You compressed 100 `2Diamond Lock`` into a `2Blue Gem Lock``!");
    else if (id == 7188) // @note a Blue Gem Lock -> 100 Diamond Locks
        convert(event, {7188, 1}, {1796, 100}, "You shattered a `2Blue Gem Lock`` into 100 `2Diamond Lock``!");
    else if (id == 1796)
        convert(event, {1796, 1}, {242, 100}, "You shattered a `2Diamond Lock`` into 100 `2World Lock``!");
    else if (id == 1486 && have >= 100)
        convert(event, {1486, 100}, {6802, 1}, "You compressed 100 `2Growtoken`` into a `2Mega Growtoken``!");
    else if (id == 6802)
        convert(event, {6802, 1}, {1486, 100}, "You shattered a `2Mega Growtoken`` into 100 `2Growtoken``!");
}
