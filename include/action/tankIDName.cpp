#include "pch.hpp"
#include "tools/web_login.hpp"
#include "action/quit.hpp"
#include "automate/holiday.hpp"

#include "tankIDName.hpp"

/* GameLogicComponent::OnInitialLogonAccepted(VariantList*) */

void action::tankIDName(ENetEvent& event, const std::string& header)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    /* @note logging in again on the same connection reloaded the backpack from the database: items given away since
     * the last save came back (duplication), and progress since then was lost */
    if (pPeer->loaded) return;

    std::vector<std::string> pipes = readch(header, '|');
    if (pipes.empty() || pipes.size() < 41ull) enet_peer_disconnect_later(event.peer, 0);

    /* @todo instead of setting these values we should check if there valid from SQL.*/
    std::string protocol{};
    for (std::size_t i = 0; i + 1 < pipes.size(); ++i) // @note i+1 read past the end on the last pipe
    {
        if      (pipes[i] == "tankIDName") pPeer->growid = pipes[i+1];
        else if (pipes[i] == "tankIDPass") pPeer->password = pipes[i+1];
        else if (pipes[i] == "protocol")   protocol = pipes[i+1];
        else if (pipes[i] == "country")    pPeer->country = pipes[i+1];
        else if (pipes[i] == "user")       pPeer->user_id = atoi(pipes[i+1].c_str()); // @note std::stoi threw on "user|x" and crashed the server @todo validate user_id
    }
    if (atoi(protocol.c_str()) == 0) return; // @todo <226

    /* @note this second connection used to trust tankIDName: a modded client could log in as anyone (even the owner).
     * tankIDPass is the session token we gave in protocol.cpp, it must belong to this very GrowID */
    const std::string owner = session_growid(pPeer->password);
    if (owner.empty() || !std::ranges::equal(owner, pPeer->growid, [](char a, char b) { return std::tolower(static_cast<u_char>(a)) == std::tolower(static_cast<u_char>(b)); }))
    {
        std::printf("[login] %s refused: no valid login token\n", pPeer->growid.c_str());
        send_action(*event.peer, "log", "msg|`4Please log in again`` on the MikeTopia login page.");
        send_action(*event.peer, "logon_fail", "");
        pPeer->growid.clear();
        return;
    }
    pPeer->growid = owner; // @note the stored spelling
    /* @note same account already online: two copies of one inventory could trade/drop the same items (duplication).
     * the old session is saved and closed first, then this one loads what it saved */
    peers("", PEER_ALL, [&event, pPeer](ENetPeer &p)
    {
        const ::peer *pOld = static_cast<::peer*>(p.data);
        if (&p == event.peer || pOld == nullptr || !pOld->loaded) return;
        if (std::ranges::equal(pOld->growid, pPeer->growid, [](char a, char b) { return std::tolower(static_cast<u_char>(a)) == std::tolower(static_cast<u_char>(b)); }))
        {
            std::printf("[login] %s logged in again, closing the old session\n", pPeer->growid.c_str());
            send_action(p, "log", "msg|`4Your account was logged in from another place.``");
            enet_host_flush(host);
            ENetEvent old{ .peer = &p };
            action::quit(old, ""); // @note leaves the world, deletes (= saves) the player, resets the connection
        }
    });
    pPeer->load(pPeer->growid, pPeer->password); // @note the ACTUAL loading. the one in protocol.cpp must be removed for performance...
    if (pPeer->banned)
    {
        send_action(*event.peer, "log", "msg|`4Sorry, this account has been banned from MikeTopia.``");
        send_action(*event.peer, "logon_fail", ""); // @note triggers action|quit on client.
        return;
    }

    send_varlist(event.peer, { "OnOverrideGDPRFromServer", 18, 1, 0, 1 });

    /* v5.58 */
    send_varlist(event.peer, {
        "OnSuperMainStartAcceptLogonHrdxs47254722215a",
        items_hash, // @note items.dat hash, computed at startup (was a fixed number that no longer matched the file)
        "ubistatic-a.akamaihd.net",
        "0098/024920264/cache/",
        "cc.cz.madkite.freedom org.aqua.gg idv.aqua.bulldog com.cih.gamecih2 com.cih.gamecih com.cih.game_cih cn.maocai.gamekiller com.gmd.speedtime org.dax.attack com.x0.strai.frep com.x0.strai.free org.cheatengine.cegui org.sbtools.gamehack com.skgames.traffikrider org.sbtoods.gamehaca com.skype.ralder org.cheatengine.cegui.xx.multi1458919170111 com.prohiro.macro me.autotouch.autotouch com.cygery.repetitouch.free com.cygery.repetitouch.pro com.proziro.zacro com.slash.gamebuster",
        std::format(
            "proto={}|choosemusic=audio/mp3/about_theme.mp3|active_holiday={}|wing_week_day=0|ubi_week_day=0|server_tick=0|game_theme={}|clash_active=0|drop_lavacheck_faster=1|isPayingUser=1|usingStoreNavigation=1|enableInventoryTab=1|bigBackpack=1|seed_diary_hash=4266294761", 
            protocol, holiday, game_theme_string()
        ) + "|m_clientBits=|eventButtons={\"EventButtonData\":[{\"active\":false,\"buttonAction\":\"eventmenu\",\"buttonTemplate\":\"BaseEventButton\",\"counter\":0,\"counterMax\":0,\"itemIdIcon\":6244,\"name\":\"ClashEventButton\",\"order\":9,\"rcssClass\":\"clash-event\",\"text\":\"\"},{\"active\":true,\"buttonAction\":\"dailychallengemenu\",\"buttonTemplate\":\"BaseEventButton\",\"counter\":0,\"counterMax\":0,\"itemIdIcon\":23,\"name\":\"DailyChallenge\",\"order\":10,\"rcssClass\":\"daily_challenge\",\"text\":\"\"},{\"active\":true,\"buttonAction\":\"openPiggyBank\",\"buttonTemplate\":\"BaseEventButton\",\"counter\":0,\"counterMax\":0,\"name\":\"PiggyBankButton\",\"order\":20,\"rcssClass\":\"piggybank\",\"text\":\"\"},{\"active\":false,\"buttonAction\":\"showdungeonsui\",\"buttonTemplate\":\"DungeonEventButton\",\"counter\":0,\"counterMax\":20,\"name\":\"ScrollsPurchaseButton\",\"order\":30,\"rcssClass\":\"scrollbank\",\"text\":\"\"},{\"active\":false,\"buttonAction\":\"show_mailbox_ui\",\"buttonTemplate\":\"BaseEventButton\",\"counter\":0,\"counterMax\":0,\"name\":\"MailboxButton\",\"order\":30,\"rcssClass\":\"mailbox\",\"text\":\"\"},{\"active\":false,\"buttonAction\":\"show_auction_ui\",\"buttonTemplate\":\"BaseEventButton\",\"counter\":0,\"counterMax\":0,\"name\":\"AuctionButton\",\"order\":30,\"rcssClass\":\"auction\",\"text\":\"\"},{\"active\":false,\"buttonTemplate\":\"ActiveAuctionEventButton\",\"counter\":0,\"counterMax\":20,\"name\":\"ActiveAuctionButton\",\"order\":30,\"rcssClass\":\"activeauction\",\"text\":\"\"},{\"active\":false,\"buttonAction\":\"eventmenu\",\"buttonTemplate\":\"BaseEventButton\",\"counter\":0,\"counterMax\":0,\"itemIdIcon\":6244,\"name\":\"ClashEventButton\",\"order\":21,\"rcssClass\":\"clash-event\",\"text\":\"\"},{\"active\":false,\"buttonAction\":\"show_bingo_ui\",\"buttonTemplate\":\"BaseEventButton\",\"counter\":0,\"counterMax\":0,\"name\":\"WinterBingoButton\",\"order\":49,\"rcssClass\":\"wf-bingo\",\"text\":\"\"},{\"active\":false,\"buttonAction\":\"show_bingo_ui\",\"buttonTemplate\":\"BaseEventButton\",\"name\":\"UbiBingoButton\",\"order\":50,\"rcssClass\":\"ubi-bingo\",\"text\":\"\"},{\"active\":false,\"buttonAction\":\"winterrallymenu\",\"buttonTemplate\":\"BaseEventButton\",\"counter\":0,\"counterMax\":0,\"name\":\"WinterRallyButton\",\"order\":50,\"rcssClass\":\"winter-rally\",\"text\":\"\"},{\"active\":false,\"buttonAction\":\"leaderboardBtnClicked\",\"buttonTemplate\":\"BaseEventButton\",\"counter\":0,\"counterMax\":0,\"name\":\"AnniversaryLeaderboardButton\",\"order\":50,\"rcssClass\":\"anniversary-leaderboard\",\"text\":\"\"},{\"active\":false,\"buttonAction\":\"euphoriaBtnClicked\",\"buttonTemplate\":\"BaseEventButton\",\"counter\":0,\"counterMax\":0,\"name\":\"AnniversaryEuphoriaButton\",\"order\":50,\"rcssClass\":\"anniversary-euphoria\",\"text\":\"\"},{\"active\":false,\"buttonAction\":\"openLnySparksPopup\",\"buttonTemplate\":\"BaseEventButton\",\"counter\":0,\"counterMax\":5,\"name\":\"LnyButton\",\"order\":50,\"rcssClass\":\"cny\",\"text\":\"\"},{\"active\":false,\"buttonAction\":\"ShowValentinesQuestDialog\",\"buttonTemplate\":\"EventButtonWithCounter\",\"counter\":0,\"counterMax\":100,\"name\":\"ValentinesButton\",\"order\":50,\"rcssClass\":\"valentines_day\",\"text\":\"\"},{\"active\":false,\"buttonAction\":\"showegseeventui\",\"buttonTemplate\":\"EventButtonWithCounter\",\"counter\":0,\"counterMax\":20,\"name\":\"EasterButton\",\"order\":50,\"rcssClass\":\"easter_event\",\"text\":\"\"},{\"active\":false,\"buttonAction\":\"openStPatrickPiggyBank\",\"buttonTemplate\":\"BaseEventButton\",\"name\":\"StPatrickPBButton\",\"order\":50,\"rcssClass\":\"st_patrick_event\",\"text\":\"\"},{\"active\":false,\"buttonAction\":\"dailyrewardmenu\",\"buttonTemplate\":\"BaseEventButton\",\"counter\":0,\"counterMax\":1,\"name\":\"CincoPinataButton\",\"order\":50,\"rcssClass\":\"cinco_pinata_event\",\"text\":\"\"},{\"active\":false,\"buttonAction\":\"show_fruit_mixer_dialog\",\"buttonTemplate\":\"BaseEventButton\",\"counter\":0,\"counterMax\":0,\"name\":\"SPP_TropicalFruitsButton\",\"order\":50,\"rcssClass\":\"spp_tropical_fruits\",\"text\":\"\"},{\"active\":false,\"buttonAction\":\"claimprogressbar\",\"buttonTemplate\":\"EventButtonWithCounter\",\"counter\":0,\"counterMax\":0,\"name\":\"SummerfestButton\",\"order\":50,\"rcssClass\":\"summerfest\",\"text\":\"\"},{\"active\":false,\"buttonAction\":\"claimprogressbar\",\"buttonTemplate\":\"EventButtonWithCounter\",\"counter\":0,\"counterMax\":15,\"name\":\"HalloweenButton\",\"order\":50,\"rcssClass\":\"halloween\",\"text\":\"\"}]}"
    });
}