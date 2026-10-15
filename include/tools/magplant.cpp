#include "pch.hpp"
#include <unordered_map>
#include "tools/area_lock.hpp"
#include "tools/economy.hpp"

#include "magplant.hpp"

namespace
{
    constexpr u_short MAGPLANT_5000 = 5638, GAIAS_BEACON = 6946, UNSTABLE_TESSERACT = 6948;

    struct magplant
    {
        ::pos pos{};
        short item{};     // @note 0 = nothing chosen yet
        int count{};
        bool collecting{ true };
        bool building{};  // @note the remote builds/plants from this one
    };

    /* @note world name -> machines. never erased, same reason as vending machines (world copies) */
    std::unordered_map<std::string, std::vector<magplant>> magplants{};

    int capacity(u_short machine) { return (machine == GAIAS_BEACON || machine == UNSTABLE_TESSERACT) ? 1500 : 5000; }
    bool can_build_mode(u_short machine) { return machine != GAIAS_BEACON && machine != UNSTABLE_TESSERACT; }

    magplant *find(::world &world, const ::pos &pos)
    {
        auto &list = magplants[world.name];
        auto it = std::ranges::find(list, pos, &magplant::pos);
        return (it != list.end()) ? &*it : nullptr;
    }

    magplant *building_machine(::world &world)
    {
        auto &list = magplants[world.name];
        auto it = std::ranges::find(list, true, &magplant::building);
        return (it != list.end()) ? &*it : nullptr;
    }

    u_short machine_at(::world &world, const ::pos &pos)
    {
        if (!in_world(world, pos.x_int(), pos.y_int())) return 0;
        const u_short fg = world.blocks[cord(pos.x_int(), pos.y_int())].fg;
        return (id_to_item(fg).type == type::MAGPLANT) ? fg : 0;
    }

    /* @note items the remote can put in the world: blocks, backgrounds, seeds and the like */
    bool placeable(const ::item &item)
    {
        switch (item.type)
        {
            case type::FOREGROUND: case type::BACKGROUND: case type::SEED: case type::PLATFORM:
            case type::DEADLY: case type::TRAMPOLINE: case type::BOUNCY: case type::SLIPPERY:
            case type::ANIMATED: case type::ART_WALL: case type::SFX_BLOCK: case type::SFX_BACKGROUND:
            case type::TOGGLEABLE_BLOCK: case type::TOGGLEABLE_ANIMATED_BLOCK: case type::TOGGLEABLE_DEADLY:
            case type::FIRE_PAIN: case type::STING_PAIN: case type::PROVIDER: case type::STEAM_BLOCK:
                return true;
        }
        return false;
    }

    /* @note what may go in this machine. untradeable/public items never (else it's a way to move them) */
    std::string refuse_reason(const ::item &item, u_short machine)
    {
        if (item.id == 0 || item.id == 112/*Gems*/ || (item.cat & CAT_UNTRADEABLE) || (item.cat & CAT_PUBLIC))
            return "This item is not compatible.";
        if (machine == GAIAS_BEACON && item.type != type::SEED) return "You can only store seeds in this machine.";
        if (machine == UNSTABLE_TESSERACT && item.type == type::SEED) return "You cannot store seeds in this machine.";
        if (!placeable(item) && item.type != type::CONSUMEABLE && item.type != type::CLOTHING) return "This item is not compatible.";
        return "";
    }

    int held(const ::peer &p, short id)
    {
        auto it = std::ranges::find(p.slots, id, &::slot::id);
        return (it != p.slots.end()) ? it->count : 0;
    }

    bool backpack_room(const ::peer &p, short id)
    {
        return held(p, id) > 0 || static_cast<int>(p.slots.size()) < p.slot_size;
    }

    void update_tile(ENetEvent &event, ::world &world, const ::pos &pos)
    {
        ::gamePacket gamePacket{};
        gamePacket.punch = pos;
        send_tile_update(event, gamePacket, world.blocks[cord(pos.x_int(), pos.y_int())], world);
    }

