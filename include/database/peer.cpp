#include "pch.hpp"
#include "tools/profile.hpp"
#include "tools/cps.hpp"
#include "tools/rank.hpp"
#include "items.hpp"
#include "world.hpp"
#include "onVariant/SetClothing.hpp"
#include "onVariant/CountryState.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "commands/punch.hpp"
#include "action/quit_to_exit.hpp"
#include "tools/string.hpp"

#include "peer.hpp"

bool peer::exists(const std::string &growid)
{
    ::hStmt hStmt{ "SELECT 1 FROM peer WHERE growid = ? LIMIT 1" };

    MYSQL_BIND param = make_bind_in(growid); // WHERE
    hStmt.bind_param(&param);
    hStmt.execute();

    return (!mysql_stmt_store_result(hStmt.pStmt) && mysql_stmt_num_rows(hStmt.pStmt) > 0);
}

::blob slot::to_blob() const
{
    ::blob blob;
    blob.i16(this->id);
    blob.i16(this->count);

    return blob;
}

template<typename T>
void peer::mysql_insert(const std::string &column, const T &value)
{
    ::hStmt hStmt{ std::format("INSERT INTO peer ({}) VALUES (?)", column).c_str() };

    MYSQL_BIND param = make_bind_in(value); // VALUES
    hStmt.bind_param(&param);
    hStmt.execute();
}
template void peer::mysql_insert<signed>(const std::string&, const signed&);
template void peer::mysql_insert<unsigned>(const std::string&, const unsigned&);
template void peer::mysql_insert<float>(const std::string&, const float&);
template void peer::mysql_insert<std::string>(const std::string&, const std::string&);

template<typename T>
void peer::mysql_update(const std::string &column, const T &value)
{
    ::hStmt hStmt{ std::format("UPDATE peer SET {} = ? WHERE growid = ?", column).c_str() };

    MYSQL_BIND params[2] = {
        make_bind_in(value),       // SET
        make_bind_in(this->growid) // WHERE
    };
    hStmt.bind_param(params);
    hStmt.execute();
}
template void peer::mysql_update<signed>(const std::string&, const signed&);
template void peer::mysql_update<unsigned>(const std::string&, const unsigned&);
template void peer::mysql_update<float>(const std::string&, const float&);
template void peer::mysql_update<std::string>(const std::string&, const std::string&);

template<typename T>
T peer::mysql_select(const std::string &column, const std::string &arg)
{
    T value{};
    ::hStmt hStmt{ std::format("SELECT {}({}) FROM peer WHERE growid = ? LIMIT 1", arg, column).c_str() };

    MYSQL_BIND param = make_bind_in(this->growid); // WHERE
    hStmt.bind_param(&param);

    u_long length = 0;
    MYSQL_BIND result = make_bind_out(value);
    result.length = &length;
    mysql_stmt_bind_result(hStmt.pStmt, &result);

    hStmt.execute();
    hStmt.fetch();
    if constexpr (std::is_same_v<T, std::string>)
        value.resize(length);

    return value;
}
/* since we will only select during mysql_select_all */ // @note add templates here if use select outside of this file.

