#include "pch.hpp"
#include <filesystem>
#include <fstream>
#include <random>
#include "tools/string.hpp"
#include "tools/profile.hpp"
#include "tools/economy.hpp"
#include "automate/holiday.hpp"
#include "onVariant/ConsoleMessage.hpp"

#include "telephone.hpp"

namespace
{
    constexpr short GROWTOKEN = 1486;
    constexpr int MAX_STACK = 200;
    constexpr float NEAR_TILES = 3.0f; // @note how close you must stand to the phone (same as doors)

    /* @note Sales-Man's prices for 1 Growtoken (wiki: Growtoken / Sales-Man) */
    struct offer { const char *button; short id; short count; };
    constexpr offer sales_man[]{
        { "sm_caduceus", 4298, 100 }, // @note Caduceus
        { "sm_wolf",     4354, 20  }, // @note Wolf Ticket
        { "sm_brain",    4450, 300 }, // @note Zombie Brain
        { "sm_buckazoid",6418, 100 }  // @note Buckazoid
    };

    /* @note which phone each player is on (user_id -> world, tile). the dialogs don't carry it,
     * so a faked dialog can't call without standing at a Telephone */
    struct call { std::string world{}; ::pos tile{}; };
    std::unordered_map<int, call> calls{};

    ::peer &me(ENetEvent &event) { return *static_cast<::peer*>(event.peer->data); }

    short held(const ::peer &p, short id)
    {
        auto it = std::ranges::find(p.slots, id, &::slot::id);
        return (it == p.slots.end()) ? 0 : it->count;
    }

    /* @note room for 1 more Growtoken (a stack that exists, even used up, keeps its slot) */
    bool room_for_token(const ::peer &p)
    {
        auto it = std::ranges::find(p.slots, GROWTOKEN, &::slot::id);
        if (it != p.slots.end()) return it->count + 1 <= MAX_STACK;
        return static_cast<int>(p.slots.size()) < p.slot_size;
    }

    /* @note the day number in server time, same as the Daily Spin (resets at local midnight) */
    int today()
    {
        std::tm t = localtime();
        t.tm_hour = 12, t.tm_min = 0, t.tm_sec = 0;
        return static_cast<int>(std::mktime(&t) / 86400);
    }

    void bubble(ENetEvent &event, const std::string &text)
    {
        send_varlist(event.peer, { "OnTalkBubble", me(event).netid, text, 0u, 1u });
    }

    std::string item_name(short id) { return id_to_item(id).raw_name; }

    /* ---------- resources/crazy_jim.txt, read again whenever the file changes ---------- */
    std::vector<std::pair<short, short>> jim_items{};
    std::filesystem::file_time_type jim_at{};

