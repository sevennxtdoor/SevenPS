#include "pch.hpp"
#include "tools/economy.hpp"
#include <unordered_map>
#include "onVariant/ConsoleMessage.hpp"

#include "vending.hpp"
#include "tools/area_lock.hpp"

namespace
{
    /* @note world name -> machines. kept for the whole server run (never erased): ::world gets copied around inside
     * the worlds vector, and a copy being destroyed must not wipe the live world's machines */
    std::unordered_map<std::string, std::vector<::vending>> vendings{};
}

static constexpr short WORLD_LOCK = 242, DIAMOND_LOCK = 1796;

static short owned(const ::peer &p, short id)
{
    auto it = std::ranges::find(p.slots, id, &::slot::id);
    return (it == p.slots.end()) ? 0 : it->count;
}

::vending *find_vending(::world &world, const ::pos &pos)
{
    auto &list = vendings[world.name];
    auto it = std::ranges::find(list, pos, &::vending::pos);
    return (it == list.end()) ? nullptr : &*it;
}

static ::vending &get_or_add(::world &world, const ::pos &pos)
{
    if (::vending *v = find_vending(world, pos)) return *v;
    return vendings[world.name].emplace_back(::vending{ .pos = pos });
}

void load_vendings(::world &world)
{
    if (vendings.contains(world.name)) return; // @note already in memory (newer than the database)
    auto &list = vendings[world.name];

    ::hStmt hStmt{ "SELECT x, y, item, stock, price, per_lock, earned FROM vending WHERE world = ?" };
    MYSQL_BIND param = make_bind_in(world.name);
    hStmt.bind_param(&param);

    signed x{}, y{}, item{}, stock{}, price{}, per_lock{}, earned{};
    MYSQL_BIND result[7] = {
        make_bind_out(x), make_bind_out(y), make_bind_out(item), make_bind_out(stock),
        make_bind_out(price), make_bind_out(per_lock), make_bind_out(earned)
    };
    mysql_stmt_bind_result(hStmt.pStmt, result);
    hStmt.execute();
    mysql_stmt_store_result(hStmt.pStmt);
    while (mysql_stmt_fetch(hStmt.pStmt) == 0)
        list.emplace_back(::vending{ ::pos{ x, y }, item, stock, price, per_lock != 0, earned });
}

void save_vendings(::world &world)
{
    {
        ::hStmt hStmt{ "DELETE FROM vending WHERE world = ?" };
        MYSQL_BIND param = make_bind_in(world.name);
        hStmt.bind_param(&param);
        hStmt.execute();
    }
    for (const ::vending &v : vendings[world.name])
    {
        /* @note skip machines that were broken, or are empty and untouched */
        if (id_to_item(world.blocks[cord(v.pos.x_int(), v.pos.y_int())].fg).type != type::VENDING_MACHINE) continue;
        if (v.item == 0 && v.earned == 0) continue;

        ::hStmt hStmt{ "INSERT INTO vending (world, x, y, item, stock, price, per_lock, earned) VALUES (?, ?, ?, ?, ?, ?, ?, ?)" };
        const signed x = v.pos.x_int(), y = v.pos.y_int(), per_lock = v.per_lock;
        MYSQL_BIND params[8] = {
            make_bind_in(world.name), make_bind_in(x), make_bind_in(y), make_bind_in(v.item),
            make_bind_in(v.stock), make_bind_in(v.price), make_bind_in(per_lock), make_bind_in(v.earned)
        };
        hStmt.bind_param(params);
        hStmt.execute();
    }
}

void vending_extra(::blob &blob, ::world &world, const ::pos &pos)
{
    const ::vending *v = find_vending(world, pos);
    const bool selling = v && v->item != 0 && v->stock > 0 && v->price > 0;
    blob.i32(selling ? v->item : 0);
    blob.i32(selling ? (v->per_lock ? -v->price : v->price) : 0);
}

bool vending_in_use(::world &world, const ::pos &pos)
{
    const ::vending *v = find_vending(world, pos);
    return v && (v->stock > 0 || v->earned > 0);
}

/* @note show the machine's new item/price to everyone in the world */
static void update_tile(ENetEvent &event, ::world &world, const ::pos &pos)
{
    ::gamePacket gamePacket{};
    gamePacket.punch = pos;
    send_tile_update(event, gamePacket, world.blocks[cord(pos.x_int(), pos.y_int())], world);
}

/* @note the World Lock owner, or the area lock owner when the machine stands in one. no lock at all = anyone (like before) */
static bool is_owner(const ::peer &p, ::world &world, const ::pos &pos)
{
    const int owner = tile_owner(world, pos);
    return owner == 0 || owner == p.user_id || p.role >= DEVELOPER;
}

