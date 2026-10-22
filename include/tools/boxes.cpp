#include "pch.hpp"
#include <unordered_map>
#include "onVariant/ConsoleMessage.hpp"
#include "tools/string.hpp"
#include "tools/economy.hpp"
#include "tools/area_lock.hpp"
#include "tools/duct_tape.hpp"

#include "boxes.hpp"

namespace
{
    struct note
    {
        std::string author{};
        std::string text{};
        int item{}, count{}; // @note Donation Box gift
    };
    struct box
    {
        ::pos pos{};
        std::string password{};         // @note Safe Vault, empty = none
        std::vector<::slot> items{};    // @note Storage Box, Untrade-a-Box, Safe Vault, Donation Box gifts
        std::vector<note> notes{};      // @note Mailbox, Bulletin Board, Donation Box
    };

    /* @note world name -> boxes. never erased, same reason as vending machines (world copies) */
    std::unordered_map<std::string, std::vector<box>> boxes{};

    constexpr std::size_t MAX_NOTES = 20, MAX_TEXT = 120;
}

box_kind box_type(const ::item &item)
{
    if (item.id == 4516) return box_kind::UNTRADE; // @note Untrade-a-Box
    switch (item.type)
    {
        case type::STORAGE_BOX:  return box_kind::STORAGE;
        case type::SAFE_VAULT:   return box_kind::VAULT;
        case type::DONATION_BOX: return box_kind::DONATION;
        case type::MAILBOX:      return box_kind::MAIL;
        case type::BULLETIN:     return box_kind::BULLETIN;
        default:                 return box_kind::NONE;
    }
}

static std::size_t capacity(short id)
{
    return (id == 6288) ? 40 : (id == 6290) ? 90 : 20; // @note Storage Box Xtreme Level 2 / 3, the rest 20
}

static box *find_box(::world &world, const ::pos &pos)
{
    auto &list = boxes[world.name];
    auto it = std::ranges::find(list, pos, &box::pos);
    return (it == list.end()) ? nullptr : &*it;
}

static box &get_or_add(::world &world, const ::pos &pos)
{
    if (box *b = find_box(world, pos)) return *b;
    return boxes[world.name].emplace_back(box{ .pos = pos });
}

bool box_in_use(::world &world, const ::pos &pos)
{
    const box *b = find_box(world, pos);
    return b && !b->items.empty();
}

void box_removed(::world &world, const ::pos &pos)
{
    std::erase_if(boxes[world.name], [&pos](const box &b) { return b.pos == pos; });
}

/* @note player text goes into dialogs and the database: '|' and new lines would let someone inject dialog
 * elements (fake buttons, embed_data), the 0x1e/0x1f bytes are our own separators in the database */
static std::string clean(std::string text, std::size_t max = MAX_TEXT)
{
    std::erase_if(text, [](char c) { return c == '|' || static_cast<u_char>(c) < 0x20; });
    if (text.size() > max) text.resize(max);
    return text;
}

static short owned(const ::peer &p, short id)
{
    auto it = std::ranges::find(p.slots, id, &::slot::id);
    return (it == p.slots.end()) ? 0 : it->count;
}

/* @note give up to `count`, capped by the 200 stack and backpack room. @return how many were given */
static int give(ENetEvent &event, short id, int count)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const short have = owned(*pPeer, id);
    if (have == 0 && (int)pPeer->slots.size() >= pPeer->slot_size) return 0;

    const int amount = std::min(count, 200 - have);
    if (amount > 0) modify_item_inventory(event, ::slot{ id, static_cast<short>(amount) });
    return std::max(amount, 0);
}

/* @note tell the owner if they're online */
static void notify(int user_id, const std::string &message)
{
    for (ENetPeer *p : peers())
        if (static_cast<::peer*>(p->data)->user_id == user_id) on::ConsoleMessage(p, message);
}

static std::string header(const ::item &item, const ::pos &pos, const std::string &password = "")
{
    return std::format("set_default_color|`o\nadd_label_with_icon|big|`w{}``|left|{}|\nembed_data|tilex|{}\nembed_data|tiley|{}\n{}",
        item.raw_name, item.id, pos.x_int(), pos.y_int(), password.empty() ? "" : std::format("embed_data|pw|{}\n", password));
}

