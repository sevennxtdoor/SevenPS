#include "pch.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "onVariant/SetBux.hpp"
#include "action/quit_to_exit.hpp"
#include "commands/warp.hpp"
#include "tools/profile.hpp"
#include "tools/rank.hpp"
#include "tools/jammer.hpp"
#include "tools/playmods.hpp"
#include "tools/buffs.hpp"
#include "tools/wrench_menu.hpp"
#include "tools/economy.hpp"

#include "gtos.hpp"

namespace
{
    std::string last_sb_world{};
    constexpr std::string_view CURSE_WORLD{ "HELL" };
}

static ::peer &me(ENetEvent &event) { return *static_cast<::peer*>(event.peer->data); }

static std::string lower(std::string s) { for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }

static std::vector<std::string> args_of(const std::string_view text) { return readch(std::string{ text }, ' '); }

static ENetPeer *find_online(const std::string &growid)
{
    for (ENetPeer *p : peers())
        if (::peer *pp = static_cast<::peer*>(p->data); pp && !pp->growid.empty() && lower(pp->growid) == lower(growid)) return p;
    return nullptr;
}

static bool staff(ENetEvent &event, u_char role)
{
    if (me(event).role >= role) return true;
    send_action(*event.peer, "log", "msg|`4Unknown command.`` Enter `$/?`` for a list of valid commands.");
    return false;
}

/* @note "10" -> 10 minutes, "2h" -> 120, "1d" -> 1440. 0 if it isn't a number */
static int minutes_of(const std::string &s)
{
    if (s.empty() || !std::isdigit(static_cast<unsigned char>(s.front()))) return 0;
    const long long n = std::clamp<long long>(atoll(s.c_str()), 0, 365LL * 24 * 60);
    const int m = static_cast<int>(s.ends_with('d') ? n * 24 * 60 : s.ends_with('h') ? n * 60 : n);
    return std::clamp(m, 0, 365 * 24 * 60);
}

static std::string duration(int seconds)
{
    if (seconds >= 86400) return std::format("{}d {}h", seconds / 86400, seconds % 86400 / 3600);
    if (seconds >= 3600) return std::format("{}h {}m", seconds / 3600, seconds % 3600 / 60);
    return std::format("{}m", (seconds + 59) / 60);
}

int muted_left(const ::peer &p) { return std::max<int>(0, pget(p, "mute_until") - static_cast<int>(std::time(nullptr))); }
int cursed_left(const ::peer &p) { return std::max<int>(0, pget(p, "curse_until") - static_cast<int>(std::time(nullptr))); }
void set_last_sb_world(const std::string &world) { last_sb_world = world; }
bool radio_off(const ::peer &p) { return pget(p, "radio_off") != 0; }

/* ---------- players ---------- */

void pay(ENetEvent &event, const std::string_view text)
{
    std::vector<std::string> args = args_of(text);
    if (args.size() < 3) return on::ConsoleMessage(event.peer, "`oUsage: /pay {growid} {gems}``");
    const long long amount = atoll(args[2].c_str());
    ::peer &p = me(event);
    if (staff_locked(p)) return staff_locked_message(*event.peer);
    if (amount <= 0 || amount > p.gems) return on::ConsoleMessage(event.peer, std::format("`4You can pay between 1 and {} gems.``", p.gems));

    ENetPeer *target = find_online(args[1]);
    if (target == nullptr) return on::ConsoleMessage(event.peer, "`4That player is offline.``");
    ::peer &t = *static_cast<::peer*>(target->data);
    if (&t == &p) return on::ConsoleMessage(event.peer, "`4You can't pay yourself.``");

    p.gems -= static_cast<int>(amount);
    t.gems += static_cast<int>(amount);
    on::SetBux(event);
    ENetEvent te{ .peer = target };
    on::SetBux(te);
    on::ConsoleMessage(event.peer, std::format("`2You paid `w{}`` gems to ``{}`2.``", amount, t.display_growid));
    on::ConsoleMessage(target, std::format("`2{}`2 paid you `w{}`` gems!``", p.display_growid, amount));
    send_action(*target, "play_sfx", "file|audio/cash_register.wav\ndelayMS|0\n");
}

