#include "pch.hpp"
#include "tools/tutorial.hpp"
#include <unordered_map>
#include "onVariant/ConsoleMessage.hpp"
#include "onVariant/SetBux.hpp"
#include "onVariant/SetClothing.hpp"
#include "commands/titles.hpp"

#include "tools/cps.hpp"
#include "tools/economy.hpp"

#include "profile.hpp"

extern std::unordered_map<std::string_view, std::string_view> emoticon; // @note onVariant/EmoticonDataChanged.cpp

static constexpr short WORLD_LOCK = 242;

/* ---------- key=value storage ---------- */

static std::string clean(std::string text)
{
    for (char &c : text)
        if (c == ';' || c == '=' || c == ',' || c == '|') c = ' ';
    std::erase_if(text, [](char c) { return c == '\n' || c == '\r'; });
    return text;
}

int pget(const ::peer &p, const std::string &key, int fallback)
{
    auto it = p.profile.find(key);
    return (it == p.profile.end() || it->second.empty()) ? fallback : atoi(it->second.c_str());
}

void pset(::peer &p, const std::string &key, int value) { p.profile[key] = std::to_string(value); }

std::string pget_str(const ::peer &p, const std::string &key)
{
    auto it = p.profile.find(key);
    return (it == p.profile.end()) ? "" : it->second;
}

void pset_str(::peer &p, const std::string &key, const std::string &value) { p.profile[key] = clean(value); }

std::vector<std::string> pget_list(const ::peer &p, const std::string &key)
{
    std::vector<std::string> list{};
    for (const std::string &item : readch(pget_str(p, key), ','))
        if (!item.empty()) list.push_back(item);
    return list;
}

void pset_list(::peer &p, const std::string &key, const std::vector<std::string> &list)
{
    std::string joined{};
    for (const std::string &item : list) joined += (joined.empty() ? "" : ",") + clean(item);
    p.profile[key] = joined;
}

std::string profile_serialize(const ::peer &p)
{
    std::string data{};
    for (const auto &[key, value] : p.profile) data += std::format("{}={};", key, value);
    return data;
}

void profile_load(::peer &p, const std::string &data)
{
    p.profile.clear();
    for (const std::string &pair : readch(data, ';'))
        if (const std::size_t eq = pair.find('='); eq != std::string::npos)
            p.profile[pair.substr(0, eq)] = pair.substr(eq + 1);
}

/* ---------- helpers ---------- */

static ::peer &me(ENetEvent &event) { return *static_cast<::peer*>(event.peer->data); }

static short owned(const ::peer &p, short id)
{
    auto it = std::ranges::find(p.slots, id, &::slot::id);
    return (it == p.slots.end()) ? 0 : it->count;
}

static bool has_room(const ::peer &p, short id, int count)
{
    const short have = owned(p, id);
    return have + count <= 200 && (have > 0 || (int)p.slots.size() < p.slot_size);
}

static void sfx(ENetPeer &p, std::string_view file) { send_action(p, "play_sfx", std::format("file|{}\ndelayMS|0\n", file)); }

static void add_gems(ENetEvent &event, int gems)
{
    me(event).gems += gems;
    econ(gems > 0 ? "daily goals & grow pass" : "card battle license", gems);
    on::SetBux(event);
}

static void dialog(ENetEvent &event, std::string_view page, const std::string &body, std::string_view ok = "")
{
    send_varlist(event.peer, { "OnDialogRequest", std::format(
        "set_default_color|`o\n"
        "embed_data|page|{}\n"
        "{}"
        "end_dialog|mt_profile|Close|{}|\n", page, body, ok) });
}

static std::string number(int n) // @note 10000 -> "10,000"
{
    std::string s = std::to_string(n);
    for (int i = (int)s.size() - 3; i > 0; i -= 3) s.insert(i, ",");
    return s;
}

/* ---------- stats & achievements ---------- */

