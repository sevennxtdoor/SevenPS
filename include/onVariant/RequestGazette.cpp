#include "pch.hpp"
#include "tools/create_dialog.hpp"
#include "automate/holiday.hpp"
#include "commands/gtos.hpp"

#include "RequestGazette.hpp"

void on::RequestGazette(ENetEvent& event)
{
    std::tm time = localtime();
    constexpr std::array<std::string_view, 12> month{ "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
    const std::string &greeting = holiday_greeting().first;

    std::string dialog = ::create_dialog()
        .set_default_color("`o")
        .add_label_with_icon("big", "`wThe MikeTopia Gazette``", 5016)
        .add_textbox("`oPowered By `6@Mike``")
        .add_spacer("small")
        .add_image_button("banner", holiday_banner(), "bannerlayout", "")
        .add_spacer("small")
        .add_textbox(std::format("`w{} {}{}``{}{}", month[time.tm_mon], time.tm_mday,
            (time.tm_mday >= 11 && time.tm_mday <= 13) ? "th" :
            (time.tm_mday % 10 == 1) ? "st" :
            (time.tm_mday % 10 == 2) ? "nd" :
            (time.tm_mday % 10 == 3) ? "rd" : "th", greeting.empty() ? "" : ": ", greeting))
        .add_spacer("small")
        /* IOTM goes here. but it's P2W, so i will not add. */
        .add_textbox("Welcome to `2MikeTopia``! A private Growtopia server built and run by `6@Mike``. Build your world, lock it with a `2World Lock`` and have fun!")
        .add_spacer("small")
        .add_textbox("`9MikeTopia: Season I``")
        .add_textbox("The economy works like real Growtopia: items come from `2farming``, the `2Store`` and `2trading``. A hard-working player can earn about `w1 World Lock per hour``!")
        .add_spacer("small")
        .add_textbox("`5Latest Updates:``")
        .add_textbox("`w- `9Fossils & treasure``: dig fossils with a `wRock Hammer`` and `wRock Chisel``, clean them in a `wFossil Prep Station``. Mystery Blocks, Pots O' Gold, pinatas and treasure hoards drop real prizes now!``")
        .add_textbox("`w- `9Harmonic Crystals & Bunny Eggs``: stack 5 crystals, surround them with the right Chi and tap them for crystal wings, dragons and more. Grow a `wBunny Egg`` with Magic Eggs and break it for a prize!``")
        .add_textbox("`w- `9Providers``: Cows, Chickens, Wells, ATMs, Science Stations, Sheep, Unicorns... now grow for their real time and give their real harvest. Wrench one to see when it's ready.``")
        .add_textbox("`w- `9Daily Spin & Redeem Codes``: type `2/spin`` once a day for a free prize, and `2/redeem CODE`` (or Store > Redeem Code) for codes from events. 100 Diamond Locks now make a `2Blue Gem Lock``!``")
        .add_textbox("`w- `9Lucky Box & Leaderboards``: try your luck with `2/luckybox`` (ranks get a discount and more Daily Spins), and see who's the best with `2/top``.``")
        .add_textbox("`w- `9Consumables``: Blueberries, Devil Horns, Freeze Wands, Lucky Clovers, Apples and more now really work, for a while! See your mods with `$/status``.``")
        .add_textbox("`w- `9Safer server``: protection against bots, spam and hacked clients. Logging in somewhere else now closes your old session. And MikeTopia has its own login page now.``")
        .add_textbox("`w- `9Sponsor Items``: the `wMikeTopia Crown``, `wCape of Mike``, `wMikeTopia Aura`` and `wMike's Blade``, each with a special power. Type `$/sponsor``!``")
        .add_textbox("`w- `9Guilds``: start one with `$/guild create {name}``, invite friends, chat with `$/gc`` and pick a guild flag!``")
        .add_textbox("`w- `9Tutorial``: new here? Type `$/tutorial`` and follow 6 easy steps for `w1000`` gems.``")
        .add_textbox("`w- `9Item Hunt``: every 3 hours `w5 Treasure Chests`` are hidden in START. Find one for gems! Type `$/hunt`` for hints.``")
        .add_textbox("`w- `9Casino``: everyone in the world now sees Roulette Wheel spins, with the reme number. Get the `2Vegas Pack`` in the Store!``")
        .add_textbox("`w- `9Fishing``: wear a Fishing Rod, pour some water, use bait and punch when it splashes. Get the `2Fishin' Pack`` in the Store!``")
        .add_textbox("`w- `9Mannequins``, the `9Magplant 5000`` + Remote and `9Password Doors`` are here.``")
        .add_spacer("small")
        .add_textbox("`5Server Features:``")
        .add_textbox("`w- `9Automatic events`` every 2 hours: Gems x2, XP x2, Lucky Drops, Surgery Day! Check `$/event```")
        .add_textbox("`w- Chat quizzes, online rewards every 30 minutes and a daily prize in `$/parkour```")
        .add_textbox("`w- `$/top`` leaderboards, `$/rank`` for ranks from VIP to GOD``")
        .add_textbox("`w- Wrench yourself: Achievements, Daily Goals, Grow Pass, Missions, Wardrobe, World Lock Bank``")
        .add_textbox("`w- `2Surgery`` at the Surg-E, `2Trading``, `2Vending Machines`` and `2Area Locks```")
        .add_textbox("`w- Type `$/start`` to go to the START world, `$/help`` for all commands``")
        .add_spacer("small")
        .add_textbox("`4Important:`` read the `$/rules`` first. Staff will `4never`` ask for your password!")
        .add_button("mt_rules", "`wServer Rules``")
        .add_spacer("small")
        .add_textbox("`4Coming Soon:`` more sponsor items and events! Keep an eye on the news!")
        .add_spacer("small")
        .add_image_button("gazette_PrivacyPolicy", "interface/large/gazette/gazette_3columns_policy_btn02.rttex", "3imageslayout", "https://www.ubisoft.com/en-us/privacy-policy")
        .add_image_button("gazette_GrowtopianCode", "interface/large/gazette/gazette_3columns_policy_btn01.rttex", "3imageslayout", "https://support.ubi.com/en-us/growtopia-faqs/the-growtopian-code/")
        .add_image_button("gazette_TermsofUse", "interface/large/gazette/gazette_3columns_policy_btn03.rttex", "3imageslayout", "https://legal.ubi.com/termsofuse/")
        .add_quick_exit().add_spacer("small")
        .end_dialog("gazette", "", "OK");

    send_varlist(event.peer, { "OnDialogRequest", dialog });
}

void gazette_return(ENetEvent &event, const ::hPipe &hPipe)
{
    if (hPipe["buttonClicked"] == "mt_rules") rules(event, "rules"); // @note the "Server Rules" button
}
