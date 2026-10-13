#include "pch.hpp"
#include <deque>
#include <unordered_map>
#include "onVariant/ConsoleMessage.hpp"
#include "tools/string.hpp"

#include "area_lock.hpp"

namespace
{
    /* @note world name -> area locks. never erased, same reason as the vending machines: ::world gets copied
     * around inside the worlds vector and a destroyed copy must not wipe the live world's locks */
    std::unordered_map<std::string, std::vector<::area_lock>> locks{};

    constexpr short SMALL_LOCK = 202, BIG_LOCK = 204, HUGE_LOCK = 206, BUILDER_LOCK = 4994;
    constexpr std::size_t MAX_ACCESS = 20;
}

static int lock_size(short item)
{
    return (item == SMALL_LOCK) ? 10 : (item == BIG_LOCK) ? 48 : 200; // @note Huge & Builder's Lock
}

static void bubble(ENetEvent &event, const std::string &text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, text, 0u, 1u });
}

static bool has_access(const ::peer &p, const ::area_lock &lock)
{
    return p.user_id == lock.owner || std::ranges::find(lock.access, p.user_id, &std::pair<int, std::string>::first) != lock.access.end();
}

::area_lock *area_lock_at(::world &world, const ::pos &tile)
{
    auto it = locks.find(world.name);
    if (it == locks.end()) return nullptr;

    const u_short index = cord(tile.x_int(), tile.y_int());
    for (::area_lock &lock : it->second)
        if (lock.pos == tile || std::ranges::find(lock.tiles, index) != lock.tiles.end())
            return &lock;
    return nullptr;
}

/* @note spreads from the lock over connected tiles, nearest first, skipping tiles another lock already owns.
 * empty air is locked too (like real Growtopia) unless "Ignore empty air" is on. the main door and bedrock are never locked */
static std::vector<u_short> fill(::world &world, const ::area_lock &lock)
{
    const int width = 100, height = static_cast<int>(world.blocks.size()) / width;
    std::vector<bool> seen(world.blocks.size());
    for (const ::area_lock &other : locks[world.name])
    {
        if (other.pos == lock.pos) continue;
        seen[cord(other.pos.x_int(), other.pos.y_int())] = true;
        for (u_short tile : other.tiles) seen[tile] = true;
    }
    const int start = cord(lock.pos.x_int(), lock.pos.y_int());
    seen[start] = true;

    std::vector<u_short> tiles{};
    const std::size_t max = lock_size(lock.item);
    std::deque<int> queue{ start };
    while (!queue.empty() && tiles.size() < max)
    {
        const int at = queue.front(); queue.pop_front();
        const int x = at % width, y = at / width;
        for (auto [dx, dy] : { std::pair{0, -1}, {-1, 0}, {1, 0}, {0, 1} })
        {
            const int nx = x + dx, ny = y + dy;
            if (nx < 0 || ny < 0 || nx >= width || ny >= height) continue;
            const int next = cord(nx, ny);
            if (seen[next]) continue;
            seen[next] = true;

            const ::block &block = world.blocks[next];
            if (block.fg == 0 && block.bg == 0 && (lock.flags & AL_IGNORE_AIR)) continue;
            const ::item &item = id_to_item(block.fg);
            if (block.fg != 0 && (item.type == type::MAIN_DOOR || item.type == type::STRONG || item.type == type::LOCK)) continue;

            tiles.emplace_back(static_cast<u_short>(next));
            queue.emplace_back(next);
            if (tiles.size() >= max) break;
        }
    }
    return tiles;
}

/* @note PACKET_SEND_LOCK: the lock's item and position, then every locked tile as a 2 byte index (same layout as GTOS' apply_lock_packet).
 * owner id follows the World Lock (user id): players with access see themselves as the owner so their game lets them build */
