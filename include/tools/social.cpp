#include "pch.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "action/friends.hpp"
#include "tools/profile.hpp"
#include "tools/wrench_menu.hpp"

#include "social.hpp"

static constexpr int MAX_FRIENDS = 50;

static ::peer &me(ENetEvent &event) { return *static_cast<::peer*>(event.peer->data); }

static ENetPeer *online(const std::string &growid)
{
    for (ENetPeer *p : peers())
        if (::peer *pp = static_cast<::peer*>(p->data); pp && !pp->growid.empty() && pp->growid == growid) return p;
    return nullptr;
}

static void sfx(ENetPeer &p, std::string_view file) { send_action(p, "play_sfx", std::format("file|{}\ndelayMS|0\n", file)); }

/* ---------- friends (database) ---------- */

static std::vector<std::string> friend_list(const std::string &growid)
{
    std::vector<std::string> list{};
    ::hStmt hStmt{ "SELECT b FROM friends WHERE a = ? ORDER BY b" };
    MYSQL_BIND param = make_bind_in(growid);
    hStmt.bind_param(&param);

    char name[32]{};
    u_long length = 0;
    MYSQL_BIND result{};
    result.buffer_type = MYSQL_TYPE_STRING;
    result.buffer = name;
    result.buffer_length = sizeof(name);
    result.length = &length;
    mysql_stmt_bind_result(hStmt.pStmt, &result);
    hStmt.execute();
    mysql_stmt_store_result(hStmt.pStmt);
    while (mysql_stmt_fetch(hStmt.pStmt) == 0) list.emplace_back(name, std::min<u_long>(length, sizeof(name)));
    return list;
}

int friends_online(const std::string &growid)
{
    int count = 0;
    for (const std::string &name : friend_list(growid))
        if (online(name)) ++count;
    return count;
}

bool are_friends(const std::string &a, const std::string &b)
{
    const std::vector<std::string> list = friend_list(a);
    return std::ranges::find(list, b) != list.end();
}

static void set_friends(const std::string &a, const std::string &b, bool add)
{
    for (const auto &[x, y] : { std::pair{ a, b }, std::pair{ b, a } })
    {
        ::hStmt hStmt{ add ? "INSERT IGNORE INTO friends (a, b) VALUES (?, ?)" : "DELETE FROM friends WHERE a = ? AND b = ?" };
        MYSQL_BIND params[2] = { make_bind_in(x), make_bind_in(y) };
        hStmt.bind_param(params);
        hStmt.execute();
    }
}

/* @note keep the "Friendly" achievement counter up to date for whoever is online */
static void update_friend_count(const std::string &growid)
{
    if (ENetPeer *p = online(growid))
    {
        pset(*static_cast<::peer*>(p->data), "friend_count", static_cast<int>(friend_list(growid).size()));
        ENetEvent e{ .peer = p };
        add_stat(e, "friend_count", 0); // @note re-checks achievements
    }
}

void friend_button(ENetEvent &event, ::peer &target)
{
    ::peer &p = me(event);
    if (are_friends(p.growid, target.growid))
    {
        set_friends(p.growid, target.growid, false);
        on::ConsoleMessage(event.peer, std::format("`oYou are no longer friends with {}.``", target.display_growid));
        update_friend_count(p.growid); update_friend_count(target.growid);
        return;
    }
    if (friend_list(p.growid).size() >= MAX_FRIENDS) return on::ConsoleMessage(event.peer, "`4Your friend list is full.``");
    if (is_ignoring(target, p.growid)) return on::ConsoleMessage(event.peer, "`4That player isn't accepting friend requests from you.``");

    ENetPeer *to = online(target.growid);
    if (to == nullptr) return;
    send_varlist(to, { "OnDialogRequest", std::format(
        "set_default_color|`o\n"
        "add_label|big|`wFriend Request``|left|\n"
        "embed_data|from|{}\n"
        "add_spacer|small|\n"
        "add_textbox|{} wants to be your friend!|left|\n"
        "add_button|accept|`2Accept``|noflags|0|0|\n"
        "end_dialog|mt_friend|Decline||\n", p.growid, p.display_growid) });
    sfx(*to, "audio/pay_time.wav");
    on::ConsoleMessage(event.peer, std::format("`oSent a friend request to {}.``", target.display_growid));
}