void go(ENetEvent &event, const std::string_view text)
{
    if (last_sb_world.empty()) return on::ConsoleMessage(event.peer, "`oNobody sent a super broadcast yet.``");
    if (me(event).role < MODERATOR && world_jammed(last_sb_world)) return on::ConsoleMessage(event.peer, "`4That world is `4JAMMED``, you can't go there with /go.``");
    warp(event, std::format("warp {}", last_sb_world));
}

void rules(ENetEvent &event, const std::string_view text)
{
    send_varlist(event.peer, { "OnDialogRequest",
        "set_default_color|`o\n"
        "add_label_with_icon|big|`wMikeTopia Rules``|left|242|\n"
        "add_spacer|small|\n"
        "add_textbox|`w1. No scamming.`` Always trade with the `2trade window``, never \"drop first\". Real Roulette Wheel results only come from a real spin, never from chat.|left|\n"
        "add_textbox|`w2. Keep your password safe.`` Never share it with anyone. Staff will `4never`` ask for it.|left|\n"
        "add_textbox|`w3. No cheating.`` No hacks, modded clients, bots, macros or auto clickers.|left|\n"
        "add_textbox|`w4. No bug abuse.`` Found a bug? Report it to staff instead of using it.|left|\n"
        "add_textbox|`w5. Be polite.`` No spamming, swearing, racism, insults or begging.|left|\n"
        "add_textbox|`w6. Keep names clean.`` World names, signs and GrowIDs must not be rude or offensive.|left|\n"
        "add_textbox|`w7. Bet at your own risk.`` Staff won't refund items or World Locks lost in casinos or bets between players.|left|\n"
        "add_textbox|`w8. Respect everyone.`` Staff decisions are final.|left|\n"
        "add_spacer|small|\n"
        "add_smalltext|Breaking the rules can get you `4muted``, `4cursed`` (sent to HELL), `4world banned`` or `4banned``. Report players with wrench > Report, and type /mods to see which staff are online.|left|\n"
        "end_dialog|rules||I Understand|\n" });
}

void mods(ENetEvent &event, const std::string_view text)
{
    std::string names{};
    for (ENetPeer *p : peers())
    {
        ::peer *pp = static_cast<::peer*>(p->data);
        if (pp == nullptr || pp->growid.empty() || pp->role < MODERATOR) continue;
        if ((pp->state & S_GHOST) || !pp->nick.empty() || pp->hide_status) continue; // @note undercover staff
        names += std::format("{}{}", names.empty() ? "" : "`o, ", pp->display_growid);
    }
    on::ConsoleMessage(event.peer, names.empty() ? "`oNo staff online right now. Use wrench > Report to report a player.``" : std::format("`oStaff online: {}``", names));
}

void radio(ENetEvent &event, const std::string_view text)
{
    ::peer &p = me(event);
    pset(p, "radio_off", radio_off(p) ? 0 : 1);
    on::ConsoleMessage(event.peer, radio_off(p) ? "`oRadio `4off``: you won't see player super broadcasts. Type `$/radio`` again to turn it on.``" : "`oRadio `2on``: you see super broadcasts again.``");
}

void info(ENetEvent &event, const std::string_view text)
{
    std::vector<std::string> args = args_of(text);
    if (args.size() < 2) return on::ConsoleMessage(event.peer, "`oUsage: /info {item id or name}``");

    const ::item *found = nullptr;
    if (std::ranges::all_of(args[1], ::isdigit)) found = &id_to_item(static_cast<u_short>(std::clamp(atoi(args[1].c_str()), 0, 65535)));
    else
    {
        const std::string name = lower(std::string{ text.substr(text.find(' ') + 1) });
        for (const ::item &it : items) if (lower(it.raw_name) == name) { found = &it; break; }
        if (found == nullptr) for (const ::item &it : items) if (lower(it.raw_name).find(name) != std::string::npos) { found = &it; break; }
    }
    if (found == nullptr || found->id == 0) return on::ConsoleMessage(event.peer, "`4No item found.``");

    send_varlist(event.peer, { "OnDialogRequest", std::format(
        "set_default_color|`o\n"
        "add_label_with_icon|big|`w{}``|left|{}|\n"
        "add_spacer|small|\n"
        "add_textbox|Item ID: `w{}``  Rarity: `w{}``  Type: `w{}``|left|\n"
        "add_textbox|Hits to break: `w{}``  Grow time: `w{}``|left|\n"
        "add_textbox|{}|left|\n"
        "end_dialog|item_info||OK|\n",
        found->raw_name, found->id, found->id, found->rarity, static_cast<int>(found->type), static_cast<int>(found->hits),
        found->tick > 0 ? duration(found->tick) : "-", found->info.empty() ? "No description." : found->info) });
}

