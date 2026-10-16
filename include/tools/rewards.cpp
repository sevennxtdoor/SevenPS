#include "pch.hpp"
#include <filesystem>
#include <fstream>
#include "tools/random.hpp"
#include "tools/string.hpp"
#include "tools/profile.hpp"
#include "tools/economy.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "onVariant/SetBux.hpp"
#include "automate/holiday.hpp"

#include "rewards.hpp"

namespace
{
    constexpr short GEMS = 112;
    constexpr int MAX_STACK = 200;

    ::peer &me(ENetEvent &event) { return *static_cast<::peer*>(event.peer->data); }
    long long now() { return static_cast<long long>(std::time(nullptr)); }

    void bubble(ENetEvent &event, const std::string &text)
    {
        send_varlist(event.peer, { "OnTalkBubble", me(event).netid, text, 0u, 1u });
    }

    std::string upper(std::string s)
    {
        for (char &c : s) c = static_cast<char>(std::toupper(static_cast<u_char>(c)));
        return s;
    }

    /* @note A-Z 0-9, 3-20 characters */
    bool valid_code(const std::string &code)
    {
        return code.size() >= 3 && code.size() <= 20 && std::ranges::all_of(code, [](char c) { return std::isalnum(static_cast<u_char>(c)); });
    }

    struct gift { short id; int count; };

    /* @note "242:5,611:3" -> items. false if anything is wrong */
    bool parse_items(const std::string &text, std::vector<gift> &out)
    {
        if (text.empty()) return true;
        for (const std::string &part : readch(text, ','))
        {
            const std::size_t colon = part.find(':');
            const int id = atoi(part.substr(0, colon).c_str());
            const int count = (colon == std::string::npos) ? 1 : atoi(part.substr(colon + 1).c_str());
            if (id <= 0 || id == GEMS || id >= static_cast<int>(items.size()) || count < 1 || count > MAX_STACK) return false;
            out.emplace_back(gift{ static_cast<short>(id), count });
        }
        return out.size() <= 10;
    }

    std::string items_text(const std::vector<gift> &list)
    {
        std::string text{};
        for (const gift &g : list) text += std::format("{}{} {}", text.empty() ? "" : ", ", g.count, id_to_item(g.id).raw_name);
        return text;
    }

    /* @note room for every item (new kinds need a free slot, a stack holds 200) */
    bool fits(const ::peer &p, const std::vector<gift> &list)
    {
        int new_kinds = 0;
        for (const gift &g : list)
        {
            auto it = std::ranges::find(p.slots, g.id, &::slot::id);
            if (it == p.slots.end()) ++new_kinds;
            else if (it->count + g.count > MAX_STACK) return false;
        }
        return static_cast<int>(p.slots.size()) + new_kinds <= p.slot_size;
    }

    void give(ENetEvent &event, const std::vector<gift> &list, int gems, std::string_view source)
    {
        ::peer &p = me(event);
        for (const gift &g : list) modify_item_inventory(event, ::slot(g.id, g.count));
        if (gems > 0)
        {
            p.gems = static_cast<signed>(std::min<long long>(p.gems + static_cast<long long>(gems), std::numeric_limits<signed>::max()));
            econ(source, gems);
            on::SetBux(event);
        }
    }

    void announce(const std::string &message)
    {
        peers("", PEER_ALL, [&message](ENetPeer &p) { send_action(p, "log", "msg|" + message); });
    }

    /* ---------- redeem codes ---------- */

    struct code_row { std::string code{}, items{}; int gems{}, uses_left{}; long long expires{}; };

    std::optional<code_row> find_code(const std::string &code)
    {
        ::hStmt hStmt{ "SELECT gems, items, uses_left, expires FROM redeem_code WHERE code = ?" };
        MYSQL_BIND param = make_bind_in(code);
        hStmt.bind_param(&param);

        code_row row{ .code = code };
        char text[256]{};
        u_long length{};
        MYSQL_BIND result[4] = { make_bind_out(row.gems), {}, make_bind_out(row.uses_left), make_bind_out(row.expires) };
        result[1].buffer_type = MYSQL_TYPE_STRING;
        result[1].buffer = text;
        result[1].buffer_length = sizeof(text) - 1;
        result[1].length = &length;
        mysql_stmt_bind_result(hStmt.pStmt, result);
        hStmt.execute();
        mysql_stmt_store_result(hStmt.pStmt);
        if (mysql_stmt_fetch(hStmt.pStmt) != 0) return std::nullopt;
        row.items.assign(text, std::min<u_long>(length, sizeof(text) - 1));
        return row;
    }

