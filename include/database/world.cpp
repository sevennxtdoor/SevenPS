#include "pch.hpp"
#include "tools/item_hunt.hpp"
#include "tools/profile.hpp"
#include "tools/random.hpp"
#include "tools/time.hpp"
#include "onVariant/ConsoleMessage.hpp"

#include "world.hpp"
#include "tools/vending.hpp"
#include "tools/system_worlds.hpp"
#include "tools/area_lock.hpp"
#include "tools/boxes.hpp"
#include "tools/magplant.hpp"
#include "tools/mannequin.hpp"
#include "tools/special_blocks.hpp"
#include "tools/special_tiles.hpp"
#include "tools/guild.hpp"

/* @note the user ids a World Lock tells the game have access: the access list, plus the allowed guild members in a guild's home world */
static std::vector<int> lock_access_ids(const ::world &world)
{
    std::vector<int> ids{};
    for (int uid : world.access) if (uid > 0) ids.push_back(uid);
    for (int uid : guild_access_ids(world)) if (std::ranges::find(ids, uid) == ids.end()) ids.push_back(uid);
    return ids;
}

char get_type(const ::item &item)
{
    switch (item.type)
    {
        case type::MAIN_DOOR: case type::DOOR: case type::PORTAL: return '\x01';
        case type::SIGN: return '\x02';
        case type::LOCK: return '\x03';
        case type::SEED: return '\x04';
        case type::RANDOM: return '\x08';
        case type::PROVIDER: return '\x09';
        case type::DISPLAY_BLOCK: return '\x17';
        case type::VENDING_MACHINE: return '\x18';
        case type::MAGPLANT: return '\x3e'; // @note Magplant 5000, Gaia's Beacon, Unstable Tesseract
        case type::MANNEQUIN: return '\x0e';
    }
    return static_cast<char>(special_extra_type(item)); // @note crystals, bunny egg, mailbox... (tools/special_tiles.cpp)
}

void block::reset()
{
    this->fg = 0;
    this->bg = 0;
    for (u_char &s : this->state) s = 0;
}

::blob block::to_blob() const
{
    blob blob;
    blob.i16(this->fg);
    blob.i16(this->bg);
    blob.u8(this->state[0]);
    blob.u8(this->state[1]);
    blob.u8(this->state[2]);
    blob.u8(this->state[3]);

    return blob;
}

::blob object::to_blob() const
{
    blob blob;
    blob.i16(this->id);
    blob.f32(this->pos.x);
    blob.f32(this->pos.y);
    blob.i16(this->count);
    blob.i32(this->uid);

    return blob;
}

::blob door::to_blob() const
{
    blob blob;
    blob.i16(this->label.length());
    for (char c : this->label) blob.u8(c);
    blob.u8('\0');

    return blob;
}

::blob sign::to_blob() const
{
    blob blob;
    blob.i16(this->label.length());
    for (char c : this->label) blob.u8(c);
    blob.u32(this->idk);

    return blob;
}

::blob tree::to_blob(bool seconds) const
{
    blob blob;
    blob.i32((seconds) ? ticks() - this->tick : this->tick);
    blob.u8(this->fruit);

    return blob;
}

/* @note a door/sign/seed tile ALWAYS has its extra data, even when we have no entry for it (placed by a machine, an old
 * save...): the loader and the game read it anyway, so a missing one shifted everything after it - the rest of the
 * world loaded as garbage, and the game failed to enter the world */
static void door_extra(::blob &blob, const ::world &world, const ::pos &pos)
{
    auto door = std::ranges::find(world.doors, pos, &::door::pos);
    blob.push_back(door != world.doors.end() ? door->to_blob() : ::door("", "", "", pos).to_blob());
}
static void sign_extra(::blob &blob, const ::world &world, const ::pos &pos)
{
    auto sign = std::ranges::find(world.signs, pos, &::sign::pos);
    blob.push_back(sign != world.signs.end() ? sign->to_blob() : ::sign("", pos).to_blob());
}
static void tree_extra(::blob &blob, const ::world &world, const ::pos &pos, bool seconds)
{
    auto tree = std::ranges::find(world.trees, pos, &::tree::pos);
    blob.push_back(tree != world.trees.end() ? tree->to_blob(seconds) : ::tree(ticks(), 1, pos).to_blob(seconds));
}