static void send_visual(ENetPeer &peer, const ::area_lock &lock)
{
    const ::peer *p = static_cast<::peer*>(peer.data);
    if (p == nullptr) return;

    ::blob blob = compress_state(::gamePacket{
        .type = 0x0f, // @note PACKET_SEND_LOCK
        .netid = (has_access(*p, lock) || p->role >= MODERATOR) ? p->user_id : lock.owner,
        .uid = static_cast<int>(lock.tiles.size()),
        .state = state::S_EXTENDED,
        .id = lock.item,
        .punch = lock.pos,
        .size = static_cast<u_int>(lock.tiles.size() * sizeof(u_short))
    });
    for (u_short tile : lock.tiles) blob.i16(static_cast<short>(tile));

    ENetPacket *packet = enet_packet_create(blob.data().data(), blob.size(), ENET_PACKET_FLAG_RELIABLE);
    if (enet_peer_send(&peer, 0, packet)) enet_packet_destroy(packet);
}

void send_area_locks(ENetPeer &peer, ::world &world)
{
    if (auto it = locks.find(world.name); it != locks.end())
        for (const ::area_lock &lock : it->second) send_visual(peer, lock);
}

void send_area_lock_visuals(ENetEvent &event, ::world &world, const ::pos &pos)
{
    ::area_lock *lock = area_lock_at(world, pos);
    if (lock == nullptr || lock->pos != pos) return;
    peers(world.name, PEER_SAME_WORLD, [lock](ENetPeer &p) { send_visual(p, *lock); });
}

bool area_lock_allows(ENetEvent &event, ::world &world, const ::pos &tile, short action)
{
    ::area_lock *lock = area_lock_at(world, tile);
    if (lock == nullptr) return true;

    const ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (pPeer->role >= MODERATOR || pPeer->user_id == lock->owner) return true;
    if (action == 32 && tile == lock->pos) return true; // @note anyone may wrench the lock to see whose it is

    if (has_access(*pPeer, *lock))
    {
        if (action == 18 && tile == lock->pos)
        {
            bubble(event, "Only the owner can remove this lock.");
            return false;
        }
        if (lock->item == BUILDER_LOCK && (lock->flags & AL_BUILD_ONLY) && action == 18)
        {
            bubble(event, "This `$Builder's Lock`` only lets you build here.");
            return false;
        }
        if (lock->item == BUILDER_LOCK && (lock->flags & AL_BREAK_ONLY) && action != 18 && action != 32)
        {
            bubble(event, "This `$Builder's Lock`` only lets you break here.");
            return false;
        }
        return true;
    }
    if (action != 32) bubble(event, std::format("`wThat area is owned by {}``", lock->owner_name));
    return false;
}

void area_lock_place(ENetEvent &event, ::world &world, short item, const ::pos &pos)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (world.owner != 0)
        throw std::runtime_error("This world is already `$World Locked``, area locks can't be used here.");

    ::area_lock lock{ .pos = pos, .item = item, .owner = pPeer->user_id, .owner_name = pPeer->growid };
    lock.tiles = fill(world, lock);
    locks[world.name].emplace_back(std::move(lock));

    const ::area_lock &placed = locks[world.name].back();
    bubble(event, "Area locked.");
    on::ConsoleMessage(event.peer, std::format("`2{}`` locked `w{}`` tiles (max {}). Wrench it to add access or re-apply the lock.",
        id_to_item(item).raw_name, placed.tiles.size(), lock_size(item)));
}

void area_lock_removed(::world &world, const ::pos &pos)
{
    if (auto it = locks.find(world.name); it != locks.end())
        std::erase_if(it->second, [&pos](const ::area_lock &lock) { return lock.pos == pos; });
}

std::string area_lock_other_owner(::world &world, int user_id)
{
    if (auto it = locks.find(world.name); it != locks.end())
        for (const ::area_lock &lock : it->second)
            if (lock.owner != user_id) return lock.owner_name;
    return "";
}

