#include "pch.hpp"
#include "onVariant/NameChanged.hpp"
#include "onVariant/SetClothing.hpp"
#include "onVariant/Action.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "commands/weather.hpp"
#include "item_activate.hpp"
#include "tools/random.hpp"
#include "tools/time.hpp"
#include "tools/playmods.hpp"
#include "tools/area_lock.hpp"
#include "tools/boxes.hpp"
#include "tools/create_dialog.hpp"
#include "tools/vending.hpp"
#include "tools/surgery.hpp"
#include "tools/profile.hpp"
#include "tools/cps.hpp"
#include "tools/rank.hpp"
#include "tools/system_worlds.hpp"
#include "tools/jammer.hpp"
#include "tools/consumables.hpp"
#include "tools/buffs.hpp"
#include "action/quit_to_exit.hpp"
#include "action/join_request.hpp"
#include "item_activate_object.hpp"

#include "tools/password_door.hpp"
#include "tools/magplant.hpp"
#include "tools/mannequin.hpp"
#include "tools/fishing.hpp"
#include "tools/casino.hpp"
#include "tools/economy.hpp"
#include "tools/special_blocks.hpp"
#include "tools/special_tiles.hpp"
#include "tools/combiner.hpp"
#include "tools/auth.hpp"
#include "tools/anticheat.hpp"
#include "tile_change.hpp"

float max_reach = 4.0f; // @note Growtopia reaches 3 tiles (wiki: Fist), +1 for lag. playmods ('pr+N'/'br+N') add on top

