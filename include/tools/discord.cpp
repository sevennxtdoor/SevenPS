#include "pch.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "tools/string.hpp"

#include "discord.hpp"

namespace
{
    constexpr std::time_t STATUS_EVERY = 30;   // @note the bot calls the server offline after 90 s without an update
    constexpr std::time_t OUTBOX_KEEP  = 3600;
    constexpr std::size_t MAX_TEXT     = 500;  // @note `text` column is VARCHAR(512)
    constexpr std::time_t LINK_WAIT    = 3;    // @note seconds between /link tries: a code can't be guessed by trying many

    std::time_t next_status = 0;
    const std::time_t started = std::time(nullptr);
    std::unordered_map<int, std::time_t> last_link_try{}; // @note user_id -> last /link {code}

    /* @note tab and newline split the `players` lines, they must never come from a name */
    std::string field(std::string_view text)
    {
        std::string out{ text };
        std::ranges::replace_if(out, [](char c) { return c == '\t' || c == '\n' || c == '\r'; }, ' ');
        return out;
    }

    /* @note local date as one number, the peak online count starts again at 0 each day */
    signed today()
    {
        const std::time_t now = std::time(nullptr);
        const std::tm *tm = std::localtime(&now);
        return tm ? (tm->tm_year + 1900) * 1000 + tm->tm_yday : 0;
    }
}

void discord_post(std::string_view channel, std::string_view text)
{
    const signed now = static_cast<signed>(std::time(nullptr));
    const std::string c{ channel }, t{ text.substr(0, MAX_TEXT) };
    ::hStmt hStmt{ "INSERT INTO discord_outbox (time, channel, text) VALUES (?, ?, ?)" };
    MYSQL_BIND params[3] = { make_bind_in(now), make_bind_in(c), make_bind_in(t) };
    hStmt.bind_param(params);
    hStmt.execute();
}

std::string discord_players()
{
    std::string lines{};
    for (ENetPeer *p : peers())
    {
        const ::peer *pp = static_cast<::peer*>(p->data);
        if (pp == nullptr || !pp->loaded) continue;
        /* @note staff under /nick show as their nick, and their real GrowID stays out (the bot can't find them by it) */
        const std::string &shown = pp->display_growid.empty() ? pp->growid : pp->display_growid;
        const std::string world = pp->netid != 0 ? pp->recent_worlds.back() : "";
        lines += std::format("{}{}\t{}\t{}", lines.empty() ? "" : "\n", field(shown), field(world), pp->nick.empty() ? field(pp->growid) : "");
    }
    return lines;
}

void discord_tick()
{
    const std::time_t now = std::time(nullptr);
    if (now < next_status) return;
    next_status = now + STATUS_EVERY;

    const signed online = static_cast<signed>(std::ranges::count_if(peers(), [](ENetPeer *p)
    {
        const ::peer *pp = static_cast<::peer*>(p->data);
        return pp != nullptr && pp->loaded;
    }));
    const signed updated = static_cast<signed>(now), since = static_cast<signed>(started), day = today(),
        loaded_worlds = static_cast<signed>(worlds.size());
    const std::string players = discord_players();
    {
        /* @note peak is set before peak_day in the UPDATE part, so it still compares with the old day */
        ::hStmt hStmt{ "INSERT INTO server_status (id, updated, online, started, players, peak, peak_day, worlds) VALUES (1, ?, ?, ?, ?, ?, ?, ?) "
            "ON DUPLICATE KEY UPDATE updated = VALUES(updated), online = VALUES(online), started = VALUES(started), players = VALUES(players), "
            "peak = IF(peak_day = VALUES(peak_day), GREATEST(peak, VALUES(online)), VALUES(online)), peak_day = VALUES(peak_day), worlds = VALUES(worlds)" };
        MYSQL_BIND params[7] = { make_bind_in(updated), make_bind_in(online), make_bind_in(since), make_bind_in(players),
            make_bind_in(online), make_bind_in(day), make_bind_in(loaded_worlds) };
        hStmt.bind_param(params);
        hStmt.execute();
    }
    {
        const signed oldest = static_cast<signed>(now - OUTBOX_KEEP);
        ::hStmt hStmt{ "DELETE FROM discord_outbox WHERE time < ?" };
        MYSQL_BIND param = make_bind_in(oldest);
        hStmt.bind_param(&param);
        hStmt.execute();
    }
    {
        const signed now_s = static_cast<signed>(now);
        ::hStmt hStmt{ "DELETE FROM discord_link_pending WHERE expires < ?" };
        MYSQL_BIND param = make_bind_in(now_s);
        hStmt.bind_param(&param);
        hStmt.execute();
    }
}

std::string discord_link(const std::string &growid, std::string_view typed)
{
    std::string code{};
    for (char c : typed) if (std::isalnum(static_cast<u_char>(c))) code += static_cast<char>(std::toupper(static_cast<u_char>(c)));
    if (code.size() != 6) return "`4That's not a link code.`` Type `w/link`` in Discord to get one (6 letters/numbers).";

    long long discord_id{};
    {
        const signed now = static_cast<signed>(std::time(nullptr));
        ::hStmt hStmt{ "SELECT discord_id FROM discord_link_pending WHERE code = ? AND expires >= ?" };
        MYSQL_BIND params[2] = { make_bind_in(code), make_bind_in(now) };
        hStmt.bind_param(params);
        MYSQL_BIND result = make_bind_out(discord_id);
        mysql_stmt_bind_result(hStmt.pStmt, &result);
        hStmt.execute();
        mysql_stmt_store_result(hStmt.pStmt);
        if (mysql_stmt_fetch(hStmt.pStmt) != 0 || discord_id <= 0)
            return "`4That code is wrong or too old.`` Codes last 10 minutes: type `w/link`` in Discord for a new one.";
    }
    {
        /* @note one GrowID per Discord account and one Discord account per GrowID: an older link of either goes */
        ::hStmt hStmt{ "DELETE FROM discord_link WHERE growid = ? OR discord_id = ?" };
        MYSQL_BIND params[2] = { make_bind_in(growid), make_bind_in(discord_id) };
        hStmt.bind_param(params);
        hStmt.execute();
    }
    {
        const signed now = static_cast<signed>(std::time(nullptr));
        ::hStmt hStmt{ "INSERT INTO discord_link (growid, discord_id, linked) VALUES (?, ?, ?)" };
        MYSQL_BIND params[3] = { make_bind_in(growid), make_bind_in(discord_id), make_bind_in(now) };
        hStmt.bind_param(params);
        hStmt.execute();
    }
    {
        ::hStmt hStmt{ "DELETE FROM discord_link_pending WHERE discord_id = ?" };
        MYSQL_BIND param = make_bind_in(discord_id);
        hStmt.bind_param(&param);
        hStmt.execute();
    }
    return "`2Linked!`` Your Discord account is now linked to this GrowID. Your Discord roles update within a minute.";
}

void link_command(ENetEvent &event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const std::vector<std::string> args = readch(std::string{ text }, ' ');

    if (args.size() < 2)
        return on::ConsoleMessage(event.peer, "`oLink your Discord account: type `w/link`` in the SevenPS Discord, then `w/link {code}`` here. `w/unlink`` removes the link.``");

    const std::time_t now = std::time(nullptr);
    std::time_t &last = last_link_try[pPeer->user_id];
    if (now - last < LINK_WAIT) return on::ConsoleMessage(event.peer, "`4Wait a few seconds`` before trying again.");
    last = now;

    on::ConsoleMessage(event.peer, discord_link(pPeer->growid, args[1]));
}

void unlink_command(ENetEvent &event, const std::string_view)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    ::hStmt hStmt{ "DELETE FROM discord_link WHERE growid = ?" };
    MYSQL_BIND param = make_bind_in(pPeer->growid);
    hStmt.bind_param(&param);
    hStmt.execute();
    on::ConsoleMessage(event.peer, mysql_stmt_affected_rows(hStmt.pStmt) > 0 ?
        "`oYour Discord account is no longer linked.``" : "`oThis GrowID isn't linked to a Discord account.``");
}