static void dialog(ENetEvent &event, const std::string &text)
{
    send_varlist(event.peer, { "OnDialogRequest", text });
}

static void storage_dialog(ENetEvent &event, ::world &world, const ::pos &pos, const ::item &item, box &b, const std::string &password)
{
    std::string list{};
    for (std::size_t i = 0; i < b.items.size(); ++i)
        list += std::format("add_button|take_{}|`w{}`` x {}|noflags|0|0|\n", i, b.items[i].count, id_to_item(b.items[i].id).raw_name);
    if (list.empty()) list = "add_textbox|It's empty.|left|\n";

    const std::string vault = (box_type(item) == box_kind::VAULT) ?
        std::format("add_spacer|small|\nadd_text_input|new_pw|Password (empty = none)|{}|24|\nadd_button|set_pw|`wSet password``|noflags|0|0|\n", b.password) : "";

    dialog(event, header(item, pos, password) +
        std::format("add_textbox|`w{}``/{} stacks stored. Click a stack to take it out.|left|\n", b.items.size(), capacity(item.id)) +
        list + "add_spacer|small|\n" +
        std::format("add_item_picker|deposit|`wPut an item in``|Choose an item to store!|\n") + vault +
        "end_dialog|mt_box|Close||\n");
}

static std::string note_list(const box &b, bool donation)
{
    std::string list{};
    for (const note &n : b.notes)
        list += donation ?
            std::format("add_label_with_icon|small|`w{}``: {} x {}{}|left|{}|\n", n.author, n.count, id_to_item(n.item).raw_name, n.text.empty() ? "" : std::format(" - `o\"{}\"``", n.text), n.item) :
            std::format("add_textbox|`w{}``: `o{}``|left|\n", n.author, n.text);
    return list;
}

void box_dialog(ENetEvent &event, ::world &world, const ::pos &pos)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const ::item &item = id_to_item(world.blocks[cord(pos.x_int(), pos.y_int())].fg);
    const box_kind kind = box_type(item);
    if (kind == box_kind::NONE) return;

    const int owner = tile_owner(world, pos);
    if (owner == 0)
        return dialog(event, header(item, pos) + "add_textbox|This only works in a locked area (World Lock or Small/Big/Huge Lock).|left|\nend_dialog|mt_box|Close||\n");

    box &b = get_or_add(world, pos);
    const bool is_owner = pPeer->user_id == owner;
    switch (kind)
    {
        case box_kind::STORAGE: case box_kind::UNTRADE: case box_kind::VAULT:
        {
            if (!is_owner) return dialog(event, header(item, pos) + "add_textbox|Only the owner of this lock can open it.|left|\nend_dialog|mt_box|Close||\n");
            if (kind == box_kind::VAULT && !b.password.empty())
                return dialog(event, header(item, pos) + "add_textbox|This Safe Vault is protected.|left|\nadd_text_input|pw_try|Password||24|\nend_dialog|mt_box|Cancel|Open|\n");
            return storage_dialog(event, world, pos, item, b, "");
        }
        case box_kind::DONATION:
        {
            if (is_owner)
                return dialog(event, header(item, pos) +
                    std::format("add_textbox|`w{}`` gifts waiting.|left|\n", b.items.size()) + note_list(b, true) +
                    (b.items.empty() ? "" : "add_button|collect|`wCollect gifts``|noflags|0|0|\n") + "end_dialog|mt_box|Close||\n");
            return dialog(event, header(item, pos) +
                "add_textbox|Give a gift to the owner of this world!|left|\n"
                "add_item_picker|donate|`wChoose a gift``|Choose an item to give!|\n"
                "end_dialog|mt_box|Close||\n");
        }
        case box_kind::MAIL:
        {
            if (is_owner)
                return dialog(event, header(item, pos) +
                    std::format("add_textbox|`w{}`` letters.|left|\n", b.notes.size()) + note_list(b, false) +
                    (b.notes.empty() ? "" : "add_button|clear|`wEmpty mailbox``|noflags|0|0|\n") + "end_dialog|mt_box|Close||\n");
            return dialog(event, header(item, pos) +
                "add_textbox|Write a private letter to the owner:|left|\n" +
                std::format("add_text_input|text||{}|{}|\n", "", MAX_TEXT) +
                "end_dialog|mt_box|Cancel|Send|\n");
        }
        case box_kind::BULLETIN:
        {
            return dialog(event, header(item, pos) +
                (b.notes.empty() ? "add_textbox|Nobody wrote anything yet.|left|\n" : note_list(b, false)) +
                "add_spacer|small|\n" +
                std::format("add_text_input|text|Write something:||{}|\n", MAX_TEXT) +
                (is_owner && !b.notes.empty() ? "add_button|clear|`wClear the board``|noflags|0|0|\n" : "") +
                "end_dialog|mt_box|Close|Post|\n");
        }
        default: return;
    }
}

