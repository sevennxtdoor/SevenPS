#include "pch.hpp"
#include <unordered_map>
#include "tools/area_lock.hpp"
#include "tools/economy.hpp"
#include "tools/string.hpp"

#include "mannequin.hpp"

namespace
{
    constexpr int PARTS = 9; // @note ::clothing HAIR(hat) .. CHARM(chest), ANCES is not worn by mannequins
    constexpr float NEAR_TILES = 3.0f; // @note how close you must stand to dress it (GTopia: under 4 tiles)
    constexpr const char *PART_NAMES[PARTS] = { "Hat", "Shirt", "Pants", "Shoes", "Face Item", "Gloves", "Back", "Hair", "Chest Item" };

    struct mannequin
    {
        ::pos pos{};
        std::string text{};
        short clothes[PARTS]{};
    };

    /* @note world name -> mannequins. never erased, same reason as vending machines (world copies) */
    std::unordered_map<std::string, std::vector<mannequin>> mannequins{};

    mannequin *find(::world &world, const ::pos &pos)
    {
        auto &list = mannequins[world.name];
        auto it = std::ranges::find(list, pos, &mannequin::pos);
        return (it != list.end()) ? &*it : nullptr;
    }

    bool is_mannequin(::world &world, const ::pos &pos)
    {
        return in_world(world, pos.x_int(), pos.y_int()) && id_to_item(world.blocks[cord(pos.x_int(), pos.y_int())].fg).type == type::MANNEQUIN;
    }

    /* @note placed before this system existed: start it empty */
    mannequin &get(::world &world, const ::pos &pos)
    {
        if (mannequin *m = find(world, pos)) return *m;
        return mannequins[world.name].emplace_back(mannequin{ .pos = pos });
    }

    bool empty(const mannequin &m) { return std::ranges::all_of(m.clothes, [](short id) { return id == 0; }); }

    int held(const ::peer &p, short id)
    {
        auto it = std::ranges::find(p.slots, id, &::slot::id);
        return (it != p.slots.end()) ? it->count : 0;
    }

    /* @note room for one more of this item: its slot below 200 (an emptied slot stays in peer::slots) or a free slot */
    bool backpack_room(const ::peer &p, short id)
    {
        if (std::ranges::find(p.slots, id, &::slot::id) != p.slots.end()) return held(p, id) < 200;
        return static_cast<int>(p.slots.size()) < p.slot_size;
    }

    /* @note signs go into a dialog line, so '|' and new lines would add dialog elements */
    std::string clean_text(std::string text)
    {
        std::erase_if(text, [](char c) { return c == '|' || c == '\n' || c == '\r'; });
        if (text.size() > 128) text.resize(128);
        return text;
    }

    void talk(ENetEvent &event, const std::string &text)
    {
        send_varlist(event.peer, { "OnTalkBubble", static_cast<::peer*>(event.peer->data)->netid, text, 0u, 1u });
    }

    void sfx_world(::world &world, std::string_view file)
    {
        peers(world.name, PEER_SAME_WORLD, [file](ENetPeer &p) {
            send_action(p, "play_sfx", std::format("file|{}\ndelayMS|0\n", file));
        });
    }

    void update_tile(ENetEvent &event, ::world &world, const ::pos &pos)
    {
        ::gamePacket gamePacket{};
        gamePacket.punch = pos;
        send_tile_update(event, gamePacket, world.blocks[cord(pos.x_int(), pos.y_int())], world);
    }

    /* @note owner/access in a locked spot. @return why not, empty when allowed */
    std::string refuse_use(const ::peer &p, ::world &world, const ::pos &pos, const ::item &mannequin_item)
    {
        if (tile_owner(world, pos) == 0) return std::format("It's not safe to use an unlocked {}!", mannequin_item.raw_name);
        if (!tile_access(p, world, pos)) return std::format("That {} isn't yours!", mannequin_item.raw_name);
        return "";
    }

    /* @note puts one piece back in the backpack. @return false (and says why) when it doesn't fit */
    bool take_back(ENetEvent &event, mannequin &m, int part)
    {
        ::peer *pPeer = static_cast<::peer*>(event.peer->data);
        const short id = m.clothes[part];
        if (id == 0) return true;
        if (!backpack_room(*pPeer, id))
        {
            talk(event, std::format("I don't have enough room in my backpack to get `5{}`` from the mannequin!", id_to_item(id).raw_name));
            return false;
        }
        m.clothes[part] = 0; // @note empty the slot first, then give: never both at once
        modify_item_inventory(event, ::slot(id, 1));
        return true;
    }
}

void mannequin_extra(::blob &blob, ::world &world, const ::pos &pos)
{
    const mannequin *m = find(world, pos);
    const std::string text = m ? m->text : "";
    blob.i16(text.length());
    for (char c : text) blob.u8(c);
    blob.u8(0x00); // @note flags
    blob.u32(0xffffffff); // @note hair color (white = the hair's own color)
    for (int i = 0; i < PARTS; ++i) blob.i16(m ? m->clothes[i] : 0);
}

