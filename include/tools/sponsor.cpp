#include "pch.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "onVariant/SetBux.hpp"
#include "tools/economy.hpp"

#include "sponsor.hpp"

#include <fstream>

namespace
{
    struct offer { short id{}; std::string description{}; int price{}; };

    /* @note read every time: the file is tiny and the owner may edit prices while the server runs (names need a restart) */
    std::vector<offer> offers()
    {
        std::vector<offer> list{};
        std::ifstream file("resources/sponsor.txt");
        for (std::string line; std::getline(file, line); )
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line.front() == '#') continue;
            const std::vector<std::string> f = readch(line, '|');
            if (f.size() < 4) continue;
            const int id = atoi(f[0].c_str()), price = atoi(f[3].c_str());
            if (id <= 0 || id >= static_cast<int>(items.size()) || price <= 0) continue;
            list.push_back(offer{ static_cast<short>(id), f[2], price });
        }
        return list;
    }

    int held(const ::peer &p, short id)
    {
        auto it = std::ranges::find(p.slots, id, &::slot::id);
        return (it != p.slots.end()) ? it->count : 0;
    }
}

void sponsor_command(ENetEvent &event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    std::string rows{};
    for (const offer &o : offers())
    {
        const bool owned = held(*pPeer, o.id) > 0 || std::ranges::any_of(pPeer->clothing, [&](auto c) { return c == o.id; });
        rows += std::format("add_label_with_icon|big|`w{}``|left|{}|\nadd_smalltext|{}|left|\n", id_to_item(o.id).raw_name, o.id, o.description);
        rows += owned ? "add_smalltext|`2You own this one.``|left|\n" :
            std::format("add_button|buy_{}|Buy for `${}`` gems|noflags|0|0|\n", o.id, o.price);
        rows += "add_spacer|small|\n";
    }
    if (rows.empty()) rows = "add_textbox|No sponsor items right now. Check back later!|left|\n";
    send_varlist(event.peer, { "OnDialogRequest", std::format(
        "set_default_color|`o\n"
        "add_label_with_icon|big|`9MikeTopia Sponsor Items``|left|242|\n"
        "add_textbox|Exclusive MikeTopia items with special powers. You have `${}`` gems.|left|\n"
        "add_spacer|small|\n"
        "{}"
        "end_dialog|mt_sponsor||Close|\n", pPeer->gems, rows) });
}

void sponsor_return(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const std::string clicked = hPipe["buttonClicked"];
    if (!clicked.starts_with("buy_")) return;
    const int id = atoi(clicked.c_str() + 4);

    /* @note never trust the dialog: the item and price come from our file, not from the button */
    const std::vector<offer> list = offers();
    auto it = std::ranges::find(list, static_cast<short>(id), &offer::id);
    if (it == list.end()) return;
    const std::string name = id_to_item(it->id).raw_name;

    if (held(*pPeer, it->id) > 0 || std::ranges::any_of(pPeer->clothing, [&](auto c) { return c == it->id; }))
        return on::ConsoleMessage(event.peer, std::format("`4You already own {}.``", name));
    if (pPeer->gems < it->price)
        return on::ConsoleMessage(event.peer, std::format("`4You need {} gems for {}.``", it->price, name));
    const bool has_slot = std::ranges::find(pPeer->slots, it->id, &::slot::id) != pPeer->slots.end();
    if (!has_slot && static_cast<int>(std::ranges::count_if(pPeer->slots, [](const ::slot &s) { return s.count > 0; })) >= pPeer->slot_size)
        return on::ConsoleMessage(event.peer, "`4Your backpack is full.``");

    pPeer->gems -= it->price;
    econ("sponsor shop", -it->price);
    on::SetBux(event);
    modify_item_inventory(event, ::slot(it->id, 1));
    send_action(*event.peer, "play_sfx", "file|audio/cash_register.wav\ndelayMS|0\n");
    on::ConsoleMessage(event.peer, std::format("`9You bought `w{}``! Wear it to use its power.``", name));
    peers("", PEER_ALL, [&](ENetPeer &p) {
        if (static_cast<::peer*>(p.data) && !static_cast<::peer*>(p.data)->growid.empty())
            on::ConsoleMessage(&p, std::format("`9** ``{} `9just bought the sponsor item `w{}``!``", pPeer->display_growid, name));
    });
}
