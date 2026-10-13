#include "pch.hpp"
#include "onVariant/Action.hpp"
#include "time.hpp"
#include "find.hpp"
#include "warp.hpp"
#include "punch.hpp"
#include "skin.hpp"
#include "sb.hpp"
#include "who.hpp"
#include "me.hpp"
#include "news.hpp"
#include "weather.hpp"
#include "tools/cps.hpp"
#include "tools/rank.hpp"
#include "tools/system_worlds.hpp"
#include "gtos.hpp"
#include "ghost.hpp"
#include "give.hpp"
#include "setrole.hpp"
#include "moderation.hpp"
#include "restart.hpp"
#include "titles.hpp"
#include "__command.hpp"

/* emote commands all dispatch to on::Action. listed once here so the
 * cmd_pool registration and the /help text stay in sync automatically. */
static constexpr std::string_view emotes[24]{
    "wave", "dance", "love", "sleep", "facepalm", "fp",
    "smh", "yes", "no", "omg", "idk", "shrug",
    "furious", "rolleyes", "foldarms", "fa", "stubborn", "fold",
    "dab", "sassy", "dance2", "march", "grumpy", "shy"
};

/* named commands with their usage hint, shown in /help */
static constexpr std::string_view named_help =
    "/time /sb {msg} /find /warp {world} /punch {id} /skin {id} /who /me {msg} "
    "/news /weather {id} /ghost /wban {growid} /uba /title /start /event /top {level|gems|surgeon|rich} /rank /msg {growid} {msg} /online /parkour /pay {growid} {gems} /go /rules /mods /radio /info {item} /status";

/* moderator+, shown in /help to MODERATOR and DEVELOPER */
static constexpr std::string_view mod_help =
    "/warpto {growid} /mute {growid} {time} {reason} /unmute {growid} /curse {growid} {time} {reason} /uncurse {growid} /warn {growid} {reason} /kick {growid} /ban {growid} /unban {growid} /nuke {world} /unnuke {world} /wban {growid} {minutes} /nick {name} /hidestatus";

/* developer+, shown in /help to DEVELOPER and OWNER */
static constexpr std::string_view dev_help =
    "/kickall /giverank {growid} {0-5} /event start {gems|xp|lucky|surgery} {minutes} /event stop /give {id} {amount} /givegems {growid} {amount} /setrole {growid} {player|mod} /givetitle {growid} {dr|legend|bluename|all|none}";

/* owner only */
static constexpr std::string_view owner_help =
    "/setrole {growid} dev /restart {minutes} /restart cancel /rebuild {START|PARKOUR}";

std::array<std::string_view, 14> cmd_requires_arg{
    "sb", "warp", "punch", "skin", "me", "weather", "give", "setrole", "kick", "ban", "unban", "wban", "givetitle", "givegems"
};

/* if you plan to use this outside of this file, please include in __command.hpp (^-^) - and just make it a void. */
auto help_return = [](ENetEvent& event, const std::string_view text) 
{
    std::string list{ named_help };
    for (std::string_view emote : emotes)
        list += std::format(" /{}", emote);

    send_action(*event.peer, "log", std::format("msg|>> Commands: {} \0", list));
    const u_char role = static_cast<::peer*>(event.peer->data)->role;
    if (role >= MODERATOR)
        send_action(*event.peer, "log", std::format("msg|>> `#Moderator:`` {}", mod_help));
    if (role >= DEVELOPER)
        send_action(*event.peer, "log", std::format("msg|>> `6Developer:`` {}", dev_help));
    if (role >= OWNER)
        send_action(*event.peer, "log", std::format("msg|>> `9Owner:`` {}", owner_help));
};

std::unordered_map<std::string_view, std::function<void(ENetEvent&, const std::string_view)>> cmd_pool = []
{
    std::unordered_map<std::string_view, std::function<void(ENetEvent&, const std::string_view)>> pool
    {
        {"help", help_return },
        {"?", help_return },
        {"time", &command::time}, // @note namespace is to prevent mismatching C time
        {"find", &find},
        {"warp", &warp},
        {"punch", &punch},
        {"skin", &skin},
        {"sb", &sb},
        {"who", &who},
        {"me", &me},
        {"news", &news},
        {"weather", &weather},
        {"ghost", &ghost},
        {"give", &give},
        {"setrole", &setrole},
        {"kick", &kick},
        {"ban", &ban},
        {"unban", &unban},
        {"nuke", &nuke},
        {"unnuke", &unnuke},
        {"restart", &restart},
        {"uba", &uba},
        {"wban", &wban},
        {"title", &title},
        {"givetitle", &givetitle},
        {"nick", &nick},
        {"givegems", &givegems},
        {"hidestatus", &hidestatus},
        {"event", &event_command},
        {"top", &top},
        {"start", &start_command},
        {"rank", &rank_command},
        {"giverank", &giverank},
        {"msg", &msg_command},
        {"online", &online_command},
        {"rebuild", &rebuild_command},
        {"pay", &pay}, {"go", &go}, {"rules", &rules}, {"mods", &mods}, {"radio", &radio}, {"info", &info}, {"status", &status},
        {"warpto", &warpto}, {"mute", &mute}, {"unmute", &unmute}, {"curse", &curse}, {"uncurse", &uncurse}, {"warn", &warn}, {"kickall", &kickall},
        {"parkour", [](ENetEvent &event, const std::string_view) { warp(event, "warp PARKOUR"); }}
    };

    for (std::string_view emote : emotes)
        pool.emplace(emote, &on::Action);

    return pool;
}();