    bool used_by(const std::string &code, int user_id)
    {
        ::hStmt hStmt{ "SELECT 1 FROM redeem_used WHERE code = ? AND user_id = ? LIMIT 1" };
        MYSQL_BIND params[2] = { make_bind_in(code), make_bind_in(user_id) };
        hStmt.bind_param(params);
        hStmt.execute();
        return !mysql_stmt_store_result(hStmt.pStmt) && mysql_stmt_num_rows(hStmt.pStmt) > 0;
    }

    void redeem(ENetEvent &event, std::string code)
    {
        ::peer &p = me(event);
        code = upper(code);
        if (staff_locked(p)) return on::ConsoleMessage(event.peer, "`4Staff accounts can't redeem codes.``");
        if (!valid_code(code)) return on::ConsoleMessage(event.peer, "`4That isn't a valid code.``");

        /* @note slow down guessing: one try every 3 seconds */
        const int last = pget(p, "redeem_try");
        if (now() - last < 3) return on::ConsoleMessage(event.peer, "`4Wait a moment before trying another code.``");
        pset(p, "redeem_try", static_cast<int>(now()));

        const std::optional<code_row> row = find_code(code);
        if (!row) return on::ConsoleMessage(event.peer, "`4That code doesn't exist.``");
        if (used_by(code, p.user_id)) return on::ConsoleMessage(event.peer, "`4You already redeemed this code.``");
        if (row->expires > 0 && now() > row->expires) return on::ConsoleMessage(event.peer, "`4That code has expired.``");
        if (row->uses_left <= 0) return on::ConsoleMessage(event.peer, "`4That code has been used up.``");

        std::vector<gift> list{};
        parse_items(row->items, list);
        if (!fits(p, list)) return on::ConsoleMessage(event.peer, "`4Make room in your backpack first.``");

        {
            ::hStmt hStmt{ "UPDATE redeem_code SET uses_left = uses_left - 1 WHERE code = ? AND uses_left > 0" };
            MYSQL_BIND param = make_bind_in(code);
            hStmt.bind_param(&param);
            hStmt.execute();
            if (mysql_stmt_affected_rows(hStmt.pStmt) == 0) return on::ConsoleMessage(event.peer, "`4That code has been used up.``");
        }
        {
            const signed user_id = p.user_id;
            const long long at = now();
            ::hStmt hStmt{ "INSERT INTO redeem_used (code, user_id, used) VALUES (?, ?, ?)" };
            MYSQL_BIND params[3] = { make_bind_in(code), make_bind_in(user_id), make_bind_in(at) };
            hStmt.bind_param(params);
            hStmt.execute();
        }
        give(event, list, row->gems, "redeem codes");

        std::string got = items_text(list);
        if (row->gems > 0) got = std::format("{} gems{}{}", row->gems, got.empty() ? "" : ", ", got);
        on::ConsoleMessage(event.peer, std::format("`2Code `w{}`` redeemed! You got: `w{}``", code, got));
        bubble(event, std::format("`2Code redeemed: `w{}``", got));
        send_action(*event.peer, "play_sfx", "file|audio/success.wav\ndelayMS|0\n");
    }

    /* ---------- daily spin ---------- */

    struct spin_prize { short id; int min, max, weight; bool rare; };
    std::vector<spin_prize> spin_prizes{};
    std::filesystem::file_time_type spin_at{};

    void load_spin()
    {
        std::error_code ec;
        const auto at = std::filesystem::last_write_time("resources/daily_spin.txt", ec);
        if (ec || at == spin_at) return;
        spin_at = at;
        spin_prizes.clear();
        std::ifstream file("resources/daily_spin.txt");
        for (std::string line; std::getline(file, line); )
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line.starts_with('#')) continue;
            const std::vector<std::string> v = readch(line, '|');
            if (v.size() < 5 || v[0] != "prize") continue;
            spin_prize sp{ static_cast<short>(atoi(v[1].c_str())), atoi(v[2].c_str()), atoi(v[3].c_str()), atoi(v[4].c_str()), v.size() > 6 && v[6] == "rare" };
            if (sp.id > 0 && sp.min > 0 && sp.max >= sp.min && sp.weight > 0) spin_prizes.emplace_back(sp);
        }
    }

    /* @note the day number in server time (the spin resets at local midnight) */
    int today()
    {
        std::tm t = localtime();
        t.tm_hour = 12, t.tm_min = 0, t.tm_sec = 0;
        return static_cast<int>(std::mktime(&t) / 86400);
    }
}