/* @note puts `count` of an item in a storage box. @return false + message when it doesn't fit */
static bool store(ENetEvent &event, box &b, short box_item, short id, int count)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const ::item &item = id_to_item(id);
    const bool untradeable = item.cat & CAT_UNTRADEABLE;
    if (id == 18 || id == 32 || item.id == 0 || count <= 0 || count > owned(*pPeer, id)) return false;
    if (box_type(id_to_item(box_item)) == box_kind::UNTRADE ? !untradeable : untradeable)
    {
        on::ConsoleMessage(event.peer, box_item == 4516 ? "`4Only untradeable items go in an Untrade-a-Box.``" : "`4Untradeable items can't go in here.``");
        return false;
    }
    auto stack = std::ranges::find_if(b.items, [id, count](const ::slot &s) { return s.id == id && s.count + count <= 200; });
    if (stack == b.items.end() && b.items.size() >= capacity(box_item))
    {
        on::ConsoleMessage(event.peer, "`4This box is full!``");
        return false;
    }
    modify_item_inventory(event, ::slot{ id, static_cast<short>(-count) });
    if (stack != b.items.end()) stack->count += count;
    else b.items.emplace_back(id, static_cast<short>(count));
    return true;
}

void box_return(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    auto world = std::ranges::find(worlds, pPeer->recent_worlds.back(), &::world::name);
    if (pPeer->netid == 0 || world == worlds.end()) return;

    /* @note never trust the dialog: position inside the world, a box really there, owner rechecked on every action */
    const int x = atoi(hPipe["tilex"].c_str()), y = atoi(hPipe["tiley"].c_str());
    if (!in_world(*world, x, y)) return;
    const ::pos pos{ x, y };
    const ::item &item = id_to_item(world->blocks[cord(x, y)].fg);
    const box_kind kind = box_type(item);
    const int owner = tile_owner(*world, pos);
    if (kind == box_kind::NONE || owner == 0) return;

    box &b = get_or_add(*world, pos);
    const bool is_owner = pPeer->user_id == owner;
    const std::string clicked = hPipe["buttonClicked"];

    if (kind == box_kind::STORAGE || kind == box_kind::UNTRADE || kind == box_kind::VAULT)
    {
        if (!is_owner) return;
        std::string password = hPipe["pw"];
        if (kind == box_kind::VAULT && !b.password.empty())
        {
            if (const std::string tried = hPipe["pw_try"]; !tried.empty()) password = tried;
            if (password != b.password) return on::ConsoleMessage(event.peer, "`4Wrong password!``");
        }
        if (staff_locked(*pPeer) && (!hPipe["deposit"].empty() || !hPipe["dep_item"].empty())) return staff_locked_message(*event.peer);

        if (const std::string dep_item = hPipe["dep_item"]; !dep_item.empty())
        {
            const short id = static_cast<short>(atoi(dep_item.c_str()));
            if (store(event, b, item.id, id, atoi(hPipe["dep_count"].c_str())))
                on::ConsoleMessage(event.peer, std::format("`oStored `w{}`` {}.``", atoi(hPipe["dep_count"].c_str()), id_to_item(id).raw_name));
        }
        else if (const std::string deposit = hPipe["deposit"]; !deposit.empty())
        {
            const short id = static_cast<short>(atoi(deposit.c_str()));
            const short have = owned(*pPeer, id);
            if (have <= 0) return;
            return dialog(event, header(item, pos, password) +
                std::format("embed_data|dep_item|{}\nadd_label_with_icon|small|`w{}``|left|{}|\nadd_text_input|dep_count|How many? (you have {})|{}|3|\nend_dialog|mt_box|Cancel|Store|\n",
                    id, id_to_item(id).raw_name, id, have, have));
        }
        else if (clicked.starts_with("take_"))
        {
            const std::size_t index = atoi(clicked.substr(5).c_str());
            if (index >= b.items.size()) return;
            ::slot &stack = b.items[index];
            const int given = give(event, stack.id, stack.count);
            if (given == 0) on::ConsoleMessage(event.peer, "`4No room in your backpack!``");
            stack.count -= given;
            if (stack.count <= 0) b.items.erase(b.items.begin() + index);
        }
        else if (clicked == "set_pw" && kind == box_kind::VAULT)
        {
            b.password = clean(hPipe["new_pw"], 24);
            password = b.password;
            on::ConsoleMessage(event.peer, b.password.empty() ? "`oThe Safe Vault has no password now.``" : "`oPassword set. Don't forget it!``");
        }
        return storage_dialog(event, *world, pos, item, b, password);
    }

    if (kind == box_kind::DONATION)
    {
        if (is_owner)
        {
            if (clicked != "collect") return;
            b.notes.resize(b.items.size()); // @note one note per gift
            std::size_t kept = 0;
            for (std::size_t i = 0; i < b.items.size(); ++i)
            {
                const int given = give(event, b.items[i].id, b.items[i].count);
                b.items[i].count -= given;
                if (b.items[i].count > 0) { b.items[kept] = b.items[i]; b.notes[kept] = b.notes[i]; ++kept; }
            }
            b.items.resize(kept); b.notes.resize(kept);
            on::ConsoleMessage(event.peer, kept ? "`oYour backpack is full, some gifts are still in the box.``" : "`oYou collected all the gifts!``");
            return;
        }
        if (staff_locked(*pPeer)) return staff_locked_message(*event.peer); // @note no giving spawned items to players
        if (const std::string gift = hPipe["gift_item"]; !gift.empty())
        {
            const short id = static_cast<short>(atoi(gift.c_str()));
            const int count = atoi(hPipe["gift_count"].c_str());
            const ::item &given = id_to_item(id);
            if (id == 18 || id == 32 || given.id == 0 || count <= 0 || count > owned(*pPeer, id)) return;
            if (given.cat & CAT_UNTRADEABLE) return on::ConsoleMessage(event.peer, "`4You can't give untradeable items.``");
            if (b.items.size() >= MAX_NOTES) return on::ConsoleMessage(event.peer, "`4This donation box is full!``");

            modify_item_inventory(event, ::slot{ id, static_cast<short>(-count) });
            b.items.emplace_back(id, static_cast<short>(count));
            b.notes.emplace_back(note{ pPeer->growid, clean(hPipe["gift_note"]), id, count });
            on::ConsoleMessage(event.peer, std::format("`oYou gave `w{}`` {}. Thank you!``", count, given.raw_name));
            notify(owner, std::format("`5[`w{}`` put `w{}`` {} in your {} in `w{}```5]``", pPeer->growid, count, given.raw_name, item.raw_name, world->name));
        }
        else if (const std::string donate = hPipe["donate"]; !donate.empty())
        {
            const short id = static_cast<short>(atoi(donate.c_str()));
            const short have = owned(*pPeer, id);
            if (have <= 0) return;
            dialog(event, header(item, pos) +
                std::format("embed_data|gift_item|{}\nadd_label_with_icon|small|`w{}``|left|{}|\nadd_text_input|gift_count|How many? (you have {})|1|3|\nadd_text_input|gift_note|Note (optional)||{}|\nend_dialog|mt_box|Cancel|Give|\n",
                    id, id_to_item(id).raw_name, id, have, MAX_TEXT));
        }
        return;
    }

    /* @note Mailbox & Bulletin Board */
    if (clicked == "clear" && is_owner)
    {
        b.notes.clear();
        return on::ConsoleMessage(event.peer, std::format("`oYour {} is empty now.``", item.raw_name));
    }
    if (kind == box_kind::MAIL && is_owner) return;
    const std::string text = clean(hPipe["text"]);
    if (text.empty()) return;
    if (tape_blocks(*event.peer)) return; // @note duct tape: no writing on bulletin boards and mailboxes
    if (b.notes.size() >= MAX_NOTES)
    {
        if (kind == box_kind::MAIL) return on::ConsoleMessage(event.peer, "`4This mailbox is full!``");
        b.notes.erase(b.notes.begin()); // @note the board drops its oldest message
    }
    b.notes.emplace_back(note{ pPeer->growid, text });
    if (kind == box_kind::MAIL)
    {
        on::ConsoleMessage(event.peer, "`oYour letter was delivered.``");
        notify(owner, std::format("`5[`wYou've got mail from {} in {}!```5]``", pPeer->growid, world->name));
    }
    else box_dialog(event, *world, pos);
}