namespace
{
    struct achievement { std::string_view name, desc, key; int target; };
    constexpr achievement ACHIEVEMENTS[] {
        {"Builder", "Place 100 blocks", "placed", 100},
        {"Architect", "Place 1,000 blocks", "placed", 1000},
        {"Master Builder", "Place 10,000 blocks", "placed", 10000},
        {"Wrecker", "Break 100 blocks", "broken", 100},
        {"Demolisher", "Break 1,000 blocks", "broken", 1000},
        {"Wrecking Ball", "Break 10,000 blocks", "broken", 10000},
        {"Green Thumb", "Harvest 10 trees", "harvested", 10},
        {"Farmer", "Harvest 100 trees", "harvested", 100},
        {"Tycoon Farmer", "Harvest 1,000 trees", "harvested", 1000},
        {"Intern", "Complete a surgery", "surgeries", 1},
        {"Doctor", "Complete 10 surgeries", "surgeries", 10},
        {"Chief Surgeon", "Complete 50 surgeries", "surgeries", 50},
        {"Merchant", "Complete a trade", "trades", 1},
        {"Dealer", "Complete 25 trades", "trades", 25},
        {"Firefighter", "Put out 10 fires", "fires", 10},
        {"Fire Chief", "Put out 100 fires", "fires", 100},
        {"Getting Started", "Reach level 10", "@level", 10},
        {"Experienced", "Reach level 30", "@level", 30},
        {"Legendary", "Reach level 60", "@level", 60},
        {"Gem Collector", "Have 10,000 gems", "@gems", 10000},
        {"Saver", "Keep 100 World Locks in the World Lock Bank", "@bank", 100},
        {"Friendly", "Have 5 friends", "friend_count", 5},
        {"Dedicated", "Play for 10 hours", "@hours", 10},
        {"Daily Grinder", "Reach a 7 day Grow Pass streak", "bonus_best", 7},
        {"Goal Getter", "Finish 10 daily goals", "goals", 10},
    };
}

static int stat_value(const ::peer &p, std::string_view key)
{
    if (key == "@level") return p.level.front();
    if (key == "@gems")  return p.gems;
    if (key == "@bank")  return p.wl_bank;
    if (key == "@hours") return static_cast<int>((p.playtime + (std::time(nullptr) - p.login_time)) / 3600);
    return pget(p, std::string{ key });
}

int achievements_total() { return static_cast<int>(std::size(ACHIEVEMENTS)); }

int achievements_unlocked(const ::peer &p) { return std::popcount(static_cast<u_int>(pget(p, "ach"))); }

static void check_achievements(ENetEvent &event)
{
    ::peer &p = me(event);
    int mask = pget(p, "ach");
    for (int i = 0; i < achievements_total(); ++i)
    {
        const achievement &a = ACHIEVEMENTS[i];
        if ((mask & (1 << i)) || stat_value(p, a.key) < a.target) continue;
        mask |= (1 << i);
        on::ConsoleMessage(event.peer, std::format("`9Achievement Unlocked: `w{}``! `o({})``", a.name, a.desc));
        send_varlist(event.peer, { "OnTextOverlay", std::format("`9Achievement Unlocked: `w{}``!", a.name) });
        sfx(*event.peer, "audio/double_chance.wav");
        announce_achievement(p, a.name);
    }
    pset(p, "ach", mask);
}

void add_stat(ENetEvent &event, const std::string &key, int amount)
{
    if (event.peer == nullptr || event.peer->data == nullptr) return;
    pset(me(event), key, pget(me(event), key) + amount);
    check_achievements(event);
    tutorial_check(event); // @note new player tutorial steps
}

static void send_achievements(ENetEvent &event)
{
    check_achievements(event); // @note level, gems... change outside add_stat
    ::peer &p = me(event);
    const int mask = pget(p, "ach");
    std::string body = std::format("add_label|big|`wAchievements`` ({}/{})|left|\nadd_spacer|small|\n", achievements_unlocked(p), achievements_total());
    for (int i = 0; i < achievements_total(); ++i)
    {
        const achievement &a = ACHIEVEMENTS[i];
        const bool done = mask & (1 << i);
        body += done ?
            std::format("add_smalltext|`2[DONE]`` `w{}`` - {}|left|\n", a.name, a.desc) :
            std::format("add_smalltext|`4[    ]`` `w{}`` - {} `o({}/{})``|left|\n", a.name, a.desc,
                number(std::min(stat_value(p, a.key), a.target)), number(a.target));
    }
    dialog(event, "achievements", body);
}

/* ---------- daily goals ---------- */

