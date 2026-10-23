#include "pch.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "onVariant/CountryState.hpp"
#include "onVariant/SetBux.hpp"
#include "tools/economy.hpp"
#include "tools/wrench_menu.hpp" // @note clean_text()
#include "commands/titles.hpp"    // @note refresh_display_name()
#include "action/friends.hpp"     // @note Back -> Social Portal
#include "tools/system_worlds.hpp"
#include "tools/guild_level.hpp"

#include "guild.hpp"

namespace
{
    constexpr int CREATE_COST = 200000; // @note growtopiawiki.com/w/Guilds: 200,000 gems (GT also needs Supporter; SevenPS asks level 5 instead)
    constexpr int CREATE_LEVEL = 5;
    constexpr short WORLD_LOCK = 242, MY_FIRST_WORLD_LOCK = 9640;
    /* @note the flag over the head and the GL/GC/GE tag (|showGuild + OnGuildDataChanged, GTOS values). the force-close after Create Guild
     * was the Guild Lock tile missing its 16 bytes, not this (tested in game), so every guild member gets it again */
    constexpr bool SHOW_FLAG = true;
    constexpr int INVITE_TIME = 120;   // @note seconds to accept
    /* @note growtopiawiki.com/w/Guilds: mascot with a foreground and a background / a foreground only */
    constexpr int MASCOT_COST = 50000, MASCOT_LEVEL = 3, MASCOT_MEMBERS = 15;
    constexpr int MASCOT_FG_COST = 250000, MASCOT_FG_LEVEL = 6, MASCOT_FG_MEMBERS = 30;

    enum rank : int { MEMBER, ELDER, CO_LEADER, LEADER };
    constexpr std::array<std::string_view, 4> RANK_NAME{ "Member", "Elder", "Co-Leader", "Leader" };

    struct member { std::string growid; int rank{}; };
    struct guild
    {
        std::string name{}, statement{};
        int fg{}, bg{}; // @note the mascot (0 = none)
        long long created{};
        std::vector<member> members{};
        std::string world{}; // @note home world, where the Guild Lock is
        int access{};        // @note bit per rank: guild members of that rank may build in the home world (none by default, a guess)
        int lock{ WORLD_LOCK }; // @note the lock the Guild Lock replaced, kept in the database only. a disband always gives a World Lock (erase)
        int level{ 1 };
        long long xp{};         // @note toward the next level, from the members' XP
        bool unsaved{};         // @note XP changed since the last save (saved with the autosave)
    };

    std::vector<guild> guilds{};
    struct invite { std::string guild; std::time_t until{}; };
    std::unordered_map<std::string, invite> invites{}; // @note lower-case growid -> invite

    ::peer &me(ENetEvent &event) { return *static_cast<::peer*>(event.peer->data); }
    std::string lower(std::string s) { for (char &c : s) c = static_cast<char>(std::tolower(static_cast<u_char>(c))); return s; }
    void say(ENetEvent &event, const std::string &text) { on::ConsoleMessage(event.peer, text); }

    guild *guild_of(const std::string &growid)
    {
        for (guild &g : guilds)
            if (std::ranges::any_of(g.members, [&](const member &m) { return lower(m.growid) == lower(growid); })) return &g;
        return nullptr;
    }
    guild *guild_named(const std::string &name)
    {
        for (guild &g : guilds) if (lower(g.name) == lower(name)) return &g;
        return nullptr;
    }
    member *member_of(guild &g, const std::string &growid)
    {
        for (member &m : g.members) if (lower(m.growid) == lower(growid)) return &m;
        return nullptr;
    }
    ENetPeer *online(const std::string &growid)
    {
        for (ENetPeer *p : peers())
            if (const ::peer *pp = static_cast<::peer*>(p->data); pp && !pp->growid.empty() && lower(pp->growid) == lower(growid)) return p;
        return nullptr;
    }

    void to_guild(const guild &g, const std::string &text)
    {
        for (const member &m : g.members)
            if (ENetPeer *p = online(m.growid)) on::ConsoleMessage(p, text);
    }

    std::string members_text(const guild &g)
    {
        std::string s{};
        for (const member &m : g.members) s += std::format("{}{}:{}", s.empty() ? "" : ",", m.growid, m.rank);
        return s;
    }

    void save(guild &g)
    {
        ::hStmt hStmt{ "REPLACE INTO guild (name, statement, fg, bg, members, created, world, access_ranks, lock_item, level, xp) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)" };
        const std::string members = members_text(g);
        MYSQL_BIND params[11] = { make_bind_in(g.name), make_bind_in(g.statement), make_bind_in(g.fg), make_bind_in(g.bg), make_bind_in(members), make_bind_in(g.created),
                                  make_bind_in(g.world), make_bind_in(g.access), make_bind_in(g.lock), make_bind_in(g.level), make_bind_in(g.xp) };
        hStmt.bind_param(params);
        hStmt.execute();
        g.unsaved = false;
    }

    std::string gems_text64(long long n) // @note 200000 -> "200,000"
    {
        std::string s = std::to_string(n);
        for (int i = static_cast<int>(s.size()) - 3; i > (n < 0 ? 1 : 0); i -= 3) s.insert(i, ",");
        return s;
    }
    std::string gems_text(int gems) { return gems_text64(gems); }

    /* @note the user id of a growid, online or not. 0 if there's no such player */
    int uid_of(const std::string &growid)
    {
        if (ENetPeer *p = online(growid)) return static_cast<::peer*>(p->data)->user_id;
        ::hStmt hStmt{ "SELECT uid FROM peer WHERE growid = ? LIMIT 1" };
        MYSQL_BIND param = make_bind_in(growid);
        hStmt.bind_param(&param);
        signed uid{};
        MYSQL_BIND result = make_bind_out(uid);
        mysql_stmt_bind_result(hStmt.pStmt, &result);
        hStmt.execute();
        mysql_stmt_store_result(hStmt.pStmt);
        return (mysql_stmt_fetch(hStmt.pStmt) == 0) ? uid : 0;
    }

