#include "pch.hpp"
#include "store.hpp"

/* @note no real money on SevenPS: the Growtopia gem packs, subscriptions, passes and the Growtopia Shop link (all bought with
 * money in the real game, and pointing to a payment site) were taken out of the home tab. gems are earned by playing */
void action::store(ENetEvent& event, const std::string& header)
{
    std::vector<std::string> pipes = readch(header, '|');

    if (header.empty() || (pipes.size() > 3 && pipes[3] == "gem")) // @note location|gem. pipes[3] read past the end of a short packet
    {
        send_varlist(event.peer, {
            "OnStoreRequest",
            "set_description_text|Welcome to the `2SevenPS Store``! Select the item you'd like more info on.`o Gems can't be bought here: earn them by playing!\n"

            /* tabs */
            "enable_tabs|1\n"
            "add_tab_button|main_menu|Home|interface/large/btn_shop.rttex||1|0|0|0||||-1|-1|||0|0|CustomParams:|\n"
            "add_tab_button|locks_menu|Locks And Stuff|interface/large/btn_shop.rttex||0|1|0|0||||-1|-1|||0|0|CustomParams:|\n"
            "add_tab_button|itempack_menu|Item Packs|interface/large/btn_shop.rttex||0|3|0|0||||-1|-1|||0|0|CustomParams:|\n"
            "add_tab_button|bigitems_menu|Awesome Items|interface/large/btn_shop.rttex||0|4|0|0||||-1|-1|||0|0|CustomParams:|\n"
            "add_tab_button|weather_menu|Weather Machines|interface/large/btn_shop.rttex|Tired of the same sunny sky?  We offer alternatives within...|0|5|0|0||||-1|-1|||0|0|CustomParams:|\n"
            "add_tab_button|token_menu|Growtoken Items|interface/large/btn_shop.rttex||0|2|0|0||||-1|-1|||0|0|CustomParams:|\n"

            "add_banner|interface/large/gui_shop_featured_header.rttex|0|1|\n"
            "add_button|pet_battle_pack|`oPet Battle Pack``|interface/large/store_buttons/store_buttons42.rttex|`2You Get:`` 1 Pet Battle Supply Crate, 1 Battle Leash and 1 Battle Pet Cage.<CR><CR>`5Description:`` When a Growtopian plucks up the courage, they can get their own battle pet. This pack contains all the ingredients to create your very own starter Battle Pet and get you ready for your very first Pet Battle. Splicing new battle abilities will cost 10 World Locks per ability, World Locks not included!<CR><CR>`4This pack can be only be purchased once.``|1|6|40000|0|||-1|-1||-1|-1||1||||||0|0|CustomParams:|\n"
            "add_button|redeem_code|Redeem Code|interface/large/store_buttons/store_buttons40.rttex||1|5|0|0|||-1|-1||-1|-1|`2Redeem a SevenPS code`` from an event, the Discord or the owner.|1||||||0|0|CustomParams:|\n" // @note was OPENDIALOG&showredeemcodewindow: the game's own window, its answer is unknown
        });
    }
}