::blob world::serialize()
{
    blob blob;
    blob.i16(0x00); // @todo my rgt world says: 19 00
    blob.i32(0x00); // @todo my rgt world says: 40 00 00 00
    blob.i16(this->name.length());
    for (char c : this->name) blob.u8(c);

    const int y = this->blocks.size() / 100;
    const int x = this->blocks.size() / y;
    blob.i32(x);
    blob.i32(y);
    blob.i16(this->blocks.size());

    /*@todo*/
    blob.i32(0x00);
    blob.i16(0x00);
    blob.u8(0x00);

    for (u_short i = 0; const ::block &block : this->blocks)
    {
        blob.push_back(block.to_blob());

        if (block.fg != 0 || block.fg!=2||block.fg!=4||block.fg!=8||block.fg!=14) // @note so we can save time
        if (char type = get_type(id_to_item(block.fg)); type > '\x00')
        {
            blob.u8(type);

            const ::pos block_pos{i % x, i / x};
            if (type == '\x01'/*doors, portal*/)
            {
                if (block.fg == 6/*Main Door*/) this->spawn = block_pos.by_32(false);
                door_extra(blob, *this, block_pos);
            }
            else if (type == '\x02'/*sign*/)
            {
                sign_extra(blob, *this, block_pos);
            }
            else if (type == '\x03'/*lock*/)
            {
                if (!is_tile_lock(block.fg)) this->is_public = (block.state[2] & S_PUBLIC); // @note check if world lock has S_PUBLIC flag, i will change this later
                const std::vector<int> access = is_tile_lock(block.fg) ? std::vector<int>{} : lock_access_ids(*this);

                const ::area_lock *area = is_tile_lock(block.fg) ? area_lock_at(*this, block_pos) : nullptr;
                blob.u8(area ? 0x00 : this->lock_state);
                blob.i32(is_tile_lock(block.fg) ? (area ? area->owner : 0) : this->owner); // @note an area lock shows its own owner
                blob.i32(static_cast<int>(access.size()));
                for (int uid : access) blob.i32(uid); // @note the client reads {count} user ids
                if (block.fg == GUILD_LOCK) for (int i = 0; i < 4; ++i) blob.i32(0); // @note Guild Lock: 16 more bytes (gtworld-r), unknown, zeros
            }
            else if (type == '\x04'/*seed*/)
            {
                tree_extra(blob, *this, block_pos, true);
            }
            else if (type == '\x17'/*display block*/)
            {
                /* @note the client always reads the displayed item id here, even 0 - without it the rest of the world is misread */
                auto display = std::ranges::find(this->displays, block_pos, &::display::pos);
                blob.u32(display != this->displays.end() ? display->id : 0);
            }
            else if (type == '\x18'/*vending machine*/)
            {
                vending_extra(blob, *this, block_pos);
            }
            else if (type == '\x3e'/*magplant*/)
            {
                magplant_extra(blob, *this, block_pos);
            }
            else if (type == '\x0e'/*mannequin*/)
            {
                mannequin_extra(blob, *this, block_pos);
            }
            else special_extra(blob, *this, block_pos, static_cast<u_char>(type)); // @note dice, providers, crystals, bunny egg...
        }
        ++i;
    }
    /*@todo*/
    blob.i32(0x00);
    blob.i32(0x00);
    blob.i32(0x00);

    blob.i32(this->objects.size()); // @note number of drops
    blob.i32(this->last_object_uid);
    for (const ::object &object : this->objects) 
    {
        blob.push_back(object.to_blob());
    }
    return blob;
}

bool world::exists(const std::string& name)
{
    ::hStmt hStmt{ "SELECT 1 FROM world WHERE name = ? LIMIT 1" };

    MYSQL_BIND param = make_bind_in(name); // WHERE
    hStmt.bind_param(&param);
    hStmt.execute();

    return (!mysql_stmt_store_result(hStmt.pStmt) && mysql_stmt_num_rows(hStmt.pStmt) > 0);
}