    /* @note the world, loading it if nobody is in it. nullptr if it doesn't exist */
    ::world *world_named(const std::string &name)
    {
        if (name.empty()) return nullptr;
        auto it = std::ranges::find(worlds, name, &::world::name);
        if (it != worlds.end()) return &*it;
        if (!::world::exists(name)) return nullptr;
        return &worlds.emplace_back(name);
    }

    /* @note first tile holding `id`, {-1, -1} if none */
    ::pos tile_of(const ::world &world, short id)
    {
        const int width = static_cast<int>(world.blocks.size()) / 60;
        for (int i = 0; i < static_cast<int>(world.blocks.size()); ++i)
            if (world.blocks[i].fg == id) return ::pos{ i % width, i / width };
        return ::pos{ -1, -1 };
    }

    /* @note the lock (and the names) as everyone in that world sees them */
    void lock_refresh(::world &world, const ::pos &tile)
    {
        const auto there = peers(world.name, PEER_SAME_WORLD);
        if (there.empty()) return;
        ENetEvent any{ .peer = there.front() }; // @note send_tile_update() goes to the world of event.peer
        send_tile_update(any, ::gamePacket{ .id = world.blocks[cord(tile.x_int(), tile.y_int())].fg, .punch = tile }, world.blocks[cord(tile.x_int(), tile.y_int())], world);
        for (ENetPeer *p : there) refresh_display_name(*p);
    }

    /* @note a lock that locks the whole world and can hold a guild: World Lock and up (Diamond, Blue Gem Lock...), not My First World Lock */
    ::pos world_lock_of(const ::world &world)
    {
        const int width = static_cast<int>(world.blocks.size()) / 60;
        for (int i = 0; i < static_cast<int>(world.blocks.size()); ++i)
            if (const short fg = world.blocks[i].fg; fg != 0 && fg != GUILD_LOCK && fg != MY_FIRST_WORLD_LOCK && id_to_item(fg).type == type::LOCK && !is_tile_lock(fg))
                return ::pos{ i % width, i / width };
        return ::pos{ -1, -1 };
    }

    /* @note the home world's lock tile: the Guild Lock, else its world lock */
    ::pos home_lock_of(const ::world &world)
    {
        const ::pos guild_lock = tile_of(world, GUILD_LOCK);
        return (guild_lock.x >= 0) ? guild_lock : world_lock_of(world);
    }

    void erase(const std::string &name)
    {
        const guild *g = guild_named(name);
        const std::string home = g ? g->world : "";
        const short lock = WORLD_LOCK; // @note never g->lock: the lock was already given back on Create Guild, a Diamond/Blue Gem Lock here was a free copy every create + disband
        ::hStmt hStmt{ "DELETE FROM guild WHERE name = ?" };
        MYSQL_BIND param = make_bind_in(name);
        hStmt.bind_param(&param);
        hStmt.execute();
        std::erase_if(guilds, [&](const guild &other) { return other.name == name; });
        /* @note the home world is a plain locked world again: the Guild Lock turns back into the lock it replaced
         * (a guess: the wiki doesn't say) and guild members lose their access */
        if (::world *world = world_named(home))
        {
            const ::pos tile = home_lock_of(*world);
            if (tile.x < 0) return;
            if (world->blocks[cord(tile.x_int(), tile.y_int())].fg == GUILD_LOCK) world->blocks[cord(tile.x_int(), tile.y_int())].fg = lock;
            lock_refresh(*world, tile);
        }
    }

    void refresh(const std::string &growid) { if (ENetPeer *p = online(growid)) guild_broadcast(*p); }

    bool valid_name(const std::string &name)
    {
        return name.size() >= 3 && name.size() <= 15 && std::ranges::all_of(name, [](char c) { return std::isalnum(static_cast<u_char>(c)); });
    }

