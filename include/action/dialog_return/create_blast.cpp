#include "pch.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "commands/moderation.hpp" // @note is_nuked()

#include "action/quit_to_exit.hpp" // @note peer leave world
#include "action/join_request.hpp" // @note peer enter (blast) world

#include "create_blast.hpp"

void create_blast(ENetEvent& event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const int id = atoi(hPipe["id"].c_str());
    std::string world_name = hPipe["name"];

    for (char &c : world_name) c = std::toupper(c); // @note start -> START

    switch (id)
    {
        case 1402: // @note Thermonuclear Blast
        {
            /* @note never trust the dialog: it can be sent without owning a blast, and the old check only looked at worlds
             * in memory - an existing world nobody was in got loaded, wiped and saved (anyone could erase anyone's world) */
            auto blast = std::ranges::find(pPeer->slots, static_cast<short>(id), &::slot::id);
            if (blast == pPeer->slots.end() || blast->count < 1) return;
            if (world_name.empty() || world_name.length() > 24 || !alnum(world_name))
                return on::ConsoleMessage(event.peer, "`4World names can only have letters and numbers (max 24).``");
            if (is_nuked(world_name) || std::ranges::find(worlds, world_name, &::world::name) != worlds.end() || ::world::exists(world_name))
                return on::ConsoleMessage(event.peer, std::format("`4A world named `w{}`` already exists.`` Pick a unique name.", world_name));

            modify_item_inventory(event, ::slot(static_cast<short>(id), -1));
            action::quit_to_exit(event, "", true);
            {
                ::world world{world_name}; // @note not in worlds: no one is in it yet. the destructor saves it
                blast::thermonuclear(world);
            }
            break;
        }
        default: return;
    }
    action::join_request(event, "", world_name);
}
