#include "pch.hpp"
#include <unordered_map>
#include "tools/area_lock.hpp"
#include "tools/economy.hpp"

#include "display_block.hpp"

namespace
{
    struct display
    {
        ::pos pos{};
        short item{}; // @note 0 = nothing on display
    };

    /* @note world name -> display blocks. never erased, same reason as vending machines (world copies) */
    std::unordered_map<std::string, std::vector<display>> displays{};

    display *find(::world &world, const ::pos &pos)
    {
        auto &list = displays[world.name];
        auto it = std::ranges::find(list, pos, &display::pos);
        return (it != list.end()) ? &*it : nullptr;
    }

    bool is_display(::world &world, const ::pos &pos)
    {
        return in_world(world, pos.x_int(), pos.y_int()) && id_to_item(world.blocks[cord(pos.x_int(), pos.y_int())].fg).type == type::DISPLAY_BLOCK;
    }

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

    void talk(ENetEvent &event, const std::string &text)
    {
        send_varlist(event.peer, { "OnTalkBubble", static_cast<::peer*>(event.peer->data)->netid, text, 0u, 1u });
    }

    void update_tile(ENetEvent &event, ::world &world, const ::pos &pos)
    {
        ::gamePacket gamePacket{};
        gamePacket.punch = pos;
        send_tile_update(event, gamePacket, world.blocks[cord(pos.x_int(), pos.y_int())], world);
    }

    /* @note wiki: "Only the block's owner can place items in it." an unlocked block has no owner (same as mannequins) */
    std::string refuse_use(const ::peer &p, ::world &world, const ::pos &pos, const ::item &display_item)
    {
        const int owner = tile_owner(world, pos);
        if (owner == 0) return std::format("It's not safe to use an unlocked {}!", display_item.raw_name); // @note SevenPS text, not from the wiki
        if (owner != p.user_id) return "Only the block's owner can place items in it.";
        return "";
    }
}

void display_extra(::blob &blob, ::world &world, const ::pos &pos)
{
    const display *d = find(world, pos);
    blob.u32(d ? static_cast<u_int>(d->item) : 0); // @note the client always reads it, even 0 - without it the rest of the world is misread
}

std::string display_refuse_break(const ::peer &p, ::world &world, const ::pos &pos)
{
    const display *d = find(world, pos);
    if (d == nullptr || d->item == 0) return "";
    if (tile_owner(world, pos) != p.user_id) return "Only the block's owner can break it!"; // @note growtopiawiki.com/w/Display_Block
    return "";
}

void display_removed(ENetEvent &event, ::world &world, const ::pos &pos)
{
    auto &list = displays[world.name];
    auto it = std::ranges::find(list, pos, &display::pos);
    if (it == list.end()) return;
    const short item = it->item;
    list.erase(it); // @note gone first, then drop: never both at once
    if (item != 0) add_drop(event, ::slot(item, 1), pos.by_32(), world); // @note wiki: breaking it spits the item out
}

void display_use_item(ENetEvent &event, ::world &world, const ::pos &pos, short item_id)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (!is_display(world, pos)) return;
    const ::item &display_item = id_to_item(world.blocks[cord(pos.x_int(), pos.y_int())].fg);
    const ::item &item = id_to_item(item_id);

    if (std::string reason = refuse_use(*pPeer, world, pos, display_item); !reason.empty()) return talk(event, reason);
    /* @note texts from growtopiawiki.com/w/Display_Block */
    if (item.cat & CAT_UNTRADEABLE) return talk(event, "You can't display untradeable items.");
    if (item.raw_name == "Screen Door") return talk(event, "Don't be a scammer.");
    if (item.type == type::LOCK || item.type == type::DISPLAY_BLOCK) return talk(event, "Sorry, no displaying Display Blocks or Locks.");
    if (staff_locked(*pPeer)) return staff_locked_message(*event.peer); // @note spawned items stay out of the economy, same as boxes

    display *d = find(world, pos);
    if (d == nullptr) d = &displays[world.name].emplace_back(display{ .pos = pos }); // @note placed before this system existed
    if (d->item != 0) return talk(event, "Remove what's in there first!"); // @note guessed text, the wiki doesn't say

    const int have = held(*pPeer, item.id);
    if (have < 1) return talk(event, "You don't have that item anymore.");
    if (have == 1 && item.cloth_type != clothing::NONE && pPeer->clothing[item.cloth_type] == item.id) return talk(event, "Take it off first!"); // @note else you'd keep wearing an item you gave away

    modify_item_inventory(event, ::slot(item.id, -1));
    d->item = item.id;
    update_tile(event, world, pos);
}

void display_wrench(ENetEvent &event, ::world &world, const ::pos &pos)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (!is_display(world, pos)) return;
    display *d = find(world, pos);
    if (d == nullptr || d->item == 0) return; // @note nothing on display
    if (tile_owner(world, pos) != pPeer->user_id) return talk(event, "Only the block's owner can remove items from it."); // @note guessed text

    const short item = d->item;
    if (!backpack_room(*pPeer, item))
        return talk(event, std::format("I don't have enough room in my backpack to get `5{}`` from the {}!", id_to_item(item).raw_name, id_to_item(world.blocks[cord(pos.x_int(), pos.y_int())].fg).raw_name));
    d->item = 0; // @note empty it first, then give: never both at once
    modify_item_inventory(event, ::slot(item, 1));
    update_tile(event, world, pos);
}

void load_displays(::world &world)
{
    if (displays.contains(world.name)) return; // @note already in memory (newer than the database)
    auto &list = displays[world.name];

    ::hStmt hStmt{ "SELECT x, y, item FROM world_display WHERE world = ?" };
    MYSQL_BIND param = make_bind_in(world.name);
    hStmt.bind_param(&param);

    signed x{}, y{}, item{};
    MYSQL_BIND result[3] = { make_bind_out(x), make_bind_out(y), make_bind_out(item) };
    mysql_stmt_bind_result(hStmt.pStmt, result);
    hStmt.execute();
    mysql_stmt_store_result(hStmt.pStmt);

    while (mysql_stmt_fetch(hStmt.pStmt) == 0)
        if (item > 0 && item < static_cast<signed>(items.size())) list.emplace_back(display{ .pos = ::pos{ x, y }, .item = static_cast<short>(item) });
}

void save_displays(::world &world)
{
    {
        ::hStmt hStmt{ "DELETE FROM world_display WHERE world = ?" };
        MYSQL_BIND param = make_bind_in(world.name);
        hStmt.bind_param(&param);
        hStmt.execute();
    }
    for (const display &d : displays[world.name])
    {
        if (d.item == 0 || !is_display(world, d.pos)) continue; // @note empty, or broken
        const signed x = d.pos.x_int(), y = d.pos.y_int(), item = d.item;
        ::hStmt hStmt{ "INSERT INTO world_display (world, x, y, item) VALUES (?, ?, ?, ?)" };
        MYSQL_BIND params[4] = { make_bind_in(world.name), make_bind_in(x), make_bind_in(y), make_bind_in(item) };
        hStmt.bind_param(params);
        hStmt.execute();
    }
}