    void info(ENetEvent &event)
    {
        ::peer &p = me(event);
        guild *g = guild_of(p.growid);
        if (g == nullptr)
        {
            /* @note "Grow Guild" as in Growtopia (Growtorials - How to: Guilds); SevenPS has no guild leaderboards, so that line is changed */
            return send_varlist(event.peer, { "OnDialogRequest", std::format(
                "set_default_color|`o\n"
                "add_label_with_icon|big|`wGrow Guild``|left|{}|\n"
                "add_textbox|Welcome to Grow Guilds where you can create a Guild! With a Guild you can team up with friends, chat with `w/gc`` and build together in your Guild's home world.|left|\n"
                "add_spacer|small|\n"
                "add_textbox|To create a Guild you must be at least `2Level {}``.|left|\n"
                "add_textbox|The cost is: `${}`` Gems.|left|\n"
                "add_spacer|small|\n"
                "add_textbox|You will be charged: `${}`` Gems.|left|\n"
                "add_spacer|small|\n"
                "add_textbox|`8Caution``: A guild can only be created in a world owned by you and locked with a `#World Lock``!|left|\n"
                "add_spacer|small|\n"
                "add_smalltext|Got invited? Type `w/guild accept`` to join.|left|\n"
                "add_button|create_guild|Create a Guild|noflags|0|0|\n"
                "add_button|back|Back|noflags|0|0|\n"
                "end_dialog|mt_guild|Close||\n", GUILD_LOCK, CREATE_LEVEL, gems_text(CREATE_COST), gems_text(CREATE_COST)) });
        }
        const member *mine = member_of(*g, p.growid);
        std::string list{};
        for (int r = LEADER; r >= MEMBER; --r)
            for (const member &m : g->members)
                if (m.rank == r)
                    list += std::format("add_textbox|{}`w{}`` `o- {}``|left|\n", online(m.growid) ? "`2(online)`` " : "", m.growid, RANK_NAME[r]);
        /* @note level, XP toward the next one, and the leader's buttons */
        std::string level_lines = (g->level >= GUILD_MAX_LEVEL) ?
            std::format("add_smalltext|Guild Level: `w{}`` (max)|left|\n", g->level) :
            std::format("add_smalltext|Guild Level: `w{}``/{}  XP: `w{}``/{}|left|\n", g->level, GUILD_MAX_LEVEL, gems_text64(g->xp), gems_text64(guild_xp_needed(g->level)));
        std::string leader_lines{};
        if (mine->rank == LEADER)
        {
            if (g->level < GUILD_MAX_LEVEL)
                leader_lines += std::format("add_button|upgrade|`wUpgrade Guild`` (`${}`` Gems)|noflags|0|0|\n", gems_text(guild_gems_needed(g->level)));
            leader_lines += "add_button|mascot|`wGuild Mascot``|noflags|0|0|\n";
        }
        const std::string mascot = (g->fg > 0) ?
            std::format("add_label_with_icon|small|Mascot: `w{}``{}|left|{}|\n", id_to_item(g->fg).raw_name, g->bg > 0 ? std::format(" on `w{}``", id_to_item(g->bg).raw_name) : "", g->fg) : "";
        send_varlist(event.peer, { "OnDialogRequest", std::format(
            "set_default_color|`o\n"
            "add_label_with_icon|big|`w{}``|left|{}|\n"
            "add_textbox|`o{}``|left|\n"
            "add_smalltext|Your rank: `w{}``. Members: `w{}/{}``|left|\n"
            "{}{}"
            "add_spacer|small|\n"
            "{}"
            "add_spacer|small|\n"
            "{}"
            "add_smalltext|/gc {{message}} guild chat, wrench players to invite, promote, demote or kick, /guild leader {{growid}}, /guild statement {{text}}, /guild leave, /guild disband|left|\n"
            "end_dialog|mt_guild||Close|\n",
            g->name, GUILD_LOCK, g->statement.empty() ? "No statement yet." : g->statement, RANK_NAME[mine->rank], g->members.size(), guild_max_members(g->level),
            level_lines, mascot, list, leader_lines) });
    }

    /* @note the picked items ride along in embed_data until Set Mascot (each item picker sends the dialog on its own) */
    void mascot_dialog(ENetEvent &event, int fg, int bg, const std::string &error)
    {
        auto picked = [](std::string_view label, int id) {
            return id > 0 ? std::format("add_label_with_icon|small|{}: `w{}``|left|{}|\n", label, id_to_item(id).raw_name, id) : std::format("add_smalltext|{}: `onone``|left|\n", label);
        };
        send_varlist(event.peer, { "OnDialogRequest", std::format(
            "set_default_color|`o\n"
            "add_label_with_icon|big|`wGuild Mascot``|left|{}|\n"
            "{}"
            "add_textbox|Foreground + Background: `${}`` Gems (Guild Level `w{}``, `w{}`` members).|left|\n"
            "add_textbox|Foreground only: `${}`` Gems (Guild Level `w{}``, `w{}`` members).|left|\n"
            "add_smalltext|Each mascot belongs to one guild only. You need the blocks in your backpack.|left|\n"
            "add_spacer|small|\n"
            "embed_data|sel_fg|{}\n"
            "embed_data|sel_bg|{}\n"
            "{}"
            "add_item_picker|pick_fg|`wChoose Foreground``|Choose a foreground block!|\n"
            "{}"
            "add_item_picker|pick_bg|`wChoose Background``|Choose a background block!|\n"
            "add_button|clear_bg|`wNo Background``|noflags|0|0|\n"
            "add_spacer|small|\n"
            "add_button|set_mascot|`wSet Mascot``|noflags|0|0|\n"
            "end_dialog|mt_guild|Close||\n",
            GUILD_LOCK, error.empty() ? "" : std::format("add_textbox|{}|left|\nadd_spacer|small|\n", error),
            gems_text(MASCOT_COST), MASCOT_LEVEL, MASCOT_MEMBERS, gems_text(MASCOT_FG_COST), MASCOT_FG_LEVEL, MASCOT_FG_MEMBERS,
            fg, bg, picked("Foreground", fg), picked("Background", bg)) });
    }

