#include "pch.hpp"
#include "tools/tutorial.hpp"
#include <fstream>
#include <unordered_set>
#include "onVariant/ConsoleMessage.hpp"
#include "database/shouhin.hpp"
#include "commands/warp.hpp"
#include "tools/random.hpp"
#include "onVariant/SetBux.hpp"
#include "tools/profile.hpp"
#include "tools/social.hpp"
#include "tools/rank.hpp"
#include "tools/wrench_menu.hpp"
#include "tools/economy.hpp"
#include "action/join_request.hpp"

#include "cps.hpp"

namespace
{
    constexpr int EVENT_LENGTH   = 30 * 60; // @note automatic events last 30 minutes...
    constexpr int EVENT_GAP      = 90 * 60; // @note ...with 90 minutes between them (one every 2 hours)
    constexpr int BROADCAST_GAP  = 10 * 60;
    constexpr int QUIZ_GAP       = 15 * 60;
    constexpr int QUIZ_TIME      = 90;
    constexpr int ONLINE_REWARD_GAP = 30 * 60;
    constexpr int ONLINE_REWARD     = 300; // @note gems, + rank bonus
    constexpr int ONLINE_REWARDS_PER_DAY = 6; // @note at most 1800 gems a day from playing time, against inflation

    event_type current = event_type::NONE;
    std::time_t event_ends{}, next_event = std::time(nullptr) + 30 * 60, next_broadcast = std::time(nullptr) + BROADCAST_GAP;
    std::size_t broadcast_index{};

    std::time_t next_stats = std::time(nullptr) + 20 * 60; // @note server stats every hour
    bool event_warned{};         // @note "event starts in 5 minutes" was sent

    std::string quiz_answer{};   // @note empty = no quiz running
    int quiz_reward{};
    std::time_t quiz_ends{}, next_quiz = std::time(nullptr) + 5 * 60, next_online_check{};
}

static std::string to_lower(std::string s) { for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }

static bool logged_in(ENetPeer &p)
{
    ::peer *pp = static_cast<::peer*>(p.data);
    return pp != nullptr && !pp->growid.empty();
}

static std::string_view event_name(event_type type)
{
    switch (type)
    {
        case event_type::GEMS:    return "`2Gems x2``";
        case event_type::XP:      return "`5XP x2``";
        case event_type::LUCKY:   return "`9Lucky Drops``";
        case event_type::SURGERY: return "`3Surgery Day``";
        default:                  return "none";
    }
}

static std::string_view event_description(event_type type)
{
    switch (type)
    {
        case event_type::GEMS:    return "Breaking blocks drops double gems!";
        case event_type::XP:      return "Everything gives double XP!";
        case event_type::LUCKY:   return "Blocks drop extra seeds!";
        case event_type::SURGERY: return "Surgery rewards are doubled and malpractice is only 15 minutes!";
        default:                  return "";
    }
}

event_type active_event() { return current; }
bool event_on(event_type type) { return current == type; }

/* @note everyone who is logged in (not on the login screen) */
static void to_all(const std::string &message, bool overlay = false)
{
    peers("", PEER_ALL, [&](ENetPeer &p)
    {
        ::peer *pp = static_cast<::peer*>(p.data);
        if (pp == nullptr || pp->growid.empty()) return;
        on::ConsoleMessage(&p, message);
        if (overlay) send_varlist(&p, { "OnTextOverlay", message });
    });
}

void system_broadcast(const std::string &message, bool notification)
{
    to_all(message);
    if (notification)
        peers("", PEER_ALL, [&](ENetPeer &p)
        {
            ::peer *pp = static_cast<::peer*>(p.data);
            if (pp && !pp->growid.empty())
                send_varlist(&p, { "OnAddNotification", "interface/science_button.rttex", message, "audio/hub_open.wav", 0 });
        });
}

static int online_count()
{
    return static_cast<int>(std::ranges::count_if(peers(), [](ENetPeer *p) { return logged_in(*p); }));
}

static int registered_count()
{
    ::hStmt hStmt{ "SELECT COUNT(*) FROM peer" };
    signed count{};
    MYSQL_BIND result = make_bind_out(count);
    mysql_stmt_bind_result(hStmt.pStmt, &result);
    hStmt.execute();
    hStmt.fetch();
    return count;
}