void status(ENetEvent &event, const std::string_view text)
{
    ::peer &p = me(event);
    const ::rank *r = get_rank(p);
    const int hours = static_cast<int>((p.playtime + (std::time(nullptr) - p.login_time)) / 3600);
    on::ConsoleMessage(event.peer, std::format("`o** Status of ``{}`o: level `w{}``, `w{}`` gems, rank {}, played `w{}`` hours.``",
        p.display_growid, p.level.front(), p.gems, r ? std::format("{}{}``", r->color, r->name) : "`wnone``", hours));
    on::ConsoleMessage(event.peer, std::format("`o** Playmods: `w{}``{}{}``", playmod_names(p),
        muted_left(p) ? std::format("`o, `4muted`` for `w{}``", duration(muted_left(p))) : "",
        cursed_left(p) ? std::format("`o, `4cursed`` for `w{}``", duration(cursed_left(p))) : ""));
    if (const std::string buffs = buff_names(p); !buffs.empty())
        on::ConsoleMessage(event.peer, std::format("`o** Mods: `w{}``", buffs)); // @note timed mods from consumables
}

/* ---------- staff ---------- */

void warpto(ENetEvent &event, const std::string_view text)
{
    if (!staff(event, MODERATOR)) return;
    std::vector<std::string> args = args_of(text);
    if (args.size() < 2) return on::ConsoleMessage(event.peer, "`oUsage: /warpto {growid}``");
    ENetPeer *target = find_online(args[1]);
    if (target == nullptr) return on::ConsoleMessage(event.peer, "`4That player is offline.``");
    ::peer &t = *static_cast<::peer*>(target->data);
    if (t.netid == 0) return on::ConsoleMessage(event.peer, "`4That player isn't in a world.``");
    warp(event, std::format("warp {}", t.recent_worlds.back()));
}

/* @note shared by /mute and /curse: {growid} {minutes|2h|1d} {reason...} */
static ::peer *punish_target(ENetEvent &event, const std::string_view text, std::string_view usage, int &minutes, std::string &reason, ENetPeer *&target)
{
    std::vector<std::string> args = args_of(text);
    if (args.size() < 3 || (minutes = minutes_of(args[2])) <= 0) { on::ConsoleMessage(event.peer, std::format("`oUsage: {}``", usage)); return nullptr; }
    target = find_online(args[1]);
    if (target == nullptr) { on::ConsoleMessage(event.peer, "`4That player is offline.``"); return nullptr; }
    ::peer &t = *static_cast<::peer*>(target->data);
    if (t.role >= me(event).role) { on::ConsoleMessage(event.peer, "`4You can only punish players below your role.``"); return nullptr; }
    for (std::size_t i = 3; i < args.size(); ++i) reason += (i > 3 ? " " : "") + args[i];
    reason = clean_text(reason.empty() ? std::string{ "no reason" } : reason, 80);
    return &t;
}

void mute(ENetEvent &event, const std::string_view text)
{
    if (!staff(event, MODERATOR)) return;
    int minutes{}; std::string reason{}; ENetPeer *target{};
    ::peer *t = punish_target(event, text, "/mute {growid} {minutes, 2h or 1d} {reason}", minutes, reason, target);
    if (t == nullptr) return;
    pset(*t, "mute_until", static_cast<int>(std::time(nullptr)) + minutes * 60);
    on::ConsoleMessage(target, std::format("`4You were muted for {} by staff. Reason: ``{}", duration(minutes * 60), reason));
    send_varlist(target, { "OnAddNotification", "interface/atomic_button.rttex", std::format("`4Muted for {}: ``{}", duration(minutes * 60), reason), "audio/hub_open.wav", 0 });
    on::ConsoleMessage(event.peer, std::format("`oMuted ``{}`o for {}.``", t->display_growid, duration(minutes * 60)));
}

