#include "pch.hpp"
#include <fstream>
#include "onVariant/ConsoleMessage.hpp"
#include "tools/economy.hpp"
#include "onVariant/SetBux.hpp"
#include "onVariant/NameChanged.hpp"
#include "tools/string.hpp"

#include "rank.hpp"

namespace
{
    std::vector<::rank> ranks{};
}

static std::string to_lower(std::string s) { for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }

void load_ranks()
{
    ranks.clear();
    std::ifstream file("resources/ranks.txt");
    for (std::string line; std::getline(file, line); )
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.starts_with('#')) continue;
        std::vector<std::string> f = readch(line, '|'); // @note level|name|color|price|gem%|xp%|slots|perks
        if (f.size() < 7) continue;
        ::rank r{
            .level = atoi(f[0].c_str()), .name = f[1], .color = f[2], .price = atoi(f[3].c_str()),
            .gem_bonus = atoi(f[4].c_str()), .xp_bonus = atoi(f[5].c_str()), .slots = atoi(f[6].c_str())
        };
        if (f.size() >= 8)
            for (const std::string &perk : readch(f[7], ','))
            {
                if (perk == "sbfree") r.sb_free = true;
                if (perk == "titles") r.titles = true;
                if (perk == "nick")   r.nick = true;
                if (perk == "warp")   r.warp = true;
                if (perk.starts_with("spin"))  r.spins = std::clamp(atoi(perk.c_str() + 4), 1, 10);
                if (perk.starts_with("gacha")) r.gacha_discount = std::clamp(atoi(perk.c_str() + 5), 0, 90);
            }
        if (r.level > 0 && !r.name.empty()) ranks.push_back(r);
    }
    std::ranges::sort(ranks, {}, &::rank::level);
    std::printf("loaded %zu ranks\n", ranks.size());
}

const ::rank *get_rank(const ::peer &p)
{
    if (p.rank_level <= 0) return nullptr;
    auto it = std::ranges::find(ranks, p.rank_level, &::rank::level);
    return (it == ranks.end()) ? nullptr : &*it;
}

int gem_bonus(const ::peer &p, int gems)
{
    const ::rank *r = get_rank(p);
    return r ? gems + gems * r->gem_bonus / 100 : gems;
}

int xp_bonus(const ::peer &p, int xp)
{
    const ::rank *r = get_rank(p);
    return r ? xp + xp * r->xp_bonus / 100 : xp;
}

static std::string perks_text(const ::rank &r)
{
    std::string text = std::format("+{}% gems, +{}% XP, +{} backpack slots", r.gem_bonus, r.xp_bonus, r.slots);
    if (r.sb_free) text += ", free /sb";
    if (r.titles)  text += ", all titles";
    if (r.nick)    text += ", /nick";
    if (r.warp)    text += ", /warp";
    if (r.spins > 1) text += std::format(", {} Daily Spins", r.spins);
    if (r.gacha_discount > 0) text += std::format(", {}% off Lucky Box", r.gacha_discount);
    return text;
}

/* @note give the rank and its backpack slots (only the difference to the old rank) */
static void apply_rank(ENetPeer &p, int level)
{
    ::peer *pp = static_cast<::peer*>(p.data);
    const ::rank *old = get_rank(*pp);
    pp->rank_level = level;
    const ::rank *now = get_rank(*pp);
    pp->slot_size = std::clamp(pp->slot_size - (old ? old->slots : 0) + (now ? now->slots : 0), 16, 396);
    pp->mysql_update<signed>("rank_level", pp->rank_level);

    pp->display_growid = pp->name_as("`w");
    if (pp->netid != 0)
    {
        ENetEvent e{ .peer = &p };
        on::NameChanged(e);
        send_inventory_state(e); // @note new backpack size
    }
}

void rank_command(ENetEvent &event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const ::rank *mine = get_rank(*pPeer);

    std::string body{};
    for (const ::rank &r : ranks)
    {
        body += std::format("add_spacer|small|\nadd_label|small|{}[{}]`` - `w{}`` gems|left\nadd_smalltext|{}|left|\n",
            r.color, r.name, r.price, perks_text(r));
        if (mine && r.level <= mine->level) body += "add_smalltext|`2You have this rank.``|left|\n";
        else body += std::format("add_button|buy_{}|`2Buy {} ({} gems)``|noflags|0|0|\n", r.level, r.name, r.price);
    }
    send_varlist(event.peer, { "OnDialogRequest", std::format(
        "set_default_color|`o\n"
        "add_label|big|`9SevenPS Ranks``|left|\n"
        "add_spacer|small|\n"
        "add_textbox|Your rank: {} - you have `w{}`` gems.|left|\n"
        "add_smalltext|Buying a higher rank replaces your current one. Ranks are permanent.|left|\n"
        "{}"
        "end_dialog|mt_rank|Close||\n",
        mine ? std::format("{}[{}]``", mine->color, mine->name) : "`oNone``", pPeer->gems, body) });
}

void rank_return(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const std::string clicked = hPipe["buttonClicked"];
    if (!clicked.starts_with("buy_")) return;

    const int level = atoi(clicked.substr(4).c_str());
    auto it = std::ranges::find(ranks, level, &::rank::level);
    if (it == ranks.end() || pPeer->rank_level >= level) return;
    if (pPeer->gems < it->price)
        return on::ConsoleMessage(event.peer, std::format("`4You need `w{}`` gems for {}[{}]``.``", it->price, it->color, it->name));

    pPeer->gems -= it->price;
    econ("ranks", -it->price);
    on::SetBux(event);
    pPeer->mysql_update<signed>("gems", pPeer->gems); // @note save right away, gems were spent
    apply_rank(*event.peer, level);

    send_action(*event.peer, "play_sfx", "file|audio/cash_register.wav\ndelayMS|0\n");
    const std::string message = std::format("`9** ``{} `9just became {}[{}]``! `9Type `$/rank`` `9to see the ranks.``", pPeer->display_growid, it->color, it->name);
    peers("", PEER_ALL, [&message](ENetPeer &p)
    {
        if (::peer *pp = static_cast<::peer*>(p.data); pp && !pp->growid.empty()) on::ConsoleMessage(&p, message);
    });
}

void giverank(ENetEvent &event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (pPeer->role < OWNER) return send_action(*event.peer, "log", "msg|`4Unknown command.`` Enter `$/?`` for a list of valid commands."); // @note ranks are worth millions of gems

    std::vector<std::string> args = readch(std::string{ text }, ' ');
    if (args.size() < 3) return on::ConsoleMessage(event.peer, "`oUsage: /giverank {growid} {0-5} (0 removes the rank)``");
    const int level = atoi(args[2].c_str());
    if (level != 0 && std::ranges::find(ranks, level, &::rank::level) == ranks.end())
        return on::ConsoleMessage(event.peer, "`4No such rank level. See /rank.``");

    for (ENetPeer *p : peers())
        if (::peer *pp = static_cast<::peer*>(p->data); pp && to_lower(pp->growid) == to_lower(args[1]))
        {
            apply_rank(*p, level);
            staff_log(*pPeer, "/giverank", pp->growid, 0, level);
            on::ConsoleMessage(event.peer, std::format("`oGave rank `w{}`` to {}.``", level, pp->display_growid));
            on::ConsoleMessage(p, std::format("`9You received a rank from {}! Type `$/rank``.``", pPeer->display_growid));
            return;
        }
    on::ConsoleMessage(event.peer, "`4That player is offline.``");
}