/* @note {online}, {players}, {worlds}, {event} and {next_event} in broadcast.txt are filled in */
static std::string fill(std::string text)
{
    const auto replace = [&text](std::string_view key, const std::function<std::string()> &value)
    {
        for (std::size_t at = text.find(key); at != std::string::npos; at = text.find(key, at))
        {
            const std::string v = value();
            text.replace(at, key.size(), v);
            at += v.size();
        }
    };
    replace("{online}", [] { return std::to_string(online_count()); });
    replace("{players}", [] { return std::to_string(registered_count()); });
    replace("{worlds}", [] { return std::to_string(worlds.size()); });
    replace("{event}", [] { return std::string{ (current == event_type::NONE) ? "`onone``" : event_name(current) }; });
    replace("{next_event}", [] { return std::to_string(std::max<long long>(0, (next_event - std::time(nullptr) + 59) / 60)); });
    return text;
}

static void start_event(event_type type, int seconds)
{
    current = type;
    event_ends = std::time(nullptr) + seconds;
    event_warned = false;
    to_all(std::format("`9** EVENT STARTED: {} `9for {} minutes! ``{}", event_name(type), seconds / 60, event_description(type)), true);
    peers("", PEER_ALL, [&](ENetPeer &p)
    {
        ::peer *pp = static_cast<::peer*>(p.data);
        if (pp && !pp->growid.empty())
            send_varlist(&p, { "OnAddNotification", "interface/science_button.rttex", std::format("`9EVENT: {} `9started!``", event_name(type)), "audio/hub_open.wav", 0 });
    });
}

static void stop_event()
{
    if (current == event_type::NONE) return;
    to_all(std::format("`9** EVENT ENDED: {}`9. Thanks for playing!``", event_name(current)));
    current = event_type::NONE;
    next_event = std::time(nullptr) + EVENT_GAP;
}

/* @note resources/broadcast.txt: one message per line, '#' = comment. edit it any time, it's read again each broadcast */
static std::vector<std::string> broadcasts()
{
    std::vector<std::string> list{};
    std::ifstream file("resources/broadcast.txt");
    for (std::string line; std::getline(file, line); )
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty() && !line.starts_with('#')) list.push_back(line);
    }
    return list;
}

/* ---------- chat quiz ---------- */

static void to_all_quiz(const std::string &question)
{
    to_all(std::format("`9** QUIZ!`` {} `9First to answer in chat wins `w{}`` gems!``", question, quiz_reward));
}

static void start_quiz()
{
    quiz_reward = RandomRange(1, 4) * 100; // @note RandomRange(a, b) never returns b: 100-300 // @note small on purpose: the economy is farming-based
    quiz_ends = std::time(nullptr) + QUIZ_TIME;
    if (RandomRange(0, 2) == 0) // @note math
    {
        const int a = RandomRange(10, 100), b = RandomRange(10, 100), c = RandomRange(2, 10);
        switch (RandomRange(0, 3))
        {
            case 0:  quiz_answer = std::to_string(a + b); to_all_quiz(std::format("What is `w{} + {}``?", a, b)); break;
            case 1:  quiz_answer = std::to_string(a * c); to_all_quiz(std::format("What is `w{} x {}``?", a, c)); break;
            default: quiz_answer = std::to_string(a + b * c); to_all_quiz(std::format("What is `w{} + {} x {}``?", a, b, c)); break;
        }
    }
    else // @note unscramble a growtopia word
    {
        constexpr std::array<std::string_view, 16> words{ "growtopia", "diamond", "surgery", "vending", "weather", "dragon",
            "pinata", "rainbow", "lockdown", "magplant", "wrench", "farmable", "growtoken", "backpack", "caduceus", "megaphone" };
        std::string word{ words[RandomRange(0, words.size())] };
        std::string scrambled = word;
        for (int tries = 0; tries < 10 && scrambled == word; ++tries)
            for (std::size_t i = scrambled.size() - 1; i > 0; --i) std::swap(scrambled[i], scrambled[RandomRange(0, i + 1)]);
        quiz_answer = word;
        to_all_quiz(std::format("Unscramble this word: `w{}``", scrambled));
    }
}