void area_lock_dialog(ENetEvent &event, ::world &world, const ::pos &pos)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    ::area_lock *lock = area_lock_at(world, pos);
    if (lock == nullptr || lock->pos != pos) return;

    const ::item &item = id_to_item(lock->item);
    if (pPeer->user_id != lock->owner)
    {
        bubble(event, std::format("`w{}'s `${}``", lock->owner_name, item.raw_name));
        return;
    }

    std::string access_list{};
    for (const auto &[uid, name] : lock->access)
        access_list += std::format("add_checkbox|access_{}|{}|1\n", uid, name);
    if (access_list.empty()) access_list = "add_label|small|Currently, you're the only one with access.``|left\n";

    std::string builder{};
    if (lock->item == BUILDER_LOCK)
        builder = std::format(
            "add_spacer|small|\n"
            "add_label|small|`wPeople with access can:``|left\n"
            "add_checkbox|build_only|Only build|{}\n"
            "add_checkbox|break_only|Only break|{}\n",
            (lock->flags & AL_BUILD_ONLY) ? 1 : 0, (lock->flags & AL_BREAK_ONLY) ? 1 : 0);

    send_varlist(event.peer, {
        "OnDialogRequest",
        std::format(
            "set_default_color|`o\n"
            "add_label_with_icon|big|`wEdit {}``|left|{}|\n"
            "embed_data|tilex|{}\n"
            "embed_data|tiley|{}\n"
            "add_label|small|`wAccess list:``|left\n"
            "{}"
            "add_spacer|small|\n"
            "add_player_picker|playerNetID|`wAdd``|\n"
            "add_checkbox|checkbox_ignore|Ignore empty air|{}\n"
            "{}"
            "add_spacer|small|\n"
            "add_smalltext|This lock covers `w{}``/{} tiles. Re-apply it after building more around it.|left|\n"
            "add_button|recalcLock|`wRe-apply lock``|noflags|0|0|\n"
            "end_dialog|area_lock|Cancel|OK|\n",
            item.raw_name, item.id, pos.x_int(), pos.y_int(), access_list,
            (lock->flags & AL_IGNORE_AIR) ? 1 : 0, builder, lock->tiles.size(), lock_size(lock->item)
        )
    });
}

void area_lock_return(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    auto world = std::ranges::find(worlds, pPeer->recent_worlds.back(), &::world::name);
    if (world == worlds.end()) return;

    const ::pos pos{ atoi(hPipe["tilex"].c_str()), atoi(hPipe["tiley"].c_str()) };
    ::area_lock *lock = area_lock_at(*world, pos);
    if (lock == nullptr || lock->pos != pos || lock->owner != pPeer->user_id) return; // @note never trust the dialog: only the owner edits

    /* @note access: unticked names are removed, the player picker adds someone in this world */
    std::erase_if(lock->access, [&hPipe](const std::pair<int, std::string> &a) { return hPipe[std::format("access_{}", a.first)] == "0"; });
    if (const std::string picked = hPipe["playerNetID"]; !picked.empty())
    {
        const int netid = atoi(picked.c_str());
        for (ENetPeer *p : peers(world->name, PEER_SAME_WORLD))
        {
            ::peer *other = static_cast<::peer*>(p->data);
            if (other->netid != netid) continue;
            if (other->user_id == lock->owner || has_access(*other, *lock)) break;
            if (lock->access.size() >= MAX_ACCESS)
            {
                on::ConsoleMessage(event.peer, std::format("The access list is full ({} players).", MAX_ACCESS));
                break;
            }
            lock->access.emplace_back(other->user_id, other->growid);
            on::ConsoleMessage(event.peer, std::format("`w{}`` now has access to your `${}``.", other->growid, id_to_item(lock->item).raw_name));
            on::ConsoleMessage(p, std::format("`w{}`` gave you access to their `${}``.", pPeer->growid, id_to_item(lock->item).raw_name));
            break;
        }
    }

    const int old_flags = lock->flags;
    if (const std::string ignore = hPipe["checkbox_ignore"]; !ignore.empty())
        lock->flags = (ignore == "1") ? (lock->flags | AL_IGNORE_AIR) : (lock->flags & ~AL_IGNORE_AIR);
    if (lock->item == BUILDER_LOCK)
    {
        const bool build_only = hPipe["build_only"] == "1", break_only = hPipe["break_only"] == "1";
        lock->flags &= ~(AL_BUILD_ONLY | AL_BREAK_ONLY);
        if (build_only != break_only) lock->flags |= build_only ? AL_BUILD_ONLY : AL_BREAK_ONLY; // @note both ticked = both allowed
    }

    if (hPipe["buttonClicked"] == "recalcLock" || ((old_flags ^ lock->flags) & AL_IGNORE_AIR))
    {
        lock->tiles = fill(*world, *lock);
        on::ConsoleMessage(event.peer, std::format("Lock re-applied: `w{}``/{} tiles.", lock->tiles.size(), lock_size(lock->item)));
    }
    send_area_lock_visuals(event, *world, pos);
}

