#include "pch.hpp"
#include <fstream>
#include "onVariant/ConsoleMessage.hpp"
#include "commands/warp.hpp"
#include "tools/jammer.hpp"
#include "action/friends.hpp" // @note Back -> Social Portal

#include "community_hub.hpp"

namespace
{
    constexpr std::size_t TOP_WORLDS = 10;

    struct creator { std::string name{}, platform{}, url{}; };
    struct hub
    {
        std::vector<std::string> featured{}, wotd{}; // @note wotd: the first one is today's
        std::vector<std::string> wotd_dates{};
        std::vector<creator> creators{};
        std::string youtube{};
    };

    bool world_name_ok(const std::string &name) { return !name.empty() && name.size() <= 24 && alpha(name); }
    bool url_ok(const std::string &url) { return url.starts_with("https://") && url.size() <= 200 && url.find_first_of("|\n") == std::string::npos; }
    std::string upper(std::string s) { for (char &c : s) c = static_cast<char>(std::toupper(static_cast<u_char>(c))); return s; }

    /* @note read every time: the file is tiny and the owner may change it while the server runs */
    hub load()
    {
        hub h{};
        std::ifstream file("resources/community_hub.txt");
        for (std::string line; std::getline(file, line); )
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line.front() == '#') continue;
            const std::vector<std::string> f = readch(line, '|');
            if (f.size() < 2) continue;
            if (f[0] == "featured" && world_name_ok(f[1])) h.featured.push_back(upper(f[1]));
            else if (f[0] == "wotd" && world_name_ok(f[1]))
            {
                h.wotd.push_back(upper(f[1]));
                h.wotd_dates.push_back(f.size() >= 3 ? f[2] : "");
            }
            else if (f[0] == "creator" && f.size() >= 3) h.creators.push_back(creator{ f[1], f[2], (f.size() >= 4 && url_ok(f[3])) ? f[3] : "" });
            else if (f[0] == "youtube" && url_ok(f[1])) h.youtube = f[1];
        }
        return h;
    }

    void send(ENetEvent &event, const std::string &body)
    {
        send_varlist(event.peer, { "OnDialogRequest", "set_default_color|`o\n" + body + "add_quick_exit|\nend_dialog|mt_hub|Close||\n" });
    }

    std::string world_button(const std::string &name, const std::string &extra = "")
    {
        return std::format("add_button|go_{}|`w{}``{}|noflags|0|0|\n", name, name, extra);
    }

    void worlds_dialog(ENetEvent &event, const std::string &title, const std::string &text, const std::string &buttons)
    {
        send(event, std::format(
            "add_label_with_icon|big|`w{}``|left|3802|\n"
            "add_spacer|small|\n"
            "add_textbox|{}|left|\n"
            "add_spacer|small|\n"
            "{}"
            "add_spacer|small|\n"
            "add_button|hub|Back|noflags|0|0|\n", title, text, buttons.empty() ? "add_smalltext|`oNothing here yet.``|left|\n" : buttons));
    }
}

void send_community_hub(ENetEvent &event)
{
    const hub h = load();
    send(event, std::format(
        "add_label_with_icon|big|`wCommunity Hub``|left|1366|\n"
        "add_spacer|small|\n"
        "add_textbox|Check out the best worlds and the community's content creators!|left|\n"
        "add_spacer|small|\n"
        "add_button|hub_featured|`wFeatured Worlds``|noflags|0|0|\n"
        "add_button|hub_wotd|`wWorld of the Day``|noflags|0|0|\n"
        "add_button|hub_top|`wTop Worlds``|noflags|0|0|\n"
        "add_button|hub_creators|`wContent Creators``|noflags|0|0|\n"
        "{}"
        "add_spacer|small|\n"
        "add_button|back|Back|noflags|0|0|\n",
        h.youtube.empty() ? "" : std::format("add_url_button||`wOfficial YouTube Channel``|noflags|{}|Open the YouTube channel?|0|0|\n", h.youtube)));
}

void community_hub_return(ENetEvent &event, const ::hPipe &hPipe)
{
    const ::peer &p = *static_cast<::peer*>(event.peer->data);
    const std::string clicked = hPipe["buttonClicked"];
    if (clicked == "back") return action::friends(event, "");
    if (clicked == "hub") return send_community_hub(event);

    if (clicked.starts_with("go_"))
    {
        const std::string name = upper(clicked.substr(3));
        if (!world_name_ok(name)) return;
        if (!p.recent_worlds.empty() && p.recent_worlds.back() == name) return on::ConsoleMessage(event.peer, "`oYou're already in that world.``");
        return warp(event, "warp " + name); // @note joining checks bans, nukes and locks as any other join
    }

    const hub h = load();
    if (clicked == "hub_featured")
    {
        std::string buttons{};
        for (const std::string &name : h.featured) buttons += world_button(name);
        return worlds_dialog(event, "Featured Worlds", "Worlds picked by the SevenPS team. Tap one to visit!", buttons);
    }
    if (clicked == "hub_wotd")
    {
        std::string buttons{};
        for (std::size_t i = 0; i < h.wotd.size(); ++i)
        {
            if (i == 1) buttons += "add_spacer|small|\nadd_textbox|`wPast Worlds of the Day``|left|\n";
            const std::string date = h.wotd_dates[i].empty() ? "" : std::format(" `o({})``", h.wotd_dates[i]);
            buttons += world_button(h.wotd[i], date);
        }
        return worlds_dialog(event, "World of the Day", "Today's World of the Day is at the top.", buttons);
    }
    if (clicked == "hub_top")
    {
        std::vector<const ::world*> top{};
        for (const ::world &w : worlds)
            if (w.visitors > 0 && (p.role >= MODERATOR || !jammer_on(w, jammer::SIGNAL))) top.push_back(&w); // @note Signal Jammer hides it, as in the world menu
        std::ranges::sort(top, std::greater{}, [](const ::world *w) { return w->visitors; });
        if (top.size() > TOP_WORLDS) top.resize(TOP_WORLDS);
        std::string buttons{};
        for (const ::world *w : top) buttons += world_button(w->name, std::format(" `o({} online)``", w->visitors));
        return worlds_dialog(event, "Top Worlds", "The busiest worlds right now.", buttons);
    }
    if (clicked == "hub_creators")
    {
        std::string rows{};
        for (const creator &c : h.creators)
        {
            rows += std::format("add_label_with_icon|small|`w{}`` `o({})``|left|1366|\n", c.name, c.platform);
            if (!c.url.empty()) rows += std::format("add_url_button||`wVisit {}``|noflags|{}|Open {}'s page?|0|0|\n", c.name, c.url, c.name);
        }
        return send(event, std::format(
            "add_label_with_icon|big|`wContent Creators``|left|1366|\n"
            "add_spacer|small|\n"
            "add_textbox|Check out these content creators from the community!|left|\n"
            "add_spacer|small|\n"
            "{}"
            "add_spacer|small|\n"
            "add_button|hub|Back|noflags|0|0|\n", rows.empty() ? "add_smalltext|`oNothing here yet.``|left|\n" : rows));
    }
}
