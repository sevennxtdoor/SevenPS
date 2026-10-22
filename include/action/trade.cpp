#include "pch.hpp"
#include "tools/economy.hpp"
#include "onVariant/ConsoleMessage.hpp"

#include "trade.hpp"
#include "tools/profile.hpp"
#include "tools/social.hpp"
#include "tools/world_key.hpp"

static ::peer *data(ENetPeer &p) { return static_cast<::peer*>(p.data); }

/* @note the person we trade with, only if they are still in our world */
static ENetPeer *partner_of(ENetPeer &p)
{
    ::peer *pPeer = data(p);
    if (pPeer->trade_netid == 0 || pPeer->netid == 0) return nullptr;
    for (ENetPeer *o : peers(pPeer->recent_worlds.back(), PEER_SAME_WORLD))
        if (data(*o)->netid == pPeer->trade_netid) return o;
    return nullptr;
}

static void overlay(ENetPeer &p, const std::string &text) { send_varlist(&p, { "OnTextOverlay", text }); }
static void sfx(ENetPeer &p, std::string_view file) { send_action(p, "play_sfx", std::format("file|{}\ndelayMS|0\n", file)); }

static short owned(const ::peer &p, short id)
{
    auto it = std::ranges::find(p.slots, id, &::slot::id);
    return (it == p.slots.end()) ? 0 : it->count;
}

static void reset(::peer &p)
{
    p.trade_netid = 0;
    p.trading = p.trade_accepted = p.trade_confirmed = false;
    p.trade_items.clear();
}

static std::string slots_text(const ::peer &p)
{
    std::string text{};
    for (const ::slot &s : p.trade_items) text += std::format("add_slot|{}|{}\n", s.id, s.count);
    return text;
}

/* @note show "who"'s offer on "to"'s trade window */
static void send_status(ENetPeer &to, const ::peer &who, bool locked)
{
    send_varlist(&to, {
        "OnTradeStatus", who.netid, "",
        std::format("`o{}`o's offer.``", who.display_growid),
        std::format("{}locked|{}\n{}accepted|{}\n", slots_text(who), locked ? 1 : 0, locked ? "" : "reset_locks|1\n", who.trade_accepted ? 1 : 0)
    });
}

static void refresh(ENetPeer &a, ENetPeer &b, bool locked)
{
    for (ENetPeer *to : { &a, &b })
    {
        send_status(*to, *data(a), locked);
        send_status(*to, *data(b), locked);
    }
}

/* @note any change to an offer un-accepts both sides, so nobody gets tricked */
static void deal_changed(ENetPeer &p)
{
    ENetPeer *partner = partner_of(p);
    data(p)->trade_accepted = data(p)->trade_confirmed = false;
    if (!partner || !data(p)->trading) return;
    /* @note confirmed too: after one side pressed "Do The Trade!", the other could still swap their offer for nothing */
    data(*partner)->trade_accepted = data(*partner)->trade_confirmed = false;

    refresh(p, *partner, false);
    for (ENetPeer *to : { &p, partner })
    {
        overlay(*to, "The deal has changed");
        sfx(*to, "audio/tile_removed.wav");
    }
}

/* @return true if "who" can receive everything "from" offers */
static bool has_room(ENetPeer &who, ENetPeer &from)
{
    ::peer *pWho = data(who);
    int new_kinds{};
    for (const ::slot &s : data(from)->trade_items)
    {
        const short have = owned(*pWho, s.id);
        if (have + s.count > 200)
        {
            const std::string msg = std::format("`4Oops - {} `4is carrying too many {} and can't fit that many in their backpack.", pWho->display_growid, id_to_item(s.id).raw_name);
            overlay(who, msg); overlay(from, msg);
            return false;
        }
        if (have == 0 && std::ranges::find(pWho->slots, s.id, &::slot::id) == pWho->slots.end()) ++new_kinds;
    }
    if ((int)pWho->slots.size() + new_kinds > pWho->slot_size)
    {
        const std::string msg = std::format("`w{}`w needs more backpack room first!", pWho->display_growid);
        overlay(who, msg); overlay(from, msg);
        return false;
    }
    return true;
}