    /* @note tells a player which machine the remote builds from, 0 = none */
    void send_planter(ENetPeer &peer, ::world &world)
    {
        const magplant *m = building_machine(world);
        send_varlist(&peer, {
            "OnPlanterActivated",
            static_cast<u_int>(m ? m->item : 0),
            static_cast<u_int>(m ? m->pos.x_int() : 0),
            static_cast<u_int>(m ? m->pos.y_int() : 0)
        }, static_cast<::peer*>(peer.data)->netid);
    }

    void send_planter_all(ENetEvent &event, ::world &world)
    {
        peers(world.name, PEER_SAME_WORLD, [&world](ENetPeer &p) { send_planter(p, world); });
    }

    void talk(ENetEvent &event, const std::string &text)
    {
        send_varlist(event.peer, { "OnTalkBubble", static_cast<::peer*>(event.peer->data)->netid, text });
    }

    void amount_dialog(ENetEvent &event, const magplant &m, bool adding, int have)
    {
        const ::item &item = id_to_item(m.item);
        send_varlist(event.peer, {
            "OnDialogRequest",
            std::format(
                "set_default_color|`o\n"
                "add_label_with_icon|big|`2{}``|left|{}|\n"
                "add_textbox|{}|left|\n"
                "add_text_input|amount|Amount:|{}|5|\n"
                "embed_data|tilex|{}\n"
                "embed_data|tiley|{}\n"
                "embed_data|step|{}\n"
                "end_dialog|mt_magplant|Close|{}|",
                item.raw_name, item.id,
                adding ? std::format("You have {} `2{}`` in your backpack. How many would you like to add?", held(*static_cast<::peer*>(event.peer->data), m.item), item.raw_name)
                       : std::format("How many `2{}`` would you like to take out?", item.raw_name),
                have, m.pos.x_int(), m.pos.y_int(), adding ? "add" : "take", adding ? "Add" : "Retrieve"
            )
        });
    }
}

void magplant_extra(::blob &blob, ::world &world, const ::pos &pos)
{
    const magplant *m = find(world, pos);
    blob.i32(m ? m->item : 0);
    blob.i32(m ? m->count : 0);
    blob.u8(m ? m->collecting : 1);
    blob.u8(m ? m->building : 0);
    blob.i32(0);
}

void magplant_placed(::world &world, const ::pos &pos)
{
    auto &list = magplants[world.name];
    std::erase_if(list, [&](const magplant &m) { return m.pos == pos; });
    list.push_back(magplant{ .pos = pos });
}

bool magplant_in_use(::world &world, const ::pos &pos)
{
    const magplant *m = find(world, pos);
    return m && m->count > 0;
}

void magplant_removed(ENetEvent &event, ::world &world, const ::pos &pos)
{
    const magplant *m = find(world, pos);
    const bool was_building = m && m->building;
    std::erase_if(magplants[world.name], [&](const magplant &m) { return m.pos == pos; });
    if (was_building) send_planter_all(event, world);
}

void magplant_punched(ENetEvent &event, ::world &world, const ::pos &pos)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const u_short machine = machine_at(world, pos);
    magplant *m = find(world, pos);
    if (!machine || !m || !can_build_mode(machine)) return;
    if (tile_owner(world, pos) != pPeer->user_id) return; // @note only the owner switches modes, everyone else just hits it
    if (m->item == 0) return talk(event, "Select an item first!");
    if (!placeable(id_to_item(m->item))) return talk(event, "You cannot activate building mode for this item!");

    if (m->building) m->building = false;
    else
    {
        if (magplant *old = building_machine(world))
        {
            old->building = false;
            update_tile(event, world, old->pos);
        }
        m->building = true;
    }
    update_tile(event, world, pos);
    send_planter_all(event, world);
    talk(event, m->building ? "Building mode: `5ACTIVE``" : "Building mode: `6DISABLED``");
}

