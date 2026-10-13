#include "pch.hpp"
#include <filesystem>
#include <fstream>
#include "tools/jammer.hpp"

#include "playmods.hpp"

namespace
{
    constexpr float ZOMBIE_SPEED = 130.0f;    // @note from GTOS' g-Virus
    constexpr float ANTIGRAVITY = 400.0f;     // @note lower gravity = higher, floatier jumps

    struct playmod { float speed{}, gravity{}; std::string name{}; int flags{}; };
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
        int flags{};
        if (v.size() > 4)
            for (const std::string &extra : readch(v[4], ','))
                if (extra == "dj") flags |= PM_DOUBLE_JUMP;
                else if (extra == "dig") flags |= PM_DIG;
        mods[static_cast<short>(atoi(v[0].c_str()))] = { static_cast<float>(atof(v[1].c_str())), static_cast<float>(atof(v[2].c_str())), v[3], flags };
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

std::string playmod_names(const ::peer &p)
{
    load();
    std::string names{};
    const auto add = [&names](std::string_view n) { names += std::format("{}{}", names.empty() ? "" : ", ", n); };
    for (float cloth : p.clothing)
        if (auto it = mods.find(static_cast<short>(cloth)); cloth != 0 && it != mods.end()) add(it->second.name);
    if (p.state & S_ZOMBIE) add("`2Zombie``");
    if (in_antigravity(p)) add("Antigravity");
    return names.empty() ? "none" : names;
}