namespace
{
    struct goal { std::string_view key; int target, gems; std::string_view text; };
    constexpr goal GOALS[] {
        {"broken", 300, 300, "Break 300 blocks"},   {"placed", 300, 300, "Place 300 blocks"},
        {"harvested", 20, 400, "Harvest 20 trees"}, {"surgeries", 1, 500, "Complete a surgery"},
        {"trades", 1, 200, "Complete a trade"},     {"broken", 1000, 800, "Break 1,000 blocks"},
        {"placed", 1000, 800, "Place 1,000 blocks"}, {"harvested", 50, 700, "Harvest 50 trees"},
    };
    constexpr int GOALS_PER_DAY = 3;
}

static int today() { return static_cast<int>(std::time(nullptr) / 86400); }

/* @note goals i of today: picks never repeat the same stat on one day */
static const goal &todays_goal(int i) { return GOALS[(today() + i * 2) % std::size(GOALS)]; }

static void refresh_goals(::peer &p)
{
    if (pget(p, "gday") == today()) return;
    pset(p, "gday", today());
    pset(p, "gclaim", 0);
    for (int i = 0; i < GOALS_PER_DAY; ++i)
        pset(p, std::format("gbase{}", i), pget(p, std::string{ todays_goal(i).key }));
}

static int goal_progress(const ::peer &p, int i)
{
    return pget(p, std::string{ todays_goal(i).key }) - pget(p, std::format("gbase{}", i));
}

static void send_goals(ENetEvent &event, const std::string &note = "")
{
    ::peer &p = me(event);
    refresh_goals(p);
    const int claimed = pget(p, "gclaim");
    std::string body = "add_label|big|`wGoals & Quests``|left|\nadd_spacer|small|\n"
        "add_textbox|Finish today's goals for gems. Finish all of them for a `wWorld Lock`` bonus! New goals every day.|left|\n"
        "add_spacer|small|\n";
    if (!note.empty()) body += std::format("add_textbox|{}|left|\nadd_spacer|small|\n", note);
    for (int i = 0; i < GOALS_PER_DAY; ++i)
    {
        const goal &g = todays_goal(i);
        const int progress = std::min(goal_progress(p, i), g.target);
        if (claimed & (1 << i))
            body += std::format("add_smalltext|`2[CLAIMED]`` {}|left|\n", g.text);
        else
        {
            body += std::format("add_smalltext|{} `o({}/{})`` - reward: `w{}`` gems|left|\n", g.text, number(progress), number(g.target), g.gems);
            if (progress >= g.target) body += std::format("add_button|goal_{}|`2Claim {} gems``|noflags|0|0|\n", i, g.gems);
        }
    }
    dialog(event, "goals", body);
}

static void claim_goal(ENetEvent &event, int i)
{
    ::peer &p = me(event);
    refresh_goals(p);
    int claimed = pget(p, "gclaim");
    if (i < 0 || i >= GOALS_PER_DAY || (claimed & (1 << i))) return send_goals(event);
    const goal &g = todays_goal(i);
    if (goal_progress(p, i) < g.target) return send_goals(event);

    claimed |= (1 << i);
    pset(p, "gclaim", claimed);
    add_gems(event, g.gems);
    std::string note = std::format("`2You got {} gems!``", g.gems);
    if (claimed == (1 << GOALS_PER_DAY) - 1)
    {
        if (has_room(p, WORLD_LOCK, 1)) { modify_item_inventory(event, ::slot{ WORLD_LOCK, 1 }); note += " `9All goals done: +1 World Lock!``"; }
        else { add_gems(event, 2000); note += " `9All goals done! (no room for a World Lock, got 2000 gems instead)``"; }
    }
    sfx(*event.peer, "audio/cash_register.wav");
    add_stat(event, "goals");
    send_goals(event, note);
}

/* ---------- grow pass (daily bonus) ---------- */

namespace
{
    struct bonus { int gems; short wl; };
    constexpr bonus GROW_PASS[7] { {100, 0}, {200, 0}, {300, 0}, {0, 1}, {500, 0}, {750, 0}, {0, 2} };
    constexpr int BONUS_COOLDOWN = 20 * 60 * 60; // @note 20 hours, so a daily player never misses one
    constexpr int STREAK_LOST    = 48 * 60 * 60;
}