void redeem_command(ENetEvent &event, const std::string_view text)
{
    const std::size_t space = text.find(' ');
    if (space == std::string_view::npos) return redeem_dialog(event);
    std::string code{ text.substr(space + 1) };
    std::erase(code, ' ');
    redeem(event, code);
}

void redeem_dialog(ENetEvent &event)
{
    send_varlist(event.peer, {
        "OnDialogRequest",
        "set_default_color|`o\n"
        "add_label_with_icon|big|`wRedeem Code``|left|1486|\n"
        "add_textbox|Got a code from an event, the Discord or the owner? Type it here.|left|\n"
        "add_text_input|code|Code||20|\n"
        "end_dialog|mt_redeem|Cancel|Redeem|\n"
    });
}

void redeem_return(ENetEvent &event, const ::hPipe &hPipe)
{
    std::string code = hPipe["code"];
    std::erase(code, ' ');
    if (!code.empty()) redeem(event, code);
}

void code_command(ENetEvent &event, const std::string_view text)
{
    ::peer &p = me(event);
    if (p.role < OWNER) return on::ConsoleMessage(event.peer, "`4Only the owner can make redeem codes.``");
    std::vector<std::string> args{};
    for (const std::string &a : readch(std::string(text), ' ')) if (!a.empty()) args.emplace_back(a);
    const std::string what = args.size() > 1 ? args[1] : "";

    if (what == "create" && args.size() >= 3)
    {
        const std::string code = upper(args[2]);
        if (!valid_code(code)) return on::ConsoleMessage(event.peer, "`4A code is 3-20 letters/numbers.``");
        int gems = 0, uses = 1, days = 7;
        std::string items_arg{};
        for (std::size_t i = 3; i < args.size(); ++i)
        {
            const std::size_t eq = args[i].find('=');
            if (eq == std::string::npos) continue;
            const std::string key = args[i].substr(0, eq), value = args[i].substr(eq + 1);
            if (key == "gems") gems = atoi(value.c_str());
            else if (key == "items") items_arg = value;
            else if (key == "uses") uses = atoi(value.c_str());
            else if (key == "days") days = atoi(value.c_str());
        }
        std::vector<gift> list{};
        if (!parse_items(items_arg, list)) return on::ConsoleMessage(event.peer, "`4items= looks like 242:5,611:3 (id:count, max 200 each, max 10 items).``");
        if (gems < 0 || gems > 1000000 || uses < 1 || uses > 100000 || days < 0 || days > 365)
            return on::ConsoleMessage(event.peer, "`4gems 0-1000000, uses 1-100000, days 0-365 (0 = never ends).``");
        if (gems == 0 && list.empty()) return on::ConsoleMessage(event.peer, "`4Give it gems= or items=.``");
        if (find_code(code)) return on::ConsoleMessage(event.peer, std::format("`4Code `w{}`` already exists.``", code));

        const long long expires = (days == 0) ? 0 : now() + days * 86400LL;
        std::string stored{};
        for (const gift &g : list) stored += std::format("{}{}:{}", stored.empty() ? "" : ",", g.id, g.count);
        ::hStmt hStmt{ "INSERT INTO redeem_code (code, gems, items, uses_left, expires, created_by) VALUES (?, ?, ?, ?, ?, ?)" };
        MYSQL_BIND params[6] = { make_bind_in(code), make_bind_in(gems), make_bind_in(stored), make_bind_in(uses), make_bind_in(expires), make_bind_in(p.growid) };
        hStmt.bind_param(params);
        hStmt.execute();
        staff_log(p, "/code create", code, list.empty() ? 0 : list.front().id, gems);
        return on::ConsoleMessage(event.peer, std::format("`2Code `w{}`` made: `w{}{}{}``, {} use{}, {}.``",
            code, gems > 0 ? std::format("{} gems", gems) : "", (gems > 0 && !list.empty()) ? ", " : "", items_text(list),
            uses, uses == 1 ? "" : "s", days == 0 ? "never ends" : std::format("ends in {} day{}", days, days == 1 ? "" : "s")));
    }
    if (what == "delete" && args.size() >= 3)
    {
        const std::string code = upper(args[2]);
        ::hStmt hStmt{ "DELETE FROM redeem_code WHERE code = ?" };
        MYSQL_BIND param = make_bind_in(code);
        hStmt.bind_param(&param);
        hStmt.execute();
        return on::ConsoleMessage(event.peer, mysql_stmt_affected_rows(hStmt.pStmt) > 0 ? std::format("`2Code `w{}`` deleted.``", code) : std::format("`4No code `w{}``.``", code));
    }
    if (what == "list")
    {
        ::hStmt hStmt{ "SELECT code, gems, uses_left, expires FROM redeem_code ORDER BY expires DESC LIMIT 20" };
        char code[32]{};
        u_long length{};
        signed gems{}, uses{};
        long long expires{};
        MYSQL_BIND result[4] = { {}, make_bind_out(gems), make_bind_out(uses), make_bind_out(expires) };
        result[0].buffer_type = MYSQL_TYPE_STRING;
        result[0].buffer = code;
        result[0].buffer_length = sizeof(code) - 1;
        result[0].length = &length;
        mysql_stmt_bind_result(hStmt.pStmt, result);
        hStmt.execute();
        mysql_stmt_store_result(hStmt.pStmt);
        int n = 0;
        while (mysql_stmt_fetch(hStmt.pStmt) == 0)
        {
            ++n;
            on::ConsoleMessage(event.peer, std::format("`w{}``: {} gems, {} use(s) left, {}", std::string(code, std::min<u_long>(length, sizeof(code) - 1)), gems, uses,
                expires == 0 ? "never ends" : (now() > expires ? "`4expired``" : std::format("{} h left", (expires - now()) / 3600))));
        }
        if (n == 0) on::ConsoleMessage(event.peer, "`oNo redeem codes yet.``");
        return;
    }
    on::ConsoleMessage(event.peer, "`oUsage: `w/code create CODE gems=1000 items=242:1,611:5 uses=50 days=7`` | `w/code list`` | `w/code delete CODE``");
}