/* ---------- database ---------- */

static std::string join_items(const box &b)
{
    std::string out{};
    for (const ::slot &s : b.items) out += std::format("{}{}:{}", out.empty() ? "" : ",", s.id, s.count);
    return out;
}

static std::string join_notes(const box &b)
{
    std::string out{};
    for (const note &n : b.notes)
        out += std::format("{}{}\x1f{}\x1f{}\x1f{}", out.empty() ? "" : "\x1e", n.author, n.text, n.item, n.count);
    return out;
}

void load_boxes(::world &world)
{
    if (boxes.contains(world.name)) return; // @note already in memory (newer than the database)
    auto &list = boxes[world.name];

    ::hStmt hStmt{ "SELECT x, y, password, items, notes FROM world_box WHERE world = ?" };
    MYSQL_BIND param = make_bind_in(world.name);
    hStmt.bind_param(&param);

    signed x{}, y{};
    char password[32]{}, items[4096]{}, notes[8192]{};
    u_long password_length{}, items_length{}, notes_length{};
    MYSQL_BIND result[5] = { make_bind_out(x), make_bind_out(y) };
    const auto string_out = [](MYSQL_BIND &bind, char *buffer, u_long size, u_long &length) {
        bind.buffer_type = MYSQL_TYPE_STRING;
        bind.buffer = buffer;
        bind.buffer_length = size;
        bind.length = &length;
    };
    string_out(result[2], password, sizeof(password), password_length);
    string_out(result[3], items, sizeof(items), items_length);
    string_out(result[4], notes, sizeof(notes), notes_length);
    mysql_stmt_bind_result(hStmt.pStmt, result);
    hStmt.execute();
    mysql_stmt_store_result(hStmt.pStmt);

    while (mysql_stmt_fetch(hStmt.pStmt) == 0)
    {
        box b{ .pos = ::pos{ x, y } };
        b.password.assign(password, std::min<u_long>(password_length, sizeof(password)));

        const std::string items_text(items, std::min<u_long>(items_length, sizeof(items)));
        if (!items_text.empty())
            for (const std::string &entry : readch(items_text, ','))
                if (const std::size_t colon = entry.find(':'); colon != std::string::npos)
                    b.items.emplace_back(static_cast<short>(atoi(entry.substr(0, colon).c_str())), static_cast<short>(atoi(entry.substr(colon + 1).c_str())));

        const std::string notes_text(notes, std::min<u_long>(notes_length, sizeof(notes)));
        if (!notes_text.empty())
            for (const std::string &entry : readch(notes_text, '\x1e'))
            {
                std::vector<std::string> f = readch(entry, '\x1f');
                if (f.size() < 4) continue;
                b.notes.emplace_back(note{ f[0], f[1], atoi(f[2].c_str()), atoi(f[3].c_str()) });
            }
        list.emplace_back(std::move(b));
    }
}

void save_boxes(::world &world)
{
    {
        ::hStmt hStmt{ "DELETE FROM world_box WHERE world = ?" };
        MYSQL_BIND param = make_bind_in(world.name);
        hStmt.bind_param(&param);
        hStmt.execute();
    }
    for (const box &b : boxes[world.name])
    {
        const signed x = b.pos.x_int(), y = b.pos.y_int();
        if (!in_world(world, x, y) || box_type(id_to_item(world.blocks[cord(x, y)].fg)) == box_kind::NONE) continue; // @note the box is gone
        if (b.items.empty() && b.notes.empty() && b.password.empty()) continue;

        const std::string items = join_items(b), notes = join_notes(b);
        ::hStmt hStmt{ "INSERT INTO world_box (world, x, y, password, items, notes) VALUES (?, ?, ?, ?, ?, ?)" };
        MYSQL_BIND params[6] = { make_bind_in(world.name), make_bind_in(x), make_bind_in(y), make_bind_in(b.password), make_bind_in(items), make_bind_in(notes) };
        hStmt.bind_param(params);
        hStmt.execute();
    }
}
