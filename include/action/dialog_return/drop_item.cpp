#include "pch.hpp"
#include "tools/economy.hpp"

#include "drop_item.hpp"

void drop_item(ENetEvent& event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    auto world = std::ranges::find(worlds, pPeer->recent_worlds.back(), &::world::name);
    if (world == worlds.end()) return;

    if (staff_locked(*pPeer)) return staff_locked_message(*event.peer);

    /* @note never trust the dialog: it can be sent by a proxy with any item and count. before this check, dropping an
     * item you didn't own (or a negative count) created items out of nothing */
    const short itemID = static_cast<short>(atoi(hPipe["itemID"].c_str()));
    auto slot = std::ranges::find(pPeer->slots, itemID, &::slot::id);
    if (itemID == 18 || itemID == 32 || slot == pPeer->slots.end() || (id_to_item(itemID).cat & CAT_UNTRADEABLE)) return;
    const short count = static_cast<short>(std::clamp(atoi(hPipe["count"].c_str()), 0, static_cast<int>(slot->count)));
    if (count <= 0) return;

    modify_item_inventory(event, ::slot(itemID, -count));

    float x_nabor = (pPeer->facing_left) ? pPeer->pos.x - 32 : pPeer->pos.x + 32; // @note peer's naboring tile (drop position)
    add_drop(event, {itemID, count}, {x_nabor, pPeer->pos.y}, *world);
}