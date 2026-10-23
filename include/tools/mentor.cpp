#include "pch.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "onVariant/SetBux.hpp"
#include "tools/profile.hpp"   // @note pget/pset: tickets given, milestones claimed, daily add, leave cooldown
#include "tools/economy.hpp"
#include "action/friends.hpp"  // @note Back -> Social Portal
#include "tools/guard.hpp"     // @note peer_ip()

#include "mentor.hpp"

namespace
{
    /* @note ubisoft-mobile.helpshift.com, Growtopia > Mentorship Program */
    constexpr int MENTOR_LEVEL = 20;      // @note "You need to reach level 20 to become a Mentor"
    constexpr int APPRENTICE_BELOW = 3;   // @note "If you are Level 3 and above you cannot be an Apprentice anymore"
    constexpr int MAX_ACTIVE = 50;        // @note 50 active apprentices, one added per day
    constexpr int LEAVE_BEFORE = 20;      // @note "You cannot leave a mentor once you reach Level 20"
    constexpr int DONE_LEVEL = 30;        // @note completed; the mentor can only archive them now
    constexpr int REJOIN_WAIT = 7 * 86400; // @note no new mentor for 7 days after leaving one
    constexpr int INVITE_TIME = 120;      // @note seconds to accept (a guess, the same as guild invites)
    constexpr std::array<int, 5> TICKET_LEVELS{ 1, 5, 10, 20, 30 };
    constexpr short APPRENTICE_TICKET = 7788, MENTOR_TICKET = 7798; // @note level 1; the next levels follow 2 ids apart (seeds in between)
    constexpr short CERTIFICATE = 7786; // @note Mentorship Certificate, only its icon

    /* @note growtopiawiki.com/w/Guide:Mentorship, counted from apprentices that reached level 20.
     * GT's 1,000 tier also needs 250 recruits and the 2,000 / 4,000 tiers aren't items; SevenPS has no recruits, so it stops at 1,000 */
    struct milestone { int apprentices; int gems; short item; };
    constexpr milestone milestones[]{
        { 1, 1000, 0 }, { 5, 10000, 0 }, { 15, 30000, 0 }, { 30, 60000, 0 }, { 50, 0, 11304 /*Royal Grow Pass Token*/ },
        { 100, 100000, 0 }, { 200, 0, 8188 /*1-Year Subscription Token*/ }, { 500, 0, 9470 /*Wacky Rocket Sled*/ }, { 1000, 0, 9472 /*Dumb Mentor Title*/ },
    };

    struct link
    {
        std::string apprentice{}, mentor{};
        int level{ 1 };      // @note the apprentice's level when last seen
        bool archived{};
        int pending{};       // @note bit per ticket step: Mentor Tickets the mentor still has to get
        long long since{};
    };
    std::vector<link> links{};
    struct invite { std::string mentor; std::time_t until{}; };
    std::unordered_map<std::string, invite> invites{}; // @note lower-case apprentice growid -> invite

    ::peer &me(ENetEvent &event) { return *static_cast<::peer*>(event.peer->data); }
    std::string lower(std::string s) { for (char &c : s) c = static_cast<char>(std::tolower(static_cast<u_char>(c))); return s; }
    void say(ENetPeer *p, const std::string &text) { if (p) on::ConsoleMessage(p, text); }

    ENetPeer *online(const std::string &growid)
    {
        for (ENetPeer *p : peers())
            if (const ::peer *pp = static_cast<::peer*>(p->data); pp && !pp->growid.empty() && lower(pp->growid) == lower(growid)) return p;
        return nullptr;
    }