bool check_quiz(ENetEvent &event, const std::string &text)
{
    if (quiz_answer.empty() || to_lower(text) != quiz_answer) return false;
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const std::string answer = quiz_answer;
    quiz_answer.clear();

    pPeer->gems += quiz_reward;
    econ("chat quiz", quiz_reward);
    on::SetBux(event);
    send_action(*event.peer, "play_sfx", "file|audio/cash_register.wav\ndelayMS|0\n");
    to_all(std::format("`9** QUIZ:`` {} `9answered `w{}`` first and won `w{}`` gems!``", pPeer->display_growid, answer, quiz_reward));
    return true;
}

/* ---------- online reward ---------- */

static void online_rewards(std::time_t now)
{
    for (ENetPeer *p : peers())
    {
        if (!logged_in(*p)) continue;
        ::peer *pp = static_cast<::peer*>(p->data);
        const int mark = pget(*pp, "online_mark");
        if (mark == 0 || now - mark < ONLINE_REWARD_GAP) continue;

        pset(*pp, "online_mark", static_cast<int>(now));

        /* @note only for really playing (broke, placed or harvested something since last time), not for standing AFK */
        const int activity = pget(*pp, "broken") + pget(*pp, "placed") + pget(*pp, "harvested");
        const bool active = activity - pget(*pp, "online_activity") >= 20;
        pset(*pp, "online_activity", activity);
        if (!active) { on::ConsoleMessage(p, "`oNo online reward this time: break, place or harvest something to earn it!``"); continue; }

        const int today = static_cast<int>(now / 86400);
        if (pget(*pp, "online_day") != today) pset(*pp, "online_day", today), pset(*pp, "online_count", 0);
        if (pget(*pp, "online_count") >= ONLINE_REWARDS_PER_DAY) continue; // @note daily limit reached, quietly
        pset(*pp, "online_count", pget(*pp, "online_count") + 1);

        const int gems = gem_bonus(*pp, ONLINE_REWARD);
        pp->gems += gems;
        econ("online reward", gems);
        ENetEvent e{ .peer = p };
        on::SetBux(e);
        on::ConsoleMessage(p, std::format("`2Thanks for playing!`` You got `w{}`` gems for 30 active minutes. (`w{}/{}`` today)", gems, pget(*pp, "online_count"), ONLINE_REWARDS_PER_DAY));
    }
}

namespace
{
    constexpr auto START_JOIN_DELAY = std::chrono::milliseconds(1500);

    struct start_join { ENetPeer *peer; int user_id; std::chrono::steady_clock::time_point at; };
    std::vector<start_join> start_joins;

    void start_join_tick()
    {
        const auto now = std::chrono::steady_clock::now();
        std::erase_if(start_joins, [&](const start_join &j)
        {
            if (now < j.at) return false;
            /* @note still the same player, still connected and not already in a world they picked themselves */
            ::peer *pp = static_cast<::peer*>(j.peer->data);
            if (j.peer->state == ENET_PEER_STATE_CONNECTED && pp && pp->user_id == j.user_id && pp->netid == 0)
            {
                ENetEvent e{ .peer = j.peer };
                action::join_request(e, "", "START");
            }
            return true;
        });
    }
}

void queue_start_join(ENetPeer &p)
{
    if (::peer *pp = static_cast<::peer*>(p.data); pp)
        start_joins.push_back({ &p, pp->user_id, std::chrono::steady_clock::now() + START_JOIN_DELAY });
}

void cps_tick()
{
    start_join_tick();
    const std::time_t now = std::time(nullptr);

    if (!quiz_answer.empty() && now >= quiz_ends)
    {
        to_all(std::format("`9** QUIZ:`` Time's up! The answer was `w{}``.", quiz_answer));
        quiz_answer.clear();
    }
    else if (quiz_answer.empty() && now >= next_quiz)
    {
        next_quiz = now + QUIZ_GAP;
        if (std::ranges::any_of(peers(), [](ENetPeer *p) { return logged_in(*p); })) start_quiz();
    }
    if (now >= next_online_check)
    {
        next_online_check = now + 60;
        online_rewards(now);
    }

    if (current == event_type::NONE && !event_warned && next_event - now <= 5 * 60)
    {
        event_warned = true;
        to_all("`9** A random event starts in `w5`` minutes! Get ready (gems x2, XP x2, lucky drops or surgery day).``");
    }
    if (current != event_type::NONE && now >= event_ends) stop_event();
    else if (current == event_type::NONE && now >= next_event)
        start_event(static_cast<event_type>(RandomRange(1, 5)), EVENT_LENGTH); // @note GEMS..SURGERY

    if (now >= next_broadcast)
    {
        next_broadcast = now + BROADCAST_GAP;
        if (const std::vector<std::string> list = broadcasts(); !list.empty())
            to_all(std::format("`9[SevenPS]`` {}", fill(list[broadcast_index++ % list.size()])));
    }
    if (now >= next_stats)
    {
        next_stats = now + 60 * 60;
        if (const int online = online_count(); online > 0)
            to_all(std::format("`9[SevenPS]`` `wServer stats:`` `2{}`` online, `2{}`` players registered, `2{}`` worlds open. Thanks for playing!", online, registered_count(), worlds.size()));
    }
}