void peer::mysql_select_all()
{
    this->user_id    = this->mysql_select<signed>("uid");
    this->growid     = this->mysql_select<std::string>("growid");
    this->password   = this->mysql_select<std::string>("password");
    this->created_at = this->mysql_select<std::time_t>("created_at", "UNIX_TIMESTAMP");
    this->role       = static_cast<u_char>(this->mysql_select<signed>("role"));
    this->banned     = this->mysql_select<signed>("banned") != 0;
    this->titles     = this->mysql_select<signed>("titles");
    this->titles_on  = this->mysql_select<signed>("titles_on");
    this->gems       = this->mysql_select<signed>("gems");
    this->surgery_skill = this->mysql_select<signed>("surgery_skill");
    this->malpractice   = this->mysql_select<signed>("malpractice");
    this->notebook      = this->mysql_select<std::string>("notebook");
    this->wl_bank       = this->mysql_select<signed>("wl_bank");
    this->playtime      = this->mysql_select<signed>("playtime");
    this->login_time    = std::time(nullptr);
    profile_load(*this, this->mysql_select<std::string>("profile"));
    this->level.front() = static_cast<u_short>(std::clamp(this->mysql_select<signed>("level"), 1, 125));
    this->level.back()  = static_cast<u_short>(std::max(this->mysql_select<signed>("xp"), 0));
    this->rank_level    = this->mysql_select<signed>("rank_level");

    auto blob = this->mysql_select<std::vector<u_char>>("inventory");
    const u_char *u8 = blob.data();

    int pos{};
    memcpy(&this->slot_size, u8 + pos, sizeof(int)); pos += sizeof(int);
    short size{};
    memcpy(&size, u8 + pos, sizeof(short)); pos += sizeof(short);
    this->slots.resize(size);
    for (::slot &slot : this->slots)
    {
        memcpy(&slot.id,    u8 + pos, sizeof(short)); pos += sizeof(short);
        memcpy(&slot.count, u8 + pos, sizeof(short)); pos += sizeof(short);
    }

    /* @note equipped clothing: one u_short id per clothing:: slot. NULL column reads as all zeros */
    auto cloth_blob = this->mysql_select<std::vector<u_char>>("clothing");
    for (std::size_t i = 0; i < this->clothing.size(); ++i)
    {
        u_short id{};
        memcpy(&id, cloth_blob.data() + i * sizeof(u_short), sizeof(u_short));
        if (id != 0 && std::ranges::find(this->slots, (short)id, &::slot::id) != this->slots.end()) // @note only re-equip what is still in the backpack
            this->clothing[i] = id;
    }
    this->update_effects(); // @note restore punch effect & double jump
}

::blob peer::serialize_clothing() const
{
    ::blob blob{};
    for (float cloth : this->clothing)
        blob.i16(static_cast<u_short>(cloth));
    return blob;
}

int peer::owned_titles() const
{
    if (const ::rank *r = get_rank(*this); r && r->titles) return T_ALL; // @note rank perk
    return (this->role >= MODERATOR) ? T_ALL : this->titles; // @note super supporter perk
}

std::string peer::name_as(std::string_view color) const
{
    std::string_view tag{};
    if (this->nick.empty()) // @note /nick = undercover: hide the @ tag and role color, but keep titles
        switch (this->role)
        {
            case OWNER:     color = "`9"; tag = "@"; break;
            case DEVELOPER: color = "`6"; tag = "@"; break;
            case MODERATOR: color = "`#"; tag = "@"; break;
        }
    const int shown = this->titles_on & this->owned_titles();
    if (tag.empty() && (shown & T_DOCTOR)) color = "`4";
    std::string rank_tag{};
    if (const ::rank *r = get_rank(*this); r && tag.empty() && this->nick.empty()) // @note staff show their @ tag instead
    {
        color = r->color;
        rank_tag = std::format("[{}] ", r->name);
    } // @note like real growtopia, the Dr. title turns the name red (staff keep their role color)
    /* @note Blue Name is not a color code: on::CountryState sends |maxLevel and the client draws it blue */

    return std::format("{}{}{}{}{}{}``", color, rank_tag, tag,
        (shown & T_DOCTOR) ? "Dr. " : "",
        this->nick.empty() ? this->growid : this->nick,
        (shown & T_LEGEND) ? " of Legend" : "");
}

::blob peer::serialize_inventory() const
{
    ::blob blob{};
    blob.i32(this->slot_size);
    blob.i16(this->slots.size());
    for (const ::slot &slot : this->slots)
    {
        blob.push_back(slot.to_blob());
    }
    return blob;
}