static void send_grow_pass(ENetEvent &event, const std::string &note = "")
{
    ::peer &p = me(event);
    const int now = static_cast<int>(std::time(nullptr)), last = pget(p, "bonus_last");
    int streak = pget(p, "bonus_streak");
    if (last == 0 || now - last >= STREAK_LOST) streak = 0;
    const int next = streak % 7; // @note index of the next reward

    std::string body = "add_label|big|`wGrow Pass``|left|\nadd_spacer|small|\n"
        "add_textbox|Claim a reward every day. Don't skip a day or your streak starts over!|left|\n"
        "add_spacer|small|\n";
    if (!note.empty()) body += std::format("add_textbox|{}|left|\nadd_spacer|small|\n", note);
    for (int i = 0; i < 7; ++i)
    {
        const bonus &b = GROW_PASS[i];
        const std::string reward = b.wl ? std::format("`w{}`` World Lock{}", b.wl, b.wl > 1 ? "s" : "") : std::format("`w{}`` gems", b.gems);
        body += std::format("add_smalltext|{}Day {}: {}``|left|\n", (i < next) ? "`2[DONE]`` " : (i == next) ? "`9>> ``" : "", i + 1, reward);
    }
    body += std::format("add_spacer|small|\nadd_smalltext|Streak: `w{}`` days (best: `w{}``)|left|\n", streak, pget(p, "bonus_best"));
    if (now - last >= BONUS_COOLDOWN) body += "add_button|bonus_claim|`2Claim today's reward``|noflags|0|0|\n";
    else body += std::format("add_smalltext|Next reward in `w{}h {}m``.|left|\n", (BONUS_COOLDOWN - (now - last)) / 3600, (BONUS_COOLDOWN - (now - last)) % 3600 / 60);
    dialog(event, "grow_pass", body);
}

static void claim_grow_pass(ENetEvent &event)
{
    ::peer &p = me(event);
    const int now = static_cast<int>(std::time(nullptr)), last = pget(p, "bonus_last");
    if (now - last < BONUS_COOLDOWN) return send_grow_pass(event);

    int streak = (last != 0 && now - last < STREAK_LOST) ? pget(p, "bonus_streak") : 0;
    const bonus &b = GROW_PASS[streak % 7];
    if (b.wl && !has_room(p, WORLD_LOCK, b.wl)) return send_grow_pass(event, "`4Make room for World Locks in your backpack first!``");

    ++streak;
    pset(p, "bonus_last", now);
    pset(p, "bonus_streak", streak);
    pset(p, "bonus_best", std::max(pget(p, "bonus_best"), streak));
    if (b.wl) modify_item_inventory(event, ::slot{ WORLD_LOCK, b.wl });
    if (b.gems) add_gems(event, b.gems);
    sfx(*event.peer, "audio/cash_register.wav");
    check_achievements(event);
    send_grow_pass(event, std::format("`2Day {} claimed!``", (streak - 1) % 7 + 1));
}

/* ---------- marvelous missions ---------- */

namespace
{
    struct mission { std::string_view text, key; int target; short wl; };
    constexpr mission MISSIONS[] {
        {"Break 2,500 blocks", "broken", 2500, 2},     {"Place 2,500 blocks", "placed", 2500, 2},
        {"Harvest 250 trees", "harvested", 250, 3},    {"Complete 10 surgeries", "surgeries", 10, 5},
        {"Complete 20 trades", "trades", 20, 2},       {"Reach level 25", "@level", 25, 5},
        {"Finish 25 daily goals", "goals", 25, 5},     {"Put out 50 fires", "fires", 50, 2},
    };
}

static void send_missions(ENetEvent &event, const std::string &note = "")
{
    ::peer &p = me(event);
    const int claimed = pget(p, "mclaim");
    std::string body = "add_label|big|`wMarvelous Missions``|left|\nadd_spacer|small|\n"
        "add_textbox|Big missions with `wWorld Lock`` rewards. Each one can be claimed once.|left|\nadd_spacer|small|\n";
    if (!note.empty()) body += std::format("add_textbox|{}|left|\nadd_spacer|small|\n", note);
    for (int i = 0; i < (int)std::size(MISSIONS); ++i)
    {
        const mission &m = MISSIONS[i];
        const int progress = std::min(stat_value(p, m.key), m.target);
        if (claimed & (1 << i)) { body += std::format("add_smalltext|`2[CLAIMED]`` {}|left|\n", m.text); continue; }
        body += std::format("add_label_with_icon|small|{} `o({}/{})`` - `w{}`` World Locks|left|{}|\n", m.text, number(progress), number(m.target), m.wl, WORLD_LOCK);
        if (progress >= m.target) body += std::format("add_button|mission_{}|`2Claim {} World Locks``|noflags|0|0|\n", i, m.wl);
    }
    dialog(event, "missions", body);
}

static void claim_mission(ENetEvent &event, int i)
{
    ::peer &p = me(event);
    int claimed = pget(p, "mclaim");
    if (i < 0 || i >= (int)std::size(MISSIONS) || (claimed & (1 << i))) return send_missions(event);
    const mission &m = MISSIONS[i];
    if (stat_value(p, m.key) < m.target) return send_missions(event);
    if (!has_room(p, WORLD_LOCK, m.wl)) return send_missions(event, "`4Make room for World Locks in your backpack first!``");

    pset(p, "mclaim", claimed | (1 << i));
    modify_item_inventory(event, ::slot{ WORLD_LOCK, m.wl });
    sfx(*event.peer, "audio/cash_register.wav");
    send_missions(event, std::format("`2Mission complete! +{} World Locks``", m.wl));
}

/* ---------- personalize profile ---------- */

static void send_personalize(ENetEvent &event)
{
    ::peer &p = me(event);
    dialog(event, "personalize", std::format(
        "add_label|big|`wPersonalize Player Profile``|left|\nadd_spacer|small|\n"
        "add_textbox|This is what other players see when they wrench you.|left|\n"
        "add_text_input|bio|About me|{}|100|\n"
        "add_spacer|small|\n"
        "add_checkbox|show_ach|Show my achievements|{}\n"
        "add_checkbox|show_surg|Show my surgeon level|{}\n"
        "add_checkbox|show_time|Show my play time|{}\n"
        "add_checkbox|show_pet|Show my battle pet|{}\n",
        pget_str(p, "bio"), to_char(!pget(p, "hide_ach")), to_char(!pget(p, "hide_surg")), to_char(!pget(p, "hide_time")), to_char(!pget(p, "hide_pet"))), "Save");
}

std::string profile_public_lines(const ::peer &p)
{
    std::string lines{};
    if (const std::string bio = pget_str(p, "bio"); !bio.empty())
        lines += std::format("add_textbox|`o\"{}\"``|left|\n", bio);
    if (!pget(p, "hide_ach"))  lines += std::format("add_label|small|`1Achievements:`` {}/{}|left\n", achievements_unlocked(p), achievements_total());
    if (!pget(p, "hide_surg")) lines += std::format("add_label|small|`1Surgeon Level:`` {}|left\n", p.surgery_skill);
    if (!pget(p, "hide_time")) lines += std::format("add_label|small|`1Time Played:`` {:.1f} hours|left\n", (p.playtime + (std::time(nullptr) - p.login_time)) / 3600.0);
    const int pet = pget(p, "pet");
    lines += (pet != 0 && !pget(p, "hide_pet")) ?
        std::format("add_label_with_icon|small|`1Battle Pet:`` `w{}``|left|{}|\n", id_to_item(pet).raw_name, pet) :
        "add_textbox|(No Battle Leash equipped)|left|\n";
    lines += pget(p, "license") ? "add_textbox|`2Has a Card Battle License.``|left|\n" : "add_textbox|You need a valid license to battle!|left|\n";
    return lines;
}

/* ---------- wardrobe ---------- */

static constexpr int OUTFITS = 3;

static void send_wardrobe(ENetEvent &event, const std::string &note = "")
{
    ::peer &p = me(event);
    std::string body = "add_label|big|`wWardrobe``|left|\nadd_spacer|small|\n"
        "add_textbox|Save what you're wearing and switch outfits with one click. You must still have the clothes in your backpack.|left|\n";
    if (!note.empty()) body += std::format("add_spacer|small|\nadd_textbox|{}|left|\n", note);
    for (int i = 1; i <= OUTFITS; ++i)
    {
        body += std::format("add_spacer|small|\nadd_label|small|`wOutfit {}``|left\n", i);
        std::string pieces{};
        for (const std::string &id : pget_list(p, std::format("outfit{}", i)))
            if (const int item = atoi(id.c_str()); item != 0)
                pieces += std::format("{}{}", pieces.empty() ? "" : ", ", id_to_item(item).raw_name);
        body += std::format("add_smalltext|{}|left|\n", pieces.empty() ? "`o(empty)``" : pieces);
        if (!pieces.empty()) body += std::format("add_button|wear_{}|Wear outfit {}|noflags|0|0|\n", i, i);
        body += std::format("add_button|save_{}|Save current clothes as outfit {}|noflags|0|0|\n", i, i);
    }
    dialog(event, "wardrobe", body);
}