void announce_level(const ::peer &p, int level)
{
    if (level % 10 == 0 || level == 125)
        system_broadcast(std::format("`9** ``{} `9reached level `w{}``{}``", p.display_growid, level, (level == 125) ? "`9, the max level! LEGEND!" : "`9!"), level >= 50);
}

void announce_achievement(const ::peer &p, std::string_view name)
{
    to_all(std::format("`9** ``{} `9unlocked the achievement `w{}``!``", p.display_growid, name));
}

bool announce_join(ENetEvent &event)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    pset(*pPeer, "online_mark", static_cast<int>(std::time(nullptr))); // @note online reward timer starts now
    pset(*pPeer, "online_activity", pget(*pPeer, "broken") + pget(*pPeer, "placed") + pget(*pPeer, "harvested"));
    int online = 0;
    peers("", PEER_ALL, [&](ENetPeer &p) { if (::peer *pp = static_cast<::peer*>(p.data); pp && !pp->growid.empty()) ++online; });

    on::ConsoleMessage(event.peer, "`o>> Type `$/start`` to visit the START world, `$/rules`` for the rules and `$/top`` to see the best players!``");
    tutorial_on_join(event); // @note the next tutorial step, until it's finished
    if (current != event_type::NONE)
        on::ConsoleMessage(event.peer, std::format("`9** Event running: {} `9({} minutes left) ``{}", event_name(current),
            (event_ends - std::time(nullptr) + 59) / 60, event_description(current)));

    std::string joined = std::format("`5** ``{} `5joined SevenPS! (`w{}`` `5online)``", pPeer->display_growid, online);
    const bool first = !pget(*pPeer, "welcomed");
    if (first) // @note first time ever
    {
        pset(*pPeer, "welcomed", 1);
        joined = std::format("`2** Welcome our new player ``{}`2! Say hi and help them out! (`w{}`` `2online)``", pPeer->display_growid, online);
        on::ConsoleMessage(event.peer, "`2Welcome to SevenPS!`` You start in the `wSTART`` world: read the signs and try the `wPARKOUR``.");
        /* @note starter pack: a lock for your first world and farmable seeds, so earning your first World Lock isn't a grind */
        for (const auto &[id, count] : { std::pair<short, short>{ 9640, 1 }, { 4585, 20 }, { 5667, 10 }, { 202, 2 } })
            modify_item_inventory(event, ::slot(id, count));
        on::ConsoleMessage(event.peer, "`9Starter pack:`` `wMy First World Lock``, `w20 Pepper Tree Seeds``, `w10 Laser Grid Seeds`` and `w2 Small Locks``. "
            "Lock a world, plant the seeds, break the blocks for gems and buy a `wWorld Lock`` (2000 gems) in the store!");
    }
    peers("", PEER_ALL, [&](ENetPeer &p)
    {
        ::peer *pp = static_cast<::peer*>(p.data);
        if (pp && !pp->growid.empty() && pp != pPeer) on::ConsoleMessage(&p, joined);
    });
    return first;
}

/* ---------- /find rule ---------- */

std::string find_blocked(const ::peer &p, short id)
{
    /* @note like real Growtopia, items aren't free: farm them, buy them in the store or trade. only staff can /find */
    if (p.role >= DEVELOPER) return ""; // @note moderators moderate, they don't spawn items
    return "Items aren't free on SevenPS! Farm them, buy them in the `2Store`` or trade with other players.";
}

/* ---------- commands ---------- */

