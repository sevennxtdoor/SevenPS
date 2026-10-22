#include "pch.hpp"

#include "onVariant/BillboardChange.hpp"

#include "billboard_edit.hpp"

void billboard_edit(ENetEvent& event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    /* @note never trust the dialog: the picker only offers items in your backpack and the price box takes 5 digits,
     * a proxy sent any item id (or a negative price) to show over its head */
    const short id = static_cast<short>(atoi(hPipe["billboard_item"].c_str()));
    if (id != 0 && (id == 18 || id == 32 || std::ranges::find(pPeer->slots, id, &::slot::id) == pPeer->slots.end())) return;

    pPeer->billboard.id = id;
    pPeer->billboard.show = atoi(hPipe["billboard_toggle"].c_str());
    pPeer->billboard.isBuying = atoi(hPipe["billboard_buying_toggle"].c_str());
    pPeer->billboard.perItem = atoi(hPipe["chk_peritem"].c_str());
    pPeer->billboard.price = std::clamp(atoi(hPipe["setprice"].c_str()), 0, 99999);

    on::BillboardChange(event);
}