static void wardrobe_action(ENetEvent &event, bool save, int i)
{
    ::peer &p = me(event);
    if (i < 1 || i > OUTFITS) return;
    const std::string key = std::format("outfit{}", i);
    if (save)
    {
        std::vector<std::string> ids{};
        for (float cloth : p.clothing) ids.push_back(std::to_string(static_cast<int>(cloth)));
        pset_list(p, key, ids);
        return send_wardrobe(event, std::format("`2Saved outfit {}.``", i));
    }
    const std::vector<std::string> ids = pget_list(p, key);
    int missing = 0;
    for (std::size_t slot = 0; slot < p.clothing.size() && slot < ids.size(); ++slot)
    {
        const short id = static_cast<short>(atoi(ids[slot].c_str()));
        if (id != 0 && owned(p, id) <= 0) { ++missing; continue; } // @note keep what's worn there now
        p.clothing[slot] = id;
    }
    p.update_effects();
    if (p.netid != 0) on::SetClothing(*event.peer);
    send_wardrobe(event, missing ? std::format("`oWearing outfit {}. `4{} piece(s) are not in your backpack anymore.``", i, missing) : std::format("`2Wearing outfit {}!``", i));
}

/* ---------- card battle license, battle pets, wrench customization, growmojis ---------- */

static void send_license(ENetEvent &event)
{
    if (pget(me(event), "license"))
        return dialog(event, "license", "add_label|big|`wCard Battle License``|left|\nadd_spacer|small|\n"
            "add_textbox|`2Your license is valid.`` You can equip a Battle Pet from the wrench menu.|left|\n");
    dialog(event, "license", "add_label|big|`wGet Card Battle License``|left|\nadd_spacer|small|\n"
        "add_textbox|A license lets you equip a `wBattle Pet`` that everyone can see when they wrench you. It costs `w500`` gems.|left|\n"
        "add_button|buy_license|`2Get License (500 gems)``|noflags|0|0|\n");
}

static void send_pets(ENetEvent &event, const std::string &note = "")
{
    ::peer &p = me(event);
    if (!pget(p, "license"))
        return dialog(event, "pets", "add_label|big|`wBattle Pets``|left|\nadd_spacer|small|\n"
            "add_textbox|You need a `wCard Battle License`` first! Get one from the wrench menu.|left|\n"
            "add_button|renew_pvp_license|Get Card Battle License|noflags|0|0|\n");
    const int pet = pget(p, "pet");
    std::string body = "add_label|big|`wBattle Pets``|left|\nadd_spacer|small|\n";
    if (!note.empty()) body += std::format("add_textbox|{}|left|\n", note);
    body += (pet != 0) ? std::format("add_label_with_icon|small|Equipped: `w{}``|left|{}|\n", id_to_item(pet).raw_name, pet) : "add_textbox|No pet equipped.|left|\n";
    body += "add_item_picker|pet_item|`wChoose a pet``|Pick any item from your backpack to be your battle pet!|\n";
    if (pet != 0) body += "add_button|pet_remove|Unequip pet|noflags|0|0|\n";
    dialog(event, "pets", body);
}

static void send_wrench_customization(ENetEvent &event)
{
    ::peer &p = me(event);
    if (p.role < MODERATOR)
        return dialog(event, "wrench_custom", "add_label|big|`wWrench Customization``|left|\nadd_spacer|small|\n"
            "add_textbox|The green wrench and big name are a `5Super Supporter`` perk (staff).|left|\n");
    dialog(event, "wrench_custom", std::format("add_label|big|`wWrench Customization``|left|\nadd_spacer|small|\n"
        "add_textbox|Your Super Supporter look is `w{}``.|left|\n"
        "add_button|toggle_ss|{}|noflags|0|0|\n", p.hide_status ? "hidden" : "shown", p.hide_status ? "Show green wrench & big name" : "Hide green wrench & big name"));
}

