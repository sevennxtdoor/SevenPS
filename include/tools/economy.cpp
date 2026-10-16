#include "pch.hpp"
#include <map>
#include "onVariant/ConsoleMessage.hpp"

#include "economy.hpp"

namespace
{
    constexpr long long WL_PRICE = 2000; // @note gems, same as the store

    int counting_day{};
    std::map<std::string, long long, std::less<>> created{}, destroyed{}; // @note today only, reset at midnight (UTC)
}

void econ(std::string_view source, long long gems)
{
    if (gems == 0) return;
    const int today = static_cast<int>(std::time(nullptr) / 86400);
    if (today != counting_day) counting_day = today, created.clear(), destroyed.clear();

    auto &book = (gems > 0) ? created : destroyed;
    auto it = book.find(source);
    if (it == book.end()) it = book.emplace(std::string{ source }, 0).first;
    it->second += std::llabs(gems);
}

static long long gems_in_game()
{
    /* @note offline players from the database + online players from memory (saved only on logout) */
    for (ENetPeer *p : peers())
        if (::peer *pp = static_cast<::peer*>(p->data); pp && pp->loaded) pp->mysql_update<signed>("gems", pp->gems);
    ::hStmt hStmt{ "SELECT SUM(gems) FROM peer" };
    long long sum{};
    MYSQL_BIND result = make_bind_out(sum);
    mysql_stmt_bind_result(hStmt.pStmt, &result);
    hStmt.execute();
    hStmt.fetch();
    return sum; // @note online players were just written, so the database sum is complete
}

void economy_command(ENetEvent &event, const std::string_view text)
{
    if (static_cast<::peer*>(event.peer->data)->role < DEVELOPER)
        return send_action(*event.peer, "log", "msg|`4Unknown command.`` Enter `$/?`` for a list of valid commands.");

    long long in{}, out{};
    std::string rows{};
    for (const auto &[source, gems] : created)   in += gems,  rows += std::format("add_smalltext|`2+{}`` from {}|left|\n", gems, source);
    for (const auto &[source, gems] : destroyed) out += gems, rows += std::format("add_smalltext|`4-{}`` to {}|left|\n", gems, source);

    const long long total = gems_in_game();
    const long long net = in - out;
    const std::string_view health =
        (in == 0) ? "`oNo gems moved yet today.``" :
        (net > in / 2) ? "`4Inflation risk: far more gems are made than spent. Add gem sinks or lower rewards.``" :
        (net < 0) ? "`6More gems are spent than made today: prices may drop.``" :
                    "`2Healthy: gems made and spent are close.``";

    send_varlist(event.peer, { "OnDialogRequest", std::format(
        "set_default_color|`o\n"
        "add_label_with_icon|big|`wMikeTopia Economy``|left|242|\n"
        "add_spacer|small|\n"
        "add_textbox|Gems owned by all players: `w{}`` (= `w{}`` World Locks at {} gems)|left|\n"
        "add_textbox|Today: made `2{}``, spent `4{}``, net `w{}{}``|left|\n"
        "add_textbox|{}|left|\n"
        "add_spacer|small|\n"
        "{}"
        "end_dialog|economy||OK|\n",
        total, total / WL_PRICE, WL_PRICE, in, out, net >= 0 ? "+" : "", net, health,
        rows.empty() ? "add_smalltext|Nothing yet.|left|\n" : rows) });
}

/* ---------- staff accounts ---------- */

bool staff_locked(const ::peer &p) { return p.role >= MODERATOR && p.role < OWNER; }

void staff_locked_message(ENetPeer &p)
{
    send_varlist(&p, { "OnTextOverlay", "`4Staff accounts can't give items or gems to players.``" });
    on::ConsoleMessage(&p, "`4Staff accounts can't drop, trade, use vending machines or /pay`` - this keeps the economy fair. Use a normal account to play.");
}