void peer::load(const std::string &growid, const std::string &password)
{
    if (!this->exists(growid)) 
    {
        this->mysql_insert("growid", growid);
        this->mysql_update("password", password);

        this->slots.resize(3ull); // @note since it's pre-determined we don't need do peer::emplace, and less iteration
        this->slots[0ull] = ::slot{18, 1};   // @note Fist
        this->slots[1ull] = ::slot{32, 1};   // @note Wrench
        this->slots[2ull] = ::slot{9640, 1}; // @note My First World Lock
        this->mysql_update<std::vector<u_char>>("inventory", this->serialize_inventory().data());
    }
    this->mysql_select_all();
}

peer::~peer()
{
    this->mysql_update<std::vector<u_char>>("inventory", this->serialize_inventory().data());
    this->mysql_update<std::vector<u_char>>("clothing", this->serialize_clothing().data());
    this->mysql_update<signed>("titles_on", this->titles_on); // @note `titles` (owned) is written by /givetitle directly
    this->mysql_update<signed>("gems", this->gems);
    this->mysql_update<signed>("surgery_skill", this->surgery_skill);
    this->mysql_update<signed>("malpractice", this->malpractice);
    this->mysql_update<std::string>("notebook", this->notebook);
    if (this->login_time != 0)
    {
        this->mysql_update<std::string>("profile", profile_serialize(*this));
        this->mysql_update<signed>("level", this->level.front());
        this->mysql_update<signed>("xp", this->level.back());
    }
    this->mysql_update<signed>("wl_bank", this->wl_bank);
    if (this->login_time != 0) this->mysql_update<signed>("playtime", this->playtime + static_cast<int>(std::time(nullptr) - this->login_time));
}

u_short peer::emplace(::slot slot) 
{
    if (auto it = std::ranges::find(this->slots, slot.id, &::slot::id); it != this->slots.end()) 
    {
        const u_short excess = std::max(0, (it->count + slot.count) - 200);
        it->count = std::min(it->count + slot.count, 200);
        if (it->count == 0)
        {
            const ::item &item = id_to_item(it->id);
            if (item.cloth_type != clothing::NONE) this->clothing[item.cloth_type] = 0;
        }
        return excess;
    }
    else this->slots.emplace_back(std::move(slot)); // @note no such item in inventory, so we create a new entry.
    return 0;
}

void peer::add_xp(ENetEvent &event, u_short value) 
{
    value = static_cast<u_short>(xp_bonus(*this, value)); // @note rank bonus
    if (event_on(event_type::XP)) value *= 2; // @note XP x2 event
    u_short &lvl = this->level.front();
    u_short &xp = this->level.back() += value; // @note factor the new xp amount

    for (; lvl < 125; )
    {
        u_short xp_formula = 50 * (lvl * lvl + 2); // @author https://www.growtopiagame.com/forums/member/553046-kasete
        if (xp < xp_formula) break;

        xp -= xp_formula;
        lvl++;

        if (lvl == 50) 
        {
            modify_item_inventory(event, ::slot{1400, 1}); // @note Mini Growtopian
            /* @todo based on account age give peer other items... */
        }
        if (lvl == 125) on::CountryState(event);
        send_varlist(event.peer, { "OnPlayerLeveledUp", lvl });
        send_varlist(event.peer, { "OnParticleEffect", 46u, CL_Vec2f{1812.0f, 1724.0f}, 0.0f, 0.0f });

        std::string message = std::format("{} is now level {}!", this->display_growid, lvl);
        send_varlist(event.peer, { "OnTalkBubble", this->netid, message, 0u });
        on::ConsoleMessage(event.peer, message);
        announce_level(*this, lvl);
    }
}

void peer::update_effects()
{
    this->punch_effect = 0;
    for (float cloth : this->clothing)
    {
        u_char punch_id = get_punch_id((u_int)cloth);
        if (punch_id != 0) // @note an actual change rather than no effect.
            this->punch_effect = punch_id;
    }

    /* @note any back item (wings, capes) grants double jump */
    if (this->clothing[clothing::BACK] != 0) this->state |= S_DOUBLE_JUMP;
    else                                     this->state &= ~S_DOUBLE_JUMP;

    /* @note staff are super supporters: bigger name + green wrench, unless /hidestatus */
    if (this->role >= MODERATOR && !this->hide_status) this->state |= S_SUPER_SUPPORTER;
    else                                               this->state &= ~S_SUPER_SUPPORTER;
}