template<typename T>
void world::mysql_insert(const std::string& column, const T& value)
{
    ::hStmt hStmt{ std::format("INSERT INTO world ({}) VALUES (?)", column).c_str() };

    MYSQL_BIND param = make_bind_in(value); // VALUES
    hStmt.bind_param(&param);
    hStmt.execute();
}
template void world::mysql_insert<signed>(const std::string&, const signed&);
template void world::mysql_insert<unsigned>(const std::string&, const unsigned&);
template void world::mysql_insert<float>(const std::string&, const float&);
template void world::mysql_insert<std::string>(const std::string&, const std::string&);
template void world::mysql_insert<::blob>(const std::string&, const ::blob&);

template<typename T>
void world::mysql_update(const std::string& column, const T& value)
{
    ::hStmt hStmt{ std::format("UPDATE world SET {} = ? WHERE name = ?", column).c_str() };

    MYSQL_BIND params[2] = {
        make_bind_in(value),      // SET
        make_bind_in(this->name) // WHERE
    };
    hStmt.bind_param(params);
    hStmt.execute();
}
template void world::mysql_update<signed>(const std::string&, const signed&);
template void world::mysql_update<unsigned>(const std::string&, const unsigned&);
template void world::mysql_update<float>(const std::string&, const float&);
template void world::mysql_update<std::string>(const std::string&, const std::string&);
template void world::mysql_update<::blob>(const std::string&, const ::blob&);

template<typename T>
T world::mysql_select(const std::string &column, const std::string &arg)
{
    T value{};
    ::hStmt hStmt{ std::format("SELECT {}({}) FROM world WHERE name = ? LIMIT 1", arg, column).c_str() };

    MYSQL_BIND param = make_bind_in(this->name); // WHERE
    hStmt.bind_param(&param);
    hStmt.execute();

    u_long length = 0;
    MYSQL_BIND result = make_bind_out(value);
    result.length = &length;
    mysql_stmt_bind_result(hStmt.pStmt, &result);

    hStmt.execute();
    hStmt.fetch();
    if constexpr (std::is_same_v<T, std::string>)
        value.resize(length);
    else if constexpr (std::is_same_v<T, ::blob>)
        value.resize(length);
    else if constexpr (std::is_same_v<T, std::vector<u_char>>)
        value.resize(std::min<std::size_t>(length, value.size())); // @note else the unused buffer was read as ~3750 empty drops

    return value;
}

void world::mysql_select_all()
{
    this->name = this->mysql_select<std::string>("name");
    this->owner = this->mysql_select<int>("owner");
    {
        this->access.fill(0);
        const std::string list = this->mysql_select<std::string>("access"); // @note "12,34,56" (user ids)
        std::size_t n{};
        for (std::size_t start = 0; start < list.size() && n < this->access.size();)
        {
            const std::size_t comma = std::min(list.find(',', start), list.size());
            if (const int uid = atoi(list.substr(start, comma - start).c_str()); uid > 0) this->access[n++] = uid;
            start = comma + 1;
        }
        this->minimum_entry_level = static_cast<u_char>(std::clamp(this->mysql_select<int>("min_level"), 1, 125));
        this->lock_state = static_cast<u_char>(this->mysql_select<int>("lock_state"));
    }
    {
        this->trees.clear();
        ::blob blob = this->mysql_select<::blob>("blocks");
        if (blob.size() < cord(0, 60) * 8) // @note never saved (e.g. the server crashed), the caller generates it again
        {
            this->blocks.clear();
            return;
        }
        this->blocks.resize(cord(0, 60));

        const int x = this->blocks.size() / 60;
        int pos{};
        for (u_short i = 0; ::block &block : this->blocks)
        {
            blob.read_i16(block.fg, pos);
            blob.read_i16(block.bg, pos);
            blob.read_u8(block.state[0], pos);
            blob.read_u8(block.state[1], pos);
            blob.read_u8(block.state[2], pos);
            blob.read_u8(block.state[3], pos);

            if (block.fg != 0 || block.fg!=2||block.fg!=4||block.fg!=8||block.fg!=14) // @note so we can save time
            if (char type = get_type(id_to_item(block.fg)); type > '\x00')
            {
                const ::pos block_pos{i % x, i / x};
                if (type == '\x01'/*doors, portal*/)
                {
                    ::door &door = this->doors.emplace_back("","","", block_pos);

                    blob.read_string(door.label, pos);
                    pos++;// @note \0
                }
                else if (type == '\x02'/*sign*/)
                {
                    ::sign &sign = this->signs.emplace_back("", block_pos);

                    blob.read_string(sign.label, pos);
                    blob.read_u32(sign.idk, pos);
                }
                else if (type == '\x04'/*seed*/)
                {
                    ::tree &tree = this->trees.emplace_back(0, 0, block_pos);

                    blob.read_u32(tree.tick, pos);
                    blob.read_u8(tree.fruit, pos);
                }
            }
            ++i;
        }
    } // @note delete blob, i
    {
        ::blob blob = this->mysql_select<::blob>("objects"); // @note ::blob is cut to the real length, std::vector<u_char> stays at the full buffer size

        const u_char *u8 = blob.data().data(); // @note i did not have the brain capacity to reinterpret it. t-t (memcpy is safer anyways...)
        int i{};

        if (blob.size() >= sizeof(u_int)) memcpy(&this->last_object_uid, u8, sizeof(u_int));
        i += sizeof(u_int); // @todo real gt has this as 8 bits not just 4.

        objects.resize(blob.size() >= sizeof(u_int) ? (blob.size() - sizeof(u_int)) / 16 : 0); // @note count from the saved data, not the last drop uid. an empty save gave size - 4 = a huge number
        for (::object &object : this->objects)
        {
            memcpy(&object.id,    u8 + i, sizeof(u_short)); i += sizeof(u_short);
            memcpy(&object.pos.x, u8 + i, sizeof(float));   i += sizeof(float);
            memcpy(&object.pos.y, u8 + i, sizeof(float));   i += sizeof(float);
            memcpy(&object.count, u8 + i, sizeof(u_short)); i += sizeof(u_short);
            memcpy(&object.uid,   u8 + i, sizeof(u_int));   i += sizeof(u_int);
        }
        std::erase_if(this->objects, [](const ::object &o) { return o.id == 0 || o.count == 0; }); // @note empty drops saved by the old bug
    } // @note delete blob, i
}

