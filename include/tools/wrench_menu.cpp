#include "pch.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "tools/profile.hpp"
#include "tools/social.hpp"
#include "tools/duct_tape.hpp"
#include "commands/warp.hpp" // @note got_private_message()

#include "wrench_menu.hpp"

static constexpr short WORLD_LOCK = 242, DIAMOND_LOCK = 1796;

static short owned(const ::peer &p, short id)
{
    auto it = std::ranges::find(p.slots, id, &::slot::id);
    return (it == p.slots.end()) ? 0 : it->count;
}

static bool has_room(const ::peer &p, short id, int count)
{
    const short have = owned(p, id);
    if (have + count > 200) return false;
    return have > 0 || (int)p.slots.size() < p.slot_size;
}

static void sfx(ENetPeer &p, std::string_view file) { send_action(p, "play_sfx", std::format("file|{}\ndelayMS|0\n", file)); }

std::string clean_text(std::string text, std::size_t max_length)
{
    std::ranges::replace(text, '|', '/');
    std::erase_if(text, [](char c) { return c == '\n' || c == '\r'; });
    if (text.size() > max_length) text.resize(max_length);
    return text;
}

std::string online_status_label(const ::peer &peer)
{
    switch (peer.online_status)
    {
        case 1:  return "`3Busy``";
        case 2:  return "`8Away``";
        default: return "`2Online``";
    }
}

static ::peer *find_in_world(const ::peer &me, int netid, ENetPeer **out = nullptr)
{
    if (me.netid == 0) return nullptr;
    for (ENetPeer *p : peers(me.recent_worlds.back(), PEER_SAME_WORLD))
        if (static_cast<::peer*>(p->data)->netid == netid)
        {
            if (out) *out = p;
            return static_cast<::peer*>(p->data);
        }
    return nullptr;
}

static void send_notebook(ENetEvent &event)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    send_varlist(event.peer, { "OnDialogRequest", std::format(
        "set_default_color|`o\n"
        "add_label|big|`wNotebook``|left|\n"
        "add_spacer|small|\n"
        "add_textbox|Write anything you want to remember. Only you can see it.|left|\n"
        "add_text_input|notebook_text||{}|250|\n"
        "end_dialog|notebook|Cancel|Save|\n", pPeer->notebook) });
}

static void send_wl_bank(ENetEvent &event)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    send_varlist(event.peer, { "OnDialogRequest", std::format(
        "set_default_color|`o\n"
        "add_label_with_icon|big|`wWorld Lock Bank``|left|{}|\n"
        "add_spacer|small|\n"
        "add_textbox|Your bank holds `w{}`` World Locks (`w{}`` Diamond Locks + `w{}`` World Locks).|left|\n"
        "add_smalltext|You carry `w{}`` World Locks and `w{}`` Diamond Locks. Banked locks are safe and can't be traded or dropped.|left|\n"
        "add_spacer|small|\n"
        "add_text_input|deposit_wl|Deposit World Locks|0|5|\n"
        "add_text_input|deposit_dl|Deposit Diamond Locks|0|5|\n"
        "add_spacer|small|\n"
        "add_text_input|withdraw_wl|Withdraw World Locks|0|5|\n"
        "add_text_input|withdraw_dl|Withdraw Diamond Locks|0|5|\n"
        "end_dialog|wl_bank|Close|OK|\n",
        WORLD_LOCK, pPeer->wl_bank, pPeer->wl_bank / 100, pPeer->wl_bank % 100,
        owned(*pPeer, WORLD_LOCK), owned(*pPeer, DIAMOND_LOCK)) });
}

static void send_online_status(ENetEvent &event)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    send_varlist(event.peer, { "OnDialogRequest", std::format(
        "set_default_color|`o\n"
        "add_label|big|`wSet Online Status``|left|\n"
        "add_spacer|small|\n"
        "add_textbox|Other players see this when they wrench you.|left|\n"
        "add_checkbox|status_online|`2Online``|{}\n"
        "add_checkbox|status_busy|`3Busy``|{}\n"
        "add_checkbox|status_away|`8Away``|{}\n"
        "end_dialog|online_status|Cancel|OK|\n",
        to_char(pPeer->online_status == 0), to_char(pPeer->online_status == 1), to_char(pPeer->online_status == 2)) });
}

