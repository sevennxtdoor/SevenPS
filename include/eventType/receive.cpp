#include "pch.hpp"
#include "action/__action.hpp"
#include "gamePacket/_gamePacket.hpp"
#include "tools/guard.hpp"
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
            if (pipes[0ull] == "protocol" || pipes[0ull] == "tankIDName")
            {
                action = pipes[0ull];
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

