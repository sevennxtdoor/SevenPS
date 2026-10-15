#include "pch.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "onVariant/CountryState.hpp"
#include "onVariant/SetBux.hpp"
#include "tools/economy.hpp"
#include "tools/wrench_menu.hpp" // @note clean_text()

#include "guild.hpp"

namespace
{
    constexpr int CREATE_COST = 50000; // @note a big gem sink on purpose (25 World Locks)
    constexpr int CREATE_LEVEL = 5;
    constexpr std::size_t MAX_MEMBERS = 20;
    constexpr int INVITE_TIME = 120;   // @note seconds to accept

    enum rank : int { MEMBER, ELDER, CO_LEADER, LEADER };
    constexpr std::array<std::string_view, 4> RANK_NAME{ "Member", "Elder", "Co-Leader", "Leader" };

    struct member { std::string growid; int rank{}; };
    struct guild
    {
        std::string name{}, statement{};
        int fg{}, bg{};
        long long created{};
        std::vector<member> members{};
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

    void save(const guild &g)
    {
        ::hStmt hStmt{ "REPLACE INTO guild (name, statement, fg, bg, members, created) VALUES (?, ?, ?, ?, ?, ?)" };
        const std::string members = members_text(g);
        MYSQL_BIND params[6] = { make_bind_in(g.name), make_bind_in(g.statement), make_bind_in(g.fg), make_bind_in(g.bg), make_bind_in(members), make_bind_in(g.created) };
        hStmt.bind_param(params);
        hStmt.execute();
    }

    void erase(const std::string &name)
    {
        ::hStmt hStmt{ "DELETE FROM guild WHERE name = ?" };
        MYSQL_BIND param = make_bind_in(name);
        hStmt.bind_param(&param);
        hStmt.execute();
        std::erase_if(guilds, [&](const guild &g) { return g.name == name; });
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
            return send_varlist(event.peer, { "OnDialogRequest", std::format(
                "set_default_color|`o\n"
                "add_label_with_icon|big|`wGuilds``|left|5814|\n"
                "add_textbox|You're not in a guild yet.|left|\n"
                "add_spacer|small|\n"
                "add_textbox|`w/guild create {{name}}`` - start your own guild (`w{}`` gems, level `w{}``+)|left|\n"
                "add_textbox|`w/guild accept`` - join a guild that invited you|left|\n"
                "end_dialog|mt_guild||Close|\n", CREATE_COST, CREATE_LEVEL) });
        }
        const member *mine = member_of(*g, p.growid);
        std::string list{};
        for (int r = LEADER; r >= MEMBER; --r)
            for (const member &m : g->members)
                if (m.rank == r)
                    list += std::format("add_textbox|{}`w{}`` `o- {}``|left|\n", online(m.growid) ? "`2(online)`` " : "", m.growid, RANK_NAME[r]);
        send_varlist(event.peer, { "OnDialogRequest", std::format(
            "set_default_color|`o\n"
            "add_label_with_icon|big|`w{}``|left|5814|\n"
            "add_textbox|`o{}``|left|\n"
            "add_smalltext|Your rank: `w{}``. Members: `w{}/{}``|left|\n"
            "add_spacer|small|\n"
            "{}"
            "add_spacer|small|\n"
            "add_smalltext|/gc {{message}} guild chat, /guild invite, kick, promote, demote or leader {{growid}}, /guild statement {{text}}, /guild flag {{item id}} {{item id}}, /guild leave, /guild disband|left|\n"
            "end_dialog|mt_guild||Close|\n",
            g->name, g->statement.empty() ? "No statement yet." : g->statement, RANK_NAME[mine->rank], g->members.size(), MAX_MEMBERS, list) });
    }
}