world::world(const std::string &name) : name(name)/*DEFAULT*/
{
    if (this->exists(this->name)) 
    {
        this->mysql_select_all();
        if (this->blocks.empty()) { if (!build_system_world(*this)) generate_world(*this); } // @note broken/empty save
        else if (is_system_world(this->name) && this->signs.empty()) build_system_world(*this); // @note an old plain START gets its design (it has no signs yet)
        refresh_system_signs(*this); // @note START's "Get started" sign points to /tutorial
        guild_lock_fix(*this); // @note a Guild Lock tile crashes the game: back to the lock it replaced
    }
    else 
    {
        this->mysql_insert("name", this->name); // @note DEFAULT
        if (!build_system_world(*this)) generate_world(*this); // @note START & PARKOUR come with a design
    }
    if (!this->name.empty()) load_vendings(*this), load_doors(*this), load_area_locks(*this), load_boxes(*this), load_magplants(*this), load_mannequins(*this), load_fossil_preps(*this), load_special_tiles(*this), fossil_spawn(*this);
}
world::~world()
{
    this->save();
}

void world::save()
{
    if (this->name.empty() || this->blocks.empty()) return; // @note an empty (moved-from) copy has nothing to save
    this->mysql_update("owner", this->owner);
    {
        std::string list{};
        for (int uid : this->access) if (uid > 0) list += std::format("{}{}", list.empty() ? "" : ",", uid);
        this->mysql_update("access", list);
        this->mysql_update("min_level", static_cast<signed>(this->minimum_entry_level));
        this->mysql_update("lock_state", static_cast<signed>(this->lock_state));
    }
    if (!this->name.empty()) save_vendings(*this), save_doors(*this), save_area_locks(*this), save_boxes(*this), save_magplants(*this), save_mannequins(*this), save_fossil_preps(*this), save_special_tiles(*this);
    {
        ::blob blob;

        const int x = this->blocks.size() / 60;
        for (u_short i = 0; const ::block &block : this->blocks)
        {
            blob.push_back(block.to_blob());

            if (block.fg != 0) // @note so we can save time
            if (u_char type = get_type(id_to_item(block.fg)); type > 0x00)
            {
                const ::pos block_pos{i % x, i / x};
                if (type == '\x01'/*doors, portal*/) door_extra(blob, *this, block_pos);
                else if (type == '\x02'/*sign*/) sign_extra(blob, *this, block_pos);
                else if (type == '\x04'/*seed*/) tree_extra(blob, *this, block_pos, false);
            }
            ++i;
        }
        this->mysql_update("blocks", blob);
    }
    {
        ::blob blob{};

        blob.i32(this->last_object_uid); // @todo add the other 4 bits like real growtopia
        for (::object &object : this->objects)
        {
            if (is_hunt_drop(this->name, object.uid)) continue; // @note Item Hunt chests only live in memory
            blob.push_back(object.to_blob());
        }
        this->mysql_update("objects", blob.data());
    }
}