    link *link_of(const std::string &apprentice)
    {
        for (link &l : links) if (lower(l.apprentice) == lower(apprentice)) return &l;
        return nullptr;
    }
    bool is_mentor_of(const link &l, const std::string &growid) { return lower(l.mentor) == lower(growid); }
    int active_count(const std::string &mentor)
    {
        return static_cast<int>(std::ranges::count_if(links, [&](const link &l) { return is_mentor_of(l, mentor) && !l.archived; }));
    }
    int graduated_count(const std::string &mentor) // @note apprentices at level 20+, what the milestones count
    {
        return static_cast<int>(std::ranges::count_if(links, [&](const link &l) { return is_mentor_of(l, mentor) && l.level >= 20; }));
    }

    void save(const link &l)
    {
        ::hStmt hStmt{ "REPLACE INTO mentorship (apprentice, mentor, level, archived, pending, since) VALUES (?, ?, ?, ?, ?, ?)" };
        const signed archived = l.archived ? 1 : 0;
        MYSQL_BIND params[6] = { make_bind_in(l.apprentice), make_bind_in(l.mentor), make_bind_in(l.level), make_bind_in(archived), make_bind_in(l.pending), make_bind_in(l.since) };
        hStmt.bind_param(params);
        hStmt.execute();
    }
    void erase(const std::string &apprentice)
    {
        ::hStmt hStmt{ "DELETE FROM mentorship WHERE apprentice = ?" };
        MYSQL_BIND param = make_bind_in(apprentice);
        hStmt.bind_param(&param);
        hStmt.execute();
        std::erase_if(links, [&](const link &l) { return lower(l.apprentice) == lower(apprentice); });
    }

    std::string number_text(int n) // @note 10000 -> "10,000"
    {
        std::string s = std::to_string(n);
        for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(i, ",");
        return s;
    }

    /* @note into the backpack if it fits (a free slot or a stack with room), else nothing is given */
    bool give(ENetPeer &p, short id, int count)
    {
        ::peer &pp = *static_cast<::peer*>(p.data);
        auto slot = std::ranges::find(pp.slots, id, &::slot::id);
        if (slot == pp.slots.end() ? static_cast<int>(pp.slots.size()) >= pp.slot_size : slot->count + count > 200) return false;
        ENetEvent event{ .peer = &p };
        modify_item_inventory(event, ::slot(id, static_cast<short>(count)));
        return true;
    }

    /* @note Apprentice Tickets for levels reached, Mentor Tickets waiting for this mentor */
    void grant_tickets(ENetPeer &p)
    {
        ::peer &pp = *static_cast<::peer*>(p.data);
        if (pp.growid.empty()) return;
        if (link_of(pp.growid) != nullptr)
        {
            int given = pget(pp, "appr_tickets");
            for (std::size_t step = 0; step < TICKET_LEVELS.size(); ++step)
            {
                if ((given & (1 << step)) || pp.level.front() < TICKET_LEVELS[step]) continue;
                const short id = static_cast<short>(APPRENTICE_TICKET + step * 2);
                if (!give(p, id, 1)) { say(&p, std::format("`4No room for your `w{}``! Make room and open `wShow Apprentices``.``", id_to_item(id).raw_name)); break; }
                given |= 1 << step;
                say(&p, std::format("`5[MENTOR]`` You received an `w{}``!", id_to_item(id).raw_name));
            }
            pset(pp, "appr_tickets", given);
        }
        for (link &l : links)
        {
            if (!is_mentor_of(l, pp.growid) || l.pending == 0) continue;
            for (std::size_t step = 0; step < TICKET_LEVELS.size(); ++step)
            {
                if (!(l.pending & (1 << step))) continue;
                const short id = static_cast<short>(MENTOR_TICKET + step * 2);
                if (!give(p, id, 1)) { say(&p, std::format("`4No room for your `w{}``! Make room and open `wShow Apprentices``.``", id_to_item(id).raw_name)); save(l); return; }
                l.pending &= ~(1 << step);
                say(&p, std::format("`5[MENTOR]`` {} `5is making progress!`` You received a `w{}``!", l.apprentice, id_to_item(id).raw_name));
            }
            save(l);
        }
    }