void magplant_drop(ENetEvent &event, ::slot slot, ::pos pos, ::world &world)
{
    for (magplant &m : magplants[world.name])
    {
        if (m.item != slot.id || !m.collecting) continue;
        const u_short machine = machine_at(world, m.pos);
        if (!machine || m.count + slot.count > capacity(machine)) continue;

        const bool was_empty = m.count == 0;
        m.count += slot.count;
        if (was_empty || m.count >= capacity(machine) - 50) update_tile(event, world, m.pos); // @note like GTopia, only when the look changes
        return;
    }
    add_drop(event, slot, pos, world);
}

short magplant_remote_item(ENetEvent &event, ::world &world)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (held(*pPeer, MAGPLANT_REMOTE_ID) < 1) throw std::runtime_error("You don't have a Magplant 5000 Remote!");

    magplant *m = building_machine(world);
    if (m == nullptr || m->item == 0 || !machine_at(world, m->pos)) throw std::runtime_error("There's no active Magplant!");
    /* @note the remote can be traded, so check here too: only the owner and their access list build from the machine */
    if (!tile_access(*pPeer, world, m->pos)) throw std::runtime_error("You don't have access to this Magplant!");
    if (m->count < 1) throw std::runtime_error(std::format("The Magplant is out of {}!", id_to_item(m->item).raw_name));
    return m->item;
}

void magplant_remote_used(ENetEvent &event, ::world &world)
{
    magplant *m = building_machine(world);
    if (m == nullptr || m->count < 1) return;
    --m->count;
    const u_short machine = machine_at(world, m->pos);
    if (m->count == 0 || m->count >= capacity(machine) - 51) update_tile(event, world, m->pos);
}

void magplant_on_join(ENetEvent &event, ::world &world)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (const int remotes = held(*pPeer, MAGPLANT_REMOTE_ID); remotes > 0)
        modify_item_inventory(event, ::slot(MAGPLANT_REMOTE_ID, -remotes));
    if (building_machine(world)) send_planter(*event.peer, world);
}

void magplant_dialog(ENetEvent &event, ::world &world, const ::pos &pos)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const u_short machine = machine_at(world, pos);
    if (!machine) return;
    magplant *m = find(world, pos);
    if (m == nullptr) // @note placed before this system existed
    {
        magplant_placed(world, pos);
        m = find(world, pos);
    }
    const ::item &machine_item = id_to_item(machine);
    const bool owner = tile_owner(world, pos) == pPeer->user_id;

    std::string body = std::format(
        "set_default_color|`o\n"
        "add_label_with_icon|big|`w{}``|left|{}|\n"
        "embed_data|tilex|{}\n"
        "embed_data|tiley|{}\n"
        "embed_data|step|main\n",
        machine_item.raw_name, machine, pos.x_int(), pos.y_int());

    if (tile_owner(world, pos) == 0)
    {
        body += "add_textbox|`6This machine only works in a World Locked or area locked spot.``|left|\n"
                "end_dialog|mt_magplant||Close|";
        send_varlist(event.peer, { "OnDialogRequest", body });
        return;
    }
    if (m->item == 0)
    {
        body += owner ? std::format("add_textbox|`6The machine is empty.``|left|\n"
                                    "add_item_picker|selectitem|`wChoose Item``|Choose an item to put in the {}!|\n", machine_item.raw_name)
                      : "add_textbox|There is no item selected.|left|\n";
        body += "end_dialog|mt_magplant||Close|";
        send_varlist(event.peer, { "OnDialogRequest", body });
        return;
    }

    const ::item &item = id_to_item(m->item);
    body += std::format("add_spacer|small|\nadd_label_with_icon|small|`2{}``|left|{}|\n", item.raw_name, item.id);
    if (owner)
    {
        body += (m->count == 0) ? "add_textbox|`6The machine is currently empty!``|left|\n"
                                : std::format("add_textbox|Machine contains {} `6{}`` (max {}).|left|\n", m->count, item.raw_name, capacity(machine));
        if (m->count >= capacity(machine)) body += "add_textbox|`4The machine is at maximum capacity!``|left|\n";
        else if (held(*pPeer, m->item) > 0) body += "add_button|additem|Add Items to the machine|noflags|0|0|\n";

        if (m->count == 0) body += "add_button|clearitem|Change Item|noflags|0|0|\n";
        else if (held(*pPeer, m->item) >= 200) body += std::format("add_textbox|You have too many {}!|left|\n", item.raw_name);
        else body += "add_button|retrieveitem|Retrieve Items|noflags|0|0|\n";
    }
    if (can_build_mode(machine))
    {
        if (placeable(item))
        {
            const char *mode = (item.type == type::SEED) ? "Planting" : "Building";
            body += m->building ? std::format("add_textbox|{} mode: `5ACTIVE``|left|\n", mode)
                                : std::format("add_textbox|{} mode: `6DISABLED``|left|\n", mode);
            if (owner && !m->building) body += "add_textbox|Punch the machine to activate it.|left|\n";
            if (tile_access(*pPeer, world, pos) && held(*pPeer, MAGPLANT_REMOTE_ID) < 1)
                body += "add_button|getremote|Get Remote|noflags|0|0|\n";
        }
        else body += "add_textbox|`6You cannot place this item.``|left|\n";
    }
    if (owner)
        body += std::format("add_checkbox|chk_collect|Enable Collection|{}\n"
                            "end_dialog|mt_magplant|Close|Update|", m->collecting ? 1 : 0);
    else body += "end_dialog|mt_magplant||Close|";
    send_varlist(event.peer, { "OnDialogRequest", body });
}

