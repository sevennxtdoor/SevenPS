#include "pch.hpp"
#include <filesystem>
#include <fstream>
#include "onVariant/SetClothing.hpp"
#include "onVariant/ConsoleMessage.hpp"

#include "punch.hpp"

namespace
{
    std::unordered_map<u_int, u_char> effects{};
    std::filesystem::file_time_type loaded_at{};

    /* @note resources/punch_effects.txt: id|punch effect|name. read again whenever the file changes */
    void load()
    {
        std::error_code ec;
        const auto changed = std::filesystem::last_write_time("resources/punch_effects.txt", ec);
        if (ec || changed == loaded_at) return;
        loaded_at = changed;
        effects.clear();

        std::ifstream file("resources/punch_effects.txt");
        for (std::string line; std::getline(file, line); )
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line.starts_with('#')) continue;
            const std::vector<std::string> v = readch(line, '|');
            if (v.size() < 2) continue;
            const int id = atoi(v[0].c_str()), effect = atoi(v[1].c_str());
            if (id > 0 && effect > 0 && effect < 256) effects[static_cast<u_int>(id)] = static_cast<u_char>(effect);
        }
        std::printf("punch effects: %zu item(s)\n", effects.size());
    }
}

u_char get_punch_id(u_int item_id)
{
    load();
    const auto it = effects.find(item_id);
    return it != effects.end() ? it->second : 0;
}

void punch(ENetEvent& event, const std::string_view text) 
{
    const std::string id{ text.substr(sizeof("punch ")-1) };
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    pPeer->punch_effect = (u_char)atoi(id.c_str());
    on::SetClothing(*event.peer);
}