    /* @note why `m` can't add `t` as an apprentice right now, "" if they can */
    std::string cannot_add(ENetPeer &mentor, ENetPeer &target)
    {
        const ::peer &m = *static_cast<::peer*>(mentor.data), &t = *static_cast<::peer*>(target.data);
        if (m.growid.empty() || t.growid.empty()) return "`4You both need a GrowID.``";
        if (m.level.front() < MENTOR_LEVEL) return std::format("`4You need to be level {} to be a Mentor.``", MENTOR_LEVEL);
        if (t.level.front() >= APPRENTICE_BELOW) return std::format("`4Only players below level {} can become apprentices.``", APPRENTICE_BELOW);
        if (link_of(t.growid) != nullptr) return "`4That player already has a Mentor.``";
        if (active_count(m.growid) >= MAX_ACTIVE) return std::format("`4You already have {} active apprentices. Archive the ones that reached level {}.``", MAX_ACTIVE, DONE_LEVEL);
        if (pget(m, "mentor_day") == static_cast<int>(std::time(nullptr) / 86400)) return "`4You can only add one apprentice per day.``";
        if (pget(t, "mentor_left") + REJOIN_WAIT > std::time(nullptr)) return "`4That player left a Mentor less than 7 days ago.``";
        /* @note SevenPS: GT forbids adding your own accounts; the same connection is the part we can see (not on a local test server) */
        if (const std::string ip = peer_ip(mentor); ip == peer_ip(target) && !ip.starts_with("127.")) return "`4You can't add an account from your own connection.``";
        return "";
    }

    void send(ENetEvent &event, const std::string &body)
    {
        send_varlist(event.peer, { "OnDialogRequest", "set_default_color|`o\n" + body + "add_quick_exit|\nend_dialog|mt_mentor|Close||\n" });
    }

    void apprentice_dialog(ENetEvent &event, const link &l)
    {
        const bool done = l.level >= DONE_LEVEL;
        send(event, std::format(
            "add_label_with_icon|big|`w{}``|left|18|\n"
            "add_spacer|small|\n"
            "add_textbox|Level: `w{}``|left|\n"
            "add_textbox|Apprentice since `w{}`` days ago.|left|\n"
            "add_smalltext|{}|left|\n"
            "add_spacer|small|\n"
            "{}"
            "add_button|show|Back|noflags|0|0|\n",
            l.apprentice, l.level, (std::time(nullptr) - l.since) / 86400,
            done ? "They finished the Mentorship Program! Archive them to free a slot." : "Apprentices can't be removed once they reach level 30, only archived.",
            done ? std::format("add_button|archive_{}|`wArchive Apprentice``|noflags|0|0|\n", l.apprentice)
                 : std::format("add_button|remove_{}|`4Remove Apprentice``|noflags|0|0|\n", l.apprentice)));
    }

    void milestones_dialog(ENetEvent &event)
    {
        const ::peer &p = me(event);
        const int count = graduated_count(p.growid), claimed = pget(p, "mentor_claimed");
        std::string rows{};
        for (std::size_t i = 0; i < std::size(milestones); ++i)
        {
            const milestone &m = milestones[i];
            const std::string reward = m.gems ? std::format("{} Gems", number_text(m.gems)) : id_to_item(m.item).raw_name;
            rows += std::format("add_label_with_icon|small|`w{}`` apprentice{}: {}|left|{}|\n", number_text(m.apprentices), m.apprentices == 1 ? "" : "s", reward, m.gems ? 112 : m.item);
            if (claimed & (1 << i)) rows += "add_smalltext|`2Claimed!``|left|\n";
            else if (count >= m.apprentices) rows += std::format("add_button|claim_{}|`2Claim``|noflags|0|0|\n", i);
        }
        send(event, std::format(
            "add_label_with_icon|big|`wMilestone Rewards``|left|{}|\n"
            "add_spacer|small|\n"
            "add_textbox|Help your apprentices reach level 20 to earn milestone rewards! You have `w{}`` so far.|left|\n"
            "add_spacer|small|\n"
            "{}"
            "add_spacer|small|\n"
            "add_button|show|Back|noflags|0|0|\n", CERTIFICATE, count, rows));
    }