bool wrench_menu_button(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const std::string clicked = hPipe["buttonClicked"];

    if (profile_button(event, clicked))      return true; // @note achievements, goals, grow pass, wardrobe, pets...
    if (clicked == "trade_scan")             { send_trade_history(event); return true; }
    if (clicked == "notebook_edit")          { send_notebook(event); return true; }
    if (clicked == "open_worldlock_storage") { send_wl_bank(event); return true; }
    if (clicked == "set_online_status")      { send_online_status(event); return true; }

    if (clicked == "show_clothes" || clicked == "sendpm" || clicked == "friend_add" || clicked == "ignore_player" || clicked == "report_player")
    {
        ::peer *pOthers = find_in_world(*pPeer, atoi(hPipe["netID"].c_str()));
        if (pOthers == nullptr) { on::ConsoleMessage(event.peer, "`4That player isn't here anymore.``"); return true; }

        if (clicked == "friend_add")    { friend_button(event, *pOthers); return true; }
        if (clicked == "ignore_player") { ignore_button(event, *pOthers); return true; }
        if (clicked == "report_player") { send_report_dialog(event, *pOthers); return true; }
        if (clicked == "show_clothes")
        {
            std::string list{};
            for (float cloth : pOthers->clothing)
                if (const short id = static_cast<short>(cloth); id != 0)
                    list += std::format("add_label_with_icon|small|`w{}``|left|{}|\n", id_to_item(id).raw_name, id);
            send_varlist(event.peer, { "OnDialogRequest", std::format(
                "set_default_color|`o\n"
                "add_label|big|{}'s clothes``|left|\n"
                "add_spacer|small|\n"
                "{}"
                "end_dialog|popup||OK|\n", pOthers->display_growid, list.empty() ? "add_textbox|Not wearing anything special.|left|\n" : list) });
        }
        else
        {
            send_varlist(event.peer, { "OnDialogRequest", std::format(
                "set_default_color|`o\n"
                "add_label|big|`wSend Message to ``{}|left|\n"
                "embed_data|to|{}\n"
                "add_spacer|small|\n"
                "add_text_input|message|||120|\n"
                "end_dialog|sendpm|Cancel|Send|\n", pOthers->display_growid, pOthers->growid) });
        }
        return true;
    }

    return false;
}

void notebook_return(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    pPeer->notebook = clean_text(hPipe["notebook_text"], 250);
    send_varlist(event.peer, { "OnTextOverlay", "Notebook saved." });
}

void wl_bank_return(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    auto amount = [&](const char *key) { return std::clamp(atoi(hPipe[key].c_str()), 0, 200); };
    const int dep_wl = std::min<int>(amount("deposit_wl"), owned(*pPeer, WORLD_LOCK));
    const int dep_dl = std::min<int>(amount("deposit_dl"), owned(*pPeer, DIAMOND_LOCK));
    int wd_wl = amount("withdraw_wl"), wd_dl = amount("withdraw_dl");

    /* @note deposit first, so "deposit 1 DL, withdraw 100 WL" breaks a DL into change */
    if (dep_wl > 0) modify_item_inventory(event, ::slot{ WORLD_LOCK, static_cast<short>(-dep_wl) });
    if (dep_dl > 0) modify_item_inventory(event, ::slot{ DIAMOND_LOCK, static_cast<short>(-dep_dl) });
    pPeer->wl_bank += dep_wl + dep_dl * 100;

    std::string result = std::format("`oDeposited `w{}`` WL and `w{}`` DL.``", dep_wl, dep_dl);
    if (wd_dl > 0)
    {
        wd_dl = std::min(wd_dl, pPeer->wl_bank / 100);
        if (wd_dl > 0 && !has_room(*pPeer, DIAMOND_LOCK, wd_dl)) { wd_dl = 0; result += " `4No room for the Diamond Locks.``"; }
        if (wd_dl > 0) { pPeer->wl_bank -= wd_dl * 100; modify_item_inventory(event, ::slot{ DIAMOND_LOCK, static_cast<short>(wd_dl) }); }
    }
    if (wd_wl > 0)
    {
        wd_wl = std::min(wd_wl, pPeer->wl_bank);
        if (wd_wl > 0 && !has_room(*pPeer, WORLD_LOCK, wd_wl)) { wd_wl = 0; result += " `4No room for the World Locks.``"; }
        if (wd_wl > 0) { pPeer->wl_bank -= wd_wl; modify_item_inventory(event, ::slot{ WORLD_LOCK, static_cast<short>(wd_wl) }); }
    }
    if (wd_wl > 0 || wd_dl > 0) result += std::format(" `oWithdrew `w{}`` WL and `w{}`` DL.``", std::max(wd_wl, 0), std::max(wd_dl, 0));

    on::ConsoleMessage(event.peer, std::format("{} `oBank: `w{}`` World Locks.``", result, pPeer->wl_bank));
    pPeer->mysql_update<signed>("wl_bank", pPeer->wl_bank); // @note save right away, it's money
}

void online_status_return(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    pPeer->online_status = (hPipe["status_away"] == "1") ? 2 : (hPipe["status_busy"] == "1") ? 1 : 0;
    on::ConsoleMessage(event.peer, std::format("`oYour status is now {}.``", online_status_label(*pPeer)));
}

void sendpm_return(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const std::string message = clean_text(hPipe["message"], 120);
    if (message.empty()) return;
    if (tape_blocks(*event.peer)) return; // @note duct tape: no private messages

    const std::string to = hPipe["to"];
    for (ENetPeer *p : peers())
    {
        ::peer *pOthers = static_cast<::peer*>(p->data);
        if (pOthers == nullptr || pOthers->growid != to) continue;
        if (is_ignoring(*pOthers, pPeer->growid)) break; // @note looks like offline to the sender

        on::ConsoleMessage(p, std::format("`c>> (Message from ``{}`c) {}``", pPeer->display_growid, message));
        sfx(*p, "audio/pay_time.wav");
        got_private_message(*pOthers, *pPeer); // @note /rgo
        on::ConsoleMessage(event.peer, std::format("`6>> (Sent to ``{}`6) {}``", pOthers->display_growid, message));
        return;
    }
    on::ConsoleMessage(event.peer, "`4That player is offline.``");
}
