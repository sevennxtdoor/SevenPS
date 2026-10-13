#include "pch.hpp"
#include <unordered_map>
#include "onVariant/ConsoleMessage.hpp"

#include "weather.hpp"

int get_weather_id(u_int item_id)
{
    switch (item_id)
    {
        case 830: return 1; // @note beach (blast)
        case 934: return 2; // @note night
        case 946: return 3; // @note arid

        case 984: return 5; // @note rainy city
        case 1060: return 6; // @note harvest moon (blast)
        case 1136: return 7; // @note mars (blast)
        case 1210: return 8; // @note spooky

        case 1490: return 10; // @note nothingness
        case 1364: return 11; // @note snowy

        case 1532: return 14; // @note undersea (blast)
        case 1750: return 15; // @note warp speed

        case 2046: return 17; // @note comet
        case 2284: return 18; // @note party
        case 2744: return 19; // @note pineapples
        case 3252: return 20; // @note snowy night
        case 3446: return 21; // @note spring
        case 3534: return 22; // @note howling sky

        case 3694: return 23; // @note heatwave (24-28)
        
        case 3832: return 29; // @note stuff
        case 4242: return 30; // @note pagoda
        case 4486: return 31; // @note apocalypse
        case 4774: // @note jungle (blast)
        case 4776: return 32; // @note jungle (weather machine)
        case 4892: return 33; // @note balloon warz
        case 5000: return 34; // @note background
        case 5112: return 35; // @note autumn
        case 5654: return 36; // @note valentine's
        case 5716: return 37; // @note St. paddy's

        case 6854: return 42; // @note digital rain
        case 7380: return 43; // @note monochrome (blast)
        case 7644: return 44; // @note frozen cliffs
        case 8556: return 45; // @note surg world (blast)
        case 8738: return 46; // @note bountiful (blast)
        case 8836: return 47; // @note stargazing
        case 8896: return 48; // @note meteor shower

        case 10286: return 51; // @note celebrity hills
        case 11880: return 59; // @note plaza
        case 12054: return 60; // @note nebula
        case 12056: return 61; // @note protostar landing
        case 12408: return 62; // @note dark mountains

        case 12844: return 64; // @note Mt. growmore
        case 13004: return 65; // @note crack in reality
        case 13070: return 66; // @note 年兽 (nián) mountains

        case 13640: return 69; // @note realm of spirits
        case 13690: return 70; // @note black hole
        case 14032: return 71; // @note raining gems
        case 14094: return 72; // @note holiday heaven

        case 14598: return 76; // @note atlantis
        case 14802: return 77; // @note pinuski's petal purrfect haven
        case 14896: return 78; // @note candyland blast
        case 15150: return 79; // @note dragon's keep
        case 15240: return 80; // @note emerald city

        // @todo add newer weathers
    }
    return 0; // @note sunny
}

bool is_weather_machine(u_short item_id)
{
    const u_char t = id_to_item(item_id).type;
    return t == type::WEATHER_MACHINE || t == type::SFX_WEATHER_MACHINE || t == type::SPRITE_WEATHER_MACHINE;
}

namespace
{
    /* @note world name -> weather id set by /weather, -1 = none. cached from the `world_weather` table so it survives restarts */
    std::unordered_map<std::string, int> weather_override{};
}

static int get_override(const std::string &world_name)
{
    if (auto it = weather_override.find(world_name); it != weather_override.end()) return it->second;

    int id = -1;
    ::hStmt hStmt{ "SELECT weather FROM world_weather WHERE world = ? LIMIT 1" };
    MYSQL_BIND param = make_bind_in(world_name);
    hStmt.bind_param(&param);
    signed value{};
    MYSQL_BIND result = make_bind_out(value);
    mysql_stmt_bind_result(hStmt.pStmt, &result);
    hStmt.execute();
    mysql_stmt_store_result(hStmt.pStmt);
    if (mysql_stmt_fetch(hStmt.pStmt) == 0) id = value;

    weather_override[world_name] = id;
    return id;
}

static void set_override(const std::string &world_name, int id)
{
    weather_override[world_name] = id;
    ::hStmt hStmt{ "REPLACE INTO world_weather (world, weather) VALUES (?, ?)" };
    MYSQL_BIND params[2] = { make_bind_in(world_name), make_bind_in(id) };
    hStmt.bind_param(params);
    hStmt.execute();
}

int current_weather(const ::world &world)
{
    if (const int id = get_override(world.name); id >= 0) return id;

    for (const ::block &block : world.blocks)
        if (block.fg != 0 && (block.state[2] & S_TOGGLE) && is_weather_machine(block.fg))
            return get_weather_id(block.fg);
    return 0;
}

void clear_weather_override(const std::string &world_name)
{
    if (get_override(world_name) < 0) return; // @note nothing to clear, skip the query
    weather_override[world_name] = -1;
    ::hStmt hStmt{ "DELETE FROM world_weather WHERE world = ?" };
    MYSQL_BIND param = make_bind_in(world_name);
    hStmt.bind_param(&param);
    hStmt.execute();
}

/* /weather {id} - weather id (0-100) or a weather machine's item id.
 * world owner & staff change it for the whole world, everyone else only sees it themselves */
void weather(ENetEvent& event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    const std::string_view arg = (text.size() > strlen("weather ")) ? text.substr(strlen("weather ")) : std::string_view{};
    int id = arg.empty() ? -1 : atoi(std::string{ arg }.c_str()); // @note "/weather" alone shows the usage
    if (id > 100 && get_weather_id(id) == 0 && id != 932/*Sunny*/)
    {
        send_action(*event.peer, "log", "msg|`4That weather machine isn't supported yet.``");
        return;
    }
    if (id > 100) id = get_weather_id(id); // @note item id of a weather machine, e.g. 934 (Night)
    if (id < 0)
    {
        send_action(*event.peer, "log", "msg|`4Usage:`` /weather {id} - e.g. /weather 2 (night), /weather 0 (sunny)");
        return;
    }

    auto world = std::ranges::find(worlds, pPeer->recent_worlds.back(), &::world::name);
    const bool whole_world = pPeer->netid != 0 && world != worlds.end() &&
        ((world->owner != 0 && world->owner == pPeer->user_id) || pPeer->role >= MODERATOR);

    if (whole_world)
    {
        set_override(world->name, id);
        peers(world->name, PEER_SAME_WORLD, [id](ENetPeer &p) { send_varlist(&p, { "OnSetCurrentWeather", id }); });
        on::ConsoleMessage(event.peer, std::format("`oWeather changed to `w{}`` for the whole world.``", id));
    }
    else
    {
        send_varlist(event.peer, { "OnSetCurrentWeather", id });
        on::ConsoleMessage(event.peer, std::format("`oWeather changed to `w{}`` (only you can see it - the world owner can change it for everyone).``", id));
    }
}