static void send_growmojis(ENetEvent &event)
{
    std::vector<std::string_view> keys{};
    for (const auto &[key, value] : emoticon) keys.push_back(key);
    std::ranges::sort(keys);

    std::string body = "add_label|big|`wGrowmojis``|left|\nadd_spacer|small|\n"
        "add_textbox|Type these in chat to send a Growmoji. All of them are unlocked on SevenPS!|left|\nadd_spacer|small|\n";
    std::string line{};
    for (std::size_t i = 0; i < keys.size(); ++i)
    {
        line += std::format("{} `w({})``   ", emoticon[keys[i]], keys[i]);
        if (i % 4 == 3 || i + 1 == keys.size()) { body += std::format("add_smalltext|{}|left|\n", line); line.clear(); }
    }
    dialog(event, "growmojis", body);
}

/* ---------- routing ---------- */

bool profile_button(ENetEvent &event, const std::string &clicked)
{
    if (clicked == "alist")                  { send_achievements(event); return true; }
    if (clicked == "goals")                  { send_goals(event); return true; }
    if (clicked == "bonus")                  { send_grow_pass(event); return true; }
    if (clicked == "marvelous_missions")     { send_missions(event); return true; }
    if (clicked == "open_personlize_profile"){ send_personalize(event); return true; }
    if (clicked == "wardrobe_customization") { send_wardrobe(event); return true; }
    if (clicked == "renew_pvp_license")      { send_license(event); return true; }
    if (clicked == "pets")                   { send_pets(event); return true; }
    if (clicked == "wrench_customization")   { send_wrench_customization(event); return true; }
    if (clicked == "emojis")                 { send_growmojis(event); return true; }
    return false;
}

void profile_return(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer &p = me(event);
    const std::string page = hPipe["page"], clicked = hPipe["buttonClicked"];

    if (clicked.starts_with("goal_"))    return claim_goal(event, atoi(clicked.substr(5).c_str()));
    if (clicked.starts_with("mission_")) return claim_mission(event, atoi(clicked.substr(8).c_str()));
    if (clicked.starts_with("wear_"))    return wardrobe_action(event, false, atoi(clicked.substr(5).c_str()));
    if (clicked.starts_with("save_"))    return wardrobe_action(event, true, atoi(clicked.substr(5).c_str()));
    if (clicked == "bonus_claim")        return claim_grow_pass(event);
    if (clicked == "renew_pvp_license")  return send_license(event);
    if (clicked == "buy_license")
    {
        if (pget(p, "license")) return send_license(event);
        if (p.gems < 500) return on::ConsoleMessage(event.peer, "`4You need 500 gems for a license.``");
        add_gems(event, -500);
        pset(p, "license", 1);
        on::ConsoleMessage(event.peer, "`2You got a Card Battle License!``");
        return send_pets(event);
    }
    if (clicked == "pet_remove") { pset(p, "pet", 0); return send_pets(event, "`oPet unequipped.``"); }
    if (clicked == "toggle_ss")  { hidestatus(event, "hidestatus"); return send_wrench_customization(event); }

    if (page == "pets" && !hPipe["pet_item"].empty() && pget(p, "license"))
    {
        const short id = static_cast<short>(atoi(hPipe["pet_item"].c_str()));
        if (id == 18 || id == 32 || owned(p, id) <= 0) return send_pets(event, "`4You can't use that as a pet.``");
        pset(p, "pet", id);
        return send_pets(event, std::format("`2{} is now your battle pet!``", id_to_item(id).raw_name));
    }
    if (page == "personalize" && clicked.empty())
    {
        std::string bio = hPipe["bio"];
        if (bio.size() > 100) bio.resize(100);
        pset_str(p, "bio", bio);
        pset(p, "hide_ach",  hPipe["show_ach"] != "1");
        pset(p, "hide_surg", hPipe["show_surg"] != "1");
        pset(p, "hide_time", hPipe["show_time"] != "1");
        pset(p, "hide_pet",  hPipe["show_pet"] != "1");
        send_varlist(event.peer, { "OnTextOverlay", "Profile saved!" });
    }
}
