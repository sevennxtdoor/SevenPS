#include "pch.hpp"
#include "tools/guild.hpp"
#include "onVariant/EmoticonDataChanged.hpp"
#include "onVariant/Spawn.hpp"
#include "onVariant/BillboardChange.hpp"
#include "onVariant/SetClothing.hpp"
#include "onVariant/CountryState.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "commands/weather.hpp"
#include "commands/moderation.hpp"
#include "tools/worldban.hpp"
#include "tools/time.hpp"
#include "tools/jammer.hpp"
#include "commands/gtos.hpp"
#include "tools/area_lock.hpp"
#include "tools/magplant.hpp"
#include "tools/fishing.hpp"
#include "commands/titles.hpp"
#include "action/quit_to_exit.hpp"
#include "tools/auth.hpp"
#include "tools/system_worlds.hpp"

#include "join_request.hpp"

void action::join_request(ENetEvent& event, const std::string& header, const std::string_view world_name = "") 
{
    try 
    {
        ::peer *pPeer = static_cast<::peer*>(event.peer->data);
        ::hPipe hPipe{ header };
        /* @note a modded client can ask to join while still in a world: that world was never left (it stayed loaded with a
         * visitor that isn't there, and the player showed up in both). leave it first, like the real client does */
        if (pPeer->netid != 0) action::quit_to_exit(event, "", true);

        std::string name =  hPipe["name"];
        if (name.empty() && !world_name.empty()) name = world_name;

        if (name.length() > 24) throw std::runtime_error(""); // @note impossible unless using a proxy since client caps at 24
        if (!alnum(name)) throw std::runtime_error("Sorry, spaces and special characters are not allowed in world or door names.  Try again.");

        for (char &c : name) c = std::toupper(c); // @note start -> START
        if (pPeer->role < MODERATOR && cursed_left(*pPeer) > 0 && name != "HELL") // @note /curse: stuck in HELL until it ends
        {
            on::ConsoleMessage(event.peer, std::format("`4You are cursed for {} more minutes and can't leave HELL.``", (cursed_left(*pPeer) + 59) / 60));
            name = "HELL";
        }
        if (pPeer->role < MODERATOR && is_nuked(name)) throw std::runtime_error("That world is inaccessible.");
        if (pPeer->role < MODERATOR)
            if (const int left = world_ban_left(name, pPeer->user_id); left > 0)
                throw std::runtime_error(std::format("`4Oh no! ``You've been banned from that world by its owner! Try again in `w{}`` minute{}.", left, (left == 1) ? "" : "s"));

        auto it = std::ranges::find(worlds, name, &::world::name);
        if (it == worlds.end()) 
            it = worlds.emplace(worlds.end(), name);
            
        ::world &world = *it;
        {
            ::blob blob = compress_state(::gamePacket{ .type = 0x04, /*PACKET_SEND_MAP_DATA*/ .state = state::S_EXTENDED });
            blob.push_back(world.serialize());

            ENetPacket *packet = enet_packet_create(blob.data().data(), blob.size(), ENET_PACKET_FLAG_RELIABLE);
            if (enet_peer_send(event.peer, 0, packet)) enet_packet_destroy(packet);
        } // @note delete blob
        {
            std::string *this_world = std::ranges::find(pPeer->recent_worlds, world.name);
            std::string *end = pPeer->recent_worlds.end();
            std::string *first = this_world != end ? this_world : pPeer->recent_worlds.begin();

            std::rotate(first, first + 1, end);
            pPeer->recent_worlds.back() = world.name;
        } // @note delete name, first
        on::EmoticonDataChanged(event);

        /* @note always rebuilt here: before, only owners/admins got a new name, everyone else kept the last world's one */
        pPeer->display_growid = pPeer->name_as(
            (pPeer->user_id == world.owner) ? "`2" :
            (std::ranges::find(world.access, pPeer->user_id) != world.access.end()) ? "`c" : "`w");

        pPeer->rest_pos = world.spawn;

        pPeer->netid = ++world.netid_counter;
        peers(pPeer->recent_worlds.back(), PEER_SAME_WORLD, [&event, &pPeer, &world](ENetPeer& peer/*send to everyone in world*/) 
        {
            ::peer *pOthers = static_cast<::peer*>(peer.data); // @note everyone in world's peer.data
            
            if (pOthers->user_id != pPeer->user_id)
            {
                on::Spawn(*event.peer, pOthers->netid, pOthers->user_id, pOthers->pos, pOthers->display_growid, pOthers->country, pOthers->role, pOthers->role >= DEVELOPER, false);
                send_varlist(event.peer, { "OnCountryState", on::country_state(*pOthers) }, pOthers->netid); // @note their blue name / Dr.
                send_varlist(event.peer, { "OnNameChanged", pOthers->display_growid }, pOthers->netid); // @note their title ("of Legend"...), like a title change does
                guild_show(*event.peer, *pOthers); // @note their guild flag
                on::Spawn(peer, pPeer->netid, pPeer->user_id, pPeer->rest_pos, pPeer->display_growid, pPeer->country, pPeer->role, pPeer->role >= DEVELOPER, false);
                on::SetClothing(peer);
                on::ConsoleMessage(&peer, std::format("`5<{} entered, `w{}`` others here>``", pPeer->display_growid, world.visitors));
            }
            

            if (pOthers->user_id != pPeer->user_id) // @note the reason this is here is cause we need the peer's OnSpawn to happen before OnTalkBubble
            {
                send_varlist(&peer, {
                    "OnTalkBubble",
                    pPeer->netid,
                    std::format("`5<{} entered, `w{}`` others here>``", pPeer->display_growid, world.visitors),
                    1u
                });
            }
        });
        on::Spawn(*event.peer, pPeer->netid, pPeer->user_id, pPeer->rest_pos, pPeer->display_growid, pPeer->country, pPeer->role, pPeer->role >= DEVELOPER, true);

        if (pPeer->billboard.id != 0) on::BillboardChange(event); // @note don't waste memory if billboard is empty.

        send_varlist(event.peer, {
            "OnSetPos", 
            CL_Vec2f{pPeer->rest_pos.x, pPeer->rest_pos.y}
        }, pPeer->netid);

        on::ConsoleMessage(event.peer, 
            std::format(
                "World `w{}``{} entered.  There are `w{}`` other people here, `w{}`` online.", 
                world.name, jammer_tags(world), world.visitors, peers().size()
            )
        );
        if (world.owner && !is_system_world(world.name)) on::ConsoleMessage(event.peer, world_locked_text(world.name, world.owner)); // @note like the real game, on entering
        ++world.visitors;
        send_varlist(event.peer, { "OnSetCurrentWeather", current_weather(world) }); // @note else the world looks sunny until a machine is punched
        on::SetClothing(*event.peer);
        refresh_display_name(*event.peer); // @note name + title effects (OnNameChanged & OnCountryState) for everyone here, exactly like
                                           // changing a title: the spawn packet alone lost them after leaving a world
        guild_broadcast(*event.peer); // @note our guild flag for everyone here
        send_area_locks(*event.peer, world); // @note Small/Big/Huge/Builder's Lock areas
        magplant_on_join(event, world); // @note drops the remote from the last world, shows the building machine
        fishing_reset(*static_cast<::peer*>(event.peer->data)); // @note a line left in the last world is gone
    }
    catch (const std::exception& exc)
    {
        send_varlist(event.peer, { "OnFailedToEnterWorld" });
        if (const std::string_view msg{ exc.what() }; !msg.empty()) on::ConsoleMessage(event.peer, std::string{ msg }); // @note tell the player why
        return;
    }
}