static std::string sale_text(const ::vending &v)
{
    const std::string name = id_to_item(v.item).raw_name;
    if (v.price <= 0) return "add_textbox|Not currently for sale!|left|\n";
    return v.per_lock ?
        std::format("add_textbox|For a cost of:|left|\nadd_label_with_icon|small|1 x `8World Lock``|left|242|\nadd_textbox|You will get:|left|\nadd_label_with_icon|small|{} x `2{}``|left|{}|\n", v.price, name, v.item) :
        std::format("add_textbox|For a cost of:|left|\nadd_label_with_icon|small|{} x `8World Lock``|left|242|\nadd_textbox|You will get:|left|\nadd_label_with_icon|small|1 x `2{}``|left|{}|\n", v.price, name, v.item);
}

void send_vending_dialog(ENetEvent &event, ::world &world, const ::pos &pos)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    ::vending &v = get_or_add(world, pos);
    const std::string tile = std::format("embed_data|tilex|{}\nembed_data|tiley|{}\n", pos.x_int(), pos.y_int());
    const std::string header = "set_default_color|`o\nadd_label_with_icon|big|`wVending Machine``|left|2978|\nadd_spacer|small|\n";
    const std::string withdraw = (v.earned > 0) ?
        std::format("add_smalltext|`oYou have earned `w{}`` World Locks.``|left|\nadd_button|withdraw|Withdraw World Locks|noflags|0|0|\n", v.earned) : "";

    if (is_owner(*pPeer, world, pos))
    {
        if (v.item == 0 || v.stock <= 0)
        {
            send_varlist(event.peer, { "OnDialogRequest", header + tile +
                "add_textbox|This machine is empty.|left|\n"
                "add_item_picker|stockitem|`wPut an item in``|Choose an item to put in the machine!|\n" +
                withdraw + "end_dialog|vending|Close||\n" });
            return;
        }
        const short have = owned(*pPeer, static_cast<short>(v.item));
        const std::string add_more = (have > 0) ?
            std::format("add_smalltext|`oYou have `w{}`` {} in your backpack.``|left|\nadd_button|addvend|Add them to the machine|noflags|0|0|\n", have, id_to_item(v.item).raw_name) : "";
        send_varlist(event.peer, { "OnDialogRequest", header + tile +
            std::format("add_label_with_icon|small|The machine contains a total of `w{}`` `2{}``.|left|{}|\nadd_spacer|small|\n", v.stock, id_to_item(v.item).raw_name, v.item) +
            sale_text(v) + add_more +
            "add_button|pullstock|Empty the machine|noflags|0|0|\n"
            "add_smalltext|`5(Vending Machine will not function when price is set to 0)``|left|\n" +
            std::format("add_text_input|setprice|Price|{}|5|\n", v.price) +
            std::format("add_checkbox|chk_peritem|World Locks per Item|{}\nadd_checkbox|chk_perlock|Items per World Lock|{}\n", v.per_lock ? 0 : 1, v.per_lock ? 1 : 0) +
            withdraw + "end_dialog|vending|Close|Update|\n" });
        return;
    }

    /* @note buyer */
    if (v.item == 0 || v.stock <= 0 || v.price <= 0 || (v.per_lock && v.stock < v.price))
    {
        send_varlist(event.peer, { "OnDialogRequest", header + tile + "add_textbox|This machine is out of order.|left|\nend_dialog|vending|Close||\n" });
        return;
    }
    const int locks = owned(*pPeer, WORLD_LOCK) + owned(*pPeer, DIAMOND_LOCK) * 100;
    send_varlist(event.peer, { "OnDialogRequest", header + tile +
        std::format("add_label_with_icon|small|The machine contains a total of `w{}`` `2{}``.|left|{}|\nadd_spacer|small|\n", v.stock, id_to_item(v.item).raw_name, v.item) +
        sale_text(v) +
        std::format("add_spacer|small|\nadd_textbox|You have `w{}`` World Locks.|left|\n", locks) +
        std::format("add_text_input|buycount|How many {} would you like to buy?|0|5|\n", v.per_lock ? std::format("(multiple of {})", v.price) : "") +
        "end_dialog|vending|Close|Buy|\n" });
}

/* @note pay `cost` world locks, breaking diamond locks into change if needed. @return false if not enough */
static bool pay_locks(ENetEvent &event, int cost)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const int wl = owned(*pPeer, WORLD_LOCK), dl = owned(*pPeer, DIAMOND_LOCK);
    if (wl + dl * 100 < cost) return false;

    const int from_wl = std::min(wl, cost);
    const int dl_needed = (cost - from_wl + 99) / 100;
    const int change = dl_needed * 100 - (cost - from_wl);
    if (wl - from_wl + change > 200) return false; // @note no room for the change

    if (from_wl > 0)    modify_item_inventory(event, ::slot{ WORLD_LOCK, static_cast<short>(-from_wl) });
    if (dl_needed > 0)  modify_item_inventory(event, ::slot{ DIAMOND_LOCK, static_cast<short>(-dl_needed) });
    if (change > 0)     modify_item_inventory(event, ::slot{ WORLD_LOCK, static_cast<short>(change) });
    return true;
}