    void claim(ENetEvent &event, const std::string &index)
    {
        ::peer &p = me(event);
        if (index.empty() || index.size() > 2 || !number(index)) return;
        const std::size_t i = std::stoul(index);
        if (i >= std::size(milestones)) return;
        const int claimed = pget(p, "mentor_claimed");
        const milestone &m = milestones[i];
        if ((claimed & (1 << i)) || graduated_count(p.growid) < m.apprentices) return milestones_dialog(event);
        if (m.item && !give(*event.peer, m.item, 1)) return say(event.peer, "`4Make room in your backpack first.``");
        if (m.gems)
        {
            p.gems += m.gems;
            econ("mentor milestone", m.gems);
            on::SetBux(event);
        }
        pset(p, "mentor_claimed", claimed | (1 << i));
        say(event.peer, std::format("`5[MENTOR]`` Milestone reward claimed: `w{}``!", m.gems ? std::format("{} Gems", number_text(m.gems)) : id_to_item(m.item).raw_name));
        send_action(*event.peer, "play_sfx", "file|audio/cash_register.wav\ndelayMS|0\n");
        milestones_dialog(event);
    }

    void archived_dialog(ENetEvent &event)
    {
        std::string names{};
        for (const link &l : links)
            if (is_mentor_of(l, me(event).growid) && l.archived) names += std::format("add_label_with_icon|small|`w{}`` (Level {})|left|18|\n", l.apprentice, l.level);
        send(event, std::format(
            "add_label|big|`wArchived Apprentices``|left|\n"
            "add_spacer|small|\n"
            "{}"
            "add_spacer|small|\n"
            "add_button|show|Back|noflags|0|0|\n", names.empty() ? "add_textbox|Nobody yet. Apprentices that finished the program (level 30) can be archived.|left|\n" : names));
    }
}

void load_mentorship()
{
    links.clear();
    ::hStmt hStmt{ "SELECT apprentice, mentor, level, archived, pending, since FROM mentorship" };
    char apprentice[32]{}, mentor[32]{};
    u_long apprentice_len{}, mentor_len{};
    signed level{}, archived{}, pending{};
    long long since{};
    MYSQL_BIND result[6] = {};
    const auto string_out = [](MYSQL_BIND &bind, char *buffer, u_long size, u_long &length) {
        bind.buffer_type = MYSQL_TYPE_STRING; bind.buffer = buffer; bind.buffer_length = size; bind.length = &length;
    };
    string_out(result[0], apprentice, sizeof(apprentice), apprentice_len);
    string_out(result[1], mentor, sizeof(mentor), mentor_len);
    result[2] = make_bind_out(level);
    result[3] = make_bind_out(archived);
    result[4] = make_bind_out(pending);
    result[5] = make_bind_out(since);
    mysql_stmt_bind_result(hStmt.pStmt, result);
    hStmt.execute();
    mysql_stmt_store_result(hStmt.pStmt);
    while (mysql_stmt_fetch(hStmt.pStmt) == 0)
        links.push_back(link{ .apprentice = std::string(apprentice, std::min<u_long>(apprentice_len, sizeof(apprentice))),
                              .mentor = std::string(mentor, std::min<u_long>(mentor_len, sizeof(mentor))),
                              .level = std::max(level, 1), .archived = archived != 0, .pending = pending & 0x1F, .since = since });
    std::printf("loaded %zu mentorship(s)\n", links.size());
}