void event_command(ENetEvent &event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    std::vector<std::string> args = readch(std::string{ text }, ' ');

    if (args.size() >= 2 && pPeer->role >= DEVELOPER)
    {
        if (args[1] == "stop") return stop_event();
        if (args[1] == "start" && args.size() >= 3)
        {
            const std::string &type = args[2];
            const event_type t =
                (type == "gems") ? event_type::GEMS : (type == "xp") ? event_type::XP :
                (type == "lucky") ? event_type::LUCKY : (type == "surgery") ? event_type::SURGERY : event_type::NONE;
            if (t == event_type::NONE) return on::ConsoleMessage(event.peer, "`4Types: gems, xp, lucky, surgery``");
            const int minutes = (args.size() >= 4) ? std::clamp(atoi(args[3].c_str()), 1, 24 * 60) : 30;
            if (current != event_type::NONE) stop_event();
            return start_event(t, minutes * 60);
        }
    }
    if (current != event_type::NONE)
        on::ConsoleMessage(event.peer, std::format("`9** Event running: {} `9({} minutes left) ``{}", event_name(current),
            (event_ends - std::time(nullptr) + 59) / 60, event_description(current)));
    else
        on::ConsoleMessage(event.peer, std::format("`oNo event right now. The next one starts in `w{}`` minutes.``", std::max<long long>(0, (next_event - std::time(nullptr) + 59) / 60)));
    if (pPeer->role >= DEVELOPER) on::ConsoleMessage(event.peer, "`6Staff:`` /event start {gems|xp|lucky|surgery} {minutes}, /event stop");
}

namespace
{
    /* @note the leaderboards: button name, column, title, how a value reads. the columns are fixed here, never from the
     * player (no SQL injection) */
    struct board { std::string_view name, column, title, unit; };
    constexpr std::array<board, 5> boards{ {
        { "level",   "level",         "Highest Level",           "level " },
        { "gems",    "gems",          "Richest in Gems",         "" },
        { "rich",    "wl_bank",       "Biggest World Lock Bank", "" },
        { "surgeon", "surgery_skill", "Best Surgeons",           "" },
        { "played",  "playtime",      "Most Played",             "" },
    } };

    std::string board_buttons()
    {
        std::string out{};
        for (const board &b : boards) out += std::format("add_button|top_{}|`w{}``|noflags|0|0|\n", b.name, b.title);
        return out;
    }

    void top_menu(ENetEvent &event)
    {
        send_varlist(event.peer, { "OnDialogRequest",
            "set_default_color|`o\n"
            "add_label_with_icon|big|`wSevenPS Leaderboards``|left|1458|\n"
            "add_textbox|Who's the best on SevenPS? Pick a list.|left|\n"
            "add_spacer|small|\n" + board_buttons() +
            "end_dialog|mt_top|Close||\n" });
    }

    std::string show(const board &b, signed value)
    {
        if (b.name == "played") return std::format("{} h", value / 3600);
        return std::format("{}{}", b.unit, value);
    }
}