/* @note drop offers the owner no longer has (dropped/trashed meanwhile) */
static bool offer_valid(const ::peer &p)
{
    return std::ranges::all_of(p.trade_items, [&p](const ::slot &s) { return owned(p, s.id) >= s.count; });
}

static std::string items_for_dialog(const ::peer &p)
{
    if (p.trade_items.empty()) return "add_textbox|`oNothing``|left|\n";
    std::string text{};
    for (const ::slot &s : p.trade_items)
        text += std::format("add_label_with_icon|small|`w{}`` x`w{}``|left|{}|\n", id_to_item(s.id).raw_name, s.count, s.id);
    return text;
}

static std::string items_for_message(const ::peer &p)
{
    if (p.trade_items.empty()) return "nothing";
    std::string text{};
    for (const ::slot &s : p.trade_items)
        text += std::format("{}{} {}", text.empty() ? "" : ", ", s.count, id_to_item(s.id).raw_name);
    return text;
}

void request_trade(ENetEvent& event, ENetPeer &target)
{
    ::peer *pPeer = data(*event.peer);
    ::peer *pTarget = data(target);
    if (pTarget->user_id == pPeer->user_id) return;
    if (staff_locked(*pPeer)) return staff_locked_message(*event.peer);
    if (staff_locked(*pTarget))
    {
        send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, "`wStaff accounts can't trade.``", 0u });
        return;
    }
    if (pTarget->trading)
    {
        send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, "`wThat person is busy.``", 0u });
        return;
    }
    if (pPeer->trading) end_trade(*event.peer);

    pPeer->trade_netid = pTarget->netid;
    send_varlist(event.peer, { "OnStartTrade", pTarget->display_growid, pTarget->netid });
}

void end_trade(ENetPeer &p)
{
    ::peer *pPeer = data(p);
    if (ENetPeer *partner = partner_of(p); partner && data(*partner)->trade_netid == pPeer->netid)
    {
        const std::string msg = std::format("`6[```4Trade canceled by {}`4!```6]``", pPeer->display_growid);
        on::ConsoleMessage(partner, msg);
        overlay(*partner, msg);
        send_varlist(partner, { "OnForceTradeEnd" });
        reset(*data(*partner));
    }
    send_varlist(&p, { "OnForceTradeEnd" });
    reset(*pPeer);
}

void action::trade_started(ENetEvent& event, const std::string& header)
{
    ::peer *pPeer = data(*event.peer);
    ::hPipe hPipe{ header };
    const int netid = atoi(hPipe["netid"].c_str());
    if (netid == 0) return;

    if (staff_locked(*pPeer)) return staff_locked_message(*event.peer);
    pPeer->trade_netid = netid;
    ENetPeer *partner = partner_of(*event.peer);
    if (!partner) return;
    if (staff_locked(*data(*partner))) { pPeer->trade_netid = 0; return; }

    ::peer *pPartner = data(*partner);
    if (pPartner->trade_netid == pPeer->netid) // @note both opened the window, start!
    {
        pPeer->trading = pPartner->trading = true;
        pPeer->trade_accepted = pPartner->trade_accepted = false;
        pPeer->trade_confirmed = pPartner->trade_confirmed = false;
        if (!offer_valid(*pPeer)) pPeer->trade_items.clear();
        if (!offer_valid(*pPartner)) pPartner->trade_items.clear();
        refresh(*event.peer, *partner, false);
        return;
    }
    on::ConsoleMessage(partner, std::format("`#TRADE ALERT:`` {} `owants to trade with you!  To start, use the `wWrench`` on that person and press `wTrade``.", pPeer->display_growid));
    sfx(*partner, "audio/cash_register.wav");
}