std::vector<world> worlds;

void send_action(ENetPeer& p, const std::string &action, const std::string &str) 
{
    const std::string &fmt_action = std::format("action|{}\n", action);
    std::vector<u_char> data(sizeof(int) + fmt_action.length() + str.length(), 0x00);
    
    data[0] = 03; // @note NET_MESSAGE_GAME_MESSAGE
    {
        const u_char *i8 = reinterpret_cast<const u_char*>(fmt_action.c_str());
        for (std::size_t i = 0ull; i < fmt_action.length(); ++i)
            data[sizeof(int) + i] = i8[i];
    }
    if (!str.empty())
    {
        const u_char *i8 = reinterpret_cast<const u_char*>(str.c_str());
        for (std::size_t i = 0ull; i < str.length(); ++i)
            data[sizeof(int) + fmt_action.length() + i] = i8[i];
    }
    
    ENetPacket *packet = enet_packet_create(data.data(), data.size(), ENET_PACKET_FLAG_RELIABLE);
    if (send_packet(&p, packet)) enet_packet_destroy(packet);

}

void send_data(ENetPeer &peer, const ::blob &blob)
{
    ENetPacket *packet = enet_packet_create(blob.data().data(), blob.size(), ENET_PACKET_FLAG_RELIABLE);
    if (packet == nullptr || packet->dataLength < sizeof(::gamePacket)) return;

    if (send_packet(&peer, packet)) enet_packet_destroy(packet);
}

void state_visuals(ENetPeer &peer, ::gamePacket &&gamePacket) 
{
    ::peer *pPeer = static_cast<::peer*>(peer.data);

    peers(pPeer->recent_worlds.back(), PEER_SAME_WORLD, [&](ENetPeer &p) 
    {
        send_data(p, compress_state(gamePacket));
    });
}

void tile_apply_damage(ENetEvent &event, ::gamePacket gamePacket, block &block, u_int value)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    (block.fg == 0) ? ++block.hits[1] : ++block.hits[0];
    gamePacket.type = (value << 24) | 0x000008; // @note 0x{}000008
    gamePacket.id = 6; // @note idk exactly
    gamePacket.netid = pPeer->netid;
	state_visuals(*event.peer, std::move(gamePacket));
}

u_short modify_item_inventory(ENetEvent &event, ::slot slot)
{   
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    ::gamePacket gamePacket{.id = slot.id};
    if (slot.count < 0) gamePacket.type = (slot.count*-1 << 16) | 0x000d; // @noote 0x00{}000d
    else                gamePacket.type = (slot.count    << 24) | 0x000d; // @noote 0x{}00000d
    send_data(*event.peer, compress_state(gamePacket)); // @note only the player whose backpack changes

    return pPeer->emplace(::slot(slot.id, slot.count));
}

void item_change_object(ENetEvent &event, ::gamePacket gamePacket) 
{
    gamePacket.type = 0x0e; // @note PACKET_ITEM_CHANGE_OBJECT

    state_visuals(*event.peer, std::move(gamePacket));
}

void merge_object(ENetEvent &event, ::slot slot, const ::pos &pos, ::world &world)
{
    auto object = std::ranges::find_if(world.objects, [&](const ::object &object) {
        return object.id == slot.id && (object.pos.by_32(true) == pos.by_32(true));
    });
    /* @todo avoid surpassing 200 and call add_object() for the remaining amount */ // @note future self reference peer::emplace()...
    object->count += slot.count;

    item_change_object(event, ::gamePacket{
        .netid = (int)0xfffffffd,
        .uid   = (int)object->uid,
        .count = static_cast<float>(object->count),
        .id    = object->id,
        .pos   = object->pos
    });
}

