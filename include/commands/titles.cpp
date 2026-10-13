#include "pch.hpp"
#include "tools/rank.hpp"
#include <sstream>
#include "onVariant/CountryState.hpp"
#include "onVariant/SetClothing.hpp"

#include "titles.hpp"

namespace
{
    struct title_info
    {
        int flag;
        std::string_view key;   // @note used in /givetitle and as the dialog checkbox id
        std::string_view label;
    };
    constexpr title_info title_list[]{
        { T_DOCTOR,   "dr",       "`4Dr.`` - shows as `4Dr. Name`` (red name)" },
        { T_LEGEND,   "legend",   "`wof Legend`` - shows as `wName of Legend``" },
        { T_BLUENAME, "bluename", "`cBlue Name`` - the level 125 blue name" },
    };
}

static std::string to_lower(std::string str)
{
    for (char &c : str) c = std::tolower(c);
    return str;
}

void refresh_display_name(ENetPeer &p)
{
    ::peer *pPeer = static_cast<::peer*>(p.data);

    std::string_view color = "`w";
    auto world = std::ranges::find(worlds, pPeer->recent_worlds.back(), &::world::name);
    if (pPeer->netid != 0 && world != worlds.end())
    {
        if (world->owner != 0 && world->owner == pPeer->user_id) color = "`2";
        else if (std::ranges::find(world->access, pPeer->user_id) != world->access.end()) color = "`c";
    }
    pPeer->display_growid = pPeer->name_as(color);

    if (pPeer->netid != 0)
        peers(pPeer->recent_worlds.back(), PEER_SAME_WORLD, [&pPeer](ENetPeer &w)
        {
            send_varlist(&w, { "OnNameChanged", pPeer->display_growid }, pPeer->netid);
        });

    ENetEvent event{ .peer = &p };
    on::CountryState(event); // @note blue name (|maxLevel) may have changed
}

void send_title_dialog(ENetEvent& event)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const int owned = pPeer->owned_titles();

    std::string checkboxes{};
    for (const title_info &t : title_list)
        if (owned & t.flag)
            checkboxes += std::format("add_checkbox|title_{}|{}|{}\n", t.key, t.label, (pPeer->titles_on & t.flag) ? 1 : 0);
    if (checkboxes.empty()) checkboxes = "add_textbox|`oYou don't have any titles yet.``|left|\n";

    send_varlist(event.peer, {
        "OnDialogRequest",
        std::format(
            "set_default_color|`o\n"
            "add_label_with_icon|big|`wTitles``|left|32|\n"
            "add_spacer|small|\n"
            "add_textbox|Choose which titles to show on your name.|left|\n"
            "add_spacer|small|\n"
            "{}"
            "add_spacer|small|\n"
            "end_dialog|title_edit|Cancel|OK|\n",
            checkboxes
        )
    });
}

void title_edit(ENetEvent& event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    int shown{};
    for (const title_info &t : title_list)
        if (hPipe[std::format("title_{}", t.key)] == "1") shown |= t.flag;
    pPeer->titles_on = shown & pPeer->owned_titles(); // @note can't turn on a title you don't own

    refresh_display_name(*event.peer);
    send_action(*event.peer, "log", std::format("msg|`oYour name now shows as {}`o.``", pPeer->display_growid));
}

void title(ENetEvent& event, const std::string_view text)
{
    send_title_dialog(event);
}