void action::mod_trade(ENetEvent& event, const std::string& header)
{
    ::peer *pPeer = data(*event.peer);
    ::hPipe hPipe{ header };
    const short id = static_cast<short>(atoi(hPipe["itemID"].c_str()));

    if (staff_locked(*pPeer)) return;
    const short have = owned(*pPeer, id);
    if (id == 18 || id == 32 || have <= 0 || (id_to_item(id).cat & CAT_UNTRADEABLE)) // @note fist & wrench, untradeables
    {
        overlay(*event.peer, "You'd be sorry if you lost that!");
        sfx(*event.peer, "audio/cant_place_tile.wav");
        return;
    }
    if (pPeer->trade_items.size() >= 4 && std::ranges::find(pPeer->trade_items, id, &::slot::id) == pPeer->trade_items.end())
    {
        sfx(*event.peer, "audio/cant_place_tile.wav");
        return;
    }
    if (have == 1)
    {
        std::erase_if(pPeer->trade_items, [id](const ::slot &s) { return s.id == id; });
        pPeer->trade_items.emplace_back(id, 1);
        deal_changed(*event.peer);
        return;
    }
    send_varlist(event.peer, {
        "OnDialogRequest",
        std::format(
            "set_default_color|`o\n"
            "add_label_with_icon|big|`2Trade`` `w{}``|left|{}|\n"
            "add_textbox|`2Trade how many?``|left|\n"
            "add_text_input|count||{}|5|\n"
            "embed_data|itemID|{}\n"
            "end_dialog|trade_add|Cancel|OK|\n",
            id_to_item(id).raw_name, id, have, id
        )
    });
}

void trade_add(ENetEvent& event, const ::hPipe &hPipe)
{
    ::peer *pPeer = data(*event.peer);
    const short id = static_cast<short>(atoi(hPipe["itemID"].c_str()));
    if (staff_locked(*pPeer)) return;
    const int count = std::clamp(atoi(hPipe["count"].c_str()), 0, (int)owned(*pPeer, id));
    if (id == 18 || id == 32 || count <= 0 || (id_to_item(id).cat & CAT_UNTRADEABLE)) return; // @note a faked dialog skips mod_trade's check
    if (pPeer->trade_items.size() >= 4 && std::ranges::find(pPeer->trade_items, id, &::slot::id) == pPeer->trade_items.end()) return;

    std::erase_if(pPeer->trade_items, [id](const ::slot &s) { return s.id == id; });
    pPeer->trade_items.emplace_back(id, static_cast<short>(count));
    deal_changed(*event.peer);
}

void action::rem_trade(ENetEvent& event, const std::string& header)
{
    ::peer *pPeer = data(*event.peer);
    ::hPipe hPipe{ header };
    const short id = static_cast<short>(atoi(hPipe["itemID"].c_str()));

    std::erase_if(pPeer->trade_items, [id](const ::slot &s) { return s.id == id; });
    deal_changed(*event.peer);
}

void action::trade_accept(ENetEvent& event, const std::string& header)
{
    ::peer *pPeer = data(*event.peer);
    ::hPipe hPipe{ header };
    ENetPeer *partner = partner_of(*event.peer);
    if (!pPeer->trading || !partner) return;
    ::peer *pPartner = data(*partner);

    pPeer->trade_accepted = hPipe["status"] == "1";
    refresh(*event.peer, *partner, true);
    if (!pPeer->trade_accepted || !pPartner->trade_accepted) return;

    if (!has_room(*event.peer, *partner) || !has_room(*partner, *event.peer))
    {
        pPeer->trade_accepted = pPartner->trade_accepted = false;
        refresh(*event.peer, *partner, true);
        return;
    }

    /* @note both accepted: close the windows and ask both to confirm */
    for (ENetPeer *me : { event.peer, partner })
    {
        ENetPeer *them = (me == event.peer) ? partner : event.peer;
        const bool gets_nothing = data(*them)->trade_items.empty();
        send_varlist(me, { "OnForceTradeEnd" });
        send_varlist(me, {
            "OnDialogRequest",
            std::format(
                "set_default_color|`o\n"
                "add_label_with_icon|big|`wTrade Confirmation``|left|1366|\n"
                "add_spacer|small|\n"
                "add_textbox|`4You'll give:``|left|\n"
                "{}"
                "add_spacer|small|\n"
                "add_textbox|`2You'll get:``|left|\n"
                "{}"
                "add_spacer|small|\n"
                "{}"
                "add_button|accept|Do The Trade!|noflags|0|0|\n"
                "add_button|back|Cancel|noflags|0|0|\n"
                "end_dialog|trade_confirm|||\n",
                items_for_dialog(*data(*me)), items_for_dialog(*data(*them)),
                gets_nothing ? "add_textbox|`4SCAM WARNING: ``You are about to do a trade without receiving anything in return. Once you do the trade you cannot get the items back.|left|\nadd_spacer|small|\n" : ""
            )
        });
    }
}