void remove_object(ENetEvent& event, signed uid)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    item_change_object(event, ::gamePacket{
        .netid = pPeer->netid,
        .uid   = (int)0xffffffff,
        .id    = uid
    });
}

int add_object(ENetEvent& event, ::slot slot, const ::pos& pos, ::world &world)
{
    /* @todo got a little messy */
    auto object = std::ranges::find_if(world.objects, [&](const ::object &object) {
        return object.id == slot.id && (object.pos.by_32(true) == pos.by_32(true));
    });
    if (object != world.objects.end() && object->count < 200/*@todo*/) 
    {
        merge_object(event, slot, pos, world);
        return object->uid;
    }
    ::object it = world.objects.emplace_back(::object(slot.id, slot.count, pos, ++world.last_object_uid)); // @note a iterator ahead of time

    item_change_object(event, ::gamePacket{
        .netid = (int)0xffffffff,
        .uid   = (int)it.uid,
        .count = static_cast<float>(slot.count),
        .id    = it.id,
        .pos   = pos
    });
    return it.uid;
}

void add_drop(ENetEvent &event, ::slot im, ::pos pos, ::world &world) // @todo
{
    add_object(event, im, ::pos{
        pos.x + RandomRange(0, 16),
        pos.y + RandomRange(0, 16)
    }, world);
}

void send_tile_update(ENetEvent &event, ::gamePacket gamePacket, ::block &block, ::world &world) 
{
    gamePacket.type = 05; // @note PACKET_SEND_TILE_UPDATE_DATA
    gamePacket.state = state::S_EXTENDED;
    ::blob blob = compress_state(gamePacket);

    blob.push_back(block.to_blob());

    const ::item &item = id_to_item(block.fg);
    blob.u8(get_type(id_to_item(block.fg)));
    switch (item.type)
    {
        case type::DOOR: case type::PORTAL: case type::MAIN_DOOR: // @note all three are a door to the game (get_type 0x01): portals sent no label
        {
            door_extra(blob, world, gamePacket.punch);
            break;
        }
        case type::SIGN:
        {
            sign_extra(blob, world, gamePacket.punch);
            break;
        }
        case type::LOCK:
        {
            if (!is_tile_lock(block.fg)) world.is_public = (block.state[2] & S_PUBLIC); // @note check if world lock has S_PUBLIC flag, i will change this later
            const std::vector<int> access = is_tile_lock(block.fg) ? std::vector<int>{} : lock_access_ids(world);

            const ::area_lock *area = is_tile_lock(block.fg) ? area_lock_at(world, gamePacket.punch) : nullptr;
            blob.u8(area ? 0x00 : world.lock_state);
            blob.i32(is_tile_lock(block.fg) ? (area ? area->owner : 0) : world.owner); // @note an area lock shows its own owner
            blob.i32(static_cast<int>(access.size()));
            for (int uid : access) blob.i32(uid); // @note the client reads {count} user ids
            if (block.fg == GUILD_LOCK) for (int i = 0; i < 4; ++i) blob.i32(0); // @note Guild Lock: 16 more bytes (gtworld-r), unknown, zeros
            break;
        }
        case type::SEED:
        {
            tree_extra(blob, world, gamePacket.punch, true);
            break;
        }
        case type::DISPLAY_BLOCK:
        {
            auto display = std::ranges::find(world.displays, gamePacket.punch, &::display::pos);
            blob.u32(display != world.displays.end() ? display->id : 0);
            break;
        }
        case type::VENDING_MACHINE:
        {
            vending_extra(blob, world, gamePacket.punch);
            break;
        }
        case type::MAGPLANT:
        {
            magplant_extra(blob, world, gamePacket.punch);
            break;
        }
        case type::MANNEQUIN:
        {
            mannequin_extra(blob, world, gamePacket.punch);
            break;
        }
        default: special_extra(blob, world, gamePacket.punch, static_cast<u_char>(get_type(item))); break;
    }
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    peers(pPeer->recent_worlds.back(), PEER_SAME_WORLD, [&](ENetPeer& p) 
    {
        send_data(p, blob);
    });
}

