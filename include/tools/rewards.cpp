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
#include "tools/rank.hpp"
#include <map>

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

    /* ---------- prize tables: resources/daily_spin.txt & resources/lucky_box.txt ---------- */

    struct spin_prize { short id; int min, max, weight; bool rare; };
    struct prize_table { const char *path; std::vector<spin_prize> prizes{}; int price{}; std::filesystem::file_time_type at{}; };
    prize_table daily_spin{ "resources/daily_spin.txt" }, lucky_box{ "resources/lucky_box.txt" };

    /* @note read again whenever the file changes */
    void load(prize_table &table)
    {
        std::error_code ec;
        const auto at = std::filesystem::last_write_time(table.path, ec);
        if (ec || at == table.at) return;
        table.at = at;
        table.prizes.clear();
        std::ifstream file(table.path);
        for (std::string line; std::getline(file, line); )
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line.starts_with('#')) continue;
            const std::vector<std::string> v = readch(line, '|');
            if (v.size() >= 2 && v[0] == "price") table.price = std::max(0, atoi(v[1].c_str()));
            if (v.size() < 5 || v[0] != "prize") continue;
            spin_prize sp{ static_cast<short>(atoi(v[1].c_str())), atoi(v[2].c_str()), atoi(v[3].c_str()), atoi(v[4].c_str()), v.size() > 6 && v[6] == "rare" };
            if (sp.id > 0 && sp.min > 0 && sp.max >= sp.min && sp.weight > 0) table.prizes.emplace_back(sp);
        }
    }

    /* @return a random prize (nullptr if the table is empty) */
    const spin_prize *roll(prize_table &table)
    {
        load(table);
        int total = 0;
        for (const spin_prize &sp : table.prizes) total += sp.weight;
        if (total == 0) return nullptr;
        int pick = RandomRange(0, total);
        for (const spin_prize &sp : table.prizes) if ((pick -= sp.weight) < 0) return &sp;
        return nullptr;
    }

    std::string prize_name(short id, int count)
    {
        return (id == GEMS) ? std::format("{} gems", count) : std::format("{} {}", count, id_to_item(id).raw_name);
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

namespace
{
    /* @note the owner's half-made code while the /code window is open (user id -> draft) */
    struct draft { std::string code{}; int gems{}, uses{ 10 }, days{ 7 }, amount{ 1 }; std::vector<gift> items{}; };
    std::unordered_map<int, draft> drafts{};

    std::string random_code()
    {
        constexpr std::string_view letters = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"; // @note no 0/O, 1/I
        std::string code{};
        for (int i = 0; i < 8; ++i) code += letters[RandomRange(0, static_cast<int>(letters.size()))];
        return code;
    }

    std::string prize_text(int gems, const std::vector<gift> &list)
    {
        std::string text = items_text(list);
        if (gems > 0) text = std::format("{} gems{}{}", gems, text.empty() ? "" : ", ", text);
        return text.empty() ? "nothing yet" : text;
    }

    /* @return an error, empty when the code was made */
    std::string create_code(::peer &p, const std::string &code, int gems, const std::vector<gift> &list, int uses, int days)
    {
        if (!valid_code(code)) return "A code is 3-20 letters/numbers.";
        if (gems < 0 || gems > 1000000) return "Gems: 0 - 1,000,000.";
        if (uses < 1 || uses > 100000) return "Players: 1 - 100,000.";
        if (days < 0 || days > 365) return "Days: 0 - 365 (0 = forever).";
        if (gems == 0 && list.empty()) return "Give it some gems or an item.";
        if (find_code(code)) return std::format("Code {} already exists.", code);

        const long long expires = (days == 0) ? 0 : now() + days * 86400LL;
        std::string stored{};
        for (const gift &g : list) stored += std::format("{}{}:{}", stored.empty() ? "" : ",", g.id, g.count);
        ::hStmt hStmt{ "INSERT INTO redeem_code (code, gems, items, uses_left, expires, created_by) VALUES (?, ?, ?, ?, ?, ?)" };
        MYSQL_BIND params[6] = { make_bind_in(code), make_bind_in(gems), make_bind_in(stored), make_bind_in(uses), make_bind_in(expires), make_bind_in(p.growid) };
        hStmt.bind_param(params);
        hStmt.execute();
        staff_log(p, "/code create", code, list.empty() ? 0 : list.front().id, gems);
        return "";
    }

    bool delete_code(const std::string &code)
    {
        ::hStmt hStmt{ "DELETE FROM redeem_code WHERE code = ?" };
        MYSQL_BIND param = make_bind_in(code);
        hStmt.bind_param(&param);
        hStmt.execute();
        return mysql_stmt_affected_rows(hStmt.pStmt) > 0;
    }

    std::vector<code_row> all_codes()
    {
        std::vector<code_row> out{};
        ::hStmt hStmt{ "SELECT code, gems, items, uses_left, expires FROM redeem_code ORDER BY code LIMIT 30" };
        char code[32]{}, text[256]{};
        u_long code_length{}, text_length{};
        code_row row{};
        MYSQL_BIND result[5] = { {}, make_bind_out(row.gems), {}, make_bind_out(row.uses_left), make_bind_out(row.expires) };
        const auto string_out = [](MYSQL_BIND &bind, char *buffer, u_long size, u_long &length) {
            bind.buffer_type = MYSQL_TYPE_STRING, bind.buffer = buffer, bind.buffer_length = size - 1, bind.length = &length;
        };
        string_out(result[0], code, sizeof(code), code_length);
        string_out(result[2], text, sizeof(text), text_length);
        mysql_stmt_bind_result(hStmt.pStmt, result);
        hStmt.execute();
        mysql_stmt_store_result(hStmt.pStmt);
        while (mysql_stmt_fetch(hStmt.pStmt) == 0)
        {
            row.code.assign(code, std::min<u_long>(code_length, sizeof(code) - 1));
            row.items.assign(text, std::min<u_long>(text_length, sizeof(text) - 1));
            out.emplace_back(row);
        }
        return out;
    }

    std::string time_text(long long expires)
    {
        if (expires == 0) return "forever";
        if (now() > expires) return "`4expired``";
        const long long left = expires - now();
        return (left >= 86400) ? std::format("{}d left", left / 86400) : std::format("{}h left", left / 3600 + 1);
    }

    void codes_dialog(ENetEvent &event, const std::string &note = "")
    {
        std::string body =
            "set_default_color|`o\n"
            "add_label_with_icon|big|`wRedeem Codes``|left|1486|\n";
        if (!note.empty()) body += std::format("add_textbox|{}|left|\n", note);
        body += "add_button|new|`2+ Make a new code``|noflags|0|0|\n"
                "add_spacer|small|\n";
        const std::vector<code_row> codes = all_codes();
        if (codes.empty()) body += "add_textbox|No codes yet. Players type `w/redeem CODE`` (or Store > Redeem Code).|left|\n";
        else body += "add_textbox|Players type `w/redeem CODE`` (or Store > Redeem Code). Your codes:|left|\n";
        for (const code_row &c : codes)
        {
            std::vector<gift> list{};
            parse_items(c.items, list);
            body += std::format("add_label_with_icon|small|`w{}`` - {} - `w{}`` left - {}|left|1486|\n", c.code, prize_text(c.gems, list), c.uses_left, time_text(c.expires));
            body += std::format("add_button|del_{0}|`4Delete {0}``|noflags|0|0|\n", c.code);
        }
        body += "end_dialog|mt_code|Close||\n";
        send_varlist(event.peer, { "OnDialogRequest", body });
    }

    void new_code_dialog(ENetEvent &event, const draft &d, const std::string &note = "")
    {
        std::string body = std::format(
            "set_default_color|`o\n"
            "add_label_with_icon|big|`wNew Redeem Code``|left|1486|\n"
            "{}"
            "add_text_input|code|Code (leave empty = random)|{}|20|\n"
            "add_text_input|gems|Gems|{}|7|\n"
            "add_text_input|uses|How many players can use it|{}|6|\n"
            "add_text_input|days|Days it works (0 = forever)|{}|3|\n"
            "add_spacer|small|\n"
            "add_textbox|Prize: `2{}``|left|\n"
            "add_text_input|amount|Amount of the next item|{}|3|\n"
            "add_item_picker|additem|`wAdd an item``|Choose an item for the code (you keep yours)!|\n"
            "{}"
            "add_spacer|small|\n"
            "add_button|create|`2Create the code``|noflags|0|0|\n"
            "add_button|back|Back|noflags|0|0|\n"
            "embed_data|step|new\n"
            "end_dialog|mt_code|Cancel||\n",
            note.empty() ? "" : std::format("add_textbox|`4{}``|left|\n", note),
            d.code, d.gems, d.uses, d.days, prize_text(d.gems, d.items), d.amount,
            d.items.empty() ? "" : "add_button|clear|`4Remove the items``|noflags|0|0|\n");
        send_varlist(event.peer, { "OnDialogRequest", body });
    }
}

void code_command(ENetEvent &event, const std::string_view)
{
    if (me(event).role < OWNER) return on::ConsoleMessage(event.peer, "`4Only the owner can make redeem codes.``");
    codes_dialog(event);
}

void code_return(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer &p = me(event);
    if (p.role < OWNER) return;
    const std::string button = hPipe["buttonClicked"];

    if (hPipe["step"] != "new")
    {
        if (button == "new") { drafts[p.user_id] = draft{}; return new_code_dialog(event, drafts[p.user_id]); }
        if (button.starts_with("del_")) // @note asks first: a Delete button sits under every code, one stray tap deleted a code
        {
            const std::string code = upper(button.substr(4));
            return send_varlist(event.peer, {
                "OnDialogRequest",
                std::format(
                    "set_default_color|`o\n"
                    "add_label_with_icon|big|`4Delete {0}?``|left|1486|\n"
                    "add_textbox|Players won't be able to redeem `w{0}`` anymore.|left|\n"
                    "add_button|yesdel_{0}|`4Yes, delete it``|noflags|0|0|\n"
                    "add_button|back|No, keep it|noflags|0|0|\n"
                    "end_dialog|mt_code|||\n", code)
            });
        }
        if (button.starts_with("yesdel_"))
        {
            const std::string code = upper(button.substr(7));
            const bool deleted = delete_code(code);
            if (deleted) staff_log(p, "/code delete", code, 0, 0);
            return codes_dialog(event, deleted ? std::format("`2Code `w{}`` deleted.``", code) : "");
        }
        if (button == "back") return codes_dialog(event);
        return;
    }

    draft &d = drafts[p.user_id];
    d.code = upper(hPipe["code"]);
    std::erase(d.code, ' ');
    d.gems = std::max(0, atoi(hPipe["gems"].c_str()));
    d.uses = atoi(hPipe["uses"].c_str());
    d.days = atoi(hPipe["days"].c_str());
    d.amount = std::clamp(atoi(hPipe["amount"].c_str()), 1, MAX_STACK);

    if (const int picked = atoi(hPipe["additem"].c_str()); picked > 0 && picked < static_cast<int>(items.size()) && picked != GEMS)
    {
        if (auto it = std::ranges::find(d.items, static_cast<short>(picked), &gift::id); it != d.items.end()) it->count = d.amount;
        else if (d.items.size() < 10) d.items.emplace_back(gift{ static_cast<short>(picked), d.amount });
        return new_code_dialog(event, d);
    }
    if (button == "clear") { d.items.clear(); return new_code_dialog(event, d); }
    if (button == "back") return codes_dialog(event);
    if (button != "create") return; // @note Cancel

    const std::string code = d.code.empty() ? random_code() : d.code;
    if (const std::string error = create_code(p, code, d.gems, d.items, d.uses, d.days); !error.empty()) return new_code_dialog(event, d, error);
    const std::string summary = std::format("`2Code `w{}`` made!`` Prize: `w{}``, {} player{}, {}. Share it: `w/redeem {}``",
        code, prize_text(d.gems, d.items), d.uses, d.uses == 1 ? "" : "s", time_text(d.days == 0 ? 0 : now() + d.days * 86400LL), code);
    drafts.erase(p.user_id);
    on::ConsoleMessage(event.peer, summary);
    codes_dialog(event, summary);
}

void spin_command(ENetEvent &event, const std::string_view)
{
    ::peer &p = me(event);
    if (staff_locked(p)) return on::ConsoleMessage(event.peer, "`4Staff accounts can't use the Daily Spin.``");
    const ::rank *r = get_rank(p);
    const int spins = r ? r->spins : 1; // @note Super VIP and up spin more than once a day (resources/ranks.txt spinN)
    if (pget(p, "spin_day") != today()) pset(p, "spin_day", today()), pset(p, "spin_count", 0);
    if (pget(p, "spin_count") >= spins)
    {
        std::tm t = localtime();
        const int left = (23 - t.tm_hour) * 60 + (59 - t.tm_min);
        return on::ConsoleMessage(event.peer, std::format("`oYou already spun today. Next spin in `w{}h {}m``.``", left / 60, left % 60));
    }
    const spin_prize *won = roll(daily_spin);
    if (won == nullptr) return on::ConsoleMessage(event.peer, "`4The Daily Spin has no prizes set up.``");
    const int count = RandomRange(won->min, won->max + 1);
    std::vector<gift> list{};
    if (won->id != GEMS) list.emplace_back(gift{ won->id, count });
    if (!fits(p, list)) return on::ConsoleMessage(event.peer, "`4Make room in your backpack before spinning.``");

    pset(p, "spin_count", pget(p, "spin_count") + 1);
    give(event, list, won->id == GEMS ? count : 0, "daily spin");
    const std::string prize = prize_name(won->id, count);
    const int left = spins - pget(p, "spin_count");
    bubble(event, std::format("`w[{} `wspun the `9Lucky Wheel`` and won `2{}``!]``", p.display_growid, prize));
    on::ConsoleMessage(event.peer, std::format("`9Daily Spin:`` you won `2{}``! {}", prize,
        left > 0 ? std::format("You have `w{}`` more spin{} today.", left, left == 1 ? "" : "s") : "Come back tomorrow for another spin."));
    send_action(*event.peer, "play_sfx", "file|audio/success.wav\ndelayMS|0\n");
    if (won->rare) announce(std::format("`9LUCKY! `w{}`` won `2{}`` on the Daily Spin!", p.display_growid, prize));
}

namespace
{
    int box_price(const ::peer &p)
    {
        load(lucky_box);
        const ::rank *r = get_rank(p);
        return lucky_box.price * (100 - (r ? r->gacha_discount : 0)) / 100;
    }

    void box_dialog(ENetEvent &event, const std::string &result = "")
    {
        ::peer &p = me(event);
        load(lucky_box);
        const int price = box_price(p);
        std::string pool{};
        int total = 0;
        for (const spin_prize &sp : lucky_box.prizes) total += sp.weight;
        for (const spin_prize &sp : lucky_box.prizes)
        {
            const double chance = 100.0 * sp.weight / std::max(1, total);
            pool += std::format("add_label_with_icon|small|{}{}`` `o({:.1f}%)``|left|{}|\n", sp.rare ? "`9" : "`w",
                sp.min == sp.max ? prize_name(sp.id, sp.min) : std::format("{}-{} {}", sp.min, sp.max, sp.id == GEMS ? "gems" : id_to_item(sp.id).raw_name),
                chance, sp.id);
        }
        const ::rank *r = get_rank(p);
        send_varlist(event.peer, {
            "OnDialogRequest",
            std::format(
                "set_default_color|`o\n"
                "add_label_with_icon|big|`9Lucky Box``|left|9350|\n"
                "{}"
                "add_textbox|Open a box for `${}`` gems{} and get one of these prizes. You have `${}`` gems.|left|\n"
                "add_button|open1|`2Open 1 box`` (`${}`` gems)|noflags|0|0|\n"
                "add_button|open10|`2Open 10 boxes`` (`${}`` gems)|noflags|0|0|\n"
                "add_spacer|small|\n"
                "add_textbox|`wPrizes:``|left|\n"
                "{}"
                "end_dialog|mt_lucky_box|Close||\n",
                result.empty() ? "" : std::format("add_textbox|{}|left|\nadd_spacer|small|\n", result),
                price, (r && r->gacha_discount > 0) ? std::format(" (`2{}% off`` for {})", r->gacha_discount, r->name) : "", p.gems,
                price, price * 10, pool)
        });
    }

    std::string open_boxes(ENetEvent &event, int boxes)
    {
        ::peer &p = me(event);
        const int price = box_price(p);
        if (lucky_box.prizes.empty() || price <= 0) return "`4The Lucky Box isn't set up.``";
        if (p.gems < static_cast<long long>(price) * boxes) return std::format("`4You need `${}`` gems.``", price * boxes);
        if (static_cast<int>(p.slots.size()) + 3 > p.slot_size) return "`4Make room in your backpack first (3 free slots).``";

        p.gems -= price * boxes;
        econ("lucky box", -static_cast<long long>(price) * boxes);
        std::map<short, int> got{};
        int gems_won = 0;
        for (int i = 0; i < boxes; ++i)
        {
            const spin_prize *won = roll(lucky_box);
            const int count = RandomRange(won->min, won->max + 1);
            if (won->id == GEMS) { gems_won += count; continue; }
            std::vector<gift> one{ gift{ won->id, count } };
            if (!fits(p, one)) { gems_won += price; continue; } // @note no room: the box price back
            modify_item_inventory(event, ::slot(won->id, count));
            got[won->id] += count;
            if (won->rare) announce(std::format("`9LUCKY BOX! `w{}`` got `2{}``!", p.display_growid, prize_name(won->id, count)));
        }
        if (gems_won > 0) give(event, {}, gems_won, "lucky box prizes");
        on::SetBux(event);

        std::string text{};
        if (gems_won > 0) text = std::format("{} gems", gems_won);
        for (const auto &[id, count] : got) text += std::format("{}{}", text.empty() ? "" : ", ", prize_name(id, count));
        bubble(event, std::format("`9Lucky Box``: `2{}``!", text));
        send_action(*event.peer, "play_sfx", "file|audio/success.wav\ndelayMS|0\n");
        return std::format("`2You got:`` `w{}``", text);
    }
}

void lucky_box_command(ENetEvent &event, const std::string_view)
{
    if (staff_locked(me(event))) return on::ConsoleMessage(event.peer, "`4Staff accounts can't open Lucky Boxes.``");
    box_dialog(event);
}

void lucky_box_return(ENetEvent &event, const ::hPipe &hPipe)
{
    if (staff_locked(me(event))) return;
    const std::string button = hPipe["buttonClicked"];
    if (button == "open1") return box_dialog(event, open_boxes(event, 1));
    if (button == "open10") return box_dialog(event, open_boxes(event, 10));
}