void trade_confirm(ENetEvent& event, const ::hPipe &hPipe)
{
    ::peer *pPeer = data(*event.peer);
    if (hPipe["buttonClicked"] != "accept")
    {
        end_trade(*event.peer);
        return;
    }
    ENetPeer *partner = partner_of(*event.peer);
    if (!pPeer->trading || !partner)
    {
        overlay(*event.peer, "The other person left the trade!");
        reset(*pPeer);
        return;
    }
    ::peer *pPartner = data(*partner);
    /* @note the confirmation window is for the deal both accepted: a change since then un-accepted it */
    if (!pPeer->trade_accepted || !pPartner->trade_accepted)
    {
        overlay(*event.peer, "`4The deal has changed.`` Trade again to see the new offer.");
        end_trade(*event.peer);
        return;
    }

    pPeer->trade_confirmed = true;
    if (!pPartner->trade_confirmed)
    {
        overlay(*event.peer, std::format("Waiting for {}`w to confirm...", pPartner->display_growid));
        return;
    }

    /* @note check everything again right before swapping */
    if (!offer_valid(*pPeer) || !offer_valid(*pPartner) || !has_room(*event.peer, *partner) || !has_room(*partner, *event.peer))
    {
        overlay(*event.peer, "`4The trade failed.``");
        overlay(*partner, "`4The trade failed.``");
        reset(*pPeer); reset(*pPartner);
        return;
    }

    ENetEvent mine{ .peer = event.peer }, theirs{ .peer = partner };
    for (const ::slot &s : pPeer->trade_items)
    {
        modify_item_inventory(mine,   ::slot{ s.id, static_cast<short>(-s.count) });
        modify_item_inventory(theirs, ::slot{ s.id, s.count });
    }
    for (const ::slot &s : pPartner->trade_items)
    {
        modify_item_inventory(theirs, ::slot{ s.id, static_cast<short>(-s.count) });
        modify_item_inventory(mine,   ::slot{ s.id, s.count });
    }

    const std::string msg1 = std::format("`1{} `1traded {} to {}`1.``", pPeer->display_growid, items_for_message(*pPeer), pPartner->display_growid);
    const std::string msg2 = std::format("`1{} `1traded {} to {}`1.``", pPartner->display_growid, items_for_message(*pPartner), pPeer->display_growid);
    peers(pPeer->recent_worlds.back(), PEER_SAME_WORLD, [&](ENetPeer &p)
    {
        on::ConsoleMessage(&p, msg1);
        on::ConsoleMessage(&p, msg2);
    });
    sfx(*event.peer, "audio/keypad_hit.wav");
    sfx(*partner, "audio/keypad_hit.wav");

    /* @note a World Key changes the world's owner (tools/world_key.cpp) */
    if (std::ranges::find(pPeer->trade_items, world_key_id(), &::slot::id) != pPeer->trade_items.end()) world_key_traded(*event.peer, *partner);
    if (std::ranges::find(pPartner->trade_items, world_key_id(), &::slot::id) != pPartner->trade_items.end()) world_key_traded(*partner, *event.peer);

    log_trade(pPeer->growid, pPartner->growid, items_for_message(*pPeer), items_for_message(*pPartner));
    add_stat(mine, "trades");
    add_stat(theirs, "trades");

    reset(*pPeer); reset(*pPartner);
}

void action::trade_cancel(ENetEvent& event, const std::string& header)
{
    end_trade(*event.peer);
}