    void load_jim()
    {
        std::error_code ec;
        const auto at = std::filesystem::last_write_time("resources/crazy_jim.txt", ec);
        if (ec || at == jim_at) return;
        jim_at = at;
        jim_items.clear();
        std::ifstream file("resources/crazy_jim.txt");
        for (std::string line; std::getline(file, line); )
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line.starts_with('#')) continue;
            const std::vector<std::string> v = readch(line, '|');
            if (v.size() < 3 || v[0] != "item") continue;
            const int id = atoi(v[1].c_str()), count = atoi(v[2].c_str());
            if (id <= 0 || id >= static_cast<int>(items.size()) || count < 1 || count > MAX_STACK) continue;
            if (std::ranges::find(jim_items, static_cast<short>(id), &std::pair<short, short>::first) != jim_items.end()) continue; // @note 2 different items
            jim_items.emplace_back(static_cast<short>(id), static_cast<short>(count));
        }
    }

    /* @return the player's call if they still stand at that Telephone, else nullptr */
    ::world *on_phone(ENetEvent &event)
    {
        ::peer &p = me(event);
        auto c = calls.find(p.user_id);
        if (c == calls.end() || p.netid == 0 || p.recent_worlds.back() != c->second.world) return nullptr;

        auto world = std::ranges::find(worlds, c->second.world, &::world::name);
        if (world == worlds.end()) return nullptr;
        const ::pos tile = c->second.tile;
        if (world->blocks[cord(tile.x_int(), tile.y_int())].fg != TELEPHONE) return nullptr; // @note broken while we talked

        const ::pos at = p.pos.by_32(true);
        if (std::abs(at.x - tile.x) > NEAR_TILES || std::abs(at.y - tile.y) > NEAR_TILES) return nullptr; // @note walked away
        return &*world;
    }

    void crazy_jim_dialog(ENetEvent &event)
    {
        ::peer &p = me(event);
        load_jim();
        if (jim_items.size() < 2) return bubble(event, "`4Crazy Jim isn't answering right now.``");

        const auto quest = crazy_jim_quest(today());
        std::string body = std::format(
            "set_default_color|`o\n"
            "add_label_with_icon|big|`wCrazy Jim's Daily Quest``|left|{}|\n"
            "add_spacer|small|\n", GROWTOKEN);

        if (pget(p, "jim_day") == today())
        {
            body += "add_textbox|You already did my quest today! Call me again tomorrow for a new one.|left|\n"
                    "end_dialog|mt_phone|Hang Up||\n";
        }
        else
        {
            const bool ready = held(p, quest[0].first) >= quest[0].second && held(p, quest[1].first) >= quest[1].second;
            body += std::format(
                "add_textbox|Hiya, it's Crazy Jim! I need some stuff, and I need it today. Bring me these and I'll give you a `2Growtoken``!|left|\n"
                "add_label_with_icon|small|`w{}`` {} (you have {})|left|{}|\n"
                "add_label_with_icon|small|`w{}`` {} (you have {})|left|{}|\n"
                "add_spacer|small|\n",
                quest[0].second, item_name(quest[0].first), held(p, quest[0].first), quest[0].first,
                quest[1].second, item_name(quest[1].first), held(p, quest[1].first), quest[1].first);
            body += ready ? "add_button|jim_deliver|`2Deliver``|noflags|0|0|\n"
                          : "add_textbox|`4You don't have everything yet.``|left|\n";
            body += "end_dialog|mt_phone|Hang Up||\n";
        }
        send_varlist(event.peer, { "OnDialogRequest", body });
    }

    void crazy_jim_deliver(ENetEvent &event)
    {
        ::peer &p = me(event);
        if (staff_locked(p)) return staff_locked_message(*event.peer);
        if (pget(p, "jim_day") == today()) return bubble(event, "You already did Crazy Jim's quest today.");
        load_jim();
        if (jim_items.size() < 2) return;

        const auto quest = crazy_jim_quest(today()); // @note check again, the dialog reply can be faked
        if (held(p, quest[0].first) < quest[0].second || held(p, quest[1].first) < quest[1].second)
            return bubble(event, "`4You don't have everything Crazy Jim asked for.``");
        if (!room_for_token(p)) return bubble(event, "`4You don't have room for a Growtoken!``");

        pset(p, "jim_day", today());
        modify_item_inventory(event, ::slot(quest[0].first, static_cast<short>(-quest[0].second)));
        modify_item_inventory(event, ::slot(quest[1].first, static_cast<short>(-quest[1].second)));
        modify_item_inventory(event, ::slot(GROWTOKEN, 1));
        add_stat(event, "growtokens"); // @note Legendary Quest step
        send_action(*event.peer, "play_sfx", "file|audio/cash_register.wav\ndelayMS|0\n");
        on::ConsoleMessage(event.peer, "`2Crazy Jim:`` Thanks! Here's your `2Growtoken``. Call me again tomorrow!");
        bubble(event, "You got a `2Growtoken``!");
    }

    void sales_man_dialog(ENetEvent &event)
    {
        ::peer &p = me(event);
        /* @note text from GTOS (zKaan/GTOSLatest); offers from the wiki */
        std::string body = std::format(
            "set_default_color|`o\n"
            "add_label_with_icon|big|`wSales-Man``|left|4358|\n"
            "add_textbox|It is I, Sales-Man, savior of the wealthy! Let me rescue you from your riches. What would you like to buy today?|left|\n"
            "add_spacer|small|\n"
            "add_label_with_icon|small|`w1 Growtoken`` (you have {})|left|{}|\n", held(p, GROWTOKEN), GROWTOKEN);
        for (const offer &o : sales_man)
            body += std::format("add_button|{}|Buy for {} {} (you have {})|noflags|0|0|\n", o.button, o.count, item_name(o.id), held(p, o.id));
        body += "end_dialog|mt_phone|Hang Up||\n";
        send_varlist(event.peer, { "OnDialogRequest", body });
    }

    void sales_man_buy(ENetEvent &event, const offer &o)
    {
        ::peer &p = me(event);
        if (staff_locked(p)) return staff_locked_message(*event.peer);
        if (held(p, o.id) < o.count)
            return bubble(event, std::format("`4You need {} {} for that.``", o.count, item_name(o.id)));
        if (!room_for_token(p)) return bubble(event, "`4You don't have room for a Growtoken!``");

        modify_item_inventory(event, ::slot(o.id, static_cast<short>(-o.count)));
        modify_item_inventory(event, ::slot(GROWTOKEN, 1));
        add_stat(event, "growtokens"); // @note Legendary Quest step
        send_action(*event.peer, "play_sfx", "file|audio/cash_register.wav\ndelayMS|0\n");
        on::ConsoleMessage(event.peer, std::format("`2Sales-Man:`` You traded {} {} for a `2Growtoken``!", o.count, item_name(o.id)));
        sales_man_dialog(event); // @note stay in the shop
    }
}

std::array<std::pair<short, short>, 2> crazy_jim_quest(int day)
{
    load_jim();
    if (jim_items.size() < 2) return {};
    std::mt19937 rng(static_cast<u_int>(day)); // @note seeded by the day: everyone gets the same quest
    std::uniform_int_distribution<std::size_t> pick(0, jim_items.size() - 1);
    const std::size_t a = pick(rng);
    std::size_t b = pick(rng);
    if (b == a) b = (a + 1) % jim_items.size();
    return { jim_items[a], jim_items[b] };
}

void telephone_dialog(ENetEvent &event, ::world &world, const ::pos &tile)
{
    calls[me(event).user_id] = call{ world.name, tile };
    send_varlist(event.peer, {
        "OnDialogRequest",
        std::format(
            "set_default_color|`o\n"
            "add_label_with_icon|big|`wTelephone``|left|{}|\n"
            "add_textbox|Dial a number to call somebody in Growtopia. Some numbers are secret!|left|\n"
            "add_text_input|number|Phone #||5|\n"
            "end_dialog|mt_phone|Hang Up|Dial|\n", TELEPHONE)
    });
}

void telephone_return(ENetEvent &event, const ::hPipe &hPipe)
{
    if (on_phone(event) == nullptr) return;

    const std::string clicked{ hPipe["buttonClicked"] };
    if (clicked == "jim_deliver") return crazy_jim_deliver(event);
    for (const offer &o : sales_man)
        if (clicked == o.button) return sales_man_buy(event, o);
    if (!clicked.empty()) return;

    const std::string number{ hPipe["number"] };
    if (number == "12345") crazy_jim_dialog(event);
    else if (number == "53785") sales_man_dialog(event);
    else bubble(event, "This phone number does not exist!"); // @note text from GTOS
}
