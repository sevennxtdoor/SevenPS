#include "pch.hpp"
#include "tools/profile.hpp"
#include "tools/economy.hpp"
#include "tools/area_lock.hpp"
#include "commands/titles.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "onVariant/SetBux.hpp"

#include "legendary.hpp"

namespace
{
    constexpr float NEAR_TILES = 3.0f; // @note how close you must stand to the Wizard (same as doors)
    constexpr int MAX_STACK = 200;

    /* @note reward 0 = the "of Legend" title. quest names other than Honor are from memory of real GT (unverified) */
    struct quest { const char *key; const char *name; short reward; };
    constexpr quest QUESTS[]{
        { "honor", "Quest For Honor",      0    }, // @note Legendary Title
        { "fire",  "Quest For Fire",       1782 }, // @note Dragon of Legend
        { "steel", "Quest For Steel",      1780 }, // @note Legendbot-009
        { "sky",   "Quest Of The Heavens", 1784 }, // @note Legendary Wings
        { "blade", "Quest For The Blade",  2592 }, // @note Legendary Katana
    };
    constexpr int QUEST_COUNT = static_cast<int>(std::size(QUESTS));

    enum class kind { ITEM, GEMS, STAT };
    struct step
    {
        kind type; short item; int target; const char *stat; // @note stat: a peer::profile counter (add_stat), progress = now - when the step began
        const char *text; const char *extra; const char *none; const char *done;
    };
    /* @note GTOS order and texts. its step 2 (defeat 100 Growtopians in Games/Deathmatch) is left out */
    constexpr step STEPS[]{
        { kind::ITEM, 442,  2000,   nullptr, "I challenge you to bring me 2,000 of those Sand thingies!", nullptr, "You have none to deliver!", nullptr },
        { kind::STAT, 0,    5000,   "broken", "You must defeat 5,000 blocks in battle!", nullptr, "You are not yet worthy!", "I have slain them all!" },
        { kind::ITEM, 1422, 600,    nullptr, "I challenge you to bring me 600 of those Display Box thingies!", nullptr, "You have none to deliver!", nullptr },
        { kind::STAT, 0,    50000,  "planted_rarity", "You must plant 50,000 rarity worth of trees! Bring life to this land!",
          "(For example, if you plant a tree of rarity 50, you get 50 points. A Dirt Tree is 1 point because it is rarity 1)", "I will go plant more!", "I have planted them all!" },
        { kind::STAT, 0,    50,     "growtokens", "Prove your skill! Earn 50 Growtokens in whatever way you want! You can keep the Growtokens, I'm not a greedy wizard.",
          nullptr, "I will quest onward!", "I am talented!" },
        { kind::ITEM, 1462, 3,      nullptr, "I challenge you to bring me 3 of those Golden Diaper thingies!", nullptr, "You have none to deliver!", nullptr },
        { kind::STAT, 0,    10000,  "xp_earned", "Here's a freeform quest for you - I don't care what you do, just earn 10,000 XP doing it!", nullptr, "I'm on my way!", "I have learned!" },
        { kind::ITEM, 784,  800,    nullptr, "I challenge you to bring me 800 of those Tombstone thingies!", nullptr, "You have none to deliver!", nullptr },
        { kind::GEMS, 0,    100000, nullptr, "Look, it's not cheap being a Legendary Wizard. These aren't just purple robes, they're `9Legendary Purple Robes``! "
          "So if you could just spot me like 100,000 Gems, I swear I'll think about paying you back. I'll think hard. Whaddya say?", nullptr, "You have none to deliver!", nullptr },
        { kind::STAT, 0,    100000, "broken_rarity", "You must crush 100,000 rarity worth of blocks! Destroy!",
          "(For example, if you smash a block of rarity 50, you get 50 points. A dirt block is 1 point because it is rarity 1)", "I will go smash more!", "No block can beat me!" },
        { kind::STAT, 0,    100,    "surgeries", "Do some good for the world. Save 100 ailing Growtopians by performing surgery!", nullptr, "You haven't helped enough!", "Saving lives is what I do" },
        { kind::STAT, 0,    1000,   "provided", "I need you to pick up some groceries for me. Collect 1,000 items from Providers.",
          "(Providers are items like Science Stations and Cows, that give you an item on a regular basis)", "I'm on my way!", "I'm a cow-puncher!" },
        { kind::ITEM, 1458, 3,      nullptr, "I challenge you to bring me 3 of those Golden Heart Crystal thingies!", nullptr, "You have none to deliver!", nullptr },
        { kind::STAT, 0,    100000, "fruit_rarity", "You must pluck 100,000 rarity worth of delicious fruit from any tree! I don't want the fruit, I'm just mad that it's up there!",
          "(For example, if you smash a rarity 50 tree and get 3 fruit from it, you will get 150 points)", "I will go pick fruit!", "The fruit is no more!" },
        { kind::ITEM, 1614, 1,      nullptr, "I challenge you to bring me 1 of those Growie Award thingies!", nullptr, "You have none to deliver!", nullptr },
        { kind::ITEM, 1680, 3,      nullptr, "I challenge you to bring me 3 of those Super Fireworks thingies!", nullptr, "You have none to deliver!", nullptr },
        { kind::ITEM, 1672, 3,      nullptr, "I challenge you to bring me 3 of those Rainbow Wings thingies!", nullptr, "You have none to deliver!", nullptr },
        { kind::ITEM, 1280, 3,      nullptr, "I challenge you to bring me 3 of those Birth Certificate thingies!", nullptr, "You have none to deliver!", nullptr },
        { kind::ITEM, LEGENDARY_ORB, 1, nullptr, "And finally, I dare you to ascend to the peak of `wLEGENDARYMOUNTAIN`` and collect a Legendary Orb. "
          "This is the final challenge in any truly legendary quest.", nullptr, "You have none to deliver!", nullptr },
    };
    constexpr int STEP_COUNT = static_cast<int>(std::size(STEPS));