std::string mentor_wrench_lines(const ::peer &viewer, const ::peer &target)
{
    if (const link *l = link_of(target.growid); l != nullptr && is_mentor_of(*l, viewer.growid))
        return std::format("add_spacer|small|\nadd_label_with_icon|small|`wYour Apprentice`` `o(Level {})``|left|{}|\n", target.level.front(), CERTIFICATE);
    if (viewer.level.front() < MENTOR_LEVEL || target.level.front() >= APPRENTICE_BELOW || link_of(target.growid) != nullptr) return "";
    return "add_button|mentor_add|`wAdd Apprentice``|noflags|0|0|\n";
}

bool mentor_wrench_button(ENetEvent &event, const ::hPipe &hPipe)
{
    if (hPipe["buttonClicked"] != "mentor_add") return false;
    ::peer &p = me(event);
    if (p.netid == 0) return true;
    const int netid = atoi(hPipe["netID"].c_str());
    for (ENetPeer *other : peers(p.recent_worlds.back(), PEER_SAME_WORLD)) // @note only someone in your world, as the wrench menu shows
    {
        const ::peer &t = *static_cast<::peer*>(other->data);
        if (t.netid != netid || other == event.peer) continue;
        if (const std::string why = cannot_add(*event.peer, *other); !why.empty()) { say(event.peer, why); break; }
        invites[lower(t.growid)] = invite{ p.growid, std::time(nullptr) + INVITE_TIME };
        say(other, std::format("`5[MENTOR]`` {} `5wants to be your Mentor!``", p.display_growid)); // @note GT: "you will receive a chat message"
        send_varlist(other, { "OnDialogRequest", std::format(
            "set_default_color|`o\n"
            "add_label_with_icon|big|`wMentorship Request``|left|{}|\n"
            "add_textbox|{} `o(level {}) wants you to join their Mentorship Program!|left|\n"
            "add_smalltext|Your Mentor helps you out, and you both get rewards as you level up: tickets at levels 1, 5, 10, 20 and 30.|left|\n"
            "add_spacer|small|\n"
            "add_button|accept|`2Accept``|noflags|0|0|\n"
            "add_button|decline|`4Decline``|noflags|0|0|\n"
            "end_dialog|mt_mentor_invite|Close||\n", CERTIFICATE, p.display_growid, p.level.front()) });
        send_action(*other, "play_sfx", "file|audio/friend_beep.wav\ndelayMS|0\n");
        say(event.peer, std::format("`oMentorship request sent to `w{}``.", t.growid));
        break;
    }
    return true;
}

void mentor_invite_return(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer &p = me(event);
    const std::string clicked = hPipe["buttonClicked"];
    auto it = invites.find(lower(p.growid));
    if (it == invites.end()) return;
    const invite inv = it->second;
    invites.erase(it);
    ENetPeer *mentor = online(inv.mentor);
    if (clicked == "decline")
    {
        say(mentor, std::format("`5[MENTOR]`` {} `5declined your request.``", p.display_growid));
        return say(event.peer, "`oYou declined the mentorship request.``");
    }
    if (clicked != "accept") return;
    if (inv.until < std::time(nullptr)) return say(event.peer, "`4That request expired (they last 2 minutes).``");
    if (mentor == nullptr) return say(event.peer, "`4That Mentor isn't online anymore.``");
    if (const std::string why = cannot_add(*mentor, *event.peer); !why.empty()) return say(event.peer, why); // @note again: things changed since the request

    ::peer &m = *static_cast<::peer*>(mentor->data);
    link l{ .apprentice = p.growid, .mentor = m.growid, .level = static_cast<int>(p.level.front()), .since = std::time(nullptr) };
    links.push_back(l);
    save(l);
    pset(m, "mentor_day", static_cast<int>(std::time(nullptr) / 86400));
    say(mentor, std::format("`5[MENTOR]`` {} `5is now your apprentice!``", p.display_growid));
    say(event.peer, std::format("`5[MENTOR]`` {} `5is now your Mentor!`` Open `wSocial Portal`` > `wShow Apprentices`` to see your progress.", m.display_growid));
    grant_tickets(*event.peer);
}

