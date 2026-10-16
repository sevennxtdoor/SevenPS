#include "pch.hpp"
#include "onVariant/ConsoleMessage.hpp"

#include "trash_item.hpp"

void trash_item(ENetEvent& event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    const short itemID = atoi(hPipe["itemID"].c_str());
    const ::item &item = id_to_item(itemID);
    if (itemID == 18 || itemID == 32) return; // @note fist & wrench

    /* @note never trust the dialog: an item we don't have kept the typed count, so "count -200" gave 200 of anything */
    auto slot = std::ranges::find(pPeer->slots, itemID, &::slot::id);
    if (slot == pPeer->slots.end()) return;
    const short count = static_cast<short>(std::clamp(atoi(hPipe["count"].c_str()), 0, static_cast<int>(slot->count)));
    if (count <= 0) return;

    modify_item_inventory(event, ::slot(itemID, -count));
    on::ConsoleMessage(event.peer, std::format("{} `w{}`` recycled, `w0`` gems earned.", count, item.raw_name));
}
