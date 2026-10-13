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
        if (::peer *pp = static_cast<::peer*>(p->data); pp && !pp->growid.empty()) pp->mysql_update<signed>("gems", pp->gems);
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