/* ---------- database ---------- */

static std::string join_access(const ::area_lock &lock)
{
    std::string out{};
    for (const auto &[uid, name] : lock.access) out += std::format("{}{}:{}", out.empty() ? "" : ",", uid, name);
    return out;
}

static std::string join_tiles(const ::area_lock &lock)
{
    std::string out{};
    for (u_short tile : lock.tiles) out += std::format("{}{}", out.empty() ? "" : ",", tile);
    return out;
}

void load_area_locks(::world &world)
{
    if (locks.contains(world.name)) return; // @note already in memory (newer than the database)
    auto &list = locks[world.name];

    ::hStmt hStmt{ "SELECT x, y, item, owner, owner_name, access, flags, tiles FROM world_area_lock WHERE world = ?" };
    MYSQL_BIND param = make_bind_in(world.name);
    hStmt.bind_param(&param);

    signed x{}, y{}, item{}, owner{}, flags{};
    char owner_name[32]{}, access[1024]{}, tiles[2048]{};
    u_long name_length{}, access_length{}, tiles_length{};
    MYSQL_BIND result[8] = { make_bind_out(x), make_bind_out(y), make_bind_out(item), make_bind_out(owner) };
    const auto string_out = [](MYSQL_BIND &bind, char *buffer, u_long size, u_long &length) {
        bind.buffer_type = MYSQL_TYPE_STRING;
        bind.buffer = buffer;
        bind.buffer_length = size;
        bind.length = &length;
    };
    string_out(result[4], owner_name, sizeof(owner_name), name_length);
    string_out(result[5], access, sizeof(access), access_length);
    result[6] = make_bind_out(flags);
    string_out(result[7], tiles, sizeof(tiles), tiles_length);
    mysql_stmt_bind_result(hStmt.pStmt, result);
    hStmt.execute();
    mysql_stmt_store_result(hStmt.pStmt);

    while (mysql_stmt_fetch(hStmt.pStmt) == 0)
    {
        ::area_lock lock{ .pos = ::pos{ x, y }, .item = static_cast<short>(item), .owner = owner, .flags = flags };
        lock.owner_name.assign(owner_name, std::min<u_long>(name_length, sizeof(owner_name)));

        const std::string access_text(access, std::min<u_long>(access_length, sizeof(access)));
        if (!access_text.empty())
            for (const std::string &entry : readch(access_text, ','))
                if (const std::size_t colon = entry.find(':'); colon != std::string::npos)
                    lock.access.emplace_back(atoi(entry.substr(0, colon).c_str()), entry.substr(colon + 1));

        const std::string tiles_text(tiles, std::min<u_long>(tiles_length, sizeof(tiles)));
        if (!tiles_text.empty())
            for (const std::string &tile : readch(tiles_text, ','))
                if (const int index = atoi(tile.c_str()); index >= 0 && index < static_cast<int>(world.blocks.size()))
                    lock.tiles.emplace_back(static_cast<u_short>(index));

        list.emplace_back(std::move(lock));
    }
}

void save_area_locks(::world &world)
{
    {
        ::hStmt hStmt{ "DELETE FROM world_area_lock WHERE world = ?" };
        MYSQL_BIND param = make_bind_in(world.name);
        hStmt.bind_param(&param);
        hStmt.execute();
    }
    for (const ::area_lock &lock : locks[world.name])
    {
        const signed x = lock.pos.x_int(), y = lock.pos.y_int(), item = lock.item;
        if (x < 0 || y < 0 || cord(x, y) >= static_cast<int>(world.blocks.size())) continue;
        if (world.blocks[cord(x, y)].fg != lock.item) continue; // @note the lock is gone

        const std::string access = join_access(lock), tiles = join_tiles(lock);
        ::hStmt hStmt{ "INSERT INTO world_area_lock (world, x, y, item, owner, owner_name, access, flags, tiles) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)" };
        MYSQL_BIND params[9] = {
            make_bind_in(world.name), make_bind_in(x), make_bind_in(y), make_bind_in(item), make_bind_in(lock.owner),
            make_bind_in(lock.owner_name), make_bind_in(access), make_bind_in(lock.flags), make_bind_in(tiles)
        };
        hStmt.bind_param(params);
        hStmt.execute();
    }
}