void tile_change(ENetEvent& event, ::gamePacket gamePacket) 
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    try
    {
        auto world = std::ranges::find(worlds, pPeer->recent_worlds.back(), &::world::name);
        if (world == worlds.end()) return;
        if (!in_world(*world, gamePacket.punch)) return; // @note a tile outside the world read/wrote memory past the blocks

        ::block &block = world->blocks[cord(gamePacket.punch.x, gamePacket.punch.y)];

        bool from_remote{}; // @note placing with the Magplant 5000 Remote: the item comes out of the machine, not the backpack
        if (gamePacket.id == MAGPLANT_REMOTE_ID)
        {
            gamePacket.id = magplant_remote_item(event, *world);
            from_remote = true;
        }
        else if (gamePacket.id != 18 && gamePacket.id != 32) // @note never trust the client: you can only place/use what you have
        {
            auto slot = std::ranges::find(pPeer->slots, static_cast<short>(gamePacket.id), &::slot::id);
            if (slot == pPeer->slots.end() || slot->count < 1) return;
        }
        if (fishing_tile_change(event, *world, gamePacket)) return; // @note casting bait / reeling in, before the lock checks: fishing doesn't change the world

        /* @note reach: a hacked client punched/built anywhere in the world */
        if (pPeer->role < MODERATOR && gamePacket.id != 32)
        {
            const ::pos me = pPeer->pos.by_32(true);
            const float reach = max_reach + playmod_range(*pPeer, gamePacket.id == 18); // @note Punch/Build Range items
            if (std::abs(me.x - gamePacket.punch.x) > reach || std::abs(me.y - gamePacket.punch.y) > reach) return;
        }

        const ::item &item = id_to_item((gamePacket.id != 32 && gamePacket.id != 18) ? gamePacket.id : (block.fg != 0) ? block.fg : block.bg);
        if (item.id == 0) return;

        if (block.state[3] & S_FIRE) // @note allow anyone to take out fire
            if (pPeer->clothing[clothing::HAND] == 3066/* fire hose */)
            {
                remove_fire(event, gamePacket, block, *world);
                return; // @note avoid hitting the block
            }

        if (is_system_world(world->name) && pPeer->role < DEVELOPER) // @note START & PARKOUR: only staff can build or break
        {
            if (gamePacket.id == 18) send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, "`wThis world is protected by SevenPS.``", 0u, 1u });
            return;
        }
        if (!(gamePacket.id == 18 && (item.cat & CAT_PUBLIC)) && gamePacket.id != 32) // @note anyone may punch a public block, but not place one (Public Lava in a locked world). wrenching is checked by each dialog
            if ((world->owner && !world->is_public && !pPeer->role) &&
                (pPeer->user_id != world->owner && std::ranges::find(world->access, pPeer->user_id) == world->access.end()))
            {
                /* @note was a silent return: punching or building in someone else's world showed nothing. once a second, a held punch sends many */
                static std::unordered_map<int, std::time_t> last_shown{};
                if (std::time_t now = std::time(nullptr); last_shown[pPeer->user_id] != now)
                {
                    last_shown[pPeer->user_id] = now;
                    send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, world_locked_text(world->name, world->owner), 0u, 1u });
                }
                return;
            }
        if (!(gamePacket.id == 18 && (item.cat & CAT_PUBLIC)) && gamePacket.id != 32) // @note Small/Big/Huge/Builder's Lock: owner & access list only
            if (!area_lock_allows(event, *world, gamePacket.punch, gamePacket.id)) return;

        /* @note MOD/DEV can spawn items, so they only build in their own private world (or START/PARKOUR): blocks or seeds
         * left in a public world would be broken/harvested by players and the spawned items would leak into the economy */
        if (staff_locked(*pPeer) && gamePacket.id != 18 && gamePacket.id != 32 && !is_system_world(world->name) &&
            (world->owner != pPeer->user_id || world->is_public) &&
            !(world->owner == 0 && item.type == type::LOCK && !is_tile_lock(item.id))) // @note but they may World Lock a fresh world
        {
            send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, "`4Staff accounts can only build in their own private (World Locked) world.``", 0u, 1u });
            return;
        }

        bool tile_update{};
        bool lock_visuals{}; // @todo this looks sloppy
        bool area_lock_visuals{};
        
        if (gamePacket.id == 18) // @note punching a block
        {
            static bool punch{}; // @note true if tile_change has been called within this inital (punch)

            if (!punch) // @note put all multiple punch features here
            {
                if (!punch_allowed(*pPeer)) return; // @note fast-break hack. only the real punch counts, not Rayman's extra tiles
                punch = true;
                if (pPeer->clothing[clothing::HAND] == 5480) // @note Rayman's Fist
                {
                    ::gamePacket copy_gamePacket = gamePacket;

                    /* @note up and down */
                    if (gamePacket.punch.y == gamePacket.pos.by_32(true).y)
                    {
                        copy_gamePacket.punch.x += (pPeer->facing_left) ? -1 : 1;
                        tile_change(event, std::move(copy_gamePacket));
                        
                        copy_gamePacket.punch.x += (pPeer->facing_left) ? -1 : 1;
                        tile_change(event, std::move(copy_gamePacket));
                    }
                    /* @note left and right <- -> */
                    else if (gamePacket.punch.x == gamePacket.pos.by_32(true).x)
                    {
                        copy_gamePacket.punch.y += (gamePacket.punch.y < gamePacket.pos.by_32(true).y) ? -1 : 1;
                        tile_change(event, std::move(copy_gamePacket));

                        copy_gamePacket.punch.y += (gamePacket.punch.y < gamePacket.pos.by_32(true).y) ? -1 : 1;
                        tile_change(event, std::move(copy_gamePacket));
                    }
                    /* @note horizontal adjacent \/ */
                    else if (gamePacket.punch.y != gamePacket.pos.by_32(true).y)
                    {
                        copy_gamePacket.punch.x += (pPeer->facing_left) ? -1 : 1;
                        copy_gamePacket.punch.y += (gamePacket.punch.y < gamePacket.pos.by_32(true).y) ? -1 : 1;
                        tile_change(event, std::move(copy_gamePacket));

                        copy_gamePacket.punch.x += (pPeer->facing_left) ? -1 : 1;
                        copy_gamePacket.punch.y += (gamePacket.punch.y < gamePacket.pos.by_32(true).y) ? -1 : 1;
                        tile_change(event, std::move(copy_gamePacket));
                    }
                }
                punch = false;
            }
            u_char apply_damage_value{}; // @note used to change a tile value without using send_tile_update() 

            if (pPeer->clothing[clothing::HAND] == 2952/*Digger's Spade*/)
            {
                if(item.id == 2/*Dirt*/ || item.id == 14)
                {
                    if (block.fg != 0) block.hits[0] = 3;
                    else block.hits[1] = 3;
    
                    int color = (item.id ==  2/*Dirt*/) ? RandomRange(0x02, 0x03)/* @note idk if this is the correct one, at least by looking at the color it looks like dirt*/ : 
                                  (item.id == 14/*Cave Background*/) ? RandomRange(0x0e, 0x0f) : 0x02;

                    send_particle_effect(event, gamePacket.punch.by_32(), {color, 0x61});
                }
            }
            if (special_punch(event, *world, block, gamePacket.punch, item)) return; // @note fossils & prep station (tools/special_blocks.cpp)
            if (special_tile_punch(event, *world, block, gamePacket.punch, item)) return; // @note a harmonized crystal cluster (tools/special_tiles.cpp)
            if (combiner_punch(event, *world, block, gamePacket.punch, item)) return; // @note Laboratory, Microwave...: ingredients on it (tools/combiner.cpp)

            switch (item.id)
            {
                case 4296: // @note Surg-E
                {
                    if (surgery_in_use(*world, gamePacket.punch)) throw std::runtime_error("Someone is operating on it!");
                    break;
                }
                case 758: // @note Roulette Wheel
                {
                    roulette_spin(event); // @note tools/casino.cpp
                    break;
                }
            }
            switch (item.type)
            {
                case type::STRONG:
                {
                    if (pPeer->role < DEVELOPER) throw std::runtime_error("It's too strong to break.");
                    block.hits[0] = 99; // @note developers break bedrock in one punch
                    break;
                }
                case type::MAIN_DOOR: throw std::runtime_error("(stand over and punch to use)");
                case type::STORAGE_BOX: case type::SAFE_VAULT: case type::DONATION_BOX:
                {
                    if (box_in_use(*world, gamePacket.punch)) throw std::runtime_error("Empty the box first!");
                    break;
                }
                case type::VENDING_MACHINE:
                {
                    if (vending_in_use(*world, gamePacket.punch)) throw std::runtime_error("Empty the machine first!");
                    break;
                }
                case type::MANNEQUIN:
                {
                    if (mannequin_punched(event, *world, gamePacket.punch)) return; // @note takes a piece of clothing back instead of damage
                    break;
                }
                case type::MAGPLANT:
                {
                    if (block.hits[0] == 0) magplant_punched(event, *world, gamePacket.punch); // @note first hit switches building mode
                    else if (magplant_in_use(*world, gamePacket.punch)) throw std::runtime_error(std::format("Empty the {} before breaking!", item.raw_name));
                    break;
                }
                case type::LOCK:
                {
                    if (is_tile_lock(item.id)) break; // @todo seperate area for 'range_lock'

                    if (world->owner != pPeer->user_id)
                        throw std::runtime_error(world_locked_text(world->name, world->owner));
                    break;
                }
                /* @note providers: a ready one is harvested by special_tile_punch() (resources/providers.txt), otherwise it takes damage. the old code here dropped prizes on every punch (ATM: up to 100 gems a punch) and providers could never be broken */
                case type::SEED:
                {
                    auto tree = std::ranges::find(world->trees, gamePacket.punch, &::tree::pos);
                    if (tree == world->trees.end()) // @note a seed without its timer (old save, made by a machine...) crashed the server here
                        tree = world->trees.emplace(world->trees.end(), ticks(), RandomRange(1, 3), gamePacket.punch);
                    if (ticks() - tree->tick >= item.tick) // @todo limit this check.
                    {
                        block.hits[0] = 99;
                        magplant_drop(event, ::slot(item.id - 1, RandomRange(1, std::max<int>(tree->fruit, 1) * 3)), gamePacket.punch.by_32(), *world); // @note fruit (from tree)
                        add_stat(event, "harvested");
                    }
                    break;
                }
                case type::WEATHER_MACHINE:
                case type::SFX_WEATHER_MACHINE:    // @note Heatwave, Balloon Warz, Background, Valentine's, St. Paddy's, Digital Rain
                case type::SPRITE_WEATHER_MACHINE: // @note Stuff
                {
                    if (get_weather_id(item.id) == 0 && item.id != 932/*Sunny*/)
                    {
                        send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, "This weather machine isn't supported yet." });
                        break; // @note still takes damage so it can be broken
                    }
                    block.state[2] ^= S_TOGGLE; // @note punching again turns it off
                    const bool on = block.state[2] & S_TOGGLE;

                    if (on) // @note only one machine runs at a time, turn the others off (else the wrong one wins when entering the world)
                    {
                        const int width = world->blocks.size() / 60;
                        for (int i = 0; i < (int)world->blocks.size(); ++i)
                        {
                            ::block &other = world->blocks[i];
                            if (&other == &block || !(other.state[2] & S_TOGGLE) || other.fg == 0 || !is_weather_machine(other.fg)) continue;
                            other.state[2] &= ~S_TOGGLE;
                            ::gamePacket update{};
                            update.punch = ::pos{ i % width, i / width };
                            send_tile_update(event, update, other, *world);
                        }
                    }
                    world->weather = gamePacket.punch;
                    clear_weather_override(world->name); // @note the machine wins over /weather

                    const int weather_id = on ? get_weather_id(item.id) : 0;
                    peers(pPeer->recent_worlds.back(), PEER_SAME_WORLD, [weather_id](ENetPeer& p)
                    {
                        send_varlist(&p, { "OnSetCurrentWeather", weather_id });
                    });
                    break;
                }
                case type::TOGGLEABLE_BLOCK:
                case type::TOGGLEABLE_ANIMATED_BLOCK:
                case type::CHEST:
                case type::CHEMICAL_COMBINER: // @note opened/closed by a punch
                {
                    block.state[2] ^= S_TOGGLE;
                    if (is_jammer(block.fg)) jammer_toggled(event, *world, block, gamePacket.punch);
                    break;
                }
                case type::RANDOM:
                {
                    apply_damage_value = 
                        (item.id == 456/*Dice*/) ? RandomRange(0, 6) : // @note RandomRange(a, b) never returns b: 6 faces
                        (item.id == 1300/*Roshambo*/) ? RandomRange(1, 4) : 0;

                    auto random = std::ranges::find(world->random_blocks, gamePacket.punch, &::random_block::pos);
                    if (random == world->random_blocks.end())
                    {
                        world->random_blocks.emplace_back(::random_block{apply_damage_value, gamePacket.punch});
                    }
                    else random->value = apply_damage_value;
                    break;
                }
            }
            tile_apply_damage(event, std::move(gamePacket), block, apply_damage_value);

            /* @note Enhanced Digging (Pickaxe, Cybernetic Arm...): one hit less, never below 1 */
            const u_char hits = (item.hits > 1 && has_playmod(*pPeer, PM_DIG)) ? item.hits - 1 : item.hits;
            if (block.hits[0] >= hits) block.fg = 0, block.hits[0] = 0;
            else if (block.hits[1] >= hits) block.bg = 0, block.hits[1] = 0;
            else return;
            
            /* @todo update these changes with tile_update() */
            block.state[2] = 0x00; // @note reset tile direction
            add_stat(event, "broken");
            if (box_type(item) != box_kind::NONE) box_removed(*world, gamePacket.punch); // @note letters on a broken mailbox are gone
            if (item.type == type::MAGPLANT) magplant_removed(event, *world, gamePacket.punch);
            if (item.type == type::MANNEQUIN) mannequin_removed(*world, gamePacket.punch);
            block.state[3] &= ~S_VANISH; // @note remove paint
            const bool own_drops = special_break(event, *world, block, gamePacket.punch, item, tile_update) || // @note loot blocks, fossils, Mystery Pinata
                                   special_tile_break(event, *world, gamePacket.punch, item); // @note Bunny Egg prize, crystal cluster gone
            
            if (item.id == 392/*Heartstone*/ || item.id == 3402/*GBC*/ || item.id == 9350/*Super GBC*/)
            {
                short reward =
                    (!RandomRange(0, 99)) ? 1458 : // @note GHC
                    (!RandomRange(0, 20)) ? 362 : // @note Angel Wings
                    (!RandomRange(0, 8))  ? 366 : // @note Heartbow
                    (!RandomRange(0, 8))  ? 1470 : // @note Ruby Necklace
                    (!RandomRange(0, 20)) ? 2384 : // @note Love Bug
                    (!RandomRange(0, 4))  ? 2396 : // @note Valensign
                    (!RandomRange(0, 10)) ? 3388 : // @note Heartbreaker Hammer
                    (!RandomRange(0, 10)) ? 2390 : // @note Teeny Angel Wings
                    (!RandomRange(0, 10)) ? 3396 : // @note Lovebird Pendant
                    (!RandomRange(0, 2))  ? 3404 : // @note Sour Lollipop
                    (!RandomRange(0, 4))  ? 3406 : // @note Sweet Lollipop
                    (!RandomRange(0, 2))  ? 3408 : // @note Pink Marble Arch
                    388; // @note Perfume
                    // @todo add all the remaining drops - https://growtopia.fandom.com/wiki/Golden_Booty_Chest

                magplant_drop(event, ::slot(reward, (reward == 3408 || reward == 3404) ? 10 : 1), gamePacket.punch.by_32(), *world);
                if (reward == 1458)
                {
                    std::string message = std::format("msg|`4The Power of Love! `2{} found a `#Golden Heart Crystal`2 in a `#{}`2!", pPeer->growid, item.raw_name);
                    peers(pPeer->recent_worlds.back(), PEER_ALL, [message](ENetPeer &p)
                    {
                        send_action(p, "log", message.c_str());
                    });
                }
                if (++pPeer->gbc_pity % 100 == 0) modify_item_inventory(event, ::slot{9350, 1});
            }
            else if (is_tile_lock(item.id)) area_lock_removed(*world, gamePacket.punch);
            else if (item.type == type::LOCK && !is_tile_lock(item.id))
            {
                pPeer->display_growid = pPeer->name_as("`w");
                on::NameChanged(event);
                
                world->owner = 0; // @todo have a seperate thing for 'range_lock'
                world->access.fill(0), world->minimum_entry_level = 1; // @note the next owner starts with a clean lock
                if (auto mine = std::ranges::find(pPeer->my_worlds, world->name); mine != pPeer->my_worlds.end()) mine->clear(); // @note unlocked: not in "My Worlds" anymore
            }

            if (own_drops) { } // @note special_break() dropped its own prizes
            else if (item.cat == CAT_RETURN)
            {
                int uid = add_object(event, ::slot(item.id, 1), gamePacket.pos, *world);
                item_activate_object(event, ::gamePacket{.id = uid, .punch = gamePacket.punch});
            }
            else if (u_char(item.property) & 04) { } // @note "This item never drops any seeds."; should it drop a block?
            else // @note normal break (drop gem, seed, block & give XP)
            {
                if (item.type != type::SEED)
                { /* gem drop */
                    /* if greater than 1, assume it's a farmable.*/
                    u_char rarity_to_gem =
                        (item.rarity >= 87) ? 22 : 
                        (item.rarity >= 68) ? 18 : 
                        (item.rarity >= 53) ? 14 : 
                        (item.rarity >= 41) ? 11 : 
                        (item.rarity >= 36) ? 10 :
                        (item.rarity >= 32) ? 9 :
                        (item.rarity >= 24) ? 5 : 1;

                    if (!RandomRange(0, (rarity_to_gem > 1) ? 1 : 4)) // @note double chances if farmable.
                    {
                        /* @todo merge gems more effectively */
                        int gems = gem_bonus(*pPeer, RandomRange(1, rarity_to_gem)) * (event_on(event_type::GEMS) ? 2 : 1); // @note rank bonus, Gems x2 event
                        if (buff_lucky(*pPeer) && !RandomRange(0, 10)) gems *= RandomRange(2, 6); // @note Lucky! (Four Leaf Clover...): 10% chance of 2-5x
                        for (short i : {10, 5, 1}/* gem type */)
                            for (; gems >= i; gems -= i/* downgrade type */)
                                magplant_drop(event, {112, i}, gamePacket.punch.by_32(), *world);
                    }
                    if (!RandomRange(0, (rarity_to_gem > 1) ? 2 : 4)) magplant_drop(event, ::slot(item.id + 1, 1), gamePacket.punch.by_32(), *world); 
                    else if (!RandomRange(0, (rarity_to_gem > 1) ? 4 : 8)) magplant_drop(event, ::slot(item.id, 1), gamePacket.punch.by_32(), *world);
                    if (event_on(event_type::LUCKY) && !RandomRange(0, 2)) magplant_drop(event, ::slot(item.id + 1, 1), gamePacket.punch.by_32(), *world); // @note Lucky Drops event
                } /* ~gem drop */

                pPeer->add_xp(event, std::trunc(1.0f + item.rarity / 5.0f));
            }
        } // @note delete im, id
        else if (item.cloth_type != clothing::NONE) 
        {
            if (id_to_item(block.fg).type == type::MANNEQUIN) // @note dressing a mannequin, not yourself
            {
                if (!from_remote) mannequin_use_item(event, *world, gamePacket.punch, gamePacket.id);
                return;
            }
            if (gamePacket.punch != pPeer->pos.by_32(true)) throw std::runtime_error("To wear clothing, use on yourself");

            item_activate(event, gamePacket);
            return; 
        }
        else if (item.type == type::CONSUMEABLE) 
        {
            if (item.raw_name.find(" Blast") != std::string::npos)
            {
                send_varlist(event.peer, {
                    "OnDialogRequest",
                    std::format(
                        "set_default_color|`o\n"
                        "embed_data|id|{0}\n"
                        "add_label_with_icon|big|`w{1}``|left|{0}|\n"
                        "add_label|small|This item creates a new world! Enter a unique name for it.|left\n"
                        "add_text_input|name|New World Name||24|\n"
                        "end_dialog|create_blast|Cancel|Create!|\n", // @todo rgt "Create!" is a purple-ish pink color
                        item.id, item.raw_name
                    )
                });
            }

            if (item.raw_name.find("Paint Bucket - ") != std::string::npos && pPeer->clothing[clothing::HAND] != 3494) throw std::runtime_error("you need a Paintbrush to apply paint!");
            if (item.raw_name.find("Hair Dye") != std::string::npos)
            {
                if (gamePacket.punch != pPeer->pos.by_32(true)) throw std::runtime_error("Don't spill your dye!");
                else if (world->blocks[cord(pPeer->pos.by_32(true).x, pPeer->pos.by_32(true).y)].fg != 230/*Bathtub*/) throw std::runtime_error("You'll make a huge mess if you do that outside the Bathtub!");

                on::Action(event, "shower");
                // audio/shower.wav
                send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, "You dyed your hair!", 0u, 1u });
            }
            float color{}; // @note the color of the particle effect.
            float particle{};
            switch (item.id)
            {
                case 1404: // @note Door Mover
                {
                    if (!door_mover(*world, gamePacket.punch)) throw std::runtime_error("There's no room to put the door there! You need 2 empty spaces vertically.");

                    std::string remember_name = world->name;
                    peers(pPeer->recent_worlds.back(), PEER_SAME_WORLD, [&](ENetPeer& p) 
                    { 
                        ENetEvent fake{.peer = &p};
                        action::quit_to_exit(fake, "", true); // @note everyone in world exits
                        action::join_request(fake, "", remember_name); // @note everyone in world re-joins
                    });
                    return;
                }
                case 822: // @note Water Bucket
                {
                    if (block.state[3] & S_FIRE) remove_fire(event, gamePacket, block, *world);
                    else block.state[3] ^= S_WATER;
                    break;
                }
                case 1866: // @note Block Glue
                {
                    block.state[3] ^= S_GLUE;
                    break;
                }
                case 3062: // @note Pocket Lighter
                {
                    if (block.fg == 0 && block.bg == 0) throw std::runtime_error("There's nothing to burn!");
                    if (block.state[3] & (S_FIRE | S_WATER)) return; // @note avoid fire on water & fire on fire

                    block.state[3] |= S_FIRE;

                    std::string message = "`7[```4MWAHAHAHA!! FIRE FIRE FIRE```7]``";
                    peers(pPeer->recent_worlds.back(), PEER_SAME_WORLD, [&](ENetPeer& p) 
                    {
                        send_varlist(&p, { "OnTalkBubble", pPeer->netid, message, 0u }); // @note to everyone (was sent to the lighter's owner once per player)
                        on::ConsoleMessage(&p, message);
                    });
                    particle = 0x96;

                    if (block.fg == 3090) // @note Highly Combustible Box
                    {
                        block.fg = 3128; // @note Combusted Box
                        if (!(block.state[2] & S_TOGGLE)/*closed*/) {} // @todo recipes: https://growtopia.fandom.com/wiki/Guide:Highly_Combustible_Box
                    }
                    break;
                }
                case 3404:/*Sour Lollipop*/ case 3406:/*Sweet Lollipop*/
                {
                    send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, "`#YUM!:D", 0u });

                    break;
                }
                case 3400: // @note Love Potion #8
                {
                    if (block.fg != 10) return; // @note Rock

                    block.fg = 392; // @note Heartstone
                    particle = 0x2c;
                    break;
                }
                case 1488: // @note Experience Potion
                {
                    send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, "`#GULP! You got smarter!", 0u });
                    pPeer->add_xp(event, 10000);
                    break;
                }
                case 834: // @note Fireworks
                {
                    fireworks(event, gamePacket.punch.by_32());
                    break;
                }
                case 2480: // @note Megaphone
                {
                    send_varlist(event.peer, {
                        "OnDialogRequest",
                        ::create_dialog()
                            .set_default_color("`o")
                            .add_label_with_icon("big", "`wMegaphone``", item.id)
                            .add_textbox("Enter a message you want to broadcast to every player in Growtopia! This will use up 1 Megaphone")
                            .add_text_input("message", "", "", 128)
                            .end_dialog("megaphone", "Nevermind", "Broadcast")
                    });
                    return; // @note used up on "Broadcast" (dialog_return/megaphone.cpp), not here: "Nevermind" used to lose it too
                }
                case 408: // @note Duct Tape
                {
                    peers(pPeer->recent_worlds.back(), PEER_SAME_WORLD, [&](ENetPeer& p) 
                    {
                        ::peer *_p = static_cast<::peer*>(p.data);

                        if (gamePacket.punch == _p->pos.by_32(true))
                        {
                            _p->state |= S_DUCT_TAPE; // @todo add a 10 minute timer that will remove it.
                            on::SetClothing(p);
                        }
                    });
                    break;
                }
                case 3478: // @note Paint Bucket - Red
                {
                    block.state[3] |= S_RED;
                    color = bgra::RED, particle = 0xa8; 
                    break;
                }
                case 3480: // @note Paint Bucket - Yellow
                {
                    block.state[3] |= S_YELLOW;
                    color = bgra::GREEN | bgra::RED, particle = 0xa8;
                    break;
                }
                case 3482: // @note Paint Bucket - Green
                {
                    block.state[3] |= S_GREEN;
                    color = bgra::GREEN, particle = 0xa8;
                    break;
                }
                case 3484: // @note Paint Bucket - Aqua
                {
                    block.state[3] |= S_AQUA;
                    color = bgra::BLUE | bgra::GREEN, particle = 0xa8;
                    break;
                }
                case 3486: // @note Paint Bucket - Blue
                {
                    block.state[3] |= S_BLUE;
                    color = bgra::BLUE, particle = 0xa8;
                    break;
                }
                case 3488: // @note Paint Bucket - Purple
                {
                    block.state[3] |= S_PURPLE;
                    color = bgra::BLUE | bgra::RED, particle = 0xa8; // @note blue + red
                    break;
                }
                case 3490: // @note Paint Bucket - Charcoal
                {
                    block.state[3] |= S_CHARCOAL;
                    color = bgra::BLUE | bgra::GREEN | bgra::RED | bgra::ALPHA, particle = 0xa8; // @note max will provide a pure black color. idk if growtopia is the same.
                    break;
                }
                case 3492: // @note Paint Bucket - Vanish
                {
                    block.state[3] &= ~S_VANISH;
                    color = bgra::BLUE | bgra::GREEN | bgra::RED, particle = 0xa8; // @todo get exact color. I just guessed T-T
                }
                case 3822: break; // Red Hair Dye
                case 3824: break; // Green Hair Dye
                case 3826: break; // Blue Hair Dye
                default: use_consumable(event, *world, block, gamePacket.punch, item); return; // @note takes the item itself, only if something happened
            }
            if (particle > 0.0f)
            {
                send_particle_effect(event, gamePacket.punch.by_32(), {color, particle});
            }
            send_tile_update(event, std::move(gamePacket), block, *world);

            modify_item_inventory(event, ::slot(item.id, -1));
            pPeer->add_xp(event, 1);
            return;
        }
        else if (gamePacket.id == 32)
        {
            /* @note visitors may wrench (vending, boxes, mailbox, locks show their own dialog), but only builders get edit dialogs */
            const bool builder = can_build(*pPeer, *world, gamePacket.punch);
            if (box_type(item) != box_kind::NONE) return box_dialog(event, *world, gamePacket.punch);
            if (item.id == 4296 && !builder) return;
            if (item.id == 4296) // @note Surg-E
            {
                send_surgery_dialog(event, *world, gamePacket.punch);
                return;
            }
            if (item.id == 6016) return growscan_dialog(event, *world); // @note GrowScan 9000
            if (special_tile_wrench(event, *world, gamePacket.punch, item)) return; // @note crystal cluster, Bunny Egg
            switch (item.type)
            {
                case type::LOCK:
                {
                    if (is_tile_lock(item.id)) return area_lock_dialog(event, *world, gamePacket.punch);

                    if (pPeer->user_id == world->owner)
                    {
                        std::string access_list{};
                        for (int uid : world->access)
                            if (uid > 0) access_list += std::format("add_checkbox|access_{}|{}|1\n", uid, growid_of(uid));
                        if (access_list.empty()) access_list = "add_label|small|Currently, you're the only one with access.``|left\n";
                        send_varlist(event.peer, {
                            "OnDialogRequest",
                            std::format(
                                "set_default_color|`o\n"
                                "add_label_with_icon|big|`wEdit {}``|left|{}|\n"
                                "add_popup_name|LockEdit|\n"
                                "add_label|small|`wAccess list:``|left\n"
                                "embed_data|tilex|{}\n"
                                "embed_data|tiley|{}\n"
                                "add_spacer|small|\n"
                                "{}"
                                "add_spacer|small|\n"
                                "add_player_picker|playerNetID|`wAdd``|\n"
                                "add_checkbox|checkbox_public|Allow anyone to Build and Break|{}\n"
                                "add_checkbox|checkbox_disable_music|Disable Custom Music Blocks|{}\n"
                                "add_text_input|tempo|Music BPM|100|3|\n"
                                "add_checkbox|checkbox_disable_music_render|Make Custom Music Blocks invisible|0\n"
                                //"add_smalltext|Your current home world is: JOLEIT|left|\n"           // @todo only show when peer has a set home world.
                                "add_checkbox|checkbox_set_as_home_world|Set as Home World|0|\n"
                                "add_text_input|minimum_entry_level|World Level: |{}|3|\n"
                                "add_smalltext|Set minimum world entry level.|\n"
                                "add_button|sessionlength_dialog|`wSet World Timer``|noflags|0|0|\n"
                                "add_button|changecat|`wCategory: None``|noflags|0|0|\n"
                                "add_button|getKey|Get World Key|noflags|0|0|\n"
                                "end_dialog|lock_edit|Cancel|OK|\n",
                                item.raw_name, item.id, gamePacket.punch.x, gamePacket.punch.y, access_list, to_char(world->is_public), (world->lock_state & DISABLE_MUSIC) ? "1" : "0", world->minimum_entry_level
                            )
                        });
                    }
                    break;
                }
                case type::DOOR:
                case type::PORTAL:
                {
                    if (!builder) break;
                    std::string label, dest, id, password{};
                    for (::door& door : world->doors)
                        if (door.pos == gamePacket.punch) { label = door.label, dest = door.dest, id = door.id, password = door.password; break; }
                    const std::string password_input = (item.id == PASSWORD_DOOR) ? std::format( // @note only builders get this dialog, so only they see the password
                        "add_text_input|door_password|Password|{}|23|\n"
                        "add_smalltext|Anyone going through this door must type the `2Password`` first. Letters and numbers only, blank = no password.|left|\n", password) : "";
                        
                    send_varlist(event.peer, {
                        "OnDialogRequest",
                        std::format(
                            "set_default_color|`o\n"
                            "add_label_with_icon|big|`wEdit {}``|left|{}|\n"
                            "add_text_input|door_name|Label|{}|100|\n"
                            "add_popup_name|DoorEdit|\n"
                            "add_text_input|door_target|Destination|{}|24|\n"
                            "add_smalltext|Enter a Destination in this format: `2WORLDNAME:ID``|left|\n"
                            "add_smalltext|Leave `2WORLDNAME`` blank (:ID) to go to the door with `2ID`` in the `2Current World``.|left|\n"
                            "add_text_input|door_id|ID|{}|11|\n"
                            "add_smalltext|Set a unique `2ID`` to target this door as a Destination from another!|left|\n"
                            "{}"
                            "add_checkbox|checkbox_locked|Is open to public|1\n"
                            "embed_data|tilex|{}\n"
                            "embed_data|tiley|{}\n"
                            "end_dialog|door_edit|Cancel|OK|", 
                            item.raw_name, item.id, label, dest, id, password_input, gamePacket.punch.x, gamePacket.punch.y
                        )
                    });
                    break;
                }
                case type::SIGN:
                {
                    if (!builder) break;
                    std::string label{};
                    for (::sign& sign : world->signs)
                        if (sign.pos == gamePacket.punch) { label = sign.label; break; }

                    send_varlist(event.peer, {
                        "OnDialogRequest",
                        std::format(
                            "set_default_color|`o\n"
                            "add_popup_name|SignEdit|\n"
                            "add_label_with_icon|big|`wEdit {}``|left|{}|\n"
                            "add_textbox|What would you like to write on this sign?``|left|\n"
                            "add_text_input|sign_text||{}|128|\n"
                            "embed_data|tilex|{}\n"
                            "embed_data|tiley|{}\n"
                            "end_dialog|sign_edit|Cancel|OK|", 
                            item.raw_name, item.id, label, gamePacket.punch.x, gamePacket.punch.y
                        )
                    });
                    break;
                }
                case type::ENTRANCE:
                {
                    if (!builder) break;
                    send_varlist(event.peer, {
                        "OnDialogRequest",
                        std::format(
                            "set_default_color|`o\n"
                            "add_label_with_icon|big|`wEdit {}``|left|{}|\n"
                            "add_checkbox|checkbox_public|Is open to public|{}\n"
                            "embed_data|tilex|{}\n"
                            "embed_data|tiley|{}\n"
                            "end_dialog|gateway_edit|Cancel|OK|\n", 
                            item.raw_name, item.id, to_char((block.state[2] & S_PUBLIC)), gamePacket.punch.x, gamePacket.punch.y
                        )
                    });
                    break;
                }
                case type::DISPLAY_BLOCK:
                {
                    // @todo
                    break;
                }
                case type::VENDING_MACHINE:
                {
                    send_vending_dialog(event, *world, gamePacket.punch);
                    break;
                }
                case type::MAGPLANT:
                {
                    magplant_dialog(event, *world, gamePacket.punch); // @note checks owner/access itself
                    break;
                }
                case type::MANNEQUIN:
                {
                    mannequin_dialog(event, *world, gamePacket.punch); // @note checks owner/access itself
                    break;
                }
            }
            return; // @note leave early else wrench will act as a block unlike fist which breaks. this is cause of state_visuals()
        }
        else // @note placing a block
        {
            if (special_place(event, *world, block, gamePacket.punch, item, from_remote)) return; // @note crystals on a cluster, Magic Egg (tools/special_tiles.cpp)
            if (block.fg != 0) // @note placing something ontop of exisitng block
            {
                bool update_tile{};
                switch (items[world->blocks[cord(gamePacket.punch.x, gamePacket.punch.y)].fg].type)
                {
                    case type::MANNEQUIN: // @note dressing it: the dialog asks first, the item leaves the backpack on "Yes"
                    {
                        if (from_remote) return;
                        mannequin_use_item(event, *world, gamePacket.punch, gamePacket.id);
                        return;
                    }
                    case type::DISPLAY_BLOCK:
                    {
                        world->displays.emplace_back(::display(item.id, gamePacket.punch));
                        update_tile = true;
                        break;
                    }
                    case type::SEED:
                    {
                        auto tree = std::ranges::find(world->trees, gamePacket.punch, &::tree::pos);
                        if (tree == world->trees.end()) // @note a seed without its timer crashed the server when spliced
                            tree = world->trees.emplace(world->trees.end(), ticks(), RandomRange(1, 3), gamePacket.punch);
                        if (block.state[2] == S_SPLICED) throw std::runtime_error("It would be too dangerous to try to mix three seeds.");
                        for (::item &item : items)
                        {
                            if ((item.splice[0] == gamePacket.id && item.splice[1] == block.fg) ||
                                (item.splice[1] == gamePacket.id && item.splice[0] == block.fg) /* allow reverse splice combo */)
                            {
                                const ::item &splice0 = id_to_item(item.splice[0]);
                                const ::item &splice1 = id_to_item(item.splice[1]);

                                send_varlist(event.peer, {
                                    "OnTalkBubble", 
                                    pPeer->netid, 
                                    std::format("`w{}`` and `w{}`` have been spliced to make a `${} Tree``!", 
                                        splice0.raw_name, splice1.raw_name, item.raw_name.substr(0, item.raw_name.length()-5/* seed*/)), // @todo this is hardcoded
                                    0u, 1u
                                });
                                tree->tick = ticks();
                                block.state[2] = S_SPLICED; // @todo handle for multiple flags

                                block.fg = item.id;
                                update_tile = true;
                                break;
                            }
                        }
                        if (block.state[2] != S_SPLICED) throw std::runtime_error(std::format("Hmm, it looks like `w{}`` and `w{}`` can't be spliced.", id_to_item(block.fg).raw_name, id_to_item(gamePacket.id).raw_name));
                        break;
                    }
                }
                if (update_tile)
                {
                    if (from_remote) magplant_remote_used(event, *world);
                    else modify_item_inventory(event, ::slot(gamePacket.id, -1));
                    send_tile_update(event, std::move(gamePacket), block, *world);
                    return;
                }
            }
            if ((item.type == type::BACKGROUND && block.bg != 0) ||
                (item.type != type::BACKGROUND && block.fg != 0)) return; // @note an extra check, i will later make this cleaner.
            if (item.collision == collision::FULL)
            {
                if (gamePacket.punch == gamePacket.pos.by_32(true)) return; // @todo when moving avoid collision.
            }
            switch (item.type)
            {
                case type::DOOR:
                {
                    std::erase_if(world->doors, [&](const ::door &door) { return door.pos == gamePacket.punch; }); // @note the broken door's label/destination/password stayed behind
                    world->doors.emplace_back("","","", gamePacket.punch);
                    tile_update = true;
                    break;
                }
                case type::SIGN:
                {
                    world->signs.emplace_back("", gamePacket.punch);
                    tile_update = true;
                    break;
                }
                case type::MAGPLANT:
                {
                    magplant_placed(*world, gamePacket.punch);
                    tile_update = true;
                    break;
                }
                case type::MANNEQUIN:
                {
                    mannequin_placed(*world, gamePacket.punch);
                    tile_update = true;
                    break;
                }
                case type::LOCK:
                {
                    if (is_tile_lock(item.id))
                    {
                        area_lock_place(event, *world, item.id, gamePacket.punch);
                        area_lock_visuals = true;
                        break;
                    }
                    if (std::string other = area_lock_other_owner(*world, pPeer->user_id); !other.empty())
                        throw std::runtime_error(std::format("`wThat area is owned by {}``", other));

                    if (!world->owner)
                    {
                        world->owner = pPeer->user_id;
                        lock_visuals = true;

                        pPeer->display_growid = pPeer->name_as("`2");
                        on::NameChanged(event);
                        if (std::ranges::find(pPeer->my_worlds, world->name) == pPeer->my_worlds.end()) 
                        {
                            std::ranges::rotate(pPeer->my_worlds, pPeer->my_worlds.begin() + 1);
                            pPeer->my_worlds.back() = world->name;
                        }
                        std::string placed_message = std::format("`5[```w{}`` has been `$World Locked`` by {}`5]``", world->name, owner_display(pPeer->user_id)); // @note staff: @Name in role color
                        peers(pPeer->recent_worlds.back(), PEER_SAME_WORLD, [&event, &pPeer, placed_message](ENetPeer& peer) 
                        {
                            send_varlist(&peer, { "OnTalkBubble", pPeer->netid, placed_message });
                            on::ConsoleMessage(&peer, placed_message);
                        });
                        add_stat(event, "worlds_locked"); // @note tutorial step 1
                    }
                    else throw std::runtime_error("Only one `$World Lock`` can be placed in a world, you'd have to remove the other one first.");
                    break;
                }
                case type::SEED:
                {
                    world->trees.emplace_back(ticks(), RandomRange(1, 3), gamePacket.punch);
                    block.state[2] = 0x11; // @todo
                    tile_update = true;
                    add_stat(event, "planted"); // @note tutorial
                    break;
                }
                case type::ENTRANCE:
                {
                    block.state[2] |= S_PUBLIC;
                    tile_update = true;
                    break;
                }
                case type::PROVIDER:
                {
                    //block.tick = ticks();
                    break;
                }
            }
            block.state[2] |= (pPeer->facing_left) ? S_LEFT : S_RIGHT;
            (item.type == type::BACKGROUND) ? block.bg = gamePacket.id : block.fg = gamePacket.id;
            if (special_placed(*world, gamePacket.punch, item)) tile_update = true; // @note crystal cluster / provider timer (tools/special_tiles.cpp)
            if (from_remote) magplant_remote_used(event, *world);
            else pPeer->emplace(::slot(item.id, -1));
            add_stat(event, "placed");
        }
        gamePacket.netid = pPeer->netid; // @todo sometimes rgt has this as 0
        state_visuals(*event.peer, std::move(gamePacket)); // finished.
        if (tile_update) 
        {
            send_tile_update(event, std::move(gamePacket), block, *world);
        }
        if (area_lock_visuals) send_area_lock_visuals(event, *world, gamePacket.punch);
        if (lock_visuals) 
        {
            state_visuals(*event.peer, ::gamePacket{
                .type = 0x0f, // @note PACKET_SEND_LOCK
                .netid = world->owner, 
                .state = state::S_EXTENDED, 
                .id = gamePacket.id,
                .punch = gamePacket.punch
            });
        }
    }
    catch (const std::exception& exc)
    {
        send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, exc.what(), 0u, 1u });
        return;
    }
}