void friend_request_return(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer &p = me(event);
    const std::string from = hPipe["from"];
    if (hPipe["buttonClicked"] != "accept" || from.empty() || from == p.growid) return;

    ENetPeer *sender = online(from);
    if (sender == nullptr) return on::ConsoleMessage(event.peer, "`4That player went offline.``");
    if (friend_list(p.growid).size() >= MAX_FRIENDS || friend_list(from).size() >= MAX_FRIENDS)
        return on::ConsoleMessage(event.peer, "`4A friend list is full.``");

    set_friends(p.growid, from, true);
    on::ConsoleMessage(event.peer, std::format("`2You are now friends with {}!``", static_cast<::peer*>(sender->data)->display_growid));
    on::ConsoleMessage(sender, std::format("`2{} accepted your friend request!``", p.display_growid));
    update_friend_count(p.growid); update_friend_count(from);
}

void send_friends(ENetEvent &event, bool show_offline)
{
    ::peer &p = me(event);
    const std::vector<std::string> list = friend_list(p.growid);
    pset(p, "friend_count", static_cast<int>(list.size()));

    std::string rows{};
    int count_online = 0;
    for (const std::string &name : list)
    {
        ENetPeer *on = online(name);
        if (on) ++count_online;
        if (!on && !show_offline) continue;
        const std::string where = on ? std::format("`2online`` in `w{}``", static_cast<::peer*>(on->data)->recent_worlds.back()) : "`4offline``";
        rows += std::format("add_smalltext|`w{}`` - {}|left|\n", name, where);
        if (show_offline) rows += std::format("add_button|unfriend_{}|Remove {}|noflags|0|0|\n", name, name);
    }
    for (const std::string &name : pget_list(p, "ignored"))
        if (show_offline) rows += std::format("add_smalltext|`4[ignored]`` `w{}``|left|\nadd_button|unignore_{}|Unignore {}|noflags|0|0|\n", name, name, name);
    if (rows.empty()) rows = "add_textbox|`oNone of your friends are currently online.``|left|\n";

    send_varlist(event.peer, { "OnDialogRequest", std::format(
        "set_default_color|`o\n"
        "add_label_with_icon|big|{} of {} `wFriends Online``|left|1366|\n"
        "add_spacer|small|\n"
        "{}"
        "add_spacer|small|\n"
        "add_textbox|`oAdd friends by wrenching them and pressing \"Add as friend\".``|left|\n"
        "add_button|friend_all|Show offline and ignored too|noflags|0|0|\n"
        "add_button|back|Back|noflags|0|0|\n"
        "add_button||Close|noflags|0|0|\n"
        "end_dialog|friends|||\n"
        "add_quick_exit|\n", count_online, list.size(), rows) });
}

void friends_return(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer &p = me(event);
    const std::string clicked = hPipe["buttonClicked"];
    if (clicked == "friend_all" || clicked == "all_friends") return send_friends(event, true);
    if (clicked == "back") return action::friends(event, "");
    if (clicked.starts_with("unfriend_"))
    {
        const std::string name = clicked.substr(9);
        set_friends(p.growid, name, false);
        update_friend_count(p.growid); update_friend_count(name);
        return send_friends(event, true);
    }
    if (clicked.starts_with("unignore_"))
    {
        std::vector<std::string> ignored = pget_list(p, "ignored");
        std::erase(ignored, clicked.substr(9));
        pset_list(p, "ignored", ignored);
        return send_friends(event, true);
    }
}

/* ---------- ignore ---------- */

bool is_ignoring(const ::peer &listener, const std::string &growid)
{
    const std::vector<std::string> ignored = pget_list(listener, "ignored");
    return std::ranges::find(ignored, growid) != ignored.end();
}

void ignore_button(ENetEvent &event, ::peer &target)
{
    ::peer &p = me(event);
    std::vector<std::string> ignored = pget_list(p, "ignored");
    if (std::erase(ignored, target.growid) > 0)
        on::ConsoleMessage(event.peer, std::format("`oYou stopped ignoring {}.``", target.display_growid));
    else
    {
        if (target.role >= MODERATOR) return on::ConsoleMessage(event.peer, "`4You can't ignore staff.``");
        ignored.push_back(target.growid);
        on::ConsoleMessage(event.peer, std::format("`oYou are now ignoring {}. You won't see their chat or messages.``", target.display_growid));
    }
    pset_list(p, "ignored", ignored);
}

/* ---------- reports ---------- */

void send_report_dialog(ENetEvent &event, ::peer &target)
{
    send_varlist(event.peer, { "OnDialogRequest", std::format(
        "set_default_color|`o\n"
        "add_label|big|`wReport ``{}|left|\n"
        "embed_data|target|{}\n"
        "add_spacer|small|\n"
        "add_textbox|What did they do? Staff will see your report.|left|\n"
        "add_text_input|reason|Reason||120|\n"
        "end_dialog|mt_report|Cancel|Report|\n", target.display_growid, target.growid) });
}

