#include "pch.hpp"
#include <filesystem>
#include <fstream>
#include "onVariant/ConsoleMessage.hpp"
#include "onVariant/SetClothing.hpp"
#include "tools/profile.hpp"
#include "commands/titles.hpp"

#include "buffs.hpp"

namespace
{
    std::unordered_map<short, buff_def> defs{};
    std::filesystem::file_time_type loaded_at{};

    const std::unordered_map<std::string_view, int> state_names{
        { "horns", 0x40 }, { "halo", 0x80 }, { "frozen", 0x800 }, { "stinky", 0x4000 },
        { "sparkle", 0x8000 }, { "zombie", 0x10000 }, { "shadow", 0x40000 },
    };

    /* @note resources/consumables.txt, read again whenever it changes */
    void load()
    {
        std::error_code ec;
        const auto changed = std::filesystem::last_write_time("resources/consumables.txt", ec);
        if (ec || changed == loaded_at) return;
        loaded_at = changed;
        defs.clear();

        std::ifstream file("resources/consumables.txt");
        for (std::string line; std::getline(file, line); )
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line.starts_with('#')) continue;
            const std::vector<std::string> v = readch(line, '|');
            if (v.size() < 5) continue;
            buff_def d{ .id = static_cast<short>(atoi(v[0].c_str())), .target = v[1], .seconds = atoi(v[2].c_str()), .name = v[3] };
            if (v.size() > 5) d.added = v[5];
            if (v.size() > 6) d.removed = v[6];
            for (const std::string &e : readch(v[4], ','))
            {
                if (e.starts_with("skin=") && e.size() == 11)
                {
                    const u_int rgb = static_cast<u_int>(std::stoul(e.substr(5), nullptr, 16));
                    const u_int r = (rgb >> 16) & 0xff, g = (rgb >> 8) & 0xff, b = rgb & 0xff;
                    d.has_skin = true;
                    d.skin = (b << 24) | (g << 16) | (r << 8) | 0xff; // @note the game's skin order (same as GTopia's GetAsUINTSwap)
                }
                else if (e.starts_with("speed=")) d.speed = static_cast<float>(atof(e.c_str() + 6));
                else if (e.starts_with("gravity=")) d.gravity = static_cast<float>(atof(e.c_str() + 8));
                else if (e == "lucky") d.lucky = true;
                else if (e == "repel") d.repel = true;
                else if (e == "curse") d.curse = true;
                else if (e == "kill") d.kill = true;
                else if (e == "burp") d.burp = true;
                else if (auto s = state_names.find(e); s != state_names.end()) d.states |= s->second;
                else std::printf("consumables.txt: unknown effect '%s' (%s)\n", e.c_str(), v[0].c_str());
            }
            if (d.id > 0) defs[d.id] = std::move(d);
        }
        std::printf("consumables: %zu timed item(s)\n", defs.size());
    }

    std::string key_of(const std::string &name)
    {
        std::string k = "buff:";
        for (char c : name) if (std::isalnum(static_cast<u_char>(c))) k += c;
        return k;
    }

    /* @note every active buff of this player: {its definition, seconds left} */
    std::vector<std::pair<const buff_def*, int>> active(const ::peer &p)
    {
        load();
        std::vector<std::pair<const buff_def*, int>> out{};
        const int now = static_cast<int>(std::time(nullptr));
        for (const auto &[key, value] : p.profile)
        {
            if (!key.starts_with("buff:")) continue;
            const std::size_t slash = value.find('/');
            if (slash == std::string::npos) continue;
            const int ends = atoi(value.c_str());
            const auto def = defs.find(static_cast<short>(atoi(value.c_str() + slash + 1)));
            if (def != defs.end() && ends > now) out.emplace_back(&def->second, ends - now);
        }
        return out;
    }

    std::string time_text(int seconds)
    {
        if (seconds >= 3600) return std::format("{} hour{}", (seconds + 1800) / 3600, (seconds + 1800) / 3600 == 1 ? "" : "s");
        if (seconds >= 60) return std::format("{} min{}", (seconds + 30) / 60, (seconds + 30) / 60 == 1 ? "" : "s");
        return std::format("{} sec{}", seconds, seconds == 1 ? "" : "s");
    }

    void refresh(ENetPeer &peer, bool name)
    {
        ::peer *p = static_cast<::peer*>(peer.data);
        p->update_effects();
        if (p->netid != 0) on::SetClothing(peer);
        if (name) refresh_display_name(peer); // @note Doctor Repulsion hides "of Legend"
    }
}

