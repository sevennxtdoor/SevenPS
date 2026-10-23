#include "pch.hpp"
#include "action/__action.hpp"
#include "gamePacket/_gamePacket.hpp"
#include "tools/guard.hpp"
#include "tools/jammer.hpp" // @todo TEMPORARY debug
#include "commands/weather.hpp" // @todo TEMPORARY debug
#include <fstream>
#include "receive.hpp"

void receive(ENetEvent& event)
{
    std::span<const enet_uint8> data{event.packet->data, event.packet->dataLength};
    const ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    /* @note an empty or 1-4 byte packet used to read/copy outside the packet (one tiny packet crashed the server) */
    if (data.empty() || pPeer == nullptr) { enet_packet_destroy(event.packet); return; }
    if (!guard_packet(*event.peer, data[0ull] == 2 || data[0ull] == 3)) { enet_packet_destroy(event.packet); return; }

    /* @note last line of defence: one handler throwing (std::stoi on "abc", substr past the end...) used to end the whole server */
    try {
    switch (data[0ull])
    {
        case 2: case 3:
        {
            if (data.size() < 5ull) break; // @note 4 byte type + at least the ending 0
            std::string header{data.begin() + 4, data.end() - 1};
            fputs(guard_redact(header).c_str(), stdout); // @note never print passwords to the console

            std::ranges::replace(header, '\n', '|');
            const std::vector<std::string> pipes = readch(header, '|');
            if (pipes.size() < 2) break;

            std::string action{};
            if (pipes[0ull] == "protocol" || pipes[0ull] == "tankIDName" || pipes[0ull] == "requestedName")
            {
                action = (pipes[0ull] == "requestedName") ? "tankIDName" : pipes[0ull];
            }
            else action = std::format("{}|{}", pipes[0ull], pipes[1ull]);

            /* @note before logging in only the login itself is allowed: every other action assumed a loaded player */
            if (!pPeer->loaded && action != "protocol" && action != "tankIDName" && action != "action|quit") break;

            if (const auto i = action_pool.find(action); i != action_pool.end())
                i->second(event, header);
            break;
        }
        case 4:
        {
            if (event.packet->dataLength < sizeof(::gamePacket)) break;

            ::gamePacket gamePacket = make_gamePacket(event.packet->data);
            gamePacket.size = event.packet->dataLength - sizeof(::gamePacket); // @todo did i do this right? or check for flag ::EXTENDED

            if (!pPeer->loaded && gamePacket.type != 0x15 && gamePacket.type != 0x1a) break; // @note ping and disconnect only
            /* @todo TEMPORARY debug (jammer/weather not toggling in game): every packet aimed at one goes to debug_toggle.log. remove after */
            if (gamePacket.type != 0x00 && gamePacket.type != 0x15 && pPeer->netid != 0)
                if (auto w = std::ranges::find(worlds, pPeer->recent_worlds.back(), &::world::name); w != worlds.end() && gamePacket.punch.x >= 0 && gamePacket.punch.y >= 0 && gamePacket.punch.x < 100 &&
                    cord(gamePacket.punch.x, gamePacket.punch.y) < static_cast<int>(w->blocks.size()))
                    if (const ::block &b = w->blocks[cord(gamePacket.punch.x, gamePacket.punch.y)]; is_jammer(b.fg) || is_weather_machine(b.fg))
                        if (std::ofstream log{ "debug_toggle.log", std::ios::app }; log)
                            log << std::format("{} user={} role={} world={} owner={} public={} type={:#x} id={} punch={},{} fg={} state2={:#04x} hits={}\n",
                                std::time(nullptr), pPeer->user_id, (int)pPeer->role, w->name, w->owner, w->is_public, gamePacket.type, gamePacket.id,
                                gamePacket.punch.x, gamePacket.punch.y, b.fg, b.state[2], b.hits[0]);
            if (const auto i = gamePacket_pool.find(gamePacket.type); i != gamePacket_pool.end())
                i->second(event, std::move(gamePacket));
            break;
        }
        default: printf("received unqiue: %d\n", data[0ull]);
    }
    }
    catch (const std::exception &exc)
    {
        std::printf("[receive] handler error (ignored): %s\n", exc.what());
    }
    enet_packet_destroy(event.packet);
}

