#include "pch.hpp"
#include "tools/create_dialog.hpp"
#include "automate/holiday.hpp"
#include "commands/gtos.hpp"

#include "RequestGazette.hpp"

void on::RequestGazette(ENetEvent& event)
{
    std::tm time = localtime();
    constexpr std::array<std::string_view, 12> month{ "Januari", "Februari", "Maret", "April", "Mei", "Juni", "Juli", "Agustus", "September", "Oktober", "November", "Desember" };
    const std::string &greeting = holiday_greeting().first;

    std::string dialog = ::create_dialog()
        .set_default_color("`o")
        .add_label_with_icon("big", "`wKoran MikeTopia``", 5016)
        .add_textbox("`oDipersembahkan oleh `6@Mike``")
        .add_spacer("small")
        .add_image_button("banner", holiday_banner(), "bannerlayout", "")
        .add_spacer("small")
        .add_textbox(std::format("`w{} {} {}``{}{}", time.tm_mday, month[time.tm_mon], time.tm_year + 1900, greeting.empty() ? "" : ": ", greeting))
        .add_spacer("small")
        /* IOTM goes here. but it's P2W, so i will not add. */
        .add_textbox("Selamat datang di `2MikeTopia``! Server Growtopia pribadi yang dibangun dan dikelola langsung oleh `6@Mike``. Bangun world kamu, kunci pakai `2World Lock``, dan bersenang-senanglah!")
        .add_spacer("small")
        .add_textbox("`9MikeTopia: Season I``")
        .add_textbox("Ekonomi di sini seperti Growtopia asli: item didapat dari `2farming``, `2Store`` dan `2trade``. Pemain rajin bisa dapat sekitar `w1 World Lock per jam``!")
        .add_spacer("small")
        .add_textbox("`5Update Terbaru:``")
        .add_textbox("`w- `9Item Hunt``: tiap 3 jam ada `w5 Treasure Chest`` tersembunyi di START. Temukan untuk dapat gems! Ketik `$/hunt`` untuk petunjuk.``")
        .add_textbox("`w- `9Casino``: Roulette Wheel sekarang dilihat semua orang, lengkap dengan angka reme. Beli `2Vegas Pack`` di Store!``")
        .add_textbox("`w- `9Fishing``: pakai Fishing Rod, siram air, pasang umpan, lalu pukul saat ada cipratan. Beli `2Fishin' Pack`` di Store!``")
        .add_textbox("`w- `9Mannequin``, `9Magplant 5000`` + Remote, dan `9Password Door`` sudah bisa dipakai.``")
        .add_spacer("small")
        .add_textbox("`5Fitur Server:``")
        .add_textbox("`w- `9Event otomatis`` tiap 2 jam: Gems x2, XP x2, Lucky Drops, Surgery Day! Cek `$/event```")
        .add_textbox("`w- Kuis di chat, hadiah online tiap 30 menit, dan hadiah harian di `$/parkour```")
        .add_textbox("`w- `$/top`` papan peringkat, `$/rank`` untuk rank VIP sampai GOD``")
        .add_textbox("`w- Wrench dirimu: Achievements, Goals harian, Grow Pass, Missions, Wardrobe, World Lock Bank``")
        .add_textbox("`w- `2Surgery`` di Surg-E, `2Trade``, `2Vending Machine`` dan `2Area Lock```")
        .add_textbox("`w- Ketik `$/start`` untuk ke world START, `$/help`` untuk semua command``")
        .add_spacer("small")
        .add_textbox("`4Penting:`` baca `$/rules`` dulu ya. Staff `4tidak pernah`` meminta password kamu!")
        .add_button("mt_rules", "`wPeraturan Server``")
        .add_spacer("small")
        .add_textbox("`4Segera Hadir:`` `9Sponsor Items`` eksklusif MikeTopia dan `9Guild``! Pantau terus koran ini ya!")
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
    if (hPipe["buttonClicked"] == "mt_rules") rules(event, "rules"); // @note the "Peraturan Server" button
}