/* /givetitle {growid} {dr|legend|bluename|all|none} */
void givetitle(ENetEvent& event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (pPeer->role < DEVELOPER)
    {
        send_action(*event.peer, "log", "msg|`4Unknown command.`` Enter `$/?`` for a list of valid commands.");
        return;
    }

    std::istringstream args{ std::string{ text.substr(sizeof("givetitle ")-1) } };
    std::string growid{}, key{};
    if (!(args >> growid >> key))
    {
        send_action(*event.peer, "log", "msg|`4Usage:`` /givetitle {growid} {dr|legend|bluename|all|none}");
        return;
    }
    key = to_lower(key);

    int flag{ -1 };
    if (key == "all") flag = T_ALL;
    else if (key == "none") flag = 0;
    else for (const title_info &t : title_list) if (key == t.key) flag = t.flag;
    if (flag < 0)
    {
        send_action(*event.peer, "log", "msg|`4Unknown title.`` Use: dr, legend, bluename, all, none");
        return;
    }

    {
        ::hStmt hStmt{ "SELECT 1 FROM peer WHERE growid = ? LIMIT 1" };
        MYSQL_BIND param = make_bind_in(growid);
        hStmt.bind_param(&param);
        hStmt.execute();
        if (mysql_stmt_store_result(hStmt.pStmt) || mysql_stmt_num_rows(hStmt.pStmt) == 0)
        {
            send_action(*event.peer, "log", std::format("msg|`4Player `w{}`` `4not found.``", growid));
            return;
        }
    }
    {
        /* @note "none" clears every title, anything else adds to what they already own */
        ::hStmt hStmt{ (flag == 0) ? "UPDATE peer SET titles = 0, titles_on = 0 WHERE growid = ?" : "UPDATE peer SET titles = titles | ? WHERE growid = ?" };
        const signed value = flag;
        MYSQL_BIND params[2] = { make_bind_in(value), make_bind_in(growid) };
        hStmt.bind_param((flag == 0) ? &params[1] : params);
        hStmt.execute();
    }

    peers("", PEER_ALL, [&](ENetPeer &p)
    {
        ::peer *pOthers = static_cast<::peer*>(p.data);
        if (!pOthers || to_lower(pOthers->growid) != to_lower(growid)) return;

        if (flag == 0) pOthers->titles = pOthers->titles_on = 0;
        else pOthers->titles |= flag;
        refresh_display_name(p);
        send_action(p, "log", (flag == 0) ?
            "msg|`oYour titles were removed.``" :
            "msg|`2You received a new title!`` Wrench yourself and press `wTitle`` to show it.");
    });
    send_action(*event.peer, "log", std::format("msg|`2Updated titles of `w{}``.", growid));
}

/* /nick {name} - change your shown name (hides the @ tag). /nick alone goes back to normal */
void nick(ENetEvent& event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const ::rank *r = get_rank(*pPeer);
    if (pPeer->role < MODERATOR && !(r && r->nick)) // @note staff, or a rank with the /nick perk
    {
        send_action(*event.peer, "log", "msg|`4Unknown command.`` Enter `$/?`` for a list of valid commands.");
        return;
    }

    std::string name = (text.size() > sizeof("nick")-1) ? std::string{ text.substr(sizeof("nick ")-1) } : "";
    if (!name.empty() && (name.size() < 3 || name.size() > 18 || !alnum(name)))
    {
        send_action(*event.peer, "log", "msg|`4Nick must be 3-18 letters/numbers.``");
        return;
    }
    pPeer->nick = name;
    refresh_display_name(*event.peer);
    send_action(*event.peer, "log", name.empty() ?
        "msg|`oYour name is back to normal.``" :
        std::format("msg|`oYou are now known as {}`o. Type `$/nick`` to go back.``", pPeer->display_growid));
}

std::string status_label(const ::peer &peer)
{
    switch (peer.role)
    {
        case OWNER:     return "`9Owner`` `o|`` `5Super Supporter``";
        case DEVELOPER: return "`6Developer`` `o|`` `5Super Supporter``";
        case MODERATOR: return "`#Moderator`` `o|`` `5Super Supporter``";
        default:        return "`wPlayer``";
    }
}

/* /hidestatus - like real growtopia, toggle the super supporter bigger name & green wrench */
void hidestatus(ENetEvent& event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (pPeer->role < MODERATOR)
    {
        send_action(*event.peer, "log", "msg|`4Unknown command.`` Enter `$/?`` for a list of valid commands.");
        return;
    }
    pPeer->hide_status = !pPeer->hide_status;
    pPeer->update_effects();
    if (pPeer->netid != 0) on::SetClothing(*event.peer);
    send_action(*event.peer, "log", pPeer->hide_status ? 
        "msg|`oSuper Supporter status is now `4hidden``." : 
        "msg|`oSuper Supporter status is now `2shown``.");
}