void mannequin_placed(::world &world, const ::pos &pos)
{
    auto &list = mannequins[world.name];
    std::erase_if(list, [&](const mannequin &m) { return m.pos == pos; });
    list.push_back(mannequin{ .pos = pos });
}

void mannequin_removed(::world &world, const ::pos &pos)
{
    std::erase_if(mannequins[world.name], [&](const mannequin &m) { return m.pos == pos; });
}

bool mannequin_punched(ENetEvent &event, ::world &world, const ::pos &pos)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    mannequin *m = find(world, pos);
    if (m == nullptr || empty(*m)) return false;
    if (!tile_access(*pPeer, world, pos)) // @note e.g. the lock was removed: the clothes aren't yours, so it can't be broken either
    {
        talk(event, "Someone's clothes are on it!");
        return true;
    }

    /* @note clothes on it: the punch takes one back instead of hurting it, so it can only be broken empty */
    for (int part = 0; part < PARTS; ++part)
    {
        if (m->clothes[part] == 0) continue;
        if (take_back(event, *m, part))
        {
            sfx_world(world, "audio/change_clothes.wav");
            update_tile(event, world, pos);
        }
        break;
    }
    return true;
}

void mannequin_use_item(ENetEvent &event, ::world &world, const ::pos &pos, short item_id)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (!is_mannequin(world, pos)) return;
    const ::item &mannequin_item = id_to_item(world.blocks[cord(pos.x_int(), pos.y_int())].fg);
    const ::item &cloth = id_to_item(item_id);

    if (std::string reason = refuse_use(*pPeer, world, pos, mannequin_item); !reason.empty()) return talk(event, reason);
    if (cloth.type != type::CLOTHING || cloth.cloth_type >= PARTS)
        return talk(event, std::format("That would just be weird. Try putting clothes on your {} instead!", mannequin_item.raw_name));

    send_varlist(event.peer, {
        "OnDialogRequest",
        std::format(
            "set_default_color|`o\n"
            "add_label_with_icon|big|`w{}``|left|{}|\n"
            "add_textbox|Do you really want to put your {} on the {}?|left|\n"
            "embed_data|put|{}\n"
            "embed_data|tilex|{}\n"
            "embed_data|tiley|{}\n"
            "end_dialog|mt_mannequin|No|Yes|",
            cloth.raw_name, cloth.id, cloth.raw_name, mannequin_item.raw_name, cloth.id, pos.x_int(), pos.y_int()
        )
    });
}

void mannequin_dialog(ENetEvent &event, ::world &world, const ::pos &pos)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (!is_mannequin(world, pos)) return;
    const ::item &mannequin_item = id_to_item(world.blocks[cord(pos.x_int(), pos.y_int())].fg);
    if (std::string reason = refuse_use(*pPeer, world, pos, mannequin_item); !reason.empty()) return talk(event, reason);

    const mannequin &m = get(world, pos);
    std::string body = std::format(
        "set_default_color|`o\n"
        "add_label_with_icon|big|`wEdit {}``|left|{}|\n"
        "add_textbox|To dress, select a clothing item then use on the mannequin. To remove clothes, punch it or select which item to remove.<CR><CR>It will go into your backpack if you have room.|left|\n",
        mannequin_item.raw_name, mannequin_item.id);

    if (!empty(m))
    {
        for (int part = 0; part < PARTS; ++part)
            if (m.clothes[part] != 0)
                body += std::format("add_checkbox|cloth_{}|{} ({})|0\n", part, id_to_item(m.clothes[part]).raw_name, PART_NAMES[part]);
        body += "add_spacer|small|\n"
                "add_button|clear_selected|`4Remove Selected Items``|noflags|0|0|\n"
                "add_button|clear|`4Remove All Items``|noflags|0|0|\n";
    }
    body += std::format(
        "add_textbox|<CR><CR>What would you like to write on its sign?``|left|\n"
        "add_text_input|sign_text||{}|128|\n"
        "embed_data|tilex|{}\n"
        "embed_data|tiley|{}\n"
        "end_dialog|mt_mannequin|Cancel|OK|",
        m.text, pos.x_int(), pos.y_int());
    send_varlist(event.peer, { "OnDialogRequest", body });
}