void unmute(ENetEvent &event, const std::string_view text)
{
    if (!staff(event, MODERATOR)) return;
    std::vector<std::string> args = args_of(text);
    ENetPeer *target = (args.size() >= 2) ? find_online(args[1]) : nullptr;
    if (target == nullptr) return on::ConsoleMessage(event.peer, "`oUsage: /unmute {growid} (player must be online)``");
    pset(*static_cast<::peer*>(target->data), "mute_until", 0);
    on::ConsoleMessage(target, "`2You were unmuted. Please follow the rules (`$/rules``).``");
    on::ConsoleMessage(event.peer, "`2Unmuted.``");
}

void curse(ENetEvent &event, const std::string_view text)
{
    if (!staff(event, MODERATOR)) return;
    int minutes{}; std::string reason{}; ENetPeer *target{};
    ::peer *t = punish_target(event, text, "/curse {growid} {minutes, 2h or 1d} {reason}", minutes, reason, target);
    if (t == nullptr) return;
    pset(*t, "curse_until", static_cast<int>(std::time(nullptr)) + minutes * 60);
    on::ConsoleMessage(target, std::format("`4You were cursed to {} for {}. Reason: ``{}", CURSE_WORLD, duration(minutes * 60), reason));
    send_varlist(target, { "OnAddNotification", "interface/atomic_button.rttex", std::format("`4Cursed for {}: ``{}", duration(minutes * 60), reason), "audio/hub_open.wav", 0 });
    ENetEvent te{ .peer = target };
    warp(te, std::format("warp {}", CURSE_WORLD));
    on::ConsoleMessage(event.peer, std::format("`oCursed ``{}`o for {}.``", t->display_growid, duration(minutes * 60)));
}

void uncurse(ENetEvent &event, const std::string_view text)
{
    if (!staff(event, MODERATOR)) return;
    std::vector<std::string> args = args_of(text);
    ENetPeer *target = (args.size() >= 2) ? find_online(args[1]) : nullptr;
    if (target == nullptr) return on::ConsoleMessage(event.peer, "`oUsage: /uncurse {growid} (player must be online)``");
    pset(*static_cast<::peer*>(target->data), "curse_until", 0);
    on::ConsoleMessage(target, "`2Your curse was lifted. You can leave now.``");
    on::ConsoleMessage(event.peer, "`2Uncursed.``");
}

void warn(ENetEvent &event, const std::string_view text)
{
    if (!staff(event, MODERATOR)) return;
    std::vector<std::string> args = args_of(text);
    if (args.size() < 3) return on::ConsoleMessage(event.peer, "`oUsage: /warn {growid} {reason}``");
    ENetPeer *target = find_online(args[1]);
    if (target == nullptr) return on::ConsoleMessage(event.peer, "`4That player is offline.``");
    const std::string reason = clean_text(std::string{ text.substr(text.find(' ', text.find(' ') + 1) + 1) }, 120);
    send_varlist(target, { "OnAddNotification", "interface/atomic_button.rttex", std::format("`4Warning from staff: ``{}", reason), "audio/hub_open.wav", 0 });
    on::ConsoleMessage(target, std::format("`4** Warning from staff: ``{}", reason));
    on::ConsoleMessage(event.peer, std::format("`oWarned ``{}`o.``", static_cast<::peer*>(target->data)->display_growid));
}

void kickall(ENetEvent &event, const std::string_view text)
{
    if (!staff(event, DEVELOPER)) return;
    ::peer &p = me(event);
    if (p.netid == 0) return;
    const std::string world = p.recent_worlds.back();
    int kicked = 0;
    for (ENetPeer *other : peers(world, PEER_SAME_WORLD))
    {
        ::peer *o = static_cast<::peer*>(other->data);
        if (o == &p || o->role >= MODERATOR) continue;
        on::ConsoleMessage(other, "`4Everyone was kicked from this world by staff.``");
        ENetEvent oe{ .peer = other };
        action::quit_to_exit(oe, "", false);
        ++kicked;
    }
    on::ConsoleMessage(event.peer, std::format("`oKicked `w{}`` players from {}.``", kicked, world));
}