void send_particle_effect(ENetEvent &event, const ::pos& pos, ::pos speed, int id, float offset)
{
    state_visuals(*event.peer, ::gamePacket{
        .type = 0x11, // @note PACKET_SEND_PARTICLE_EFFECT
        .netid = id, // @todo figure out if this is correct, i just assumed from firework visuals
        .id = id,
        .pos = pos,
        .speed = speed,
        .idk = offset
    });
}

void remove_fire(ENetEvent &event, gamePacket gamePacket, ::block &block, ::world &world)
{
    send_particle_effect(event, gamePacket.punch.by_32(), {0x00, 0x95});

    block.state[3] &= ~S_FIRE;
    send_tile_update(event, gamePacket, block, world);

    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    add_stat(event, "fires");
    if (++pPeer->fires_removed % 100 == 0) 
    {
        on::ConsoleMessage(event.peer, "`oI'm so good at fighting fires, I rescused this `2Highly Combustible Box``!");
        modify_item_inventory(event, {3090/*Combustible Box*/, 1});
    }
    pPeer->add_xp(event, 1);
}

void fireworks(ENetEvent &event, const ::pos &pos)
{
    int type  [3]{ RandomRange(0x25, 0x28), RandomRange(0x25, 0x28), RandomRange(0x25, 0x28) };
    int offset[3]{ RandomRange(260, 2200), RandomRange(260, 2200), RandomRange(260, 2200) };

    send_particle_effect(event, pos, {0xb3, type[0]}, 0xc8*0, offset[0]);
    send_particle_effect(event, pos, {0xbe, type[1]}, 0xc8*1, offset[1]);
    send_particle_effect(event, pos, {0x7c, type[2]}, 0xc8*2, offset[2]);
}

void generate_world(::world &world)
{
    u_short main_door = RandomRange(2, cord(0, 60) / 100 - 4);
    std::vector<::block> blocks(cord(0, 60), ::block{0, 0});
    const int x = blocks.size() / 60;
    
    for (int i = 0ull; i < blocks.size(); ++i)
    {
        ::block &block = blocks[i];
        if (i >= cord(0, 37))
        {
            block.bg = 14; // @note cave background
            if (i >= cord(0, 38) && i < cord(0, 50) /* (above) lava level */ && RandomRange(0, 38) <= 1) block.fg = 10; // rock
            else if (i > cord(0, 50) && i < cord(0, 54) /* (above) bedrock level */ && RandomRange(0, 8) < 3) block.fg = 4; // lava
            else block.fg = (i >= cord(0, 54)) ? 8 : 2;
        }
        if (i == cord(main_door, 36))
        {
            block.fg = 6;
            world.doors.emplace_back(::door("EXIT","","", ::pos(i % x, i / x))); // @todo seems a bit hardcoded.
        }
        else if (i == cord(main_door, 37)) block.fg = 8; // @note bedrock (below main door)
    }
    world.blocks = std::move(blocks);
}

bool door_mover(::world &world, const ::pos &pos)
{
    std::vector<::block> &blocks = world.blocks;
    const int rows = static_cast<int>(blocks.size()) / 100;
    if (pos.x < 0 || pos.x >= 100 || pos.y < 0 || pos.y + 1 >= rows) return false; // @note the bedrock goes below: on the last row it was written past the world

    if (blocks[cord(pos.x, pos.y)].fg != 0 ||
        blocks[cord(pos.x, (pos.y + 1))].fg != 0) return false;

    for (std::size_t i = 0ull; i < blocks.size(); ++i)
    {
        if (blocks[i].fg == 6/*Main Door*/)
        {
            blocks[i].fg = 0; // @note remove main door
            if (static_cast<int>(i / 100) + 1 < rows) blocks[cord(i % 100, (i / 100 + 1))].fg = 0; // @note remove bedrock below
            break;
        }
    }
    blocks[cord(pos.x, pos.y)].fg = 6;
    blocks[cord(pos.x, (pos.y + 1))].fg = 8;
    return true;
}

void blast::thermonuclear(::world &world)
{
    for (::block &block : world.blocks)
    {
        if (block.fg == 6/*main door*/ || block.fg == 8/*bedrock*/) continue;

        block.reset();
    }
}