    /* @note the leader's buttons in the guild menu: Upgrade Guild and Guild Mascot */
    void leader_buttons(ENetEvent &event, guild &g, const ::hPipe &hPipe)
    {
        ::peer &p = me(event);
        const member *mine = member_of(g, p.growid);
        if (mine == nullptr || mine->rank != LEADER) return; // @note never trust the dialog
        const std::string clicked = hPipe["buttonClicked"];
        if (clicked == "upgrade")
        {
            if (g.level >= GUILD_MAX_LEVEL) return say(event, "`4Your guild is already at the max level.``");
            const long long xp = guild_xp_needed(g.level);
            const int gems = guild_gems_needed(g.level);
            if (g.xp < xp) return say(event, std::format("`4Your guild needs `w{}`` more XP. Members earn it by playing.``", gems_text64(xp - g.xp)));
            if (p.gems < gems) return say(event, std::format("`4Upgrading costs {} gems.``", gems_text(gems)));
            p.gems -= gems;
            econ("guild upgrade", -gems);
            on::SetBux(event);
            g.xp -= xp;
            ++g.level;
            save(g);
            to_guild(g, std::format("`5[GUILD]`` `2{} reached level {}!`` `5Up to `w{}`` members now.``", g.name, g.level, guild_max_members(g.level)));
            send_action(*event.peer, "play_sfx", "file|audio/levelup2.wav\ndelayMS|0\n");
            return info(event);
        }
        if (clicked == "mascot") return mascot_dialog(event, g.fg, g.bg, "");

        /* @note mascot dialog: what was picked so far, then this pick */
        auto item_id = [](const std::string &text) { const int id = atoi(text.c_str()); return (id > 0 && id < static_cast<int>(items.size())) ? id : 0; };
        int fg = item_id(hPipe["sel_fg"]), bg = item_id(hPipe["sel_bg"]);
        const bool picked_fg = !hPipe["pick_fg"].empty(), picked_bg = !hPipe["pick_bg"].empty();
        if (!picked_fg && !picked_bg && clicked != "clear_bg" && clicked != "set_mascot") return; // @note Close
        if (picked_fg)
        {
            const int id = item_id(hPipe["pick_fg"]);
            if (id == 0 || id_to_item(id).type != type::FOREGROUND) return mascot_dialog(event, fg, bg, "`4The foreground must be a foreground block.``");
            fg = id;
        }
        if (picked_bg)
        {
            const int id = item_id(hPipe["pick_bg"]);
            if (id == 0 || id_to_item(id).type != type::BACKGROUND) return mascot_dialog(event, fg, bg, "`4The background must be a background block.``");
            bg = id;
        }
        if (clicked == "clear_bg") bg = 0;
        if (clicked != "set_mascot") return mascot_dialog(event, fg, bg, "");

        /* @note growtopiawiki.com/w/Guilds: Level 3 + 15 members + 50,000 gems, or foreground only: Level 6 + 30 members + 250,000 gems */
        const bool fg_only = (bg == 0);
        const int cost = fg_only ? MASCOT_FG_COST : MASCOT_COST, level = fg_only ? MASCOT_FG_LEVEL : MASCOT_LEVEL;
        const std::size_t members = fg_only ? MASCOT_FG_MEMBERS : MASCOT_MEMBERS;
        auto owns = [&](int id) { return std::ranges::any_of(p.slots, [id](const ::slot &s) { return s.id == id && s.count > 0; }); };
        if (fg == 0) return mascot_dialog(event, fg, bg, "`4Choose a foreground block first.``");
        if (g.level < level) return mascot_dialog(event, fg, bg, std::format("`4Your guild must be level {} for this mascot.``", level));
        if (g.members.size() < members) return mascot_dialog(event, fg, bg, std::format("`4Your guild needs {} members for this mascot.``", members));
        if (!owns(fg) || (bg > 0 && !owns(bg))) return mascot_dialog(event, fg, bg, "`4You need the blocks in your backpack.``");
        if (std::ranges::any_of(guilds, [&](const guild &other) { return &other != &g && other.fg == fg && other.bg == bg; }))
            return mascot_dialog(event, fg, bg, "`4That mascot belongs to another guild.``");
        if (p.gems < cost) return mascot_dialog(event, fg, bg, std::format("`4This mascot costs {} gems.``", gems_text(cost)));
        p.gems -= cost;
        econ("guild mascot", -cost);
        on::SetBux(event);
        g.fg = fg; g.bg = bg;
        save(g);
        to_guild(g, std::format("`5[GUILD]`` `5New mascot: `w{}``{}``", id_to_item(fg).raw_name, bg > 0 ? std::format(" `5on `w{}``", id_to_item(bg).raw_name) : ""));
        for (const member &m : g.members) refresh(m.growid);
    }
}

void load_guilds()
{
    guilds.clear();
    ::hStmt hStmt{ "SELECT name, statement, fg, bg, members, created, world, access_ranks, lock_item, level, xp FROM guild" };
    char name[32]{}, statement[256]{}, members[4096]{}, world[32]{};
    u_long name_len{}, statement_len{}, members_len{}, world_len{};
    signed fg{}, bg{}, access{}, lock{}, level{};
    long long created{}, xp{};
    MYSQL_BIND result[11] = {};
    const auto string_out = [](MYSQL_BIND &bind, char *buffer, u_long size, u_long &length) {
        bind.buffer_type = MYSQL_TYPE_STRING; bind.buffer = buffer; bind.buffer_length = size; bind.length = &length;
    };
    string_out(result[0], name, sizeof(name), name_len);
    string_out(result[1], statement, sizeof(statement), statement_len);
    result[2] = make_bind_out(fg);
    result[3] = make_bind_out(bg);
    string_out(result[4], members, sizeof(members), members_len);
    result[5] = make_bind_out(created);
    string_out(result[6], world, sizeof(world), world_len);
    result[7] = make_bind_out(access);
    result[8] = make_bind_out(lock);
    result[9] = make_bind_out(level);
    result[10] = make_bind_out(xp);
    mysql_stmt_bind_result(hStmt.pStmt, result);
    hStmt.execute();
    mysql_stmt_store_result(hStmt.pStmt);
    while (mysql_stmt_fetch(hStmt.pStmt) == 0)
    {
        guild g{ .name = std::string(name, std::min<u_long>(name_len, sizeof(name))),
                 .statement = std::string(statement, std::min<u_long>(statement_len, sizeof(statement))),
                 .fg = fg, .bg = bg, .created = created,
                 .world = std::string(world, std::min<u_long>(world_len, sizeof(world))), .access = access, .lock = lock > 0 ? lock : WORLD_LOCK,
                 .level = std::clamp(level, 1, GUILD_MAX_LEVEL), .xp = std::max(xp, 0LL) };
        world_len = 0, access = 0, lock = 0, level = 0, xp = 0; // @note NULL columns (guilds from before Guild Locks) leave the old values
        for (const std::string &entry : readch(std::string(members, std::min<u_long>(members_len, sizeof(members))), ','))
        {
            const std::size_t colon = entry.find(':');
            if (colon == std::string::npos) continue;
            g.members.push_back(member{ entry.substr(0, colon), std::clamp(atoi(entry.c_str() + colon + 1), 0, 3) });
        }
        if (!g.members.empty()) guilds.push_back(std::move(g));
    }
    std::printf("loaded %zu guild(s)\n", guilds.size());
}