    /* @note which Wizard each player talks to (user_id -> world, tile). the dialogs don't carry it */
    struct talk { std::string world{}; ::pos tile{}; };
    std::unordered_map<int, talk> talks{};

    ::peer &me(ENetEvent &event) { return *static_cast<::peer*>(event.peer->data); }

    short held(const ::peer &p, short id)
    {
        auto it = std::ranges::find(p.slots, id, &::slot::id);
        return (it == p.slots.end()) ? 0 : it->count;
    }

    bool room_for(const ::peer &p, short id)
    {
        auto it = std::ranges::find(p.slots, id, &::slot::id);
        if (it != p.slots.end()) return it->count + 1 <= MAX_STACK;
        return static_cast<int>(p.slots.size()) < p.slot_size;
    }

    std::string number(int n)
    {
        std::string s = std::to_string(n);
        for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(static_cast<std::size_t>(i), ",");
        return s;
    }

    int active(const ::peer &p) { const int q = pget(p, "lq"); return (q >= 1 && q <= QUEST_COUNT) ? q : 0; }
    const step &current(const ::peer &p) { return STEPS[std::clamp(pget(p, "lq_step"), 1, STEP_COUNT) - 1]; }

    /* @note how far the player is on the current step */
    int progress(const ::peer &p)
    {
        const step &s = current(p);
        if (s.type == kind::STAT) return std::max(0, pget(p, s.stat) - pget(p, "lq_base"));
        return pget(p, "lq_prog");
    }

    void begin_step(::peer &p, int number)
    {
        pset(p, "lq_step", number);
        pset(p, "lq_prog", 0);
        const step &s = STEPS[number - 1];
        pset(p, "lq_base", (s.type == kind::STAT) ? pget(p, s.stat) : 0); // @note only what is done from now on counts
    }

    void end_quest(::peer &p)
    {
        for (const char *key : { "lq", "lq_step", "lq_prog", "lq_base" }) pset(p, key, 0);
    }

    /* @return the Wizard the player talks to if they still stand next to it and may use it, else nullptr */
    ::world *at_wizard(ENetEvent &event, ::pos &tile)
    {
        ::peer &p = me(event);
        auto t = talks.find(p.user_id);
        if (t == talks.end() || p.netid == 0 || p.recent_worlds.back() != t->second.world) return nullptr;

        auto world = std::ranges::find(worlds, t->second.world, &::world::name);
        if (world == worlds.end()) return nullptr;
        tile = t->second.tile;
        if (world->blocks[cord(tile.x_int(), tile.y_int())].fg != LEGENDARY_WIZARD) return nullptr; // @note gone (somebody finished their quest)
        if (!can_build(p, *world, tile)) return nullptr; // @note "any Legendary Wizard you have access to"

        const ::pos at = p.pos.by_32(true);
        if (std::abs(at.x - tile.x) > NEAR_TILES || std::abs(at.y - tile.y) > NEAR_TILES) return nullptr; // @note walked away
        return &*world;
    }

    void overlay(ENetEvent &event, const std::string &text) { send_varlist(event.peer, { "OnTextOverlay", text }); }

    void effect_world(::world &world, const ::peer &p, std::initializer_list<u_int> effects)
    {
        const CL_Vec2f where{ p.pos.x + 15.0f, p.pos.y + 15.0f };
        peers(world.name, PEER_SAME_WORLD, [&](ENetPeer &other) {
            for (u_int id : effects) send_varlist(&other, { "OnParticleEffect", id, where, 0.0f, 0.0f });
        });
    }