const buff_def *buff_for_item(short id)
{
    load();
    const auto it = defs.find(id);
    return it != defs.end() ? &it->second : nullptr;
}

void buff_add(ENetEvent &target, const buff_def &def, ENetEvent &by)
{
    ::peer &t = *static_cast<::peer*>(target.peer->data);
    if (def.burp)
        peers(t.recent_worlds.back(), PEER_SAME_WORLD, [&t](ENetPeer &p) { send_varlist(&p, { "OnTalkBubble", t.netid, "`2BURRRPPP...!``", 0u }); });
    if (def.seconds <= 0) return;

    const int ends = static_cast<int>(std::time(nullptr)) + def.seconds;
    pset_str(t, key_of(def.name), std::format("{}/{}", ends, def.id));
    if (!def.added.empty()) send_varlist(target.peer, { "OnTalkBubble", t.netid, def.added, 0u, 1u });
    on::ConsoleMessage(target.peer, std::format("{} (`${}`` mod added, `${}`` left)", def.added, def.name, time_text(def.seconds)));
    if (target.peer != by.peer)
        on::ConsoleMessage(by.peer, std::format("`oYou used it on ``{}`o.``", t.display_growid));
    refresh(*target.peer, def.repel);
}

bool buff_remove(ENetEvent &event, const std::string &name)
{
    ::peer &p = *static_cast<::peer*>(event.peer->data);
    if (!p.profile.contains(key_of(name))) return false;
    p.profile.erase(key_of(name));
    refresh(*event.peer, true);
    return true;
}

bool has_buff(const ::peer &p, const std::string &name)
{
    for (const auto &[def, left] : active(p)) if (def->name == name) return true;
    return false;
}

int buff_states(const ::peer &p)
{
    int states{};
    for (const auto &[def, left] : active(p)) states |= def->states;
    return states;
}

std::optional<u_int> buff_skin(const ::peer &p)
{
    std::optional<u_int> skin{};
    int shortest = INT_MAX; // @note the newest-looking one: the buff with the least time left wins (a quick Egg over an hour of Blueberry)
    for (const auto &[def, left] : active(p))
        if (def->has_skin && left < shortest) shortest = left, skin = def->skin;
    return skin;
}

void buff_physics(const ::peer &p, float &speed, float &gravity)
{
    for (const auto &[def, left] : active(p))
    {
        if (def->speed > 0) speed = def->speed;
        if (def->gravity > 0) gravity = def->gravity;
    }
}

bool buff_lucky(const ::peer &p)
{
    for (const auto &[def, left] : active(p)) if (def->lucky) return true;
    return false;
}

bool buff_repel(const ::peer &p)
{
    for (const auto &[def, left] : active(p)) if (def->repel) return true;
    return false;
}

std::string buff_names(const ::peer &p)
{
    std::string out{};
    for (const auto &[def, left] : active(p))
        out += std::format("{}{} ({})", out.empty() ? "" : ", ", def->name, time_text(left));
    return out;
}

void buff_tick()
{
    static std::time_t last{};
    const std::time_t now = std::time(nullptr);
    if (now == last) return; // @note once a second is plenty
    last = now;
    load();

    peers("", PEER_ALL, [now](ENetPeer &peer)
    {
        ::peer *p = static_cast<::peer*>(peer.data);
        if (p == nullptr || !p->loaded) return;
        std::vector<std::string> ended{};
        for (const auto &[key, value] : p->profile)
            if (key.starts_with("buff:") && atoi(value.c_str()) <= now) ended.push_back(key);
        if (ended.empty()) return;
        bool name{};
        for (const std::string &key : ended)
        {
            const std::string value = p->profile[key];
            p->profile.erase(key);
            const std::size_t slash = value.find('/');
            const auto def = (slash == std::string::npos) ? defs.end() : defs.find(static_cast<short>(atoi(value.c_str() + slash + 1)));
            if (def == defs.end()) continue;
            on::ConsoleMessage(&peer, std::format("{} (`${}`` mod removed)", def->second.removed, def->second.name));
            name |= def->second.repel;
        }
        refresh(peer, name);
    });
}