void load_guilds()
{
    guilds.clear();
    ::hStmt hStmt{ "SELECT name, statement, fg, bg, members, created FROM guild" };
    char name[32]{}, statement[256]{}, members[4096]{};
    u_long name_len{}, statement_len{}, members_len{};
    signed fg{}, bg{};
    long long created{};
    MYSQL_BIND result[6] = {};
    const auto string_out = [](MYSQL_BIND &bind, char *buffer, u_long size, u_long &length) {
        bind.buffer_type = MYSQL_TYPE_STRING; bind.buffer = buffer; bind.buffer_length = size; bind.length = &length;
    };
    string_out(result[0], name, sizeof(name), name_len);
    string_out(result[1], statement, sizeof(statement), statement_len);
    result[2] = make_bind_out(fg);
    result[3] = make_bind_out(bg);
    string_out(result[4], members, sizeof(members), members_len);
    result[5] = make_bind_out(created);
    mysql_stmt_bind_result(hStmt.pStmt, result);
    hStmt.execute();
    mysql_stmt_store_result(hStmt.pStmt);
    while (mysql_stmt_fetch(hStmt.pStmt) == 0)
    {
        guild g{ .name = std::string(name, std::min<u_long>(name_len, sizeof(name))),
                 .statement = std::string(statement, std::min<u_long>(statement_len, sizeof(statement))),
                 .fg = fg, .bg = bg, .created = created };
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

bool in_guild(const std::string &growid) { return !growid.empty() && guild_of(growid) != nullptr; }

void guild_show(ENetPeer &viewer, const ::peer &target)
{
    const guild *g = guild_of(target.growid);
    if (g == nullptr) return;
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

    if (sub == "create")
    {
        if (g) return say(event, "`4You're already in a guild.`` Leave it first.");
        if (!valid_name(arg)) return say(event, "`4Guild names are 3-15 letters or numbers.``");
        if (guild_named(arg)) return say(event, "`4That guild name is taken.``");
        if (p.level[0] < CREATE_LEVEL) return say(event, std::format("`4You need level {} to create a guild.``", CREATE_LEVEL));
        if (p.gems < CREATE_COST) return say(event, std::format("`4Creating a guild costs {} gems.``", CREATE_COST));
        p.gems -= CREATE_COST;
        econ("guild create", -CREATE_COST);
        on::SetBux(event);
        guilds.push_back(guild{ .name = arg, .created = std::time(nullptr), .members = { member{ p.growid, LEADER } } });
        save(guilds.back());
        say(event, std::format("`2Guild `w{}`` created!`` Invite friends with `$/guild invite {{growid}}``, chat with `$/gc``.", arg));
        send_action(*event.peer, "play_sfx", "file|audio/levelup2.wav\ndelayMS|0\n");
        return guild_broadcast(*event.peer);
    }
    if (sub == "accept")
    {
        auto it = invites.find(lower(p.growid));
        if (it == invites.end() || it->second.until < std::time(nullptr)) return say(event, "`4You have no guild invite (they last 2 minutes).``");
        if (g) return say(event, "`4You're already in a guild.``");
        guild *target = guild_named(it->second.guild);
        invites.erase(it);
        if (target == nullptr) return say(event, "`4That guild doesn't exist anymore.``");
        if (target->members.size() >= MAX_MEMBERS) return say(event, "`4That guild is full.``");
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
    if (sub == "flag")
    {
        if (mine->rank != LEADER) return say(event, "`4Only the leader can change the flag.``");
        if (args.size() < 4) return say(event, "`oUsage: /guild flag {foreground item id} {background item id} - you must own both items.``");
        const int fg = atoi(args[2].c_str()), bg = atoi(args[3].c_str());
        auto owns = [&](int id) { return id > 0 && std::ranges::any_of(p.slots, [id](const ::slot &s) { return s.id == id && s.count > 0; }); };
        if (!owns(fg) || !owns(bg)) return say(event, "`4You need both items in your backpack.``");
        g->fg = fg; g->bg = bg;
        save(*g);
        to_guild(*g, std::format("`5[GUILD]`` `5New flag: ``{} `5on ``{}", id_to_item(fg).raw_name, id_to_item(bg).raw_name));
        for (const member &m : g->members) refresh(m.growid);
        return;
    }

    /* @note everything below is about another member */
    if (arg.empty()) return say(event, std::format("`oUsage: /guild {} {{growid}}``", sub));
    if (sub == "invite")
    {
        if (mine->rank < CO_LEADER) return say(event, "`4Only the leader and co-leaders can invite.``");
        if (g->members.size() >= MAX_MEMBERS) return say(event, std::format("`4Your guild is full ({} members).``", MAX_MEMBERS));
        ENetPeer *target = online(arg);
        if (target == nullptr) return say(event, "`4That player isn't online.``");
        const ::peer &t = *static_cast<::peer*>(target->data);
        if (in_guild(t.growid)) return say(event, "`4That player is already in a guild.``");
        invites[lower(t.growid)] = invite{ g->name, std::time(nullptr) + INVITE_TIME };
        on::ConsoleMessage(target, std::format("`5[GUILD]`` {} `5invited you to `w{}``! Type `$/guild accept`` within 2 minutes to join.``", p.display_growid, g->name));
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
        if (mine->rank != LEADER) return say(event, "`4Only the leader can change ranks.``");
        const int next = other->rank + (sub == "promote" ? 1 : -1);
        if (next < MEMBER || next > CO_LEADER) return say(event, std::format("`4{} can't go {}.``", other->growid, sub == "promote" ? "higher (use /guild leader to hand over the guild)" : "lower"));
        other->rank = next;
        save(*g);
        to_guild(*g, std::format("`5[GUILD]`` `w{}`` `5is now `w{}``.``", other->growid, RANK_NAME[next]));
        return refresh(other->growid);
    }
    if (sub == "leader")
    {
        if (mine->rank != LEADER) return say(event, "`4Only the leader can hand over the guild.``");
        other->rank = LEADER;
        mine->rank = CO_LEADER;
        save(*g);
        to_guild(*g, std::format("`5[GUILD]`` `w{}`` `5is the new leader!``", other->growid));
        refresh(other->growid);
        return refresh(p.growid);
    }
    say(event, "`4Unknown guild command.`` Type `$/guild`` for help.");
}