void magplant_return(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    auto world = std::ranges::find(worlds, pPeer->recent_worlds.back(), &::world::name);
    if (world == worlds.end()) return;

    const ::pos pos{ atoi(hPipe["tilex"].c_str()), atoi(hPipe["tiley"].c_str()) };
    const u_short machine = machine_at(*world, pos); // @note never trust the dialog: is there really a machine there?
    magplant *m = find(*world, pos);
    if (!machine || m == nullptr) return talk(event, "Huh? It's gone!");

    const std::string button = hPipe["buttonClicked"];
    if (button == "getremote")
    {
        if (!can_build_mode(machine) || m->item == 0 || !placeable(id_to_item(m->item))) return;
        if (!tile_access(*pPeer, *world, pos)) return;
        if (held(*pPeer, MAGPLANT_REMOTE_ID) > 0) return;
        if (!backpack_room(*pPeer, MAGPLANT_REMOTE_ID)) return talk(event, "Your backpack is full!");
        modify_item_inventory(event, ::slot(MAGPLANT_REMOTE_ID, 1));
        talk(event, std::format("You received a {}!", id_to_item(MAGPLANT_REMOTE_ID).raw_name));
        return;
    }

    if (tile_owner(*world, pos) != pPeer->user_id) return; // @note everything below is owner only

    const std::string step = hPipe["step"];
    if (step == "add" || step == "take")
    {
        if (m->item == 0) return;
        const int asked = atoi(hPipe["amount"].c_str());
        if (asked < 1) return;
        if (step == "add")
        {
            if (staff_locked(*pPeer)) return staff_locked_message(*event.peer); // @note spawned items stay out of the economy, same as boxes
            const int amount = std::min({ asked, held(*pPeer, m->item), capacity(machine) - m->count }); // @note clamp: a faked amount can't add items you don't have
            if (amount < 1) return talk(event, "You can't add any more.");
            modify_item_inventory(event, ::slot(m->item, -amount));
            m->count += amount;
            talk(event, std::format("Added {} {} to the machine.", amount, id_to_item(m->item).raw_name));
        }
        else
        {
            if (!backpack_room(*pPeer, m->item)) return talk(event, "Your backpack is full!");
            const int amount = std::min({ asked, m->count, 200 - held(*pPeer, m->item) });
            if (amount < 1) return talk(event, "You don't have any space left in your backpack for this.");
            m->count -= amount;
            modify_item_inventory(event, ::slot(m->item, amount));
            talk(event, std::format("You took {} {} out of the machine.", amount, id_to_item(m->item).raw_name));
        }
        update_tile(event, *world, pos);
        return;
    }

    bool changed{};
    if (!hPipe["chk_collect"].empty())
    {
        const bool collect = hPipe["chk_collect"] == "1";
        if (collect != m->collecting) m->collecting = collect, changed = true;
    }
    if (const std::string picked = hPipe["selectitem"]; !picked.empty() && m->item == 0)
    {
        const ::item &item = id_to_item(static_cast<u_short>(atoi(picked.c_str())));
        if (held(*pPeer, item.id) < 1) return talk(event, "You don't have that.");
        if (std::string reason = refuse_reason(item, machine); !reason.empty()) return talk(event, reason);
        m->item = item.id, m->count = 0, m->collecting = true, m->building = false;
        changed = true;
    }
    if (button == "clearitem")
    {
        if (m->count != 0) return talk(event, "Empty the machine first.");
        const bool was_building = m->building;
        m->item = 0, m->collecting = true, m->building = false;
        update_tile(event, *world, pos);
        if (was_building) send_planter_all(event, *world);
        magplant_dialog(event, *world, pos);
        return;
    }
    if (button == "additem") return amount_dialog(event, *m, true, std::min(held(*pPeer, m->item), capacity(machine) - m->count));
    if (button == "retrieveitem") return amount_dialog(event, *m, false, std::min(m->count, 200 - held(*pPeer, m->item)));

    if (changed) update_tile(event, *world, pos);
}