/* @note give up to `count` of an item, capped by the 200 stack and backpack room. @return how many were given */
static int give(ENetEvent &event, short id, int count)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const short have = owned(*pPeer, id);
    if (have == 0 && std::ranges::find(pPeer->slots, id, &::slot::id) == pPeer->slots.end() && (int)pPeer->slots.size() >= pPeer->slot_size) return 0;

    const int amount = std::min(count, 200 - have);
    if (amount > 0) modify_item_inventory(event, ::slot{ id, static_cast<short>(amount) });
    return std::max(amount, 0);
}

void vending_return(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    auto world = std::ranges::find(worlds, pPeer->recent_worlds.back(), &::world::name);
    if (pPeer->netid == 0 || world == worlds.end()) return;

    const ::pos pos{ atoi(hPipe["tilex"].c_str()), atoi(hPipe["tiley"].c_str()) };
    if (pos.x < 0 || pos.y < 0 || pos.x >= 100 || pos.y >= 60) return;
    if (id_to_item(world->blocks[cord(pos.x_int(), pos.y_int())].fg).type != type::VENDING_MACHINE) return;

    ::vending &v = get_or_add(*world, pos);
    const std::string clicked = hPipe["buttonClicked"];
    if (staff_locked(*pPeer) && (!hPipe["stockitem"].empty() || clicked == "addvend" || !hPipe["buycount"].empty()))
        return staff_locked_message(*event.peer); // @note no selling spawned items, no paying players with spawned World Locks

    if (is_owner(*pPeer, *world, pos))
    {
        if (const std::string stockitem = hPipe["stockitem"]; !stockitem.empty() && (v.item == 0 || v.stock <= 0))
        {
            const short id = static_cast<short>(atoi(stockitem.c_str()));
            const short have = owned(*pPeer, id);
            if (id == 18 || id == 32 || id == WORLD_LOCK || id == DIAMOND_LOCK || have <= 0)
                return on::ConsoleMessage(event.peer, "`4You can't put that in a vending machine.``");
            modify_item_inventory(event, ::slot{ id, static_cast<short>(-have) });
            v.item = id; v.stock = have; v.price = 0; v.per_lock = false;
            on::ConsoleMessage(event.peer, std::format("`oPut `w{}`` {} in the machine. Wrench it again to set a price.``", have, id_to_item(id).raw_name));
        }
        else if (clicked == "addvend" && v.item != 0)
        {
            const short have = owned(*pPeer, static_cast<short>(v.item));
            if (have > 0)
            {
                modify_item_inventory(event, ::slot{ static_cast<short>(v.item), static_cast<short>(-have) });
                v.stock += have;
            }
        }
        else if (clicked == "pullstock" && v.item != 0)
        {
            const int given = give(event, static_cast<short>(v.item), v.stock);
            v.stock -= given;
            if (v.stock <= 0) { v.item = 0; v.stock = 0; v.price = 0; }
            else on::ConsoleMessage(event.peer, "`oYour backpack is full, some items are still in the machine.``");
        }
        else if (clicked == "withdraw" && v.earned > 0)
        {
            v.earned -= give(event, WORLD_LOCK, v.earned);
            if (v.earned > 0) on::ConsoleMessage(event.peer, "`oYou can't carry more World Locks, the rest stays in the machine.``");
        }
        else if (!hPipe["setprice"].empty())
        {
            v.price = std::clamp(atoi(hPipe["setprice"].c_str()), 0, 20000);
            v.per_lock = hPipe["chk_perlock"] == "1" && hPipe["chk_peritem"] != "1";
        }
        update_tile(event, *world, pos);
        return;
    }

    /* @note buyer pressed Buy */
    const int count = atoi(hPipe["buycount"].c_str());
    if (count <= 0 || v.item == 0 || v.price <= 0) return;
    if (count > v.stock) return on::ConsoleMessage(event.peer, "`4The machine doesn't have that many.``");
    if (v.per_lock && count % v.price != 0)
        return on::ConsoleMessage(event.peer, std::format("`4You must buy in multiples of `w{}``.``", v.price));

    const int cost = v.per_lock ? count / v.price : count * v.price;
    const short have = owned(*pPeer, static_cast<short>(v.item));
    if (have + count > 200) return on::ConsoleMessage(event.peer, "`4You can't carry that many.``");
    if (have == 0 && std::ranges::find(pPeer->slots, static_cast<short>(v.item), &::slot::id) == pPeer->slots.end() && (int)pPeer->slots.size() >= pPeer->slot_size)
        return on::ConsoleMessage(event.peer, "`4Your backpack is full!``");
    if (!pay_locks(event, cost)) return on::ConsoleMessage(event.peer, "`4You can't afford that!``");

    modify_item_inventory(event, ::slot{ static_cast<short>(v.item), static_cast<short>(count) });
    v.stock -= count;
    v.earned += cost;

    const std::string msg = std::format("`7[``{} bought `5{}`` {} for `5{}`` World Locks.`7]``", pPeer->display_growid, count, id_to_item(v.item).raw_name, cost);
    peers(world->name, PEER_SAME_WORLD, [&msg](ENetPeer &p) { on::ConsoleMessage(&p, msg); });
    if (v.stock <= 0) { v.item = 0; v.stock = 0; v.price = 0; }
    update_tile(event, *world, pos);
}