void guild_return(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer &p = me(event);
    const std::string clicked = hPipe["buttonClicked"];
    if (clicked == "back") return action::friends(event, "");
    if (guild *g = guild_of(p.growid)) return leader_buttons(event, *g, hPipe); // @note the buttons below are for players without a guild

    /* @note "Grow Guild Creation" as in Growtopia; the statement box starts with GT's hint text.
     * a refused Create Guild opens it again with the reason on top and what was typed (the console line was easy to miss) */
    auto creation = [&](const std::string &error, const std::string &name, const std::string &statement)
    {
        if (!error.empty()) say(event, error);
        send_varlist(event.peer, { "OnDialogRequest", std::format(
            "set_default_color|`o\n"
            "add_label_with_icon|big|`wGrow Guild Creation``|left|{}|\n"
            "{}"
            "add_text_input|guild_name|Guild Name:|{}|15|\n"
            "add_text_input|guild_statement|Guild Statement:|{}|100|\n"
            "add_spacer|small|\n"
            "add_textbox|Confirm your guild settings by selecting `2Create Guild`` below to create your guild.|left|\n"
            "add_textbox|`8Remember``: A guild can only be created in a world owned by you and locked with a `#World Lock``!|left|\n"
            "add_spacer|small|\n"
            "add_textbox|`4Warning!`` The guild name cannot be changed once you have confirmed the guild settings!|left|\n"
            "add_spacer|small|\n"
            "add_button|create|Create Guild|noflags|0|0|\n"
            "end_dialog|mt_guild|Close||\n", GUILD_LOCK, error.empty() ? "" : std::format("add_textbox|{}|left|\nadd_spacer|small|\n", error),
            name, statement.empty() ? "Enter a Guild statement here!" : statement) });
    };
    if (clicked == "create_guild") return creation("", "", "");
    if (clicked != "create") return;

    const std::string name = clean_text(hPipe["guild_name"], 15);
    std::string statement = clean_text(hPipe["guild_statement"], 100);
    if (statement == "Enter a Guild statement here!") statement.clear();
    auto fail = [&](const std::string &error) { creation(error, name, statement); };
    if (!valid_name(name)) return fail("`4Guild names are 3-15 letters or numbers.``");
    if (guild_named(name)) return fail("`4That guild name is taken.``");
    if (p.level[0] < CREATE_LEVEL) return fail(std::format("`4You need level {} to create a guild.``", CREATE_LEVEL));
    if (p.gems < CREATE_COST) return fail(std::format("`4Creating a guild costs {} gems.``", gems_text(CREATE_COST)));

    /* @note the world lock of a world you own becomes the Guild Lock and goes back to your backpack (growtopiawiki.com/w/Guild_Lock).
     * only a World Lock, like Growtopia: a disbanded guild's lock turns back into a World Lock, so a Diamond or Blue Gem Lock would
     * come back as a copy. with GUILD_LOCK_TILE off the lock stays where it is */
    auto world = std::ranges::find(worlds, p.recent_worlds.back(), &::world::name);
    if (p.netid == 0 || world == worlds.end() || is_system_world(world->name) || world->owner != p.user_id)
        return fail("`4A guild can only be created in a world owned by you and locked with a World Lock!``");
    if (is_guild_home(*world)) return fail("`4This world is already a guild's home.``");
    const ::pos lock = world_lock_of(*world);
    if (lock.x < 0) return fail("`4A guild can only be created in a world owned by you and locked with a World Lock!``");
    const short old_lock = world->blocks[cord(lock.x_int(), lock.y_int())].fg;
    if (old_lock != WORLD_LOCK) return fail("`4A guild can only be created in a world owned by you and locked with a World Lock!``");
    const auto held = std::ranges::find(p.slots, old_lock, &::slot::id);
    if (GUILD_LOCK_TILE && ((held != p.slots.end() && held->count >= 200) || (held == p.slots.end() && static_cast<int>(p.slots.size()) >= p.slot_size)))
        return fail(std::format("`4Make room in your backpack for the {} first.``", id_to_item(old_lock).raw_name));

    p.gems -= CREATE_COST;
    econ("guild create", -CREATE_COST);
    on::SetBux(event);
    guilds.push_back(guild{ .name = name, .statement = statement, .created = std::time(nullptr), .members = { member{ p.growid, LEADER } }, .world = world->name, .lock = old_lock });
    save(guilds.back());
    if (GUILD_LOCK_TILE)
    {
        world->blocks[cord(lock.x_int(), lock.y_int())].fg = GUILD_LOCK;
        modify_item_inventory(event, ::slot{ old_lock, 1 });
    }
    lock_refresh(*world, lock); // @note the Guild Lock, and guild members with access get it in the lock data
    say(event, std::format(GUILD_LOCK_TILE ? "`2Guild `w{}`` created!`` Your `w{}`` became a `#Guild Lock``. Wrench players to invite them, chat with `$/gc``." :
                                             "`2Guild `w{}`` created!`` `w{}`` is now your guild's home. Wrench players to invite them, chat with `$/gc``.",
                           name, GUILD_LOCK_TILE ? id_to_item(old_lock).raw_name : world->name));
    send_action(*event.peer, "play_sfx", "file|audio/levelup2.wav\ndelayMS|0\n");
    guild_broadcast(*event.peer);
}