void staff_log(const ::peer &staff, std::string_view action, std::string_view target, int item, int amount)
{
    ::hStmt hStmt{ "INSERT INTO staff_log (time, staff, action, target, item, amount) VALUES (?, ?, ?, ?, ?, ?)" };
    const signed now = static_cast<signed>(std::time(nullptr));
    const std::string a{ action }, t{ target };
    MYSQL_BIND params[6] = { make_bind_in(now), make_bind_in(staff.growid), make_bind_in(a), make_bind_in(t), make_bind_in(item), make_bind_in(amount) };
    hStmt.bind_param(params);
    hStmt.execute();
}

void stafflog_command(ENetEvent &event, const std::string_view text)
{
    if (static_cast<::peer*>(event.peer->data)->role < OWNER)
        return send_action(*event.peer, "log", "msg|`4Unknown command.`` Enter `$/?`` for a list of valid commands.");

    /* @note optional filter: /stafflog {growid}. bound as a parameter, never pasted into the SQL */
    std::vector<std::string> args = readch(std::string{ text }, ' ');
    const std::string who = (args.size() >= 2) ? args[1] : "";
    ::hStmt hStmt{ who.empty() ?
        "SELECT time, staff, action, target, item, amount FROM staff_log ORDER BY id DESC LIMIT 30" :
        "SELECT time, staff, action, target, item, amount FROM staff_log WHERE staff = ? ORDER BY id DESC LIMIT 30" };
    MYSQL_BIND param{};
    if (!who.empty()) { param = make_bind_in(who); hStmt.bind_param(&param); }

    signed time{}, item{}, amount{};
    char staff[32]{}, action[24]{}, target[32]{};
    u_long staff_len{}, action_len{}, target_len{};
    MYSQL_BIND result[6]{};
    result[0] = make_bind_out(time);
    result[1].buffer_type = MYSQL_TYPE_STRING; result[1].buffer = staff;  result[1].buffer_length = sizeof(staff);  result[1].length = &staff_len;
    result[2].buffer_type = MYSQL_TYPE_STRING; result[2].buffer = action; result[2].buffer_length = sizeof(action); result[2].length = &action_len;
    result[3].buffer_type = MYSQL_TYPE_STRING; result[3].buffer = target; result[3].buffer_length = sizeof(target); result[3].length = &target_len;
    result[4] = make_bind_out(item);
    result[5] = make_bind_out(amount);
    mysql_stmt_bind_result(hStmt.pStmt, result);
    hStmt.execute();
    mysql_stmt_store_result(hStmt.pStmt);

    std::string rows{};
    while (mysql_stmt_fetch(hStmt.pStmt) == 0)
    {
        const std::time_t t = time;
        char when[20]{};
        std::strftime(when, sizeof(when), "%d/%m %H:%M", std::localtime(&t));
        const std::string s(staff, std::min<u_long>(staff_len, sizeof(staff))), a(action, std::min<u_long>(action_len, sizeof(action))),
                          tg(target, std::min<u_long>(target_len, sizeof(target)));
        const std::string what = (item > 0) ? std::format("{} x{}", id_to_item(static_cast<u_short>(item)).raw_name, amount) : std::format("{}", amount);
        rows += std::format("add_smalltext|`o{}`` `w{}`` {} {}{}|left|\n", when, s, a, what, tg.empty() ? "" : std::format(" -> `w{}``", tg));
    }
    send_varlist(event.peer, { "OnDialogRequest", std::format(
        "set_default_color|`o\n"
        "add_label_with_icon|big|`wStaff Log``{}|left|32|\n"
        "add_smalltext|Last 30 item & gem spawns by staff. Filter: /stafflog {{growid}}|left|\n"
        "add_spacer|small|\n"
        "{}"
        "end_dialog|stafflog||OK|\n", who.empty() ? "" : std::format(" ({})", who), rows.empty() ? "add_smalltext|Nothing logged.|left|\n" : rows) });
}