void spin_command(ENetEvent &event, const std::string_view)
{
    ::peer &p = me(event);
    if (staff_locked(p)) return on::ConsoleMessage(event.peer, "`4Staff accounts can't use the Daily Spin.``");
    if (pget(p, "spin_day") == today())
    {
        std::tm t = localtime();
        const int left = (23 - t.tm_hour) * 60 + (59 - t.tm_min);
        return on::ConsoleMessage(event.peer, std::format("`oYou already spun today. Next spin in `w{}h {}m``.``", left / 60, left % 60));
    }
    load_spin();
    int total = 0;
    for (const spin_prize &sp : spin_prizes) total += sp.weight;
    if (total == 0) return on::ConsoleMessage(event.peer, "`4The Daily Spin has no prizes set up.``");

    int pick = RandomRange(0, total);
    const spin_prize *won = nullptr;
    for (const spin_prize &sp : spin_prizes) if ((pick -= sp.weight) < 0) { won = &sp; break; }
    const int count = RandomRange(won->min, won->max + 1);
    std::vector<gift> list{};
    if (won->id != GEMS) list.emplace_back(gift{ won->id, count });
    if (!fits(p, list)) return on::ConsoleMessage(event.peer, "`4Make room in your backpack before spinning.``");

    pset(p, "spin_day", today());
    give(event, list, won->id == GEMS ? count : 0, "daily spin");
    const std::string prize = (won->id == GEMS) ? std::format("{} gems", count) : std::format("{} {}", count, id_to_item(won->id).raw_name);
    bubble(event, std::format("`w[{} `wspun the `9Lucky Wheel`` and won `2{}``!]``", p.display_growid, prize));
    on::ConsoleMessage(event.peer, std::format("`9Daily Spin:`` you won `2{}``! Come back tomorrow for another spin.", prize));
    send_action(*event.peer, "play_sfx", "file|audio/success.wav\ndelayMS|0\n");
    if (won->rare) announce(std::format("`9LUCKY! `w{}`` won `2{}`` on the Daily Spin!", p.display_growid, prize));
}
