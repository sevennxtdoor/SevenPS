#include "pch.hpp"
#include "autosave.hpp"

namespace
{
    constexpr std::time_t AUTOSAVE_SECONDS = 5 * 60;
}

void save_everything()
{
    const auto start = std::chrono::steady_clock::now();
    int players{};
    peers("", PEER_ALL, [&players](ENetPeer &p)
    {
        if (::peer *pPeer = static_cast<::peer*>(p.data); pPeer != nullptr && pPeer->loaded)
        {
            pPeer->save();
            ++players;
        }
    });
    for (::world &world : worlds) world.save();

    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    std::printf("[autosave] %d player(s), %zu world(s) saved in %lld ms\n", players, worlds.size(), static_cast<long long>(ms));
}

void autosave_tick()
{
    static std::time_t last = std::time(nullptr);
    const std::time_t now = std::time(nullptr);
    if (now - last < AUTOSAVE_SECONDS) return;
    last = now;
    save_everything();
}
