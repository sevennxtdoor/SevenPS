#include "pch.hpp"
#include "tools/create_dialog.hpp"
#include "automate/holiday.hpp"

#include "RequestGazette.hpp"

void on::RequestGazette(ENetEvent& event)
{
    std::tm time = localtime();
    std::vector<std::string> month = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };

    std::string dialog = ::create_dialog()
        .set_default_color("`o")
        .add_label_with_icon("big", "`wThe MikeTopia Gazette``", 5016)
        .add_textbox("`oPowered By `6@Mike``")
        .add_spacer("small")
        .add_image_button("banner", holiday_banner(), "bannerlayout", "")
        .add_spacer("small")
        .add_textbox(std::format("`w{} {}{}: {}|", month[time.tm_mon], time.tm_mday,
            (time.tm_mday >= 11 && time.tm_mday <= 13) ? "th" :
            (time.tm_mday % 10 == 1) ? "st" :
            (time.tm_mday % 10 == 2) ? "nd" :
            (time.tm_mday % 10 == 3) ? "rd" : "th", holiday_greeting().first))
        .add_spacer("small")
        /* IOTM goes here. but it's P2W, so i will not add. */
        .add_textbox("Selamat datang di `2MikeTopia``! Server Growtopia pribadi yang dibangun dan dikelola langsung oleh `6@Mike``. Bangun world kamu, kunci pakai `2World Lock``, dan bersenang-senanglah!")
        .add_spacer("small")
        .add_textbox("`5Fitur Server:``")
        .add_textbox("`w- `2Store`` lengkap: World Lock, Grow Spray, Upgrade Backpack, dan banyak lagi``")
        .add_textbox("`w- Pakai `2Wings`` atau item punggung untuk `2Double Jump``!``")
        .add_textbox("`w- Ketik `$/help`` untuk melihat semua command``")
        .add_spacer("small")
        .add_textbox("`4Segera Hadir:`` `9Sponsor Items`` eksklusif MikeTopia! Pantau terus news ini ya!")
        .add_spacer("small")
        .add_image_button("gazette_PrivacyPolicy", "interface/large/gazette/gazette_3columns_policy_btn02.rttex", "3imageslayout", "https://www.ubisoft.com/en-us/privacy-policy")
        .add_image_button("gazette_GrowtopianCode", "interface/large/gazette/gazette_3columns_policy_btn01.rttex", "3imageslayout", "https://support.ubi.com/en-us/growtopia-faqs/the-growtopian-code/")
        .add_image_button("gazette_TermsofUse", "interface/large/gazette/gazette_3columns_policy_btn03.rttex", "3imageslayout", "https://legal.ubi.com/termsofuse/")
        .add_quick_exit().add_spacer("small")
        .end_dialog("gazette", "", "OK");

    send_varlist(event.peer, { "OnDialogRequest", dialog });
}
