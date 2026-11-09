#include "pch.hpp"

#include "discord.hpp"

namespace
{
    constexpr std::time_t STATUS_EVERY = 30;   // @note the bot calls the server offline after 90 s without an update
    constexpr std::time_t OUTBOX_KEEP  = 3600;
    constexpr std::size_t MAX_TEXT     = 500;  // @note `text` column is VARCHAR(512)

    std::time_t next_status = 0;
    const std::time_t started = std::time(nullptr);
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
    const signed updated = static_cast<signed>(now), since = static_cast<signed>(started);
    {
        ::hStmt hStmt{ "REPLACE INTO server_status (id, updated, online, started) VALUES (1, ?, ?, ?)" };
        MYSQL_BIND params[3] = { make_bind_in(updated), make_bind_in(online), make_bind_in(since) };
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
}