void send_apprentices(ENetEvent &event)
{
    ::peer &p = me(event);
    grant_tickets(*event.peer); // @note anything left over from a full backpack
    std::string body = std::format("add_label_with_icon|big|`wApprentices``|left|{}|\nadd_spacer|small|\n", CERTIFICATE);

    if (const link *l = link_of(p.growid); l != nullptr && !l->archived)
    {
        const int lvl = p.level.front();
        auto next = std::ranges::find_if(TICKET_LEVELS, [&](int t) { return t > lvl; });
        body += std::format("add_label_with_icon|small|`wYour Mentor: {}``|left|{}|\n", l->mentor, MENTOR_TICKET);
        body += (next == TICKET_LEVELS.end()) ? "add_smalltext|You completed the Mentorship Program!|left|\n"
                                              : std::format("add_smalltext|Next Apprentice Ticket at level `w{}``.|left|\n", *next);
        if (lvl < LEAVE_BEFORE) body += "add_button|mentor_leave|`4Leave Mentor``|noflags|0|0|\n";
        body += "add_spacer|small|\n";
    }

    if (p.level.front() >= MENTOR_LEVEL)
    {
        body += std::format("add_textbox|`wMy Apprentices`` ({}/{})|left|\n", active_count(p.growid), MAX_ACTIVE);
        bool any{};
        for (const link &l : links)
            if (is_mentor_of(l, p.growid) && !l.archived)
            {
                body += std::format("add_button|appr_{}|`w{}`` `o(Level {})``|noflags|0|0|\n", l.apprentice, l.apprentice, l.level);
                any = true;
            }
        if (!any) body += std::format("add_smalltext|No apprentices yet. Wrench a player below level {} and press `wAdd Apprentice``. You can add one apprentice per day.|left|\n", APPRENTICE_BELOW);
        body += "add_spacer|small|\n"
                "add_button|mentor_milestones|`wMilestone Rewards``|noflags|0|0|\n"
                "add_button|mentor_archived|`wArchived Apprentices``|noflags|0|0|\n";
    }
    else if (link_of(p.growid) == nullptr)
        body += std::format("add_textbox|Reach level `w{}`` to become a Mentor and help new players! Players below level {} can be added as apprentices by a Mentor.|left|\n", MENTOR_LEVEL, APPRENTICE_BELOW);

    body += "add_spacer|small|\nadd_button|back|Back|noflags|0|0|\n";
    send(event, body);
}