void mannequin_return(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    auto world = std::ranges::find(worlds, pPeer->recent_worlds.back(), &::world::name);
    if (world == worlds.end()) return;

    const ::pos pos{ atoi(hPipe["tilex"].c_str()), atoi(hPipe["tiley"].c_str()) };
    if (!is_mannequin(*world, pos)) return talk(event, "Huh? The mannequin is gone!"); // @note never trust the dialog: is there really one there?
    const ::item &mannequin_item = id_to_item(world->blocks[cord(pos.x_int(), pos.y_int())].fg);
    if (std::string reason = refuse_use(*pPeer, *world, pos, mannequin_item); !reason.empty()) return talk(event, reason);
    mannequin &m = get(*world, pos);

    if (const std::string put = hPipe["put"]; !put.empty())
    {
        const ::item &cloth = id_to_item(static_cast<u_short>(atoi(put.c_str())));
        if (cloth.type != type::CLOTHING || cloth.cloth_type >= PARTS) return;

        /* @note everything is checked again here: the item may be gone or traded since the question */
        const ::pos me = pPeer->pos.by_32(true);
        if (std::abs(me.x - pos.x) > NEAR_TILES || std::abs(me.y - pos.y) > NEAR_TILES) return talk(event, "I'll need to get closer.");
        if (staff_locked(*pPeer)) return staff_locked_message(*event.peer); // @note spawned items stay out of the economy, same as boxes
        if (cloth.cat & CAT_UNTRADEABLE) return talk(event, "That item is just too valuable to part with.");
        if (cloth.id == 5712/*Will of the Wild*/ || cloth.id == 10044/*Golem's Gift*/)
            return talk(event, "That item is just too valuable to part with."); // @note GTopia sends extra character flags for these two, layout unknown here
        const int have = held(*pPeer, cloth.id);
        if (have < 1) return talk(event, "You don't have that item anymore.");
        if (have == 1 && pPeer->clothing[cloth.cloth_type] == cloth.id) return talk(event, "Take it off first!"); // @note else you'd keep wearing an item you gave away

        short &slot = m.clothes[cloth.cloth_type];
        if (slot == cloth.id) return talk(event, "You giggle as you swap two identical items.");
        if (slot != 0 && !take_back(event, m, cloth.cloth_type)) return; // @note the old piece comes back first, only if it fits

        modify_item_inventory(event, ::slot(cloth.id, -1));
        slot = cloth.id;
        sfx_world(*world, "audio/change_clothes.wav");
        update_tile(event, *world, pos);
        return;
    }

    bool changed{};
    const std::string button = hPipe["buttonClicked"];
    if (button == "clear" || button == "clear_selected")
    {
        for (int part = 0; part < PARTS; ++part)
        {
            if (m.clothes[part] == 0) continue;
            if (button == "clear_selected" && hPipe[std::format("cloth_{}", part)] != "1") continue;
            if (!take_back(event, m, part)) break;
            changed = true;
        }
        if (changed) sfx_world(*world, "audio/change_clothes.wav");
    }
    if (const std::string text = clean_text(hPipe["sign_text"]); text != m.text)
    {
        m.text = text;
        changed = true;
    }
    if (changed) update_tile(event, *world, pos);
}

void load_mannequins(::world &world)
{
    if (mannequins.contains(world.name)) return; // @note already in memory (newer than the database)
    auto &list = mannequins[world.name];

    ::hStmt hStmt{ "SELECT x, y, text, clothes FROM world_mannequin WHERE world = ?" };
    MYSQL_BIND param = make_bind_in(world.name);
    hStmt.bind_param(&param);

    signed x{}, y{};
    char text[256]{}, clothes[128]{};
    u_long text_length{}, clothes_length{};
    MYSQL_BIND result[4] = { make_bind_out(x), make_bind_out(y) };
    const auto string_out = [](MYSQL_BIND &bind, char *buffer, u_long size, u_long &length) {
        bind.buffer_type = MYSQL_TYPE_STRING;
        bind.buffer = buffer;
        bind.buffer_length = size;
        bind.length = &length;
    };
    string_out(result[2], text, sizeof(text), text_length);
    string_out(result[3], clothes, sizeof(clothes), clothes_length);
    mysql_stmt_bind_result(hStmt.pStmt, result);
    hStmt.execute();
    mysql_stmt_store_result(hStmt.pStmt);

    while (mysql_stmt_fetch(hStmt.pStmt) == 0)
    {
        mannequin m{ .pos = ::pos{ x, y } };
        m.text = clean_text(std::string(text, std::min<u_long>(text_length, sizeof(text))));
        const std::string clothes_text(clothes, std::min<u_long>(clothes_length, sizeof(clothes)));
        if (!clothes_text.empty())
        {
            std::vector<std::string> ids = readch(clothes_text, ',');
            for (int part = 0; part < PARTS && part < static_cast<int>(ids.size()); ++part)
                m.clothes[part] = static_cast<short>(atoi(ids[part].c_str()));
        }
        list.emplace_back(std::move(m));
    }
}

void save_mannequins(::world &world)
{
    {
        ::hStmt hStmt{ "DELETE FROM world_mannequin WHERE world = ?" };
        MYSQL_BIND param = make_bind_in(world.name);
        hStmt.bind_param(&param);
        hStmt.execute();
    }
    for (const mannequin &m : mannequins[world.name])
    {
        if (!is_mannequin(world, m.pos) || (empty(m) && m.text.empty())) continue; // @note broken, or nothing to remember
        std::string clothes{};
        for (int part = 0; part < PARTS; ++part) clothes += std::format("{}{}", part ? "," : "", m.clothes[part]);

        const signed x = m.pos.x_int(), y = m.pos.y_int();
        ::hStmt hStmt{ "INSERT INTO world_mannequin (world, x, y, text, clothes) VALUES (?, ?, ?, ?, ?)" };
        MYSQL_BIND params[5] = { make_bind_in(world.name), make_bind_in(x), make_bind_in(y), make_bind_in(m.text), make_bind_in(clothes) };
        hStmt.bind_param(params);
        hStmt.execute();
    }
}