void top(ENetEvent &event, const std::string_view text)
{
    std::vector<std::string> args = readch(std::string{ text }, ' ');
    const std::string what = (args.size() >= 2) ? args[1] : "";
    auto it = std::ranges::find(boards, std::string_view{ what }, &board::name);
    if (it == boards.end()) return top_menu(event); // @note plain /top: the menu
    const board &b = *it;
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    /* @note online players are only saved on logout, write their current numbers first */
    const long long now = std::time(nullptr);
    peers("", PEER_ALL, [now](ENetPeer &p)
    {
        ::peer *pp = static_cast<::peer*>(p.data);
        if (pp == nullptr || !pp->loaded) return; // @note only real logged in players (never one still logging in)
        pp->mysql_update<signed>("level", pp->level.front());
        pp->mysql_update<signed>("xp", pp->level.back());
        pp->mysql_update<signed>("gems", pp->gems);
        pp->mysql_update<signed>("surgery_skill", pp->surgery_skill);
        pp->mysql_update<signed>("wl_bank", pp->wl_bank);
        pp->mysql_update<signed>("playtime", static_cast<signed>(pp->playtime + (now - pp->login_time)));
    });

    /* @note staff accounts can spawn gems/items, they'd top every list */
    ::hStmt hStmt{ std::format("SELECT growid, {0} FROM peer WHERE banned = 0 AND role < {1} ORDER BY {0} DESC LIMIT 10", b.column, static_cast<int>(MODERATOR)) };
    char name[32]{};
    u_long length{};
    signed value{};
    MYSQL_BIND result[2]{};
    result[0].buffer_type = MYSQL_TYPE_STRING;
    result[0].buffer = name;
    result[0].buffer_length = sizeof(name);
    result[0].length = &length;
    result[1] = make_bind_out(value);
    mysql_stmt_bind_result(hStmt.pStmt, result);
    hStmt.execute();
    mysql_stmt_store_result(hStmt.pStmt);

    std::string rows{};
    for (int rank = 1; mysql_stmt_fetch(hStmt.pStmt) == 0; ++rank)
    {
        const std::string_view medal = (rank == 1) ? "`9" : (rank == 2) ? "`w" : (rank == 3) ? "`6" : "`o";
        rows += std::format("add_smalltext|{}#{}`` `w{}`` - `2{}``|left|\n", medal, rank, std::string(name, std::min<u_long>(length, sizeof(name))), show(b, value));
    }

    /* @note where you are */
    std::string mine{};
    if (pPeer->role < MODERATOR)
    {
        ::hStmt hMine{ std::format("SELECT COUNT(*) + 1 FROM peer WHERE banned = 0 AND role < {1} AND {0} > (SELECT {0} FROM peer WHERE growid = ?)", b.column, static_cast<int>(MODERATOR)) };
        MYSQL_BIND param = make_bind_in(pPeer->growid);
        hMine.bind_param(&param);
        long long place{};
        MYSQL_BIND out = make_bind_out(place);
        mysql_stmt_bind_result(hMine.pStmt, &out);
        hMine.execute();
        mysql_stmt_store_result(hMine.pStmt);
        if (mysql_stmt_fetch(hMine.pStmt) == 0) mine = std::format("add_textbox|You are `w#{}`` on this list.|left|\n", place);
    }

    send_varlist(event.peer, { "OnDialogRequest", std::format(
        "set_default_color|`o\n"
        "add_label|big|`wTop 10: {}``|left|\n"
        "add_spacer|small|\n"
        "{}"
        "add_spacer|small|\n"
        "{}"
        "add_button|top_menu|Other lists|noflags|0|0|\n"
        "end_dialog|mt_top|OK||\n", b.title, rows.empty() ? "add_textbox|Nobody yet!|left|\n" : rows, mine) });
}

void top_return(ENetEvent &event, const ::hPipe &hPipe)
{
    const std::string button = hPipe["buttonClicked"];
    if (button == "top_menu") return top_menu(event);
    if (button.starts_with("top_")) return top(event, "top " + button.substr(4));
}

void start_command(ENetEvent &event, const std::string_view text)
{
    warp(event, "warp START");
}

void msg_command(ENetEvent &event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    std::vector<std::string> args = readch(std::string{ text }, ' ');
    if (args.size() < 3) return on::ConsoleMessage(event.peer, "`oUsage: /msg {growid} {message}``");

    const std::size_t start = text.find(' ', text.find(' ') + 1);
    const std::string message = clean_text(std::string{ text.substr(start + 1) }, 120);
    for (ENetPeer *p : peers())
    {
        ::peer *pp = static_cast<::peer*>(p->data);
        if (!logged_in(*p) || to_lower(pp->growid) != to_lower(args[1])) continue;
        if (pp == pPeer) return on::ConsoleMessage(event.peer, "`4You can't message yourself.``");
        if (!is_ignoring(*pp, pPeer->growid))
        {
            on::ConsoleMessage(p, std::format("`c>> (Message from ``{}`c) {}``", pPeer->display_growid, message));
            send_action(*p, "play_sfx", "file|audio/pay_time.wav\ndelayMS|0\n");
        }
        on::ConsoleMessage(event.peer, std::format("`6>> (Sent to ``{}`6) {}``", pp->display_growid, message));
        return;
    }
    on::ConsoleMessage(event.peer, "`4That player is offline.``");
}

void online_command(ENetEvent &event, const std::string_view text)
{
    std::string names{};
    int count = 0;
    for (ENetPeer *p : peers())
        if (logged_in(*p))
        {
            ::peer *pp = static_cast<::peer*>(p->data);
            if (pp->state & S_GHOST && pp->role >= MODERATOR) continue; // @note hidden staff
            names += std::format("{}{}", names.empty() ? "" : "`o, ", pp->display_growid);
            ++count;
        }
    on::ConsoleMessage(event.peer, std::format("`o** `w{}`` players online: {}``", count, names));
}