    void choose_dialog(ENetEvent &event)
    {
        const int done = pget(me(event), "lq_done");
        std::string body = std::format(
            "set_default_color|`o\n"
            "add_label_with_icon|big|`9The Legendary Wizard``|left|{}|\n"
            "add_textbox|Greetings, traveler! I am the Legendary Wizard. Should you wish to embark on a Legendary Quest, simply choose one below.|left|\n"
            "add_spacer|small|\n", LEGENDARY_WIZARD);
        for (int i = 0; i < QUEST_COUNT; ++i)
            body += (done & (1 << i)) ?
                std::format("add_textbox|`2{}`` (completed)|left|\n", QUESTS[i].name) :
                std::format("add_button|q_{}|`9{}``|noflags|0|0|\n", QUESTS[i].key, QUESTS[i].name);
        body += "end_dialog|mt_wizard|No Thanks||\n";
        send_varlist(event.peer, { "OnDialogRequest", body });
    }

    std::string reward_name(const quest &q)
    {
        return (q.reward == 0) ? std::string{ "`9Legendary Title``" } : std::format("`9{}``", id_to_item(q.reward).raw_name);
    }

    void intro_dialog(ENetEvent &event, const quest &q)
    {
        send_varlist(event.peer, { "OnDialogRequest", std::format(
            "set_default_color|`o\n"
            "add_label_with_icon|big|`9{0}``|left|{1}|\n"
            "add_textbox|This quest will challenge every fiber of your Growtopian being. It will cost you thousands of gems, weeks or months of time, and possibly your friends and family.|left|\n"
            "add_textbox|Every quest has {2} steps to complete, and each step alone is probably more than most Growtopians could manage.|left|\n"
            "add_textbox|But the rewards are also vast. If you complete this quest, you will earn the {3}!|left|\n"
            "add_textbox|These quest rewards are `5Untradeable``, and you will truly be a Legendary Growtopian if you complete a quest.|left|\n"
            "add_textbox|You may turn in your quests at any Legendary Wizard you have access to (we're in a union), but I will vanish permanently if somebody turns in their final quest step to me, so don't let other people access me!|left|\n"
            "add_textbox|There's one last thing you should know before you begin. You can quit your quest at any time, but be aware that if you do, you'll lose all progress on this quest. So choose your quest wisely and see it through to the end!|left|\n"
            "add_spacer|small|\n"
            "add_label|big|So... now that you've received the official disclaimer, are you truly prepared to embark on the {0}?|left|\n"
            "add_button|start_{4}|`9Yes!``|noflags|0|0|\n"
            "end_dialog|mt_wizard|No!||\n",
            q.name, LEGENDARY_WIZARD, STEP_COUNT, reward_name(q), q.key) });
    }

    void quest_dialog(ENetEvent &event)
    {
        ::peer &p = me(event);
        const quest &q = QUESTS[active(p) - 1];
        const step &s = current(p);
        const int now = progress(p);

        std::string deliver{};
        if (s.type == kind::ITEM)
        {
            const int give = std::min<int>(held(p, s.item), s.target - now);
            deliver = (give > 0) ? std::format("Deliver {} {}", number(give), id_to_item(s.item).raw_name) : s.none;
        }
        else if (s.type == kind::GEMS)
        {
            const int give = std::min(std::max(p.gems, 0), s.target - now);
            deliver = (give > 0) ? std::format("Deliver {} Gems", number(give)) : s.none;
        }
        else deliver = (now >= s.target) ? s.done : s.none;

        send_varlist(event.peer, { "OnDialogRequest", std::format(
            "set_default_color|`o\n"
            "add_label_with_icon|big|`9{}``|left|{}|\n"
            "add_textbox|(Step {}/{})|left|\n"
            "add_spacer|small|\n"
            "add_textbox|{}|left|\n"
            "{}"
            "add_spacer|small|\n"
            "add_textbox|(Current progress {}/{})|left|\n"
            "add_button|deliver|{}|noflags|0|0|\n"
            "add_button|give_up|Give up this quest|noflags|0|0|\n"
            "end_dialog|mt_wizard|Goodbye!||\n",
            q.name, LEGENDARY_WIZARD, pget(p, "lq_step"), STEP_COUNT, s.text,
            s.extra ? std::format("add_smalltext|{}|left|\n", s.extra) : "",
            number(std::min(now, s.target)), number(s.target), deliver) });
    }

