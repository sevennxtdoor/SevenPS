#include "pch.hpp"
#include <filesystem>
#include <fstream>
#include "tools/jammer.hpp"
#include "tools/buffs.hpp"

#include "playmods.hpp"

namespace
{
    constexpr float ZOMBIE_SPEED = 130.0f;    // @note from GTOS' g-Virus
    constexpr float ANTIGRAVITY = 400.0f;     // @note lower gravity = higher, floatier jumps

    struct playmod { float speed{}, gravity{}; std::string name{}; int flags{}, states{}; u_char punch_range{}, build_range{}; };

    /* @note named character states for the 'extra' column. bits from GTopia's eCharacterStateFlag (MIT, (c) keichira),
     * matching the notes already in SetClothing.cpp. only looks/abilities the client draws or applies itself */
    const std::unordered_map<std::string_view, int> state_names{
        { "eyes",       0x00000004 }, // @note invisible, only eyes and mouth (The One Ring)
        { "noface",     0x00000010 }, // @note no face (No-Face)
        { "horns",      0x00000040 }, // @note devil horns
        { "halo",       0x00000080 }, // @note angel halo
        { "highjump",   0x00000100 },
        { "fireproof",  0x00000200 }, // @note lava doesn't hurt (also checked in movement.cpp)
        { "spikeproof", 0x00000400 },
        { "stinky",     0x00004000 },
        { "sparkle",    0x00008000 },
        { "shadow",     0x00040000 },
        { "irradiated", 0x00080000 },
        { "spotlight",  0x00100000 },
    };
    std::unordered_map<short, playmod> mods{};
    std::filesystem::file_time_type loaded_at{};
}

/* @note resources/playmods.txt: id|speed|gravity|name|extra. read again whenever the file changes */
static void load()
{
    std::error_code ec;
    const auto changed = std::filesystem::last_write_time("resources/playmods.txt", ec);
    if (ec || changed == loaded_at) return;
    loaded_at = changed;
    mods.clear();

    std::ifstream file("resources/playmods.txt");
    for (std::string line; std::getline(file, line); )
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.starts_with('#')) continue;
        std::vector<std::string> v = readch(line, '|');
        if (v.size() < 4) continue;
        playmod mod{ .speed = static_cast<float>(atof(v[1].c_str())), .gravity = static_cast<float>(atof(v[2].c_str())), .name = v[3] };
        if (v.size() > 4)
            for (const std::string &extra : readch(v[4], ','))
                if (extra == "dj") mod.flags |= PM_DOUBLE_JUMP;
                else if (extra == "dig") mod.flags |= PM_DIG;
                else if (extra.starts_with("pr+")) mod.punch_range = static_cast<u_char>(std::clamp(atoi(extra.c_str() + 3), 0, 10));
                else if (extra.starts_with("br+")) mod.build_range = static_cast<u_char>(std::clamp(atoi(extra.c_str() + 3), 0, 10));
                else if (auto s = state_names.find(extra); s != state_names.end()) mod.states |= s->second;
                else std::printf("playmods.txt: unknown extra '%s' (%s)\n", extra.c_str(), v[0].c_str());
        mods[static_cast<short>(atoi(v[0].c_str()))] = std::move(mod);
    }
}

static bool in_antigravity(const ::peer &p)
{
    if (p.netid == 0) return false;
    auto world = std::ranges::find(worlds, p.recent_worlds.back(), &::world::name);
    return world != worlds.end() && jammer_on(*world, jammer::ANTIGRAVITY);
}

::pos player_physics(const ::peer &p)
{
    load();
    float speed = NORMAL_SPEED, gravity = NORMAL_GRAVITY;
    for (float cloth : p.clothing)
        if (auto it = mods.find(static_cast<short>(cloth)); cloth != 0 && it != mods.end())
        {
            if (it->second.speed > 0) speed = (it->second.speed < NORMAL_SPEED) ? std::min(speed, it->second.speed) : std::max(speed, it->second.speed);
            if (it->second.gravity > 0) gravity = std::min(gravity, it->second.gravity);
        }
    buff_physics(p, speed, gravity); // @note Caffeinated, Muddy, Slimed...
    if (p.state & S_ZOMBIE) speed = ZOMBIE_SPEED;
    if (in_antigravity(p)) gravity = std::min(gravity, ANTIGRAVITY);
    return { speed, gravity };
}

bool has_playmod(const ::peer &p, int flag)
{
    load();
    for (float cloth : p.clothing)
        if (auto it = mods.find(static_cast<short>(cloth)); cloth != 0 && it != mods.end() && (it->second.flags & flag))
            return true;
    return false;
}

int playmod_states(const ::peer &p)
{
    load();
    int states{};
    for (float cloth : p.clothing)
        if (auto it = mods.find(static_cast<short>(cloth)); cloth != 0 && it != mods.end()) states |= it->second.states;
    return states;
}

u_char playmod_range(const ::peer &p, bool punch)
{
    load();
    u_char extra{};
    for (float cloth : p.clothing)
        if (auto it = mods.find(static_cast<short>(cloth)); cloth != 0 && it != mods.end())
            extra = std::max(extra, punch ? it->second.punch_range : it->second.build_range);
    return extra;
}

std::string playmod_names(const ::peer &p)
{
    load();
    std::string names{};
    const auto add = [&names](std::string_view n) { names += std::format("{}{}", names.empty() ? "" : ", ", n); };
    for (float cloth : p.clothing)
        if (auto it = mods.find(static_cast<short>(cloth)); cloth != 0 && it != mods.end()) add(it->second.name);
    if (in_antigravity(p)) add("Antigravity");
    return names.empty() ? "none" : names;
}