bool is_guild_home(const ::world &world)
{
    return std::ranges::any_of(guilds, [&](const guild &g) { return !g.world.empty() && g.world == world.name; });
}

void guild_lock_fix(::world &world)
{
    if (GUILD_LOCK_TILE && is_guild_home(world)) return;
    for (::pos tile = tile_of(world, GUILD_LOCK); tile.x >= 0; tile = tile_of(world, GUILD_LOCK))
        world.blocks[cord(tile.x_int(), tile.y_int())].fg = WORLD_LOCK; // @note same as disbanding (erase): never the bigger lock it once replaced
}

bool guild_access(const ::peer &p, const ::world &world)
{
    for (const guild &g : guilds)
    {
        if (g.world.empty() || g.world != world.name) continue;
        for (const member &m : g.members)
            if (lower(m.growid) == lower(p.growid)) return m.rank == LEADER || (g.access & (1 << m.rank));
        return false;
    }
    return false;
}

std::vector<int> guild_access_ids(const ::world &world)
{
    std::vector<int> ids{};
    for (const guild &g : guilds)
    {
        if (g.world.empty() || g.world != world.name) continue;
        for (const member &m : g.members)
            if (ENetPeer *p = online(m.growid))
                if (const ::peer *pp = static_cast<::peer*>(p->data); guild_access(*pp, world) && pp->user_id != world.owner) ids.push_back(pp->user_id);
    }
    return ids;
}

std::string guild_lock_section(const ::world &world)
{
    for (const guild &g : guilds)
        if (!g.world.empty() && g.world == world.name)
            return std::format(
                "add_spacer|small|\n"
                "add_label|small|`wGuild access (`w{}``):``|left\n"
                "add_smalltext|Guild members with a ticked rank can build and break here.|left|\n"
                "add_checkbox|guild_access_0|Members|{}\n"
                "add_checkbox|guild_access_1|Elders|{}\n"
                "add_checkbox|guild_access_2|Co-Leaders|{}\n",
                g.name, (g.access & 1) ? 1 : 0, (g.access & 2) ? 1 : 0, (g.access & 4) ? 1 : 0);
    return "";
}

void guild_lock_edit(const ::world &world, const ::hPipe &hPipe)
{
    for (guild &g : guilds)
    {
        if (g.world.empty() || g.world != world.name) continue;
        for (int r = MEMBER; r <= CO_LEADER; ++r)
        {
            const std::string ticked = hPipe[std::format("guild_access_{}", r)];
            if (ticked == "1") g.access |= (1 << r);
            else if (ticked == "0") g.access &= ~(1 << r); // @note missing = not in the dialog, left as it was
        }
        return save(g);
    }
}

bool in_guild(const std::string &growid) { return !growid.empty() && guild_of(growid) != nullptr; }

bool guild_flag_shown(const std::string &growid)
{
    return SHOW_FLAG && in_guild(growid);
}

void guild_add_xp(const std::string &growid, int xp)
{
    if (xp <= 0 || growid.empty()) return;
    guild *g = guild_of(growid);
    if (g == nullptr || g->level >= GUILD_MAX_LEVEL) return;
    g->xp += xp;
    g->unsaved = true;
}

void save_guilds()
{
    for (guild &g : guilds) if (g.unsaved) save(g);
}

void guild_show(ENetPeer &viewer, const ::peer &target)
{
    if (!guild_flag_shown(target.growid)) return;
    const guild *g = guild_of(target.growid);
    const member *m = member_of(const_cast<guild&>(*g), target.growid);
    /* @note GTOS: guild id, guild hash (values from GTOS), flag = 65536 * background + foreground, rank */
    send_varlist(&viewer, { "OnGuildDataChanged", 36095, 63158650, 65536 * g->bg + g->fg, m->rank }, target.netid);
}

void guild_broadcast(ENetPeer &target)
{
    ::peer *pTarget = static_cast<::peer*>(target.data);
    if (pTarget == nullptr || pTarget->netid == 0) return;
    ENetEvent fake{ .peer = &target };
    on::CountryState(fake); // @note |showGuild on or off for everyone in the world
    if (!SHOW_FLAG) return;
    const bool has = in_guild(pTarget->growid);
    peers(pTarget->recent_worlds.back(), PEER_SAME_WORLD, [&](ENetPeer &p)
    {
        if (has) guild_show(p, *pTarget);
        else send_varlist(&p, { "OnGuildDataChanged", 0, 0, 0, 0 }, pTarget->netid); // @note GTOS clears it with zeros
    });
}

void gc_command(ENetEvent &event, const std::string_view text)
{
    ::peer &p = me(event);
    const guild *g = guild_of(p.growid);
    if (g == nullptr) return say(event, "`4You're not in a guild.``");
    const std::size_t space = text.find(' ');
    if (space == std::string_view::npos) return say(event, "`oUsage: /gc {message}``");
    const std::string message = clean_text(std::string{ text.substr(space + 1) }, 120);
    if (message.empty()) return;
    to_guild(*g, std::format("`5[GUILD]`` {}`5:`` `${}``", p.display_growid, message));
}

