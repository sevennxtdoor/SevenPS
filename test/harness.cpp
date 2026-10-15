// MikeTopia offline test harness: links the real server objects, replaces DB + network with fakes.
// run: test/run.sh (Mannequin, Password Door, Magplant). add a section per new feature.
#include "pch.hpp"
#include "gamePacket/tile_change.hpp"
#include "tools/mannequin.hpp"
#include "tools/string.hpp"
#include "database/database_config.hpp"
#include "gamePacket/tile_activate.hpp"
#include "tools/magplant.hpp"
#include "tools/password_door.hpp"
#include "action/dialog_return/gateway_edit.hpp"
#include "tools/fishing.hpp"
#include "tools/profile.hpp"
#include <cstdio>
#include <windows.h>
static LONG WINAPI crash_handler(EXCEPTION_POINTERS *e)
{
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH; // @note C++ throws pass through
    std::printf("CRASH at rip=%p\n", (void*)e->ContextRecord->Rip);
    /* @note return addresses found on the stack: addr2line them, the real callers are among them */
    auto *sp = reinterpret_cast<uintptr_t*>(e->ContextRecord->Rsp);
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
    for (int k = 0; k < 400; ++k) if (sp[k] > base && sp[k] < base + 0x4000000) std::printf("  stack %p\n", (void*)sp[k]);
    std::fflush(stdout); _exit(3);
}

/* ---------- fakes (win over the real definitions because this object is linked first) ---------- */
struct sent { ENetPeer *peer; std::vector<u_char> data; };
static std::vector<sent> outbox;

int enet_peer_send(ENetPeer *peer, enet_uint8, ENetPacket *packet)
{
    outbox.push_back({ peer, std::vector<u_char>(packet->data, packet->data + packet->dataLength) });
    return 1; // @note caller destroys it
}
world::~world() {}
void load_vendings(::world&) {} void load_doors(::world&) {} void load_area_locks(::world&) {}
void load_boxes(::world&) {} void load_magplants(::world&) {} void load_mannequins(::world&) {}

/* ---------- helpers ---------- */
static int fails = 0, passes = 0;
#define CHECK(cond, msg) do { if (cond) { ++passes; std::printf("  ok   %s\n", msg); } else { ++fails; std::printf("  FAIL %s  (line %d)\n", msg, __LINE__); } } while (0)

static std::string all_text() // @note every packet as text, to search dialogs / talk bubbles
{
    std::string s;
    for (auto &p : outbox) for (u_char c : p.data) s += (c >= 32 && c < 127) || c == '\n' ? static_cast<char>(c) : '.';
    return s;
}
static bool said(const std::string &needle) { return all_text().find(needle) != std::string::npos; }
static int held(const ::peer &p, short id)
{
    auto it = std::ranges::find(p.slots, id, &::slot::id);
    return it != p.slots.end() ? it->count : 0;
}
struct extra { std::string text; u_char flags; u_int hair; short clothes[9]; std::size_t size; };
static extra read_extra(::world &w, const ::pos &pos)
{
    ::blob b; mannequin_extra(b, w, pos);
    const auto &d = b.data();
    extra e{}; std::size_t i = 0;
    short len; memcpy(&len, &d[i], 2); i += 2;
    e.text.assign(reinterpret_cast<const char*>(&d[i]), len); i += len;
    e.flags = d[i++];
    memcpy(&e.hair, &d[i], 4); i += 4;
    for (int k = 0; k < 9; ++k) { memcpy(&e.clothes[k], &d[i], 2); i += 2; }
    e.size = d.size();
    CHECK(i == d.size(), "extra has exactly text + u8 + u32 + 9 x i16");
    return e;
}