void mentor_return(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer &p = me(event);
    const std::string clicked = hPipe["buttonClicked"];
    if (clicked == "back") return action::friends(event, "");
    if (clicked == "show") return send_apprentices(event);
    if (clicked == "mentor_milestones") return p.level.front() >= MENTOR_LEVEL ? milestones_dialog(event) : void();
    if (clicked == "mentor_archived") return archived_dialog(event);
    if (clicked.starts_with("claim_")) return claim(event, clicked.substr(6));

    if (clicked == "mentor_leave" || clicked == "mentor_leave_yes")
    {
        const link *l = link_of(p.growid);
        if (l == nullptr || l->archived) return;
        if (p.level.front() >= LEAVE_BEFORE) return say(event.peer, std::format("`4You can't leave your Mentor once you reach level {}.``", LEAVE_BEFORE));
        if (clicked == "mentor_leave")
            return send(event, std::format(
                "add_label|big|`wLeave Mentor?``|left|\n"
                "add_textbox|Are you sure you want to leave `w{}``? You can't get another Mentor for `w7 days``.|left|\n"
                "add_spacer|small|\n"
                "add_button|mentor_leave_yes|`4Leave Mentor``|noflags|0|0|\n"
                "add_button|show|Back|noflags|0|0|\n", l->mentor));
        const std::string mentor = l->mentor;
        erase(p.growid);
        pset(p, "mentor_left", static_cast<int>(std::time(nullptr)));
        say(online(mentor), std::format("`5[MENTOR]`` {} `5left your Mentorship Program.``", p.display_growid));
        return say(event.peer, std::format("`oYou left `w{}``'s Mentorship Program.", mentor));
    }

    /* @note appr_/remove_/archive_ + an apprentice's growid, which must be yours */
    const std::size_t sep = clicked.find('_');
    if (sep == std::string::npos) return;
    const std::string what = clicked.substr(0, sep), who = clicked.substr(sep + 1);
    link *l = link_of(who);
    if (l == nullptr || !is_mentor_of(*l, p.growid) || l->archived) return send_apprentices(event);
    if (ENetPeer *a = online(l->apprentice)) l->level = static_cast<::peer*>(a->data)->level.front();

    if (what == "appr") return apprentice_dialog(event, *l);
    if (what == "remove")
    {
        if (l->level >= DONE_LEVEL) return apprentice_dialog(event, *l); // @note level 30: archive only
        const std::string name = l->apprentice;
        erase(name);
        say(online(name), std::format("`5[MENTOR]`` {} `5removed you from their Mentorship Program.``", p.display_growid));
        say(event.peer, std::format("`oYou removed `w{}`` from your apprentices.", name));
        return send_apprentices(event);
    }
    if (what == "archive")
    {
        if (l->level < DONE_LEVEL) return apprentice_dialog(event, *l);
        l->archived = true;
        save(*l);
        say(event.peer, std::format("`oYou archived `w{}``. Thanks for mentoring!", l->apprentice));
        return send_apprentices(event);
    }
}

void mentor_level_up(ENetEvent &event, int level)
{
    ::peer &p = me(event);
    link *l = link_of(p.growid);
    if (l == nullptr || l->archived) return;
    l->level = level;
    save(*l);
    ENetPeer *mentor = online(l->mentor);
    if (level == 20) say(mentor, std::format("`5[MENTOR]`` Your apprentice {} `5reached level 20!`` Check your `wMilestone Rewards``.", p.display_growid));
    if (level == DONE_LEVEL)
    {
        say(event.peer, "`5[MENTOR]`` You completed the Mentorship Program! Congratulations!");
        say(mentor, std::format("`5[MENTOR]`` {} `5completed your Mentorship Program!``", p.display_growid));
    }
    grant_tickets(*event.peer);
}

void mentor_login(ENetEvent &event)
{
    ::peer &p = me(event);
    if (link *l = link_of(p.growid); l != nullptr && !l->archived && l->level != p.level.front())
    {
        l->level = p.level.front();
        save(*l);
    }
    grant_tickets(*event.peer);
}

int apprentice_ticket_step(short id)
{
    return (id >= APPRENTICE_TICKET && id < APPRENTICE_TICKET + 10 && (id - APPRENTICE_TICKET) % 2 == 0) ? (id - APPRENTICE_TICKET) / 2 : -1;
}

int mentor_ticket_step(short id)
{
    return (id >= MENTOR_TICKET && id < MENTOR_TICKET + 10 && (id - MENTOR_TICKET) % 2 == 0) ? (id - MENTOR_TICKET) / 2 : -1;
}

void apprentice_ticket_used(ENetEvent &event, int step)
{
    ::peer &p = me(event);
    if (step < 0 || step >= static_cast<int>(TICKET_LEVELS.size()) || p.level.front() < TICKET_LEVELS[step]) return;
    /* @note one Mentor Ticket per level per apprentice: tickets traded from others don't count twice */
    const int used = pget(p, "appr_used");
    if (used & (1 << step)) return;
    link *l = link_of(p.growid);
    if (l == nullptr) return;
    pset(p, "appr_used", used | (1 << step));
    l->pending |= 1 << step;
    save(*l);
    if (ENetPeer *mentor = online(l->mentor)) grant_tickets(*mentor); // @note offline: on their next login
}