void guild_command(ENetEvent &event, const std::string_view text)
{
    ::peer &p = me(event);
    const std::vector<std::string> args = readch(std::string{ text }, ' ');
    const std::string sub = (args.size() >= 2) ? lower(args[1]) : "";
    const std::string arg = (args.size() >= 3) ? args[2] : "";
    guild *g = guild_of(p.growid);
    member *mine = g ? member_of(*g, p.growid) : nullptr;

    if (sub.empty() || sub == "info") return info(event);

    if (sub == "create") return say(event, "`oCreate a guild from the `wSocial Portal`` > `wCreate Guild``.");
    if (sub == "accept")
    {
        auto it = invites.find(lower(p.growid));
        if (it == invites.end() || it->second.until < std::time(nullptr)) return say(event, "`4You have no guild invite (they last 2 minutes).``");
        if (g) return say(event, "`4You're already in a guild.``");
        guild *target = guild_named(it->second.guild);
        invites.erase(it);
        if (target == nullptr) return say(event, "`4That guild doesn't exist anymore.``");
        if (target->members.size() >= guild_max_members(target->level)) return say(event, "`4That guild is full.``");
        target->members.push_back(member{ p.growid, MEMBER });
        save(*target);
        to_guild(*target, std::format("`5[GUILD]`` {} `5joined the guild!``", p.display_growid));
        return guild_broadcast(*event.peer);
    }

    if (g == nullptr) return say(event, "`4You're not in a guild.`` Type `$/guild`` for help.");

    if (sub == "leave")
    {
        if (mine->rank == LEADER && g->members.size() > 1) return say(event, "`4Give the guild to someone first (`$/guild leader {growid}``) or `$/guild disband``.");
        const std::string name = g->name;
        std::erase_if(g->members, [&](const member &m) { return lower(m.growid) == lower(p.growid); });
        if (g->members.empty()) erase(name);
        else { save(*g); to_guild(*g, std::format("`5[GUILD]`` {} `5left the guild.``", p.display_growid)); }
        say(event, std::format("`oYou left `w{}``.", name));
        return guild_broadcast(*event.peer);
    }
    if (sub == "disband")
    {
        if (mine->rank != LEADER) return say(event, "`4Only the leader can disband the guild.``");
        if (args.size() < 3 || lower(arg) != "confirm") return say(event, "`4This deletes the guild for everyone!`` Type `$/guild disband confirm`` to do it.");
        const guild copy = *g;
        to_guild(copy, std::format("`5[GUILD]`` `4{} was disbanded by the leader.``", copy.name));
        erase(copy.name);
        for (const member &m : copy.members) refresh(m.growid);
        return;
    }
    if (sub == "statement")
    {
        if (mine->rank < CO_LEADER) return say(event, "`4Only the leader and co-leaders can change the statement.``");
        const std::size_t at = text.find(' ', text.find(' ') + 1);
        g->statement = (at == std::string_view::npos) ? "" : clean_text(std::string{ text.substr(at + 1) }, 100);
        save(*g);
        return to_guild(*g, std::format("`5[GUILD]`` {} `5changed the statement: ``{}", p.display_growid, g->statement));
    }
    if (sub == "flag" || sub == "mascot") return say(event, "`oThe leader sets the mascot from the guild menu (`$/guild`` > `wGuild Mascot``).");

    /* @note everything below is about another member */
    if (arg.empty()) return say(event, std::format("`oUsage: /guild {} {{growid}}``", sub));
    if (sub == "invite")
    {
        if (mine->rank < ELDER) return say(event, "`4Only Elders and up can invite.``"); // @note growtopiawiki.com/w/Guilds: Elders, Co-Leaders and the Leader
        if (g->members.size() >= guild_max_members(g->level)) return say(event, std::format("`4Your guild is full ({} members). Level it up for more room.``", guild_max_members(g->level)));
        ENetPeer *target = online(arg);
        if (target == nullptr) return say(event, "`4That player isn't online.``");
        const ::peer &t = *static_cast<::peer*>(target->data);
        if (in_guild(t.growid)) return say(event, "`4That player is already in a guild.``");
        invites[lower(t.growid)] = invite{ g->name, std::time(nullptr) + INVITE_TIME };
        on::ConsoleMessage(target, std::format("`5[GUILD]`` {} `5invited you to `w{}``! Type `$/guild accept`` within 2 minutes to join.``", p.display_growid, g->name));
        send_varlist(target, { "OnDialogRequest", std::format(
            "set_default_color|`o\n"
            "add_label_with_icon|big|`wGuild Invite``|left|{}|\n"
            "add_textbox|{} `oinvited you to join the guild `w{}``!|left|\n"
            "add_smalltext|`o{}``|left|\n"
            "add_spacer|small|\n"
            "add_button|accept|`2Accept``|noflags|0|0|\n"
            "add_button|decline|`4Decline``|noflags|0|0|\n"
            "end_dialog|mt_guild_invite|Close||\n", GUILD_LOCK, p.display_growid, g->name, g->statement) });
        send_action(*target, "play_sfx", "file|audio/friend_beep.wav\ndelayMS|0\n");
        return say(event, std::format("`oInvite sent to `w{}``.", t.growid));
    }
    member *other = member_of(*g, arg);
    if (other == nullptr) return say(event, "`4That player isn't in your guild.``");
    if (lower(other->growid) == lower(p.growid)) return say(event, "`4You can't do that to yourself.``");

    if (sub == "kick")
    {
        if (mine->rank < CO_LEADER || other->rank >= mine->rank) return say(event, "`4You can only kick members below your rank.``");
        const std::string name = other->growid;
        std::erase_if(g->members, [&](const member &m) { return lower(m.growid) == lower(name); });
        save(*g);
        to_guild(*g, std::format("`5[GUILD]`` `w{}`` `5was kicked by ``{}`5.``", name, p.display_growid));
        if (ENetPeer *t = online(name)) on::ConsoleMessage(t, std::format("`4You were kicked from {}.``", g->name));
        return refresh(name);
    }
    if (sub == "promote" || sub == "demote")
    {
        /* @note growtopiawiki.com/w/Guilds: you promote up to one rank below yours (Co-Leaders make Elders, the Leader makes Elders and Co-Leaders).
         * demoting: only someone below your rank, Co-Leader and up (a guess) */
        const int next = other->rank + (sub == "promote" ? 1 : -1);
        if (next < MEMBER || next > CO_LEADER) return say(event, std::format("`4{} can't go {}.``", other->growid, sub == "promote" ? "higher (use /guild leader to hand over the guild)" : "lower"));
        if (mine->rank < CO_LEADER || other->rank >= mine->rank || (sub == "promote" && next >= mine->rank))
            return say(event, "`4You can only change the rank of members below you, up to one rank below yours.``");
        other->rank = next;
        save(*g);
        to_guild(*g, std::format("`5[GUILD]`` `w{}`` `5is now `w{}``.``", other->growid, RANK_NAME[next]));
        return refresh(other->growid);
    }
    if (sub == "leader")
    {
        if (mine->rank != LEADER) return say(event, "`4Only the leader can hand over the guild.``");
        /* @note the home world goes with the guild (GT uses a Guild Key for this) */
        if (::world *home = world_named(g->world))
        {
            const int uid = uid_of(other->growid);
            if (uid == 0) return say(event, "`4That player can't be found.``");
            if (home->owner == p.user_id)
            {
                home->owner = uid;
                home->access.fill(0);
                if (auto old = std::ranges::find(p.my_worlds, home->name); old != p.my_worlds.end()) old->clear();
                if (ENetPeer *t = online(other->growid))
                    if (::peer &pt = *static_cast<::peer*>(t->data); std::ranges::find(pt.my_worlds, home->name) == pt.my_worlds.end())
                    {
                        std::ranges::rotate(pt.my_worlds, pt.my_worlds.begin() + 1);
                        pt.my_worlds.back() = home->name;
                    }
                if (const ::pos lock = home_lock_of(*home); lock.x >= 0) lock_refresh(*home, lock);
            }
        }
        other->rank = LEADER;
        mine->rank = CO_LEADER;
        save(*g);
        to_guild(*g, std::format("`5[GUILD]`` `w{}`` `5is the new leader!``", other->growid));
        refresh(other->growid);
        return refresh(p.growid);
    }
    say(event, "`4Unknown guild command.`` Type `$/guild`` for help.");
}