int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0); AddVectoredExceptionHandler(1, crash_handler); std::printf("base=%p\n", (void*)GetModuleHandle(nullptr)); std::puts("start");
    mysql_library_init(0, NULL, NULL); gDb_config.init(); mysql_connect(); // @note the local MariaDB: world MTTEST is made, then deleted at the end
    {
        ::hStmt del{ "DELETE FROM world WHERE name = 'MTTEST'" }; del.execute(); // @note leftover from a crashed run
    }
    decode_items(); std::puts("items ok");
    const ::item &mq = id_to_item(1420);
    std::printf("Mannequin 1420 type=%d (MANNEQUIN=%d) name=%s\n", mq.type, type::MANNEQUIN, mq.raw_name.c_str());
    if (mq.type != type::MANNEQUIN) { std::puts("1420 is not TYPE_MANNEQUIN, stop"); return 1; }

    /* @note pick real tradeable clothes: two hats and a shirt */
    std::vector<short> hats; short shirt = 0, untrade = 0;
    for (const ::item &it : items)
    {
        if (it.type != type::CLOTHING || it.id == 0 || it.raw_name.empty()) continue;
        if (it.cat & CAT_UNTRADEABLE) { if (!untrade && it.cloth_type < 9) untrade = it.id; continue; }
        if (it.cloth_type == clothing::HAIR && hats.size() < 2) hats.push_back(it.id);
        if (it.cloth_type == clothing::SHIRT && !shirt) shirt = it.id;
    }
    const short hat1 = hats[0], hat2 = hats[1];
    std::printf("hat1=%d %s, hat2=%d %s, shirt=%d %s, untradeable=%d %s\n", hat1, id_to_item(hat1).raw_name.c_str(), hat2, id_to_item(hat2).raw_name.c_str(),
        shirt, id_to_item(shirt).raw_name.c_str(), untrade, id_to_item(untrade).raw_name.c_str());

    worlds.reserve(4);
    std::puts("making world");
    ::world &w = worlds.emplace_back("MTTEST");
    std::puts("world ok");
    w.owner = 1;
    const ::pos tile{ 10, 20 };
    w.blocks[cord(10, 20)].fg = 1420;
    mannequin_placed(w, tile);

    /* @note fake host with two connected players: owner (1) and a visitor (2) */
    static ENetPeer enet_peers[2]{};
    static ENetHost fake_host{};
    fake_host.peers = enet_peers; fake_host.peerCount = 2;
    host = &fake_host;
    ::peer *owner = new ::peer(), *visitor = new ::peer(); // @note never deleted: ~peer() writes to the database
    for (int k = 0; k < 2; ++k)
    {
        ::peer *p = k ? visitor : owner;
        p->user_id = k + 1; p->netid = k + 1; p->recent_worlds.back() = "MTTEST";
        p->pos = ::pos{ 11 * 32.0f, 20 * 32.0f };
        enet_peers[k].state = ENET_PEER_STATE_CONNECTED; enet_peers[k].data = p;
    }
    owner->slots = { ::slot(hat1, 2), ::slot(hat2, 1), ::slot(shirt, 1), ::slot(untrade, 1) };
    ENetEvent ev{}; ev.peer = &enet_peers[0];
    ENetEvent ev2{}; ev2.peer = &enet_peers[1];
    auto ret = [&](ENetEvent &e, const std::string &s) { outbox.clear(); mannequin_return(e, ::hPipe(s)); };
    const std::string at = "|tilex|10|tiley|20";

    std::puts("\n[1] empty mannequin tile extra");
    { extra e = read_extra(w, tile); CHECK(e.size == 2 + 1 + 4 + 18, "empty extra is 25 bytes"); CHECK(e.hair == 0xffffffff, "hair color white"); }

    std::puts("\n[2] use a hat on it -> question dialog, nothing taken yet");
    outbox.clear();
    tile_change(ev, ::gamePacket{ .type = 3, .id = hat1, .pos = owner->pos, .punch = tile });
    CHECK(said("Do you really want to put"), "asks Yes/No");
    CHECK(said(std::format("embed_data|put|{}", hat1)), "dialog carries the item");
    CHECK(held(*owner, hat1) == 2, "hat still in backpack");

    std::puts("\n[3] answer Yes");
    ret(ev, std::format("dialog_name|mt_mannequin|put|{}{}", hat1, at));
    { extra e = read_extra(w, tile); CHECK(e.clothes[clothing::HAIR] == hat1, "hat on the mannequin (hat slot)"); }
    CHECK(held(*owner, hat1) == 1, "one hat left the backpack");
    CHECK(said("change_clothes.wav"), "plays sound");

    std::puts("\n[4] swap to another hat -> old one comes back");
    ret(ev, std::format("dialog_name|mt_mannequin|put|{}{}", hat2, at));
    { extra e = read_extra(w, tile); CHECK(e.clothes[clothing::HAIR] == hat2, "new hat on it"); }
    CHECK(held(*owner, hat1) == 2 && held(*owner, hat2) == 0, "old hat back, new hat gone");

    std::puts("\n[5] item not owned / same item / untradeable / worn last copy / too far");
    ret(ev, std::format("dialog_name|mt_mannequin|put|{}{}", hat2, at)); CHECK(said("giggle") || said("don't have"), "same/unowned item refused");
    { extra e = read_extra(w, tile); CHECK(e.clothes[clothing::HAIR] == hat2, "unchanged"); }
    ret(ev, std::format("dialog_name|mt_mannequin|put|{}{}", untrade, at)); CHECK(said("too valuable"), "untradeable refused");
    CHECK(held(*owner, untrade) == 1, "untradeable kept");
    owner->clothing[clothing::SHIRT] = shirt;
    ret(ev, std::format("dialog_name|mt_mannequin|put|{}{}", shirt, at)); CHECK(said("Take it off first"), "worn last copy refused");
    owner->clothing[clothing::SHIRT] = 0;
    owner->pos = ::pos{ 30 * 32.0f, 20 * 32.0f };
    ret(ev, std::format("dialog_name|mt_mannequin|put|{}{}", shirt, at)); CHECK(said("closer"), "too far refused");
    owner->pos = ::pos{ 11 * 32.0f, 20 * 32.0f };
    ret(ev, std::format("dialog_name|mt_mannequin|put|{}{}", shirt, at));
    { extra e = read_extra(w, tile); CHECK(e.clothes[clothing::SHIRT] == shirt, "shirt on it"); }

    std::puts("\n[6] visitor (no access)");
    visitor->slots = { ::slot(hat1, 1) };
    ret(ev2, std::format("dialog_name|mt_mannequin|put|{}{}", hat1, at)); CHECK(said("isn't yours"), "visitor can't dress it");
    ret(ev2, "dialog_name|mt_mannequin|buttonClicked|clear" + at);
    { extra e = read_extra(w, tile); CHECK(e.clothes[clothing::HAIR] == hat2 && e.clothes[clothing::SHIRT] == shirt, "visitor can't strip it"); }
    CHECK(held(*visitor, hat2) == 0, "visitor got nothing");

    std::puts("\n[7] wrench dialog lists clothes");
    outbox.clear();
    tile_change(ev, ::gamePacket{ .type = 3, .id = 32, .pos = owner->pos, .punch = tile });
    CHECK(said("add_checkbox|cloth_0|") && said("add_checkbox|cloth_1|"), "checkbox per piece");
    CHECK(said("end_dialog|mt_mannequin|"), "right dialog name");

    std::puts("\n[8] sign text, injection cleaned");
    ret(ev, "dialog_name|mt_mannequin|sign_text|Hello\nadd_button|x" + at);
    { extra e = read_extra(w, tile); std::printf("  text='%s'\n", e.text.c_str()); CHECK(e.text.find('|') == std::string::npos && e.text.find('\n') == std::string::npos, "no | or newline in sign"); }

    std::puts("\n[9] remove selected (shirt only)");
    ret(ev, "dialog_name|mt_mannequin|cloth_0|0|cloth_1|1|buttonClicked|clear_selected" + at);
    { extra e = read_extra(w, tile); CHECK(e.clothes[clothing::SHIRT] == 0 && e.clothes[clothing::HAIR] == hat2, "only shirt removed"); }
    CHECK(held(*owner, shirt) == 1, "shirt back");

    std::puts("\n[10] punch takes the hat back, no damage");
    ::block &b = w.blocks[cord(10, 20)];
    const u_char hits_before = b.hits[0];
    outbox.clear();
    tile_change(ev, ::gamePacket{ .type = 3, .id = 18, .pos = owner->pos, .punch = tile });
    { extra e = read_extra(w, tile); CHECK(e.clothes[clothing::HAIR] == 0, "hat removed by punch"); }
    CHECK(held(*owner, hat2) == 1, "hat back in backpack");
    CHECK(b.hits[0] == hits_before && b.fg == 1420, "mannequin not damaged");

    std::puts("\n[11] full backpack: clothes stay on");
    ret(ev, std::format("dialog_name|mt_mannequin|put|{}{}", hat2, at));
    std::erase_if(owner->slots, [&](const ::slot &s) { return s.id == hat2; }); // @note as after a relog: no slot for it at all
    owner->slot_size = static_cast<int>(owner->slots.size());
    ret(ev, "dialog_name|mt_mannequin|buttonClicked|clear" + at);
    { extra e = read_extra(w, tile); CHECK(e.clothes[clothing::HAIR] == hat2, "not lost when backpack is full"); }
    CHECK(said("enough room"), "says no room");
    owner->slot_size = 16;
    ret(ev, "dialog_name|mt_mannequin|buttonClicked|clear" + at);
    { extra e = read_extra(w, tile); CHECK(e.clothes[clothing::HAIR] == 0, "clear all works with room"); }

    std::puts("\n[12] empty mannequin breaks normally");
    for (int k = 0; k < 20 && b.fg == 1420; ++k) tile_change(ev, ::gamePacket{ .type = 3, .id = 18, .pos = owner->pos, .punch = tile });
    CHECK(b.fg == 0, "broken when empty");

    std::puts("\n[13] world blob: tile extra written for a placed mannequin");
    b.fg = 1420; mannequin_placed(w, tile);
    { ::blob wb = w.serialize(); CHECK(wb.size() > 6000 * 8, "world serializes"); }

    /* ================= Password Door ================= */
    std::puts("\n[D1] Password Door item");
    const ::item &pd = id_to_item(PASSWORD_DOOR);
    std::printf("  762 = %s type=%d (DOOR=%d)\n", pd.raw_name.c_str(), pd.type, type::DOOR);
    CHECK(pd.type == type::DOOR, "762 is TYPE_DOOR (assumption from GTOS)");
    short plain_door = 0;
    for (const ::item &it : items) if (it.raw_name == "Door") { plain_door = it.id; break; }
    CHECK(plain_door != 0 && id_to_item(plain_door).type == type::DOOR, "plain Door found");

    owner->slots.push_back(::slot(PASSWORD_DOOR, 1));
    owner->slots.push_back(::slot(plain_door, 1));
    owner->pos = ::pos{ 20 * 32.0f, 20 * 32.0f };
    const ::pos pdoor{ 21, 20 }, door_b{ 40, 20 };
    tile_change(ev, ::gamePacket{ .type = 3, .id = PASSWORD_DOOR, .pos = owner->pos, .punch = pdoor });
    tile_change(ev, ::gamePacket{ .type = 3, .id = plain_door, .pos = owner->pos, .punch = door_b });
    CHECK(w.blocks[cord(21, 20)].fg == PASSWORD_DOOR && w.blocks[cord(40, 20)].fg == plain_door, "both doors placed");

    std::puts("\n[D2] owner wrench shows password field, set it");
    outbox.clear();
    tile_change(ev, ::gamePacket{ .type = 3, .id = 32, .pos = owner->pos, .punch = pdoor });
    CHECK(said("door_password"), "password field in door dialog");
    outbox.clear(); gateway_edit(ev, ::hPipe("dialog_name|door_edit|tilex|40|tiley|20|door_name|B|door_target||door_id|B|door_password|x"));
    outbox.clear(); gateway_edit(ev, ::hPipe("dialog_name|door_edit|tilex|21|tiley|20|door_name|Secret|door_target|:B|door_id|A|door_password|ab c!1"));
    auto dA = std::ranges::find(w.doors, pdoor, &::door::pos);
    auto dB = std::ranges::find(w.doors, door_b, &::door::pos);
    CHECK(dA != w.doors.end() && dA->password == "ABC1", "password cleaned to ABC1");
    CHECK(dB != w.doors.end() && dB->password.empty(), "plain door never keeps a password");

    std::puts("\n[D3] visitor can't edit the door");
    outbox.clear(); gateway_edit(ev2, ::hPipe("dialog_name|door_edit|tilex|21|tiley|20|door_name|Hacked|door_target||door_id||door_password|"));
    CHECK(dA->password == "ABC1" && dA->label == "Secret", "visitor edit ignored");

    std::puts("\n[D4] world blob never contains the password");
    { ::blob wb = w.serialize(); const auto &d = wb.data(); std::string raw(d.begin(), d.end()); CHECK(raw.find("ABC1") == std::string::npos, "ABC1 not in world data"); CHECK(raw.find("Secret") != std::string::npos, "label is"); }

    std::puts("\n[D5] enter -> asks password");
    visitor->pos = pdoor.by_32();
    ENetEvent &vev = ev2;
    outbox.clear(); tile_activate(vev, ::gamePacket{ .type = 7, .punch = pdoor });
    CHECK(said("requires a password"), "asks for password");
    CHECK(visitor->pass_door == pdoor, "server remembers which door");

    std::puts("\n[D6] wrong password x5 -> wait");
    for (int k = 0; k < 5; ++k)
    {
        visitor->pass_door = pdoor; outbox.clear();
        password_door_return(vev, ::hPipe("dialog_name|mt_passdoor|password|nope"));
    }
    CHECK(said("Wrong password"), "wrong password said");
    CHECK(visitor->pass_door_wait > std::time(nullptr), "5 fails -> must wait");
    visitor->pass_door = pdoor; outbox.clear();
    password_door_return(vev, ::hPipe("dialog_name|mt_passdoor|password|abc1"));
    CHECK(said("Wait") && visitor->pos == pdoor.by_32(), "even the right password waits");
    visitor->pass_door_wait = 0;

    std::puts("\n[D7] faked answer without a prompt does nothing");
    visitor->pass_door = ::pos{ -1, -1 }; outbox.clear();
    password_door_return(vev, ::hPipe("dialog_name|mt_passdoor|password|abc1"));
    CHECK(outbox.empty() && visitor->pos == pdoor.by_32(), "ignored");

    std::puts("\n[D8] right password (lower case ok) -> :B teleports to door B");
    visitor->pass_door = pdoor; outbox.clear();
    password_door_return(vev, ::hPipe("dialog_name|mt_passdoor|password|abc1"));
    CHECK(said("door opens"), "door opens");
    CHECK(visitor->pos == door_b.by_32(), "standing on door B");

    std::puts("\n[D9] walked away before answering");
    visitor->pass_door = pdoor; visitor->pos = ::pos{ 60 * 32.0f, 20 * 32.0f }; outbox.clear();
    password_door_return(vev, ::hPipe("dialog_name|mt_passdoor|password|abc1"));
    CHECK(visitor->pos == ::pos(60 * 32.0f, 20 * 32.0f), "no teleport from far away");

    /* ================= Magplant ================= */
    std::puts("\n[M1] items");
    const short MAG = 5638;
    CHECK(id_to_item(MAG).type == type::MAGPLANT, "5638 is TYPE_MAGPLANT");
    std::printf("  5640 = %s\n", id_to_item(MAGPLANT_REMOTE_ID).raw_name.c_str());
    CHECK(id_to_item(MAGPLANT_REMOTE_ID).raw_name.find("Remote") != std::string::npos, "5640 is the remote");

    owner->pos = ::pos{ 50 * 32.0f, 20 * 32.0f };
    owner->slots.push_back(::slot(MAG, 1));
    owner->slots.push_back(::slot(2, 50)); // @note Dirt
    const ::pos mag{ 52, 20 };
    tile_change(ev, ::gamePacket{ .type = 3, .id = MAG, .pos = owner->pos, .punch = mag });
    CHECK(w.blocks[cord(52, 20)].fg == MAG, "magplant placed");

    auto mag_extra = [&]() { ::blob b; magplant_extra(b, w, mag); std::vector<u_char> d = b.data(); int item, count; memcpy(&item, &d[0], 4); memcpy(&count, &d[4], 4); return std::tuple{ item, count, d[8], d[9], d.size() }; };
    { auto [item, count, col, bld, size] = mag_extra(); CHECK(size == 14 && item == 0, "empty extra 14 bytes, no item"); }

    std::puts("\n[M2] wrench -> pick Dirt, add (amount clamped)");
    outbox.clear(); tile_change(ev, ::gamePacket{ .type = 3, .id = 32, .pos = owner->pos, .punch = mag });
    CHECK(said("add_item_picker|selectitem"), "item picker shown");
    outbox.clear(); magplant_return(ev, ::hPipe("dialog_name|mt_magplant|tilex|52|tiley|20|step|main|selectitem|2"));
    { auto [item, count, col, bld, size] = mag_extra(); CHECK(item == 2 && count == 0 && col == 1, "Dirt chosen, collecting"); }
    outbox.clear(); magplant_return(ev, ::hPipe("dialog_name|mt_magplant|tilex|52|tiley|20|step|add|amount|9999"));
    { auto [item, count, col, bld, size] = mag_extra(); CHECK(count == 50, "added only the 50 owned"); }
    CHECK(held(*owner, 2) == 0, "dirt left backpack");

    std::puts("\n[M3] drops: Dirt collected, other items fall");
    const std::size_t objects_before = w.objects.size();
    magplant_drop(ev, ::slot(2, 3), ::pos{ 30 * 32.0f, 30 * 32.0f }, w);
    { auto [item, count, col, bld, size] = mag_extra(); CHECK(count == 53, "3 dirt collected"); }
    CHECK(w.objects.size() == objects_before, "no dirt object in the world");
    magplant_drop(ev, ::slot(4, 1), ::pos{ 30 * 32.0f, 30 * 32.0f }, w);
    CHECK(w.objects.size() == objects_before + 1, "lava dropped normally");

    std::puts("\n[M4] retrieve, visitor can't");
    outbox.clear(); magplant_return(ev, ::hPipe("dialog_name|mt_magplant|tilex|52|tiley|20|step|take|amount|10"));
    { auto [item, count, col, bld, size] = mag_extra(); CHECK(count == 43 && held(*owner, 2) == 10, "took 10"); }
    visitor->pos = ::pos{ 51 * 32.0f, 20 * 32.0f };
    outbox.clear(); magplant_return(ev2, ::hPipe("dialog_name|mt_magplant|tilex|52|tiley|20|step|take|amount|10"));
    { auto [item, count, col, bld, size] = mag_extra(); CHECK(count == 43 && held(*visitor, 2) == 0, "visitor took nothing"); }

    std::puts("\n[M5] can't break while full, punch -> building mode");
    ::block &mb = w.blocks[cord(52, 20)];
    outbox.clear(); tile_change(ev, ::gamePacket{ .type = 3, .id = 18, .pos = owner->pos, .punch = mag });
    { auto [item, count, col, bld, size] = mag_extra(); CHECK(bld == 1, "building mode on after first punch"); }
    CHECK(said("OnPlanterActivated"), "planter sent");
    for (int k = 0; k < 20; ++k) tile_change(ev, ::gamePacket{ .type = 3, .id = 18, .pos = owner->pos, .punch = mag });
    CHECK(mb.fg == MAG, "not broken while holding items");
    CHECK(said("before breaking"), "tells to empty it");

    std::puts("\n[M6] remote: get, place, visitor refused, gone on join");
    outbox.clear(); magplant_return(ev, ::hPipe("dialog_name|mt_magplant|tilex|52|tiley|20|step|main|buttonClicked|getremote"));
    CHECK(held(*owner, MAGPLANT_REMOTE_ID) == 1, "got remote");
    const ::pos spot{ 55, 18 };
    outbox.clear(); tile_change(ev, ::gamePacket{ .type = 3, .id = MAGPLANT_REMOTE_ID, .pos = owner->pos, .punch = spot });
    CHECK(w.blocks[cord(55, 18)].fg == 2, "dirt placed from machine");
    { auto [item, count, col, bld, size] = mag_extra(); CHECK(count == 42, "machine count -1"); }
    CHECK(held(*owner, MAGPLANT_REMOTE_ID) == 1 && held(*owner, 2) == 10, "remote kept, backpack dirt untouched");
    visitor->slots = { ::slot(MAGPLANT_REMOTE_ID, 1) };
    outbox.clear(); tile_change(ev2, ::gamePacket{ .type = 3, .id = MAGPLANT_REMOTE_ID, .pos = visitor->pos, .punch = ::pos{ 56, 18 } });
    CHECK(w.blocks[cord(56, 18)].fg == 0, "visitor can't build with a traded remote");
    outbox.clear(); magplant_on_join(ev, w);
    CHECK(held(*owner, MAGPLANT_REMOTE_ID) == 0, "remote removed on world join");

    std::puts("\n[M7] empty it, then it breaks");
    outbox.clear(); magplant_return(ev, ::hPipe("dialog_name|mt_magplant|tilex|52|tiley|20|step|take|amount|42"));
    { auto [item, count, col, bld, size] = mag_extra(); std::printf("  left in machine: %d, backpack dirt: %d\n", count, held(*owner, 2)); CHECK(count == 0, "emptied (stack limit 200)"); }
    for (int k = 0; k < 80 && mb.fg == MAG; ++k)
    {
        outbox.clear();
        tile_change(ev, ::gamePacket{ .type = 3, .id = 18, .pos = owner->pos, .punch = mag });
        
    }
    std::printf("  item hits needed=%d\n", id_to_item(MAG).hits);
    CHECK(mb.fg == 0, "broken when empty");

    /* ================= Fishing ================= */
    const short ROD = 2912, WORM = 2914;
    const ::pos water{ 12, 22 };
    auto fish_count = [&](const ::peer &p) { int n = 0; for (const ::slot &s : p.slots) if (s.count > 0 && id_to_item(s.id).type == type::FISH) ++n; return n; };
    auto bite_now = [&](::peer &p) { p.fish_bite = std::time(nullptr) - 1; Sleep(1100); outbox.clear(); fishing_tick(); };
    auto use = [&](ENetEvent &e, ::peer &p, short id, const ::pos &at) { outbox.clear(); tile_change(e, ::gamePacket{ .type = 3, .id = id, .pos = p.pos, .punch = at }); };

    std::puts("\n[F1] setup: water tile, rod, worms");
    w.blocks[cord(12, 22)].state[3] |= S_WATER;
    owner->pos = ::pos{ 11 * 32.0f, 21 * 32.0f };
    owner->slots.push_back(::slot(WORM, 5));
    owner->slot_size = 30;

    std::puts("\n[F2] no rod / not water / too far");
    use(ev, *owner, WORM, water); CHECK(said("Fishing Rod") && held(*owner, WORM) == 5, "needs a rod, bait kept");
    owner->clothing[clothing::HAND] = ROD;
    use(ev, *owner, WORM, ::pos{ 13, 22 }); CHECK(said("only fish in water") && held(*owner, WORM) == 5, "dry tile refused");
    owner->pos = ::pos{ 30 * 32.0f, 21 * 32.0f };
    use(ev, *owner, WORM, water); CHECK(said("Too far") && held(*owner, WORM) == 5, "too far refused");
    owner->pos = ::pos{ 11 * 32.0f, 21 * 32.0f };

    std::puts("\n[F3] cast: bait used, line packet 0x1f with the tile");
    use(ev, *owner, WORM, water);
    CHECK(held(*owner, WORM) == 4 && owner->fish_tile == water, "casting");
    bool line = false;
    for (auto &pk : outbox) if (pk.data.size() >= 60) { int type, netid, px, py; memcpy(&type, &pk.data[4], 4); memcpy(&netid, &pk.data[8], 4); memcpy(&px, &pk.data[48], 4); memcpy(&py, &pk.data[52], 4); if (type == 0x1f && netid == owner->netid && px == 12 && py == 22) line = true; }
    CHECK(line, "GONE_FISHIN sent with netid + tile");
    const std::time_t wait = owner->fish_bite - std::time(nullptr);
    CHECK(wait >= 12 && wait <= 30, "bite in 12..30 s");

    std::puts("\n[F4] punch too early");
    use(ev, *owner, 18, ::pos{ 11, 20 });
    CHECK(said("Too soon") && owner->fish_tile.x < 0 && fish_count(*owner) == 0, "fish got away, line in");

    std::puts("\n[F5] splash, then punch -> fish + gems");
    use(ev, *owner, WORM, water);
    bite_now(*owner);
    CHECK(said("OnParticleEffect") && said("splash.wav"), "splash effect + sound");
    CHECK(owner->fish_bite_end != 0, "fish on the hook");
    const int gems_before = owner->gems;
    use(ev, *owner, 18, ::pos{ 11, 20 });
    CHECK(fish_count(*owner) == 1, "fish in backpack");
    for (const ::slot &s : owner->slots) if (s.count > 0 && id_to_item(s.id).type == type::FISH) { std::printf("  caught %d lb. %s\n", s.count, id_to_item(s.id).raw_name.c_str()); CHECK(s.count >= 1 && s.count <= 25, "weight within Fishing Rod max 25"); }
    CHECK(owner->gems > gems_before, "got gems");
    CHECK(owner->fish_tile.x < 0, "line in after the catch");
    CHECK(pget(*owner, "fish_best") >= 1, "best weight saved in profile");

    std::puts("\n[F6] too slow: fish swims off, another bite comes later");
    use(ev, *owner, WORM, water);
    bite_now(*owner);
    owner->fish_bite_end = std::time(nullptr) - 1;
    Sleep(1100); fishing_tick();
    CHECK(owner->fish_bite_end == 0 && owner->fish_bite > std::time(nullptr) && owner->fish_tile == water, "still fishing, new bite scheduled");

    std::puts("\n[F7] walking away pulls the line in");
    owner->pos = ::pos{ 20 * 32.0f, 21 * 32.0f };
    outbox.clear(); fishing_moved(ev);
    CHECK(owner->fish_tile.x < 0 && said("Sit still"), "line in");
    owner->pos = ::pos{ 11 * 32.0f, 21 * 32.0f };

    std::puts("\n[F8] full backpack: the fish lands on the ground");
    use(ev, *owner, WORM, water);
    bite_now(*owner);
    owner->slot_size = static_cast<int>(owner->slots.size());
    const std::size_t drops = w.objects.size();
    const int fish_before = fish_count(*owner);
    use(ev, *owner, 18, ::pos{ 11, 20 });
    CHECK(w.objects.size() == drops + 1 || fish_count(*owner) == fish_before, "dropped, not lost or duplicated");
    owner->slot_size = 30;

    std::puts("\n[F9] visitor may fish in a locked world, world unchanged");
    visitor->clothing[clothing::HAND] = ROD;
    visitor->slots = { ::slot(WORM, 1) };
    visitor->pos = ::pos{ 13 * 32.0f, 21 * 32.0f };
    const ::block before = w.blocks[cord(12, 22)];
    use(ev2, *visitor, WORM, water);
    CHECK(visitor->fish_tile == water && held(*visitor, WORM) == 0, "visitor casting");
    CHECK(w.blocks[cord(12, 22)].fg == before.fg && w.blocks[cord(12, 22)].state[3] == before.state[3], "tile untouched");
    fishing_reset(*visitor);
    CHECK(visitor->fish_tile.x < 0, "reset on world change");

    {
        ::hStmt del{ "DELETE FROM world WHERE name = 'MTTEST'" }; del.execute();
        ::hStmt del2{ "DELETE FROM world_mannequin WHERE world = 'MTTEST'" }; del2.execute();
        std::puts("test world MTTEST deleted");
    }
    std::printf("\n%d passed, %d failed\n", passes, fails);
    std::fflush(stdout);
    _exit(fails ? 1 : 0); // @note skip destructors (they talk to the database)
}