void load_magplants(::world &world)
{
    if (magplants.contains(world.name)) return; // @note already in memory (newer than the database)
    auto &list = magplants[world.name];

    ::hStmt hStmt{ "SELECT x, y, item, count, flags FROM world_magplant WHERE world = ?" };
    MYSQL_BIND param = make_bind_in(world.name);
    hStmt.bind_param(&param);

    signed x{}, y{}, item{}, count{}, flags{};
    MYSQL_BIND result[5] = { make_bind_out(x), make_bind_out(y), make_bind_out(item), make_bind_out(count), make_bind_out(flags) };
    mysql_stmt_bind_result(hStmt.pStmt, result);
    hStmt.execute();
    mysql_stmt_store_result(hStmt.pStmt);

    while (mysql_stmt_fetch(hStmt.pStmt) == 0)
        list.push_back(magplant{
            .pos = ::pos{ x, y }, .item = static_cast<short>(item), .count = std::clamp(count, 0, 5000),
            .collecting = (flags & 1) != 0, .building = (flags & 2) != 0
        });
}

void save_magplants(::world &world)
{
    {
        ::hStmt hStmt{ "DELETE FROM world_magplant WHERE world = ?" };
        MYSQL_BIND param = make_bind_in(world.name);
        hStmt.bind_param(&param);
        hStmt.execute();
    }
    for (const magplant &m : magplants[world.name])
    {
        if (!machine_at(world, m.pos) || m.item == 0) continue; // @note broken, or nothing to remember
        const signed x = m.pos.x_int(), y = m.pos.y_int(), item = m.item, count = m.count;
        const signed flags = (m.collecting ? 1 : 0) | (m.building ? 2 : 0);
        ::hStmt hStmt{ "INSERT INTO world_magplant (world, x, y, item, count, flags) VALUES (?, ?, ?, ?, ?, ?)" };
        MYSQL_BIND params[6] = { make_bind_in(world.name), make_bind_in(x), make_bind_in(y), make_bind_in(item), make_bind_in(count), make_bind_in(flags) };
        hStmt.bind_param(params);
        hStmt.execute();
    }
}