std::string guild_wrench_lines(const ::peer &viewer, const ::peer &target)
{
    std::string lines{};
    guild *theirs = guild_of(target.growid);
    if (theirs != nullptr) // @note growtopiawiki.com/w/Guilds: wrenching a guild member shows their guild, rank and a Guild Lock
        lines += std::format("add_spacer|small|\nadd_label_with_icon|small|`wGuild: {}`` `o({})``|left|{}|\n", theirs->name, RANK_NAME[member_of(*theirs, target.growid)->rank], GUILD_LOCK);

    guild *mine = guild_of(viewer.growid);
    if (mine == nullptr) return lines;
    const int me_rank = member_of(*mine, viewer.growid)->rank;
    std::string buttons{};
    if (theirs == nullptr && me_rank >= ELDER) buttons += "add_button|guild_invite|`wInvite to Guild``|noflags|0|0|\n";
    else if (theirs == mine)
    {
        const int rank = member_of(*mine, target.growid)->rank;
        if (me_rank >= CO_LEADER && rank < me_rank)
        {
            if (rank + 1 < me_rank && rank + 1 <= CO_LEADER) buttons += std::format("add_button|guild_promote|`wPromote to {}``|noflags|0|0|\n", RANK_NAME[rank + 1]);
            if (rank > MEMBER) buttons += std::format("add_button|guild_demote|`wDemote to {}``|noflags|0|0|\n", RANK_NAME[rank - 1]);
            buttons += "add_button|guild_kick|`4Kick from Guild``|noflags|0|0|\n";
        }
    }
    return buttons.empty() ? lines : lines + "add_spacer|small|\n" + buttons;
}

std::string guild_self_lines(const ::peer &self)
{
    guild *g = guild_of(self.growid);
    if (g == nullptr) return "";
    return std::format(
        "add_label_with_icon|small|`wGuild: {}`` `o({})``|left|{}|\n"
        "add_button|guild_info|`wShow Guild``|noflags|0|0|\n"
        "add_spacer|small|\n", g->name, RANK_NAME[member_of(*g, self.growid)->rank], GUILD_LOCK);
}

bool guild_wrench_button(ENetEvent &event, const ::hPipe &hPipe)
{
    const std::string clicked = hPipe["buttonClicked"];
    if (clicked == "guild_info") { info(event); return true; } // @note Show Guild in your own wrench menu
    if (!clicked.starts_with("guild_")) return false;
    const std::string sub = clicked.substr(6);
    if (sub != "invite" && sub != "promote" && sub != "demote" && sub != "kick") return false;
    ::peer &p = me(event);
    if (p.netid == 0) return true;
    const int netid = atoi(hPipe["netID"].c_str());
    for (ENetPeer *other : peers(p.recent_worlds.back(), PEER_SAME_WORLD)) // @note only someone in your world, as the wrench menu shows
        if (const ::peer *t = static_cast<::peer*>(other->data); t->netid == netid && !t->growid.empty())
        {
            guild_command(event, std::format("guild {} {}", sub, t->growid)); // @note same rules as the commands
            break;
        }
    return true;
}

void guild_invite_return(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer &p = me(event);
    const std::string clicked = hPipe["buttonClicked"];
    if (clicked == "accept") return guild_command(event, "guild accept");
    if (clicked != "decline") return;
    auto it = invites.find(lower(p.growid));
    if (it == invites.end()) return;
    if (const guild *g = guild_named(it->second.guild)) to_guild(*g, std::format("`5[GUILD]`` {} `5declined the invite.``", p.display_growid));
    invites.erase(it);
    say(event, "`oYou declined the guild invite.``");
}
