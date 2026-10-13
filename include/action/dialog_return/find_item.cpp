#include "pch.hpp"

#include "onVariant/SetClothing.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "tools/cps.hpp"

#include "find_item.hpp"

void find_item(ENetEvent& event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    std::vector<std::string> parts = readch(hPipe["buttonClicked"], '_'); // e.g. searchableItemListButton_2_0_-1
    if (parts.size() < 2 || parts[1].empty()) return;

    const short id = static_cast<short>(atoi(parts[1].c_str()));
    if (id <= 0 || id_to_item(id).id == 0) return;

    /* @note CreativePS rule: most items are free, shop & special items are not */
    if (const std::string reason = find_blocked(*pPeer, id); !reason.empty())
    {
        on::ConsoleMessage(event.peer, std::format("`4Can't find `w{}``: {}", id_to_item(id).raw_name, reason));
        return;
    }

    /* @note only give what fits, else the client shows more than the server has */
    auto it = std::ranges::find(pPeer->slots, id, &::slot::id);
    const short have = (it == pPeer->slots.end()) ? 0 : it->count;
    if (have == 0 && (int)pPeer->slots.size() >= pPeer->slot_size)
    {
        on::ConsoleMessage(event.peer, "`4Your backpack is full!``");
        return;
    }
    const short amount = static_cast<short>(200 - have);
    if (amount <= 0)
    {
        on::ConsoleMessage(event.peer, std::format("`oYou already have 200 `w{}``.``", id_to_item(id).raw_name));
        return;
    }
    modify_item_inventory(event, ::slot(id, amount));
}