void report_return(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer &p = me(event);
    const std::string target = clean_text(hPipe["target"], 18), reason = clean_text(hPipe["reason"], 120);
    if (target.empty() || reason.empty()) return on::ConsoleMessage(event.peer, "`4Write a reason for the report.``");

    const int now = static_cast<int>(std::time(nullptr));
    if (now - pget(p, "last_report") < 60) return on::ConsoleMessage(event.peer, "`4Please wait a minute before sending another report.``");
    pset(p, "last_report", now);

    const std::string world = (p.netid != 0) ? p.recent_worlds.back() : "";
    ::hStmt hStmt{ "INSERT INTO reports (reporter, target, world, reason, created_at) VALUES (?, ?, ?, ?, ?)" };
    MYSQL_BIND params[5] = { make_bind_in(p.growid), make_bind_in(target), make_bind_in(world), make_bind_in(reason), make_bind_in(now) };
    hStmt.bind_param(params);
    hStmt.execute();

    on::ConsoleMessage(event.peer, "`2Thanks, your report was sent to the staff.``");
    const std::string notice = std::format("`4[REPORT]`` `w{}`` reported `w{}`` in `w{}``: {}", p.growid, target, world, reason);
    peers("", PEER_ALL, [&notice](ENetPeer &staff)
    {
        if (::peer *s = static_cast<::peer*>(staff.data); s && s->role >= MODERATOR) on::ConsoleMessage(&staff, notice);
    });
}

/* ---------- trade history ---------- */

void log_trade(const std::string &a, const std::string &b, const std::string &a_items, const std::string &b_items)
{
    const int now = static_cast<int>(std::time(nullptr));
    ::hStmt hStmt{ "INSERT INTO trade_log (time, a, b, a_items, b_items) VALUES (?, ?, ?, ?, ?)" };
    const std::string ai = a_items.substr(0, 255), bi = b_items.substr(0, 255);
    MYSQL_BIND params[5] = { make_bind_in(now), make_bind_in(a), make_bind_in(b), make_bind_in(ai), make_bind_in(bi) };
    hStmt.bind_param(params);
    hStmt.execute();
}

void send_trade_history(ENetEvent &event)
{
    ::peer &p = me(event);
    ::hStmt hStmt{ "SELECT time, a, b, a_items, b_items FROM trade_log WHERE a = ? OR b = ? ORDER BY id DESC LIMIT 15" };
    MYSQL_BIND params[2] = { make_bind_in(p.growid), make_bind_in(p.growid) };
    hStmt.bind_param(params);

    signed time{};
    char a[32]{}, b[32]{}, ai[256]{}, bi[256]{};
    u_long la{}, lb{}, lai{}, lbi{};
    auto str = [](char *buf, std::size_t size, u_long &len) {
        MYSQL_BIND bind{};
        bind.buffer_type = MYSQL_TYPE_STRING;
        bind.buffer = buf;
        bind.buffer_length = size;
        bind.length = &len;
        return bind;
    };
    MYSQL_BIND result[5] = { make_bind_out(time), str(a, sizeof(a), la), str(b, sizeof(b), lb), str(ai, sizeof(ai), lai), str(bi, sizeof(bi), lbi) };
    mysql_stmt_bind_result(hStmt.pStmt, result);
    hStmt.execute();
    mysql_stmt_store_result(hStmt.pStmt);

    std::string rows{};
    const std::time_t now = std::time(nullptr);
    while (mysql_stmt_fetch(hStmt.pStmt) == 0)
    {
        const std::string sa(a, std::min<u_long>(la, sizeof(a))), sb(b, std::min<u_long>(lb, sizeof(b)));
        const std::string sai(ai, std::min<u_long>(lai, sizeof(ai))), sbi(bi, std::min<u_long>(lbi, sizeof(bi)));
        const bool mine = sa == p.growid;
        const long ago = static_cast<long>(now - time);
        const std::string when = (ago < 3600) ? std::format("{}m ago", ago / 60) : (ago < 86400) ? std::format("{}h ago", ago / 3600) : std::format("{}d ago", ago / 86400);
        rows += std::format("add_smalltext|`o{}`` with `w{}``: gave `4{}`` - got `2{}``|left|\n", when, mine ? sb : sa, mine ? sai : sbi, mine ? sbi : sai);
    }
    if (rows.empty()) rows = "add_textbox|You haven't traded with anyone yet.|left|\n";

    send_varlist(event.peer, { "OnDialogRequest", std::format(
        "set_default_color|`o\n"
        "add_label|big|`wTrade-Scan``|left|\n"
        "add_spacer|small|\n"
        "add_textbox|Your last 15 trades:|left|\n"
        "{}"
        "end_dialog|popup||OK|\n", rows) });
}