ENetHost *host;

std::vector<ENetPeer*> peers(const std::string &world, peer_condition condition, std::function<void(ENetPeer&)> fun)
{
    std::vector<ENetPeer*> _peers{};
    _peers.reserve(host->peerCount);

    for (ENetPeer &peer : std::span(host->peers, host->peerCount))
        if (peer.state == ENET_PEER_STATE_CONNECTED)
        {
            if (condition == peer_condition::PEER_SAME_WORLD)
            {
                ::peer *pOthers = static_cast<::peer*>(peer.data);
                if (pOthers->netid == 0 || (pOthers->recent_worlds.back() != world)) continue;
            }
            fun(peer);
            _peers.push_back(&peer);
        }

    return _peers;
}

void safe_disconnect_peers(int code)
{
    enet_host_flush(host); // @note send anything still queued (e.g. restart notice) before disconnect drops the queues
    peers("", peer_condition::PEER_ALL, [](ENetPeer &p)
    {
        /* @note save inventory, clothing & world - otherwise anything done since login is lost on shutdown */
        if (::peer *pPeer = static_cast<::peer*>(p.data); pPeer != nullptr)
        {
            ENetEvent event{ .peer = &p };
            if (pPeer->netid != 0) action::quit_to_exit(event, "", true);
            delete pPeer;
            p.data = nullptr;
        }
        enet_peer_disconnect(&p, 0);
    });
    worlds.clear(); // @note saves any world still in memory (world::~world)
    enet_host_flush(host);
    
    enet_host_destroy(host);
    host = nullptr; // @todo clean this up better
    enet_deinitialize();
}

gamePacket make_gamePacket(const enet_uint8 *data) 
{
    const int   *i32   = reinterpret_cast<const int*>(data);
    const u_int *u32 = reinterpret_cast<const u_int*>(data);
    const float *f32 = reinterpret_cast<const float*>(data);

    return gamePacket{
        .type  = i32[1],
        .netid = i32[2],
        .uid   = i32[3],
        .state = i32[4],
        .count = f32[5],
        .id    = i32[6],
        .pos   = ::pos{f32[7], f32[8]},
        .speed = ::pos{f32[9], f32[10]},
        .idk   = f32[11],
        .punch = ::pos{i32[12], i32[13]},
        .size  = u32[14]
    };
}

::blob compress_state(const gamePacket &gamePacket) 
{
    ::blob blob{};
    
    blob.i32(gamePacket.packet_create);
    blob.i32(gamePacket.type);
    blob.i32(gamePacket.netid);
    blob.i32(gamePacket.uid);
    blob.i32(gamePacket.state);
    blob.f32(gamePacket.count);
    blob.i32(gamePacket.id);
    blob.f32(gamePacket.pos.x);
    blob.f32(gamePacket.pos.y);
    blob.f32(gamePacket.speed.x);
    blob.f32(gamePacket.speed.y);
    blob.f32(gamePacket.idk);
    blob.i32(gamePacket.punch.x);
    blob.i32(gamePacket.punch.y);
    blob.i32(gamePacket.size);

    return blob;
}

void send_inventory_state(ENetEvent &event)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    ::blob blob = compress_state(::gamePacket{
        .type = 0x09, // @note PACKET_SEND_INVENTORY_STATE
        .netid = pPeer->netid,
        .state = state::S_EXTENDED
    });
    blob.u8(0x01); // @note enable flag for big backpack

    blob.push_back(pPeer->serialize_inventory());

    ENetPacket *packet = enet_packet_create(blob.data().data(), blob.size(), ENET_PACKET_FLAG_RELIABLE);
	if (enet_peer_send(event.peer, 0, packet)) enet_packet_destroy(packet);
}
