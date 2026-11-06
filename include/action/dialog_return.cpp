#include "pch.hpp"
#include <unordered_set>

#include "dialog_return/__dialog_return.hpp"

#include "action/dialog_return.hpp"

/* @note dialogs about a tile or a trade in the world we are in. after leaving it (or a world ban) recent_worlds.back() still
 * names it, so a faked dialog could still drop items there, edit its doors/signs/locks, use its vending machines... */
static bool needs_world(const std::string &name)
{
    static const std::unordered_set<std::string> names{
        "drop_item", "gateway_edit", "door_edit", "sign_edit", "lock_edit", "trade_add", "trade_confirm", "vending", "surge",
        "area_lock", "mt_box", "mt_magplant", "mt_passdoor", "mt_phone", "mt_wizard", "mt_mannequin"
    };
    return names.contains(name);
}

void action::dialog_return(ENetEvent& event, const std::string& header)
{
    ::hPipe hPipe{ header };
    if (static_cast<::peer*>(event.peer->data)->netid == 0 && needs_world(hPipe["dialog_name"])) return;

    // @note found at ./action/dialog_return/
    if (const auto it = dialog_return_pool.find(hPipe["dialog_name"]); it != dialog_return_pool.end())
        it->second(std::ref(event), std::move(hPipe));
}