    void finish_quest(ENetEvent &event, ::world &world, const ::pos &tile)
    {
        ::peer &p = me(event);
        const int index = active(p) - 1;
        const quest &q = QUESTS[index];

        if (q.reward == 0)
        {
            p.titles |= T_LEGEND;
            p.titles_on |= T_LEGEND; // @note shown right away
            refresh_display_name(*event.peer);
        }
        else modify_item_inventory(event, ::slot(q.reward, 1));
        pset(p, "lq_done", pget(p, "lq_done") | (1 << index));
        end_quest(p);

        /* @note the Wizard vanishes (GTOS: the tile is cleared for everybody) */
        ::block &block = world.blocks[cord(tile.x_int(), tile.y_int())];
        block.fg = 0, block.hits[0] = 0;
        send_tile_update(event, ::gamePacket{ .id = 0, .punch = tile }, block, world);

        effect_world(world, p, { 48u, 90u });
        send_action(*event.peer, "play_sfx", "file|audio/levelup2.wav\ndelayMS|0\n");
        overlay(event, std::format("`9{} complete!!``", q.name));
        const std::string message = std::format("`9{} `5completed the `9{}`` `5and earned the {}``!", p.display_growid, q.name, reward_name(q));
        peers(world.name, PEER_SAME_WORLD, [&](ENetPeer &other) {
            send_varlist(&other, { "OnTalkBubble", p.netid, message, 0u, 1u });
            on::ConsoleMessage(&other, message);
        });
    }

    void deliver(ENetEvent &event, ::world &world, const ::pos &tile)
    {
        ::peer &p = me(event);
        const step &s = current(p);
        const int now = progress(p);
        const bool last = pget(p, "lq_step") >= STEP_COUNT;

        if (s.type == kind::STAT)
        {
            if (now < s.target) return overlay(event, "`9Good luck! You can do it!``");
        }
        else
        {
            if (staff_locked(p)) return staff_locked_message(*event.peer); // @note staff can't move items/gems out of the economy either way
            if (last)
            {
                const short reward = QUESTS[active(p) - 1].reward;
                if (reward != 0 && !room_for(p, reward)) return overlay(event, "`4Make room in your backpack for your reward first!``");
            }
            int give{};
            if (s.type == kind::ITEM)
            {
                give = std::min<int>(held(p, s.item), s.target - now);
                if (give <= 0) return overlay(event, "`9Good luck! You can do it!``");
                modify_item_inventory(event, ::slot(s.item, static_cast<short>(-give)));
            }
            else
            {
                give = std::min(std::max(p.gems, 0), s.target - now);
                if (give <= 0) return overlay(event, "`9Good luck! You can do it!``");
                p.gems -= give;
                econ("legendary wizard", -give);
                on::SetBux(event);
            }
            pset(p, "lq_prog", now + give);
            if (now + give < s.target) return overlay(event, "`9Thanks! Keep it coming!``");
        }

        if (last) return finish_quest(event, world, tile);

        begin_step(p, pget(p, "lq_step") + 1);
        overlay(event, "`9Quest step complete!!``");
        effect_world(world, p, { 48u });
        quest_dialog(event);
    }
}

void legendary_wizard_dialog(ENetEvent &event, ::world &world, const ::pos &tile)
{
    ::peer &p = me(event);
    if (!can_build(p, world, tile)) return; // @note GTOS: only those with access to the Wizard
    talks[p.user_id] = talk{ world.name, tile };
    if (active(p)) quest_dialog(event);
    else choose_dialog(event);
}

void legendary_wizard_return(ENetEvent &event, const ::hPipe &hPipe)
{
    ::pos tile{};
    ::world *world = at_wizard(event, tile);
    if (world == nullptr) return;
    ::peer &p = me(event);
    const std::string clicked{ hPipe["buttonClicked"] };

    if (!active(p))
    {
        for (int i = 0; i < QUEST_COUNT; ++i)
        {
            if (pget(p, "lq_done") & (1 << i)) continue; // @note one of each
            if (clicked == std::format("q_{}", QUESTS[i].key)) return intro_dialog(event, QUESTS[i]);
            if (clicked == std::format("start_{}", QUESTS[i].key))
            {
                pset(p, "lq", i + 1);
                begin_step(p, 1);
                return quest_dialog(event);
            }
        }
        return;
    }
    if (clicked == "deliver") return deliver(event, *world, tile);
    if (clicked == "give_up")
    {
        send_varlist(event.peer, { "OnDialogRequest", std::format(
            "set_default_color|`o\n"
            "add_label_with_icon|big|`4Give up?``|left|{}|\n"
            "add_textbox|Are you sure? You will lose `4all progress`` on the {}!|left|\n"
            "add_button|give_up_yes|`4Yes, give up``|noflags|0|0|\n"
            "end_dialog|mt_wizard|No, keep going||\n", LEGENDARY_WIZARD, QUESTS[active(p) - 1].name) });
        return;
    }
    if (clicked == "give_up_yes")
    {
        end_quest(p);
        overlay(event, "You gave up your quest.");
    }
}
