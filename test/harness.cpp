// SevenPS offline test harness: links the real server objects, replaces DB + network with fakes.
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
#include "tools/casino.hpp"
#include "tools/item_hunt.hpp"
#include "gamePacket/item_activate_object.hpp"
#include "action/input.hpp"
#include "onVariant/RequestGazette.hpp"
#include "tools/backup.hpp"
#include "tools/tutorial.hpp"
#include "tools/guild.hpp"
#include "tools/sponsor.hpp"
#include "tools/auth.hpp"
#include "action/protocol.hpp"
#include "action/tankIDName.hpp"
#include "onVariant/CountryState.hpp"
#include "eventType/receive.hpp"
#include "gamePacket/movement.hpp"
#include "tools/guard.hpp"
#include "tools/web_login.hpp"
#include "onVariant/SetClothing.hpp"
#include "tools/playmods.hpp"
#include "commands/punch.hpp"
#include "tools/buffs.hpp"
#include "tools/special_blocks.hpp"
#include "tools/special_tiles.hpp"
#include "tools/rewards.hpp"
#include "commands/ghost.hpp"
#include "action/join_request.hpp"
#include "action/dialog_return/lock_edit.hpp"
#include "action/quit_to_exit.hpp"
#include "tools/rank.hpp"
#include "tools/cps.hpp"
#include "gamePacket/item_activate.hpp"
#include "action/dialog_return/trash_item.hpp"
#include "action/dialog_return/create_blast.hpp"
#include "action/trade.hpp"
#include "action/wrench.hpp"
#include "action/trash.hpp"
#include "action/drop.hpp"
#include "action/itemfavourite.hpp"
#include "action/info.hpp"
#include "action/store.hpp"
#include "tools/surgery.hpp"
#include "action/buy.hpp"
#include "database/shouhin.hpp"
#include <filesystem>
#include "automate/holiday.hpp"
#include "tools/vending.hpp"
#include "commands/gtos.hpp"
#include "commands/me.hpp"
#include <cstdio>
#include <fstream>
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
    max_reach = 1e9f; // @note most tests build far from where the fake players stand; [WL4] tests the reach
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

    /* ================= Casino ================= */
    std::puts("\n[C1] Roulette Wheel: everyone in the world sees the spin");
    CHECK(id_to_item(758).raw_name == "Roulette Wheel", "758 is the Roulette Wheel");
    const ::pos wheel{ 70, 20 };
    w.blocks[cord(70, 20)].fg = 758;
    owner->pos = ::pos{ 69 * 32.0f, 20 * 32.0f };
    owner->spin_ready = {};
    use(ev, *owner, 18, wheel);
    auto spins_to = [&](ENetPeer *who) { int n = 0; for (auto &pk : outbox) if (pk.peer == who) { std::string s(pk.data.begin(), pk.data.end()); if (s.find("spun the wheel") != std::string::npos) ++n; } return n; };
    CHECK(spins_to(&enet_peers[0]) == 2 && spins_to(&enet_peers[1]) == 2, "bubble + console to the spinner AND the other player (once each)");
    use(ev, *owner, 18, wheel);
    CHECK(spins_to(&enet_peers[0]) == 0, "no second spin within 1.5 s");

    std::puts("\n[C2] numbers 0-36, real red/black");
    {
        std::array<int, 37> seen{}; bool colors_ok = true;
        constexpr int RED[] = { 1, 3, 5, 7, 9, 12, 14, 16, 18, 19, 21, 23, 25, 27, 30, 32, 34, 36 };
        for (int k = 0; k < 3000; ++k)
        {
            owner->spin_ready = {}; outbox.clear(); roulette_spin(ev);
            const std::string s = all_text();
            const std::size_t at = s.find("got `");
            if (at == std::string::npos) { colors_ok = false; break; }
            const char color = s[at + 5]; const int n = std::atoi(s.c_str() + at + 6);
            if (n < 0 || n > 36) { colors_ok = false; break; }
            ++seen[n];
            const char want = (n == 0) ? '2' : (std::ranges::find(RED, n) != std::end(RED)) ? '4' : 'b';
            if (color != want) colors_ok = false;
        }
        CHECK(colors_ok, "every color matches the roulette table");
        CHECK(seen[0] > 0 && seen[36] > 0, "0 and 36 both come up");
        CHECK(std::ranges::all_of(seen, [](int c) { return c > 0; }), "every number 0-36 comes up");
        CHECK(said("(reme `w"), "reme shown");
    }

    std::puts("\n[C3] fake spin messages blocked");
    CHECK(fake_spin("[Mike spun the wheel and got 0!]"), "plain fake");
    CHECK(fake_spin("`w[Mike `wSpun the `2wheel and got `20`w!]"), "with colors / caps");
    CHECK(fake_spin("s p u n  t h e  w h e e l"), "spaced out");
    CHECK(!fake_spin("i spun around, nice wheel"), "normal chat passes");
    visitor->role = 0;
    outbox.clear(); action::input(ev2, "action|input|text|[Mike spun the wheel and got `20``!]");
    CHECK(said("Fake Roulette") && !said("player_chat"), "visitor's fake not broadcast");
    outbox.clear(); action::input(ev2, "action|input|text|/me spun the wheel and got 0");
    CHECK(said("Fake Roulette") && !said("player_chat"), "/me fake blocked too");

    std::puts("\n[C4] Vegas Pack in the store");
    {
        std::ifstream f("resources/store.txt"); std::string line, vegas;
        while (std::getline(f, line)) if (line.find("|vegas_pack|") != std::string::npos) vegas = line;
        CHECK(!vegas.empty(), "vegas_pack listed");
        bool names_ok = true;
        const std::vector<std::string> parts = readch(vegas, '|');
        for (const std::string &kv : readch(parts.back(), ','))
        {
            const int id = std::atoi(kv.c_str());
            std::printf("  %s = %s\n", kv.c_str(), id_to_item(id).raw_name.c_str());
            if (id_to_item(id).raw_name.empty() || id_to_item(id).raw_name.starts_with("null")) names_ok = false;
        }
        CHECK(names_ok, "every item exists");
        CHECK(vegas.find("|758:1") != std::string::npos || vegas.find(",758:1") != std::string::npos, "has the Roulette Wheel");
    }

    /* ================= Pickups & Item Hunt ================= */
    std::puts("\n[P1] picking up drops");
    owner->recent_worlds.back() = "MTTEST";
    owner->pos = ::pos{ 20 * 32.0f, 10 * 32.0f };
    outbox.clear(); item_activate_object(ev, ::gamePacket{ .type = 11, .id = 999999 });
    CHECK(true, "made-up drop uid: no crash");
    w.objects.emplace_back(::object(2, 7, ::pos{ 90 * 32.0f, 10 * 32.0f }, ++w.last_object_uid));
    const u_int far_uid = w.last_object_uid;
    const int dirt_before = held(*owner, 2);
    outbox.clear(); item_activate_object(ev, ::gamePacket{ .type = 11, .id = static_cast<int>(far_uid) });
    CHECK(held(*owner, 2) == dirt_before && std::ranges::find(w.objects, far_uid, &::object::uid) != w.objects.end(), "drop 70 tiles away not vacuumed");
    owner->pos = ::pos{ 89 * 32.0f, 10 * 32.0f };
    outbox.clear(); item_activate_object(ev, ::gamePacket{ .type = 11, .id = static_cast<int>(far_uid) });
    CHECK(held(*owner, 2) == dirt_before + 7, "close drop collected");

    std::puts("\n[H1] Item Hunt in a stand-in START");
    CHECK(id_to_item(596).raw_name.find("Treasure") != std::string::npos, "596 is a Treasure Chest");
    {
        ::hStmt del{ "DELETE FROM world WHERE name = 'MTHUNT'" }; del.execute();
    }
    ::world &hw = worlds.emplace_back("MTHUNT");
    hw.name = "START"; // @note in memory only (~world is faked): the real START is never touched
    auto chests_on_ground = [&]() { return std::ranges::count(hw.objects, 596, &::object::id); };
    owner->role = OWNER;
    owner->growid = "MTOWNER"; visitor->growid = "MTVISIT"; // @note broadcasts only go to logged-in players
    outbox.clear(); hunt_command(ev, "hunt start");
    std::printf("  chests: %d\n", (int)chests_on_ground());
    CHECK(chests_on_ground() == 5, "5 chests hidden");
    CHECK(said("ITEM HUNT"), "announced");
    bool on_ground_spots = true;
    for (const ::object &o : hw.objects) if (o.id == 596)
    {
        const int x = static_cast<int>(o.pos.x) / 32, y = static_cast<int>(o.pos.y) / 32;
        if (hw.blocks[cord(x, y)].fg != 0 || hw.blocks[cord(x, y + 1)].fg == 0) on_ground_spots = false;
        if (!is_hunt_drop("START", o.uid)) on_ground_spots = false;
    }
    CHECK(on_ground_spots, "each chest on an empty tile with ground below, marked as hunt drop");
    outbox.clear(); hunt_command(ev2, "hunt");
    CHECK(said("somewhere in"), "/hunt gives hints");
    outbox.clear(); hunt_command(ev2, "hunt stop");
    CHECK(chests_on_ground() == 5, "players can't stop it");

    std::puts("\n[H2] finding chests");
    std::vector<::object> chest_list; for (const ::object &o : hw.objects) if (o.id == 596) chest_list.push_back(o);
    owner->recent_worlds.back() = "START"; visitor->recent_worlds.back() = "START";
    owner->pos = chest_list[0].pos;
    const int g0 = owner->gems;
    outbox.clear(); item_activate_object(ev, ::gamePacket{ .type = 11, .id = static_cast<int>(chest_list[0].uid) });
    std::printf("  prize: %d gems\n", owner->gems - g0);
    CHECK(owner->gems - g0 >= 750 && held(*owner, 596) == 0, "gems instead of the chest");
    CHECK(chests_on_ground() == 4 && said("found a treasure"), "chest gone, announced");
    owner->pos = chest_list[1].pos;
    outbox.clear(); item_activate_object(ev, ::gamePacket{ .type = 11, .id = static_cast<int>(chest_list[1].uid) });
    CHECK(chests_on_ground() == 4 && said("already found"), "one chest per player");
    visitor->pos = chest_list[1].pos;
    const int vg = visitor->gems;
    outbox.clear(); item_activate_object(ev2, ::gamePacket{ .type = 11, .id = static_cast<int>(chest_list[1].uid) });
    CHECK(visitor->gems > vg && chests_on_ground() == 3, "another player finds the next");

    std::puts("\n[H3] START unloaded and loaded again: chests come back");
    std::erase_if(hw.objects, [](const ::object &o) { return o.id == 596; });
    Sleep(1100); outbox.clear(); hunt_tick();
    CHECK(chests_on_ground() == 3, "3 left chests put back");

    std::puts("\n[H4] staff stops it: chests removed");
    outbox.clear(); hunt_command(ev, "hunt stop");
    CHECK(chests_on_ground() == 0, "all chests gone");
    bool any_hunt = false; for (const ::object &o : hw.objects) if (is_hunt_drop("START", o.uid)) any_hunt = true;
    CHECK(!any_hunt && said("stopped"), "no hunt drops left");
    owner->role = 0;
    {
        ::hStmt del{ "DELETE FROM world WHERE name = 'MTHUNT'" }; del.execute();
    }

    /* ================= Backup ================= */
    std::puts("\n[B1] /backup: owner only, real dump into backup/");
    {
        namespace fs = std::filesystem;
        auto newest = []() { fs::path best; std::error_code ec; for (auto &f : fs::directory_iterator(std::getenv("MIKETOPIA_BACKUP_DIR"), ec)) if (f.path().extension() == ".sql" && f.path() > best) best = f.path(); return best; };
        const fs::path before = newest();
        visitor->role = 0;
        outbox.clear(); backup_command(ev2, "backup now");
        CHECK(said("Only the owner"), "players can't back up");
        owner->role = OWNER;
        outbox.clear(); backup_command(ev, "backup now");
        CHECK(said("Backup started"), "owner starts it");
        fs::path made;
        for (int k = 0; k < 120; ++k) { Sleep(500); outbox.clear(); backup_command(ev, "backup"); if (said("Last backup: `wok") || said("Last backup: `wfailed")) break; }
        std::printf("  %s\n", all_text().c_str() + std::min<std::size_t>(all_text().find("Last backup"), all_text().size()));
        made = newest();
        CHECK(said("Last backup: `wok"), "backup finished ok");
        std::ifstream f(made); std::string sql((std::istreambuf_iterator<char>(f)), {});
        CHECK(sql.find("CREATE TABLE") != std::string::npos && sql.find("`world`") != std::string::npos, "dump has the tables");
        CHECK(!said("MYSQL_PWD") && !said("--password"), "no password in chat output");
        (void)before;
        owner->role = 0;
    }

    /* ================= Staff building ================= */
    std::puts("\n[S1] MOD/DEV build only in their own private world");
    {
        owner->recent_worlds.back() = "MTTEST"; visitor->recent_worlds.back() = "MTTEST";
        owner->pos = ::pos{ 80 * 32.0f, 20 * 32.0f }; visitor->pos = owner->pos;
        owner->slots.push_back(::slot(2, 5)); visitor->slots = { ::slot(2, 5) };
        w.is_public = false;
        owner->role = MODERATOR;
        use(ev, *owner, 2, ::pos{ 81, 20 });
        CHECK(w.blocks[cord(81, 20)].fg == 2, "MOD builds in own private world");
        w.is_public = true;
        use(ev, *owner, 2, ::pos{ 82, 20 });
        CHECK(w.blocks[cord(82, 20)].fg == 0 && said("Staff accounts can only build"), "not when the world is public");
        visitor->role = MODERATOR;
        use(ev2, *visitor, 2, ::pos{ 83, 20 });
        CHECK(w.blocks[cord(83, 20)].fg == 0, "MOD can't build in someone else's public world");
        visitor->role = 0;
        use(ev2, *visitor, 2, ::pos{ 84, 20 });
        CHECK(w.blocks[cord(84, 20)].fg == 2, "a normal player still builds in a public world");
        owner->role = OWNER;
        use(ev, *owner, 2, ::pos{ 85, 20 });
        CHECK(w.blocks[cord(85, 20)].fg == 2, "OWNER isn't limited");
        owner->role = 0; w.is_public = false;
    }

    /* ================= Tutorial ================= */
    std::puts("\n[T1] new player tutorial");
    {
        static ENetPeer np{}; ::peer *fresh = new ::peer();
        fresh->user_id = 9; fresh->netid = 9; fresh->recent_worlds.back() = "MTTEST"; fresh->growid = "MTNEW";
        np.state = ENET_PEER_STATE_CONNECTED; np.data = fresh;
        ENetEvent e3{}; e3.peer = &np;
        outbox.clear(); tutorial_command(e3, "tutorial");
        CHECK(said(">> 1. Lock your own world"), "step 1 shown first");
        const int g = fresh->gems;
        add_stat(e3, "broken", 20);
        CHECK(pget(*fresh, "tut_step") == 0 && fresh->gems == g, "breaking first doesn't skip the world lock step");
        outbox.clear(); add_stat(e3, "worlds_locked");
        CHECK(pget(*fresh, "tut_step") == 2 && fresh->gems == g + 200, "lock + already broken 20 = two steps, 200 gems");
        CHECK(said("Break 10 blocks") && said("Place 10 blocks"), "says what's done and what's next");
        add_stat(e3, "placed", 9);
        CHECK(pget(*fresh, "tut_step") == 2, "9/10 placed: not yet");
        add_stat(e3, "placed"); add_stat(e3, "planted"); add_stat(e3, "harvested");
        outbox.clear(); add_stat(e3, "store_buys");
        CHECK(pget(*fresh, "tut_step") == 6 && fresh->gems == g + 1000, "all 6 steps: 600 + 400 bonus");
        CHECK(said("Tutorial complete"), "finished message");
        add_stat(e3, "store_buys"); add_stat(e3, "broken");
        CHECK(fresh->gems == g + 1000, "no more gems after finishing");
        outbox.clear(); tutorial_command(e3, "tutorial");
        CHECK(said("You finished the tutorial"), "/tutorial shows it's finished");

        std::puts("\n[T2] the real actions count");
        owner->recent_worlds.back() = "MTTEST"; owner->pos = ::pos{ 86 * 32.0f, 20 * 32.0f };
        const int planted = pget(*owner, "planted");
        owner->slots.push_back(::slot(3, 2)); // @note Dirt Seed
        w.blocks[cord(87, 21)].fg = 2;
        use(ev, *owner, 3, ::pos{ 87, 20 });
        CHECK(pget(*owner, "planted") == planted + 1, "planting a seed counts");
    }

    /* ================= Guild ================= */
    std::puts("\n[G1] create a guild");
    {
        { ::hStmt del{ "DELETE FROM guild WHERE name = 'MTTESTG'" }; del.execute(); }
        load_guilds();
        owner->growid = "MTOWNER"; visitor->growid = "MTVISIT";
        owner->role = 0; visitor->role = 0;
        owner->level[0] = 1; owner->gems = 60000;
        auto g = [&](ENetEvent &e, const std::string &t) { outbox.clear(); guild_command(e, t); };
        auto said_to = [&](ENetPeer *who, const std::string &needle) { for (auto &pk : outbox) if (pk.peer == who && std::string(pk.data.begin(), pk.data.end()).find(needle) != std::string::npos) return true; return false; };
        g(ev, "guild create MTTESTG");
        CHECK(said("level 5") && !in_guild("MTOWNER"), "level 5 needed");
        owner->level[0] = 10;
        g(ev, "guild create a!"); CHECK(said("3-15 letters"), "bad name refused");
        g(ev, "guild create MTTESTG");
        CHECK(in_guild("MTOWNER") && owner->gems == 10000, "created, 50000 gems paid");
        CHECK(on::country_state(*owner).find("|showGuild") != std::string::npos, "|showGuild in country state");
        CHECK(said("OnGuildDataChanged"), "flag data sent");
        visitor->gems = 60000; visitor->level[0] = 10;
        g(ev2, "guild create mttestg"); CHECK(said("taken"), "name taken (any case)");

        std::puts("\n[G2] invite, accept, guild chat");
        g(ev2, "guild accept"); CHECK(said("no guild invite"), "can't join uninvited");
        g(ev, "guild invite MTVISIT"); CHECK(said_to(&enet_peers[1], "invited you"), "invite reaches the player");
        g(ev2, "guild accept"); CHECK(in_guild("MTVISIT"), "joined");
        outbox.clear(); gc_command(ev2, "gc hello guild");
        CHECK(said_to(&enet_peers[0], "[GUILD]") && said_to(&enet_peers[0], "hello guild"), "/gc reaches the leader");

        std::puts("\n[G3] ranks");
        g(ev2, "guild kick MTOWNER"); CHECK(in_guild("MTOWNER") && said("below your rank"), "member can't kick the leader");
        g(ev2, "guild invite MTOWNER"); CHECK(said("Only the leader and co-leaders"), "member can't invite");
        g(ev, "guild promote MTVISIT"); CHECK(said("Elder"), "promoted to Elder");
        g(ev, "guild promote MTVISIT"); CHECK(said("Co-Leader"), "promoted to Co-Leader");
        g(ev, "guild promote MTVISIT"); CHECK(said("can't go higher"), "no second leader by promote");
        g(ev2, "guild statement We farm together!"); CHECK(said_to(&enet_peers[0], "We farm together!"), "co-leader sets the statement");
        g(ev, "guild leave"); CHECK(in_guild("MTOWNER") && said("Give the guild"), "leader can't just leave");
        g(ev, "guild leader MTVISIT"); CHECK(said("new leader"), "leadership handed over");
        g(ev, "guild disband confirm"); CHECK(in_guild("MTOWNER") && said("Only the leader"), "ex-leader can't disband");

        std::puts("\n[G4] saved, then disbanded");
        load_guilds();
        CHECK(in_guild("MTOWNER") && in_guild("MTVISIT"), "still there after reload from the database");
        g(ev2, "guild kick MTOWNER"); CHECK(!in_guild("MTOWNER"), "new leader kicks the old one");
        g(ev2, "guild disband"); CHECK(in_guild("MTVISIT") && said("confirm"), "disband asks to confirm");
        g(ev2, "guild disband confirm"); CHECK(!in_guild("MTVISIT"), "disbanded");
        load_guilds();
        CHECK(!in_guild("MTVISIT"), "gone from the database too");
        CHECK(on::country_state(*visitor).find("|showGuild") == std::string::npos, "no |showGuild without a guild");
        { ::hStmt del{ "DELETE FROM guild WHERE name = 'MTTESTG'" }; del.execute(); }
    }

    /* ================= Sponsor items (items.dat rewrite) ================= */
    std::puts("\n[I1] sponsor items renamed in items.dat");
    {
        std::ifstream f("items.dat", std::ios::binary);
        const std::vector<u_char> file((std::istreambuf_iterator<char>(f)), {});
        u_int count{}; memcpy(&count, &file[2], 4);
        u_int file_hash = 0x55555555; for (u_char b : file) file_hash = (file_hash >> 27) + (file_hash << 5) + b;

        for (short id : { 3132, 2168, 5020, 604 })
            std::printf("  %d = %s (type %d, cloth %d) info='%s'\n", id, id_to_item(id).raw_name.c_str(), id_to_item(id).type, id_to_item(id).cloth_type, id_to_item(id).info.c_str());
        CHECK(id_to_item(3132).raw_name == "SevenPS Crown" && id_to_item(604).raw_name == "Seven's Blade", "names changed");
        CHECK(id_to_item(2168).info.find("sponsor item") != std::string::npos, "description changed");
        CHECK(items.size() == count, "every item still parses after the rewrite");
        CHECK(items_hash != file_hash, "new hash, so games download the new data");
        const std::vector<::item> patched = items;

        std::filesystem::rename("resources/sponsor.txt", "resources/sponsor.txt.off");
        items.clear(); decode_items();
        std::filesystem::rename("resources/sponsor.txt.off", "resources/sponsor.txt");
        const std::size_t header = im_data.size() - file.size();
        {
            /* @note decode_items() already zeroes the .mp3 audio fields (old Gurotopia code), so only zeroed bytes may differ */
            bool only_mp3 = im_data.size() == header + file.size();
            int changed = 0;
            for (std::size_t i = 0; only_mp3 && i < file.size(); ++i)
                if (im_data[header + i] != file[i]) { ++changed; if (im_data[header + i] != 0) only_mp3 = false; }
            std::printf("  bytes zeroed by the mp3 fix: %d\n", changed);
            CHECK(only_mp3, "without sponsors: the original file (except the mp3 fix zeroes)");
            u_int sent_hash = 0x55555555; for (std::size_t i = header; i < im_data.size(); ++i) sent_hash = (sent_hash >> 27) + (sent_hash << 5) + im_data[i];
            CHECK(items_hash == sent_hash, "hash = Proton hash of the bytes we send (GrowServer formula)");
        }
        std::printf("  items.dat hash %u (old fixed value 1086086596)\n", items_hash);

        bool same = patched.size() == items.size();
        int differ = 0;
        for (std::size_t i = 0; same && i < items.size(); ++i)
        {
            const ::item &a = patched[i], &b = items[i];
            if (a.raw_name != b.raw_name || a.info != b.info) ++differ;
            if (a.id != b.id || a.type != b.type || a.rarity != b.rarity || a.cloth_type != b.cloth_type || a.splice != b.splice || a.tick != b.tick || a.hits != b.hits) same = false;
        }
        CHECK(same && differ == 4, "only the 4 sponsor items differ, all other fields identical");
        items.clear(); decode_items(); // @note back to the sponsored data
        CHECK(id_to_item(3132).raw_name == "SevenPS Crown", "sponsored again");

        std::puts("\n[I2] /sponsor shop");
        owner->role = 0; owner->gems = 100000; owner->slot_size = 60;
        outbox.clear(); sponsor_command(ev, "sponsor");
        CHECK(said("add_button|buy_3132|") && said("SevenPS Crown"), "shop lists the items with buy buttons");
        outbox.clear(); sponsor_return(ev, ::hPipe("dialog_name|mt_sponsor|buttonClicked|buy_2"));
        CHECK(held(*owner, 2) >= 0 && owner->gems == 100000, "fake button for a non-sponsor item ignored");
        outbox.clear(); sponsor_return(ev, ::hPipe("dialog_name|mt_sponsor|buttonClicked|buy_5020"));
        CHECK(owner->gems == 100000 && said("You need 200000"), "too expensive refused");
        outbox.clear(); sponsor_return(ev, ::hPipe("dialog_name|mt_sponsor|buttonClicked|buy_3132"));
        CHECK(held(*owner, 3132) == 1 && owner->gems == 0, "bought the crown for 100000");
        owner->gems = 100000;
        outbox.clear(); sponsor_return(ev, ::hPipe("dialog_name|mt_sponsor|buttonClicked|buy_3132"));
        CHECK(held(*owner, 3132) == 1 && owner->gems == 100000 && said("already own"), "only one each");
    }

    /* ================= Login security ================= */
    std::puts("\n[A1] password hashing");
    {
        const std::string h1 = hash_password("secret1"), h2 = hash_password("secret1");
        CHECK(h1.starts_with("pbkdf2$100000$") && h1.find("secret1") == std::string::npos, "stored as PBKDF2, not plain text");
        CHECK(h1 != h2, "random salt: same password, different hash");
        CHECK(check_password(h1, "secret1") && !check_password(h1, "secret2") && !check_password(h1, ""), "right/wrong password");
        CHECK(check_password("oldpass", "oldpass") && !check_password("oldpass", "oldpas") && !check_password("oldpass", "oldpassX"), "old plain text passwords");
        CHECK(check_password("abcdefghijklmnopqr", "abcdefghijklmnopqrSTUV"), "old 18-char column cut long passwords: still accepted");

        std::puts("\n[A2] authenticate");
        { ::hStmt del{ "DELETE FROM peer WHERE growid IN ('MTAUTH','MTAUTH2')" }; del.execute(); }
        CHECK(authenticate("MTAUTH", "secret1", false, "127.0.0.1") == "That account doesn't exist.", "unknown account on step 2");
        CHECK(!authenticate("a!", "secret1", true, "127.0.0.1").empty(), "bad GrowID refused");
        CHECK(!authenticate("MTAUTH", "abc", true, "127.0.0.1").empty(), "new accounts need 4+ characters");
        CHECK(authenticate("MTAUTH", "secret1", true, "127.0.0.1").empty(), "new account allowed");
        { ::peer *np = new ::peer(); np->growid = "MTAUTH"; np->load("MTAUTH", "secret1"); } // @note never deleted (~peer writes)
        auto stored = [](const std::string &growid) {
            ::hStmt q{ "SELECT password FROM peer WHERE growid = ?" }; MYSQL_BIND pr = make_bind_in(growid); q.bind_param(&pr);
            char b[160]{}; u_long len{}; MYSQL_BIND r{}; r.buffer_type = MYSQL_TYPE_STRING; r.buffer = b; r.buffer_length = sizeof(b); r.length = &len;
            mysql_stmt_bind_result(q.pStmt, &r); q.execute(); mysql_stmt_store_result(q.pStmt); mysql_stmt_fetch(q.pStmt); return std::string(b, len); };
        CHECK(stored("MTAUTH").starts_with("pbkdf2$"), "new account saved with a hash");
        CHECK(authenticate("MTAUTH", "secret1", false, "127.0.0.1").empty(), "right password logs in");
        CHECK(authenticate("MTAUTH", "Secret1", false, "127.0.0.1") == "Wrong password.", "wrong password refused (was accepted before!)");
        for (int k = 0; k < 4; ++k) authenticate("MTAUTH", "guess" + std::to_string(k), false, "127.0.0.1");
        CHECK(authenticate("MTAUTH", "secret1", false, "127.0.0.1").starts_with("Too many"), "5 wrong tries lock it, even for the right password");

        { ::peer *np = new ::peer(); np->growid = "MTAUTH2"; np->load("MTAUTH2", "x"); }
        { ::hStmt up{ "UPDATE peer SET password = 'plainpw' WHERE growid = 'MTAUTH2'" }; up.execute(); }
        CHECK(authenticate("MTAUTH2", "plainpw", false, "127.0.0.1").empty() && stored("MTAUTH2").starts_with("pbkdf2$"), "old plain text works once and is upgraded");
        CHECK(authenticate("MTAUTH2", "plainpw", false, "127.0.0.1").empty(), "and keeps working after the upgrade");

        std::puts("\n[A3] the real login packets");
        static ENetPeer lp{}; ::peer *login = new ::peer();
        lp.state = ENET_PEER_STATE_CONNECTED; lp.data = login;
        ENetEvent le{}; le.peer = &lp;
        auto b64 = [](const std::string &in) { static const char *t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; std::string out; int val = 0, bits = -6;
            for (u_char c : in) { val = (val << 8) + c; bits += 8; while (bits >= 0) { out.push_back(t[(val >> bits) & 0x3F]); bits -= 6; } }
            if (bits > -6) out.push_back(t[((val << 8) >> (bits + 8)) & 0x3F]); while (out.size() % 4) out.push_back('='); return out; };
        outbox.clear(); action::protocol(le, "protocol|225|ltoken|" + b64("growId=MTAUTH2&password=plainpw&"));
        CHECK(said("logon_fail") && !said("OnSendToServer"), "step 1 with the old token (base64 growId + password): refused");
        login->growid.clear(); login->password.clear();
        const std::string tok = session_new("MTAUTH2");
        outbox.clear(); action::protocol(le, "protocol|225|ltoken|" + tok);
        CHECK(said("SetHasGrowID") && said("OnSendToServer") && login->growid == "MTAUTH2", "step 1 with a session token: SetHasGrowID + OnSendToServer");
        CHECK(!said("plainpw") && said(tok), "the game gets the token, never the password");

        std::string pad; for (int k = 0; k < 40; ++k) pad += "k" + std::to_string(k) + "|v|";
        ::peer *second = new ::peer(); lp.data = second;
        outbox.clear(); action::tankIDName(le, "tankIDName|MTAUTH2|tankIDPass|hacked|protocol|225|country|id|" + pad);
        CHECK(said("logon_fail") && second->growid.empty(), "step 2 with only a GrowID (no right password): refused");
        ::peer *third = new ::peer(); lp.data = third;
        outbox.clear(); action::tankIDName(le, "tankIDName|MTAUTH2|tankIDPass|plainpw|protocol|225|country|id|" + pad);
        CHECK(said("logon_fail"), "step 2 with the password instead of the token: refused");
        third->growid.clear(); third->password.clear();
        outbox.clear(); action::tankIDName(le, "tankIDName|MTAUTH2|tankIDPass|" + tok + "|protocol|225|country|id|" + pad);
        CHECK(!said("logon_fail") && said("OnSuperMainStartAcceptLogon"), "step 2 with the token: logged in");
        ::peer *fourth = new ::peer(); lp.data = fourth;
        outbox.clear(); action::tankIDName(le, "tankIDName|Mike|tankIDPass|" + tok + "|protocol|225|country|id|" + pad);
        CHECK(said("logon_fail") && fourth->growid.empty(), "MTAUTH2's token can't log in as Mike");
        third->loaded = false; // @note never saves

        std::puts("\n[A3b] staff log on as @Name in their role color");
        CHECK(role_name("Seven", OWNER) == "`9@Seven``" && role_name("Mod", MODERATOR) == "`#@Mod``" && role_name("Dev", DEVELOPER) == "`6@Dev``", "@ tag + role color for staff");
        CHECK(role_name("Bob", PLAYER) == "`2Bob``", "players: no @");
        { ::hStmt up{ "UPDATE peer SET role = 1 WHERE growid = 'MTAUTH2'" }; up.execute(); }
        ::peer *fifth = new ::peer(); lp.data = fifth;
        outbox.clear(); action::protocol(le, "protocol|225|ltoken|" + tok);
        CHECK(said("`#@MTAUTH2``"), "SetHasGrowID gives the moderator `#@MTAUTH2 (shown as 'Logging on @MTAUTH2...')");
        ::peer *sixth = new ::peer(); lp.data = sixth;
        outbox.clear(); action::tankIDName(le, "tankIDName|`#@MTAUTH2``|tankIDPass|" + tok + "|protocol|225|country|id|" + pad);
        CHECK(!said("logon_fail") && sixth->growid == "MTAUTH2", "that name comes back as tankIDName: tag taken off, logged in");
        sixth->loaded = false;

        std::puts("\n[A4] forgotten password: reset_password.txt");
        const std::string old_tok = session_new("MTAUTH2"), other_tok = session_new("MTAUTH");
        { std::ofstream r("reset_password.txt"); r << "mtauth2|newpass1\r\n"; } // @note other spelling than the account
        apply_password_reset();
        CHECK(!std::filesystem::exists("reset_password.txt"), "file deleted right away");
        CHECK(session_growid(old_tok).empty(), "saved logins of that account stop working (a stolen token too)");
        CHECK(session_growid(other_tok) == "MTAUTH", "other accounts keep theirs");
        CHECK(authenticate("MTAUTH2", "newpass1", false, "127.0.0.1").empty() && authenticate("MTAUTH2", "plainpw", false, "127.0.0.1") == "Wrong password.", "new password works, old one doesn't");
        { ::hStmt del{ "DELETE FROM peer WHERE growid IN ('MTAUTH','MTAUTH2')" }; del.execute(); }
    }

    /* ================= Security: bots, floods, made-up packets ================= */
    std::puts("\n[S1] made-up packets don't crash the server");
    {
        ::peer *owner_p = static_cast<::peer*>(ev.peer->data);
        owner_p->recent_worlds.back() = "MTTEST";
        auto raw = [&](ENetEvent &e, std::vector<u_char> bytes) {
            ENetEvent r = e; r.packet = enet_packet_create(bytes.data(), bytes.size(), 0); outbox.clear(); receive(r); };
        raw(ev, {}); raw(ev, { 2 }); raw(ev, { 3, 0, 0 }); raw(ev, { 2, 0, 0, 0 }); raw(ev, { 4, 1 });
        CHECK(true, "empty / 1-4 byte packets: still running");

        auto punch_at = [&](float x, float y) { outbox.clear(); tile_change(ev, ::gamePacket{ .type = 3, .id = 18, .pos = owner_p->pos, .punch = ::pos{ x, y } }); };
        punch_at(1e30f, 5); punch_at(std::nanf(""), 5); punch_at(150, 5); punch_at(-1, 5); punch_at(5, 99999); punch_at(99.5f, -0.5f);
        CHECK(outbox.empty(), "punch/place outside the world (NaN, 1e30, 150, -1, 99999): ignored");
        outbox.clear(); tile_activate(ev, ::gamePacket{ .type = 7, .punch = ::pos{ -5, 3 } });
        CHECK(outbox.empty(), "tile_activate outside the world: ignored");

        owner_p->pos = ::pos{ 320.0f, 320.0f };
        movement(ev, ::gamePacket{ .type = 0, .pos = ::pos{ std::nanf(""), 1e30f } });
        movement(ev, ::gamePacket{ .type = 0, .pos = ::pos{ -500.0f, 64.0f } });
        CHECK(owner_p->pos.x == 320.0f && owner_p->pos.y == 320.0f, "made-up position (NaN / outside): not taken");
        movement(ev, ::gamePacket{ .type = 0, .pos = ::pos{ 64.0f, 96.0f } });
        CHECK(owner_p->pos.x == 64.0f, "a real position still moves you");
    }

    std::puts("\n[S2] guard: floods, IP blocks, account spam, hidden passwords");
    {
        static ENetPeer fp{}; fp.address.host.v4[0] = 10; fp.address.host.v4[3] = 5;
        CHECK(peer_ip(fp) == "10.0.0.5", "peer_ip");
        CHECK(guard_connect(fp), "new connection allowed");
        bool ok = true; for (int k = 0; k < 40; ++k) ok = ok && guard_packet(fp, true);
        CHECK(ok, "40 actions in a second: fine");
        CHECK(!guard_packet(fp, true), "41st in the same second: flooding, kicked");
        CHECK(!guard_connect(fp), "...and that IP can't connect for 5 minutes");

        static ENetPeer cp{}; cp.address.host.v4[0] = 10; cp.address.host.v4[3] = 6;
        bool all = true; for (int k = 0; k < 20; ++k) all = all && guard_connect(cp);
        CHECK(all && !guard_connect(cp), "21 new connections in a minute from one IP: blocked");
        static ENetPeer lo{}; lo.address.host.v4[0] = 127; lo.address.host.v4[3] = 1;
        all = true; for (int k = 0; k < 50; ++k) all = all && guard_connect(lo);
        CHECK(all, "this computer (127.0.0.1) is never blocked");

        for (int k = 0; k < 9; ++k) guard_login_failed("10.0.0.9");
        CHECK(guard_login_blocked("10.0.0.9").empty(), "9 wrong passwords from one IP: still allowed");
        guard_login_failed("10.0.0.9");
        CHECK(!guard_login_blocked("10.0.0.9").empty(), "10th: that IP can't log in for 15 minutes");
        {
            const std::string wait = guard_login_blocked("10.0.0.9");
            CHECK(wait.ends_with("Try again in 15:00.") || wait.ends_with("Try again in 14:59."), "the wait is m:ss, so the login page can count it down");
        }
        CHECK(authenticate("Mike", "anything", false, "10.0.0.9").starts_with("Too many wrong passwords from your connection"), "authenticate refuses it before even hashing");
        for (int k = 0; k < 20; ++k) guard_login_failed("127.0.0.1");
        CHECK(guard_login_blocked("127.0.0.1").empty(), "this computer never gets an IP login block");

        int made = 0; for (int k = 0; k < 4; ++k) if (authenticate(std::format("MTBOT{}", k), "botpass", true, "10.0.0.7").empty()) ++made;
        CHECK(made == 3, "a 4th new account from one IP on the same day: refused");

        const std::string shown = guard_redact("protocol|225\nltoken|Z3Jvd0lkPU1pa2UmcGFzc3dvcmQ9eA==\ntankIDPass|hunter2\ncountry|id");
        CHECK(shown.find("hunter2") == std::string::npos && shown.find("Z3Jvd0lk") == std::string::npos && shown.find("country|id") != std::string::npos,
            "console: passwords and ltoken hidden, the rest still shown");
    }

    std::puts("\n[S3] login connections: no saving over accounts, no double sessions");
    {
        enet_initialize();
        ENetHost *real = enet_host_create(ENET_ADDRESS_TYPE_IPV4, nullptr, 4, 2, 0, 0);
        ENetHost *saved_host = host; host = real;
        for (std::size_t k = 0; k < real->peerCount; ++k) real->peers[k].address.host.v4[0] = 127;
        auto gems_in_db = [](const std::string &growid) {
            ::hStmt q{ "SELECT gems FROM peer WHERE growid = ?" }; MYSQL_BIND pr = make_bind_in(growid); q.bind_param(&pr);
            int g{}; MYSQL_BIND r{}; r.buffer_type = MYSQL_TYPE_LONG; r.buffer = &g;
            mysql_stmt_bind_result(q.pStmt, &r); q.execute(); mysql_stmt_store_result(q.pStmt); mysql_stmt_fetch(q.pStmt); return g; };
        { ::hStmt del{ "DELETE FROM peer WHERE growid = 'MTSAFE'" }; del.execute(); }
        { ::peer *np = new ::peer(); np->growid = "MTSAFE"; np->load("MTSAFE", "safepass1"); np->loaded = false; }
        { ::hStmt up{ "UPDATE peer SET gems = 5000 WHERE growid = 'MTSAFE'" }; up.execute(); }
        auto b64 = [](const std::string &in) { static const char *t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; std::string out; int val = 0, bits = -6;
            for (u_char c : in) { val = (val << 8) + c; bits += 8; while (bits >= 0) { out.push_back(t[(val >> bits) & 0x3F]); bits -= 6; } }
            if (bits > -6) out.push_back(t[((val << 8) >> (bits + 8)) & 0x3F]); while (out.size() % 4) out.push_back('='); return out; };

        ENetPeer &a = real->peers[0]; a.state = ENET_PEER_STATE_CONNECTED; a.data = new ::peer();
        ENetEvent ea{}; ea.peer = &a;
        outbox.clear(); action::protocol(ea, "protocol|225|ltoken|" + std::string(64, 'a'));
        CHECK(said("logon_fail"), "made-up token refused");
        delete static_cast<::peer*>(a.data); a.data = nullptr;
        CHECK(gems_in_db("MTSAFE") == 5000, "closing a refused login saves nothing (it wiped Mike's account!)");

        a.data = new ::peer();
        const std::string safe_tok = session_new("MTSAFE");
        outbox.clear(); action::protocol(ea, "protocol|225|ltoken|" + safe_tok);
        CHECK(said("OnSendToServer"), "good token: handed to the game connection");
        static_cast<::peer*>(a.data)->gems = 1;
        delete static_cast<::peer*>(a.data); a.data = nullptr;
        CHECK(gems_in_db("MTSAFE") == 5000, "the first (hand-over) connection never saves");

        a.data = new ::peer();
        std::string pad; for (int k = 0; k < 40; ++k) pad += "k" + std::to_string(k) + "|v|";
        outbox.clear(); action::tankIDName(ea, "tankIDName|MTSAFE|tankIDPass|" + safe_tok + "|protocol|225|country|id|" + pad);
        ::peer *first = static_cast<::peer*>(a.data);
        CHECK(first->loaded && first->gems == 5000, "logged in, 5000 gems loaded");
        first->gems = 7777; // @note earned in game, not saved yet

        ENetPeer &b = real->peers[1]; b.state = ENET_PEER_STATE_CONNECTED; b.data = new ::peer();
        ENetEvent eb{}; eb.peer = &b;
        outbox.clear(); action::tankIDName(eb, "tankIDName|mtsafe|tankIDPass|" + session_new("MTSAFE") + "|protocol|225|country|id|" + pad);
        CHECK(a.data == nullptr && a.state != ENET_PEER_STATE_CONNECTED, "logging in again closes the old session (no duplicate inventories)");
        CHECK(said("logged in from another place"), "old session is told why");
        CHECK(static_cast<::peer*>(b.data)->gems == 7777, "new session gets what the old one had (saved first, then loaded)");

        static_cast<::peer*>(b.data)->loaded = false; // @note never deleted (~peer writes)
        b.data = nullptr; b.state = ENET_PEER_STATE_DISCONNECTED;
        host = saved_host; enet_host_destroy(real);
        { ::hStmt del{ "DELETE FROM peer WHERE growid = 'MTSAFE' OR growid LIKE 'MTBOT%'" }; del.execute(); }
    }

    std::puts("\n[S4] actions before logging in are ignored");
    {
        static ENetPeer up{}; up.state = ENET_PEER_STATE_CONNECTED; up.address.host.v4[0] = 127; up.data = new ::peer();
        ENetEvent ue{}; ue.peer = &up;
        auto text = [&](const std::string &t) { std::vector<u_char> v{ 2, 0, 0, 0 }; v.insert(v.end(), t.begin(), t.end()); v.push_back(0);
            ENetEvent r = ue; r.packet = enet_packet_create(v.data(), v.size(), 0); outbox.clear(); receive(r); };
        text("action|input\n|text|/sb hi"); CHECK(outbox.empty(), "chat/commands before login: ignored");
        text("action|dialog_return\ndialog_name|mt_sponsor\nbuttonClicked|buy_3132"); CHECK(outbox.empty(), "dialogs before login: ignored");
        text("action|join_request\nname|START"); CHECK(outbox.empty(), "joining a world before login: ignored");
        std::vector<u_char> gp(4 + sizeof(::gamePacket), 0); gp[0] = 4; gp[4] = 3;
        ENetEvent r = ue; r.packet = enet_packet_create(gp.data(), gp.size(), 0); outbox.clear(); receive(r);
        CHECK(outbox.empty(), "punching/placing before login: ignored");
    }

    std::puts("\n[W1] SevenPS's own login page");
    {
        { ::hStmt del{ "DELETE FROM peer WHERE growid IN ('MTWEB','MTWEB2')" }; del.execute(); }
        auto token_of = [](const std::string &json) { const std::size_t at = json.find("\"token\":\""); return at == std::string::npos ? std::string{} : json.substr(at + 9, 64); };

        CHECK(web_login_handle("mode=login&growId=MTWEB&password=abcd1", "127.0.0.1").find("doesn't exist") != std::string::npos, "log in to an account that doesn't exist: refused");
        CHECK(web_login_handle("mode=register&growId=MTWEB&password=abcd1&confirm=abcd2", "127.0.0.1").find("not the same") != std::string::npos, "new account: passwords must match");
        CHECK(web_login_handle("mode=register&growId=MT%21WEB&password=abcd1&confirm=abcd1", "127.0.0.1").find("\"error\"") != std::string::npos, "new account: bad GrowID refused");
        const std::string made = web_login_handle("mode=register&growId=MTWEB&password=p%40ss+word%26x&confirm=p%40ss+word%26x", "127.0.0.1");
        CHECK(made.find("\"status\":\"success\"") != std::string::npos && session_growid(token_of(made)) == "MTWEB", "new account made, gets a token");
        CHECK(account_name("mtweb").value_or("") == "MTWEB", "account is in the database");
        CHECK(web_login_handle("mode=register&growId=mtweb&password=zzzz1&confirm=zzzz1", "127.0.0.1").find("already taken") != std::string::npos, "the same GrowID can't be made twice");

        const std::string ok = web_login_handle("mode=login&growId=mtweb&password=p%40ss+word%26x", "127.0.0.1");
        CHECK(session_growid(token_of(ok)) == "MTWEB", "log in with the url-encoded password (p@ss word&x), lower-case GrowID -> MTWEB");
        CHECK(ok.find("p@ss") == std::string::npos, "the answer never has the password in it");
        CHECK(web_login_handle("mode=login&growId=MTWEB&password=wrong", "127.0.0.1").find("Wrong password") != std::string::npos, "wrong password refused");
        CHECK(web_login_handle("mode=login&growId=MTWEB", "127.0.0.1").find("Fill in") != std::string::npos, "missing password");

        CHECK(session_growid("").empty() && session_growid(std::string(64, 'f')).empty() && session_growid("Z3Jvd0lkPU1pa2U=").empty(), "empty / made-up / old-style tokens: no account");
        CHECK(session_growid(token_of(ok)) == "MTWEB" && token_of(ok) != token_of(made), "every login gets its own token");
        std::string last{}; for (int k = 0; k < 6; ++k) last = session_new("MTWEB2");
        CHECK(session_growid(last) == "MTWEB2" && session_growid(token_of(made)) == "MTWEB", "at most 5 tokens per account (others untouched)");
        CHECK(form_value("a=1&growId=Mi%6Be&b=", "growId") == "Mike" && form_value("x=1", "growId").empty(), "form_value");
        CHECK(token_json("abc").find("\"accountType\":\"growtopia\"") != std::string::npos, "answer the game understands");
        { ::hStmt del{ "DELETE FROM peer WHERE growid IN ('MTWEB','MTWEB2')" }; del.execute(); }
    }

    /* ================= Items: punch effects & playmods ================= */
    std::puts("\n[I1] punch effects from resources/punch_effects.txt");
    {
        CHECK(get_punch_id(138) == 1 && get_punch_id(3066) == 57 && get_punch_id(8006) == 19, "Cyclopean Visor 1, Fire Hose 57, Hellfire Horns 19");
        CHECK(get_punch_id(2) == 0 && get_punch_id(0) == 0, "items without an effect: 0");

        ::peer *wearer = static_cast<::peer*>(ev.peer->data);
        const auto saved = wearer->clothing;
        auto state_packet = [&]() -> std::array<u_char, 4> {
            for (auto &p : outbox) if (p.peer == ev.peer && p.data.size() > 8 && p.data[0] == 4 && p.data[4] == 0x14) return { p.data[4], p.data[5], p.data[6], p.data[7] };
            return { 0, 0, 0, 0 };
        };
        wearer->clothing.fill(0);
        wearer->clothing[clothing::HAND] = 3066; // @note Fire Hose: effect 57 + punch range +1
        wearer->update_effects();
        outbox.clear(); on::SetClothing(*ev.peer);
        const auto fh = state_packet();
        CHECK(fh[1] == 57 && fh[2] == 0x80 && fh[3] == 0x81, "Fire Hose: state packet punch 57, build 128, punch range 129");

        wearer->clothing[clothing::HAND] = 0;
        wearer->clothing[clothing::HAIR] = 4748; // @note Diamond Horns: build +1
        wearer->update_effects();
        outbox.clear(); on::SetClothing(*ev.peer);
        const auto dh = state_packet();
        CHECK(dh[2] == 0x81 && dh[3] == 0x80, "Diamond Horns: build range 129");

        wearer->clothing[clothing::HAIR] = 0;
        wearer->clothing[clothing::HAND] = 1904; // @note The One Ring: eyes only
        wearer->update_effects();
        CHECK((wearer->state & 0x4) != 0, "The One Ring: invisible state bit");
        wearer->clothing[clothing::HAND] = 0;
        wearer->update_effects();
        CHECK((wearer->state & PLAYMOD_STATES) == 0 && wearer->punch_effect == 0, "taking it off clears the bit and the punch effect");

        wearer->clothing = saved;
        wearer->update_effects();
    }

    /* ================= Timed consumables (buffs) ================= */
    std::puts("\n[B1] consumables.txt is read");
    {
        const buff_def *blue = buff_for_item(196), *curse_wand = buff_for_item(278), *fire = buff_for_item(276);
        CHECK(blue && blue->target == "self" && blue->seconds == 3600 && blue->has_skin, "Blueberry: self, 1 hour, skin");
        CHECK(blue && blue->skin == 0xFF0000FFu, "blue skin in the game's order (b<<24|g<<16|r<<8|a)");
        CHECK(curse_wand && curse_wand->curse && curse_wand->target == "other", "Curse Wand curses others");
        CHECK(fire && fire->kill && fire->seconds == 0, "Fire Wand kills, no mod");
        CHECK(buff_for_item(2) == nullptr, "Dirt is not a consumable");
    }
    {
        const ::pos me_at{ 11, 20 }, them_at{ 13, 20 };
        owner->pos = ::pos{ 11 * 32.0f, 20 * 32.0f };
        visitor->pos = ::pos{ 13 * 32.0f, 20 * 32.0f };
        const auto owner_role = owner->role, visitor_role = visitor->role;
        owner->role = PLAYER; visitor->role = PLAYER;
        owner->slot_size = 60;
        for (short id : { 196, 126, 274, 764, 782, 276, 676 }) owner->slots.push_back(::slot(id, 3));

        std::puts("\n[B2] Blueberry on yourself");
        use(ev, *owner, 196, them_at); CHECK(said("Must be used on yourself") && held(*owner, 196) == 3, "not on someone else, kept");
        use(ev, *owner, 196, me_at);
        CHECK(held(*owner, 196) == 2, "one Blueberry eaten");
        CHECK(has_buff(*owner, "Feelin' Blue") && said("Feelin' Blue"), "Feelin' Blue mod added");
        CHECK(buff_skin(*owner) == 0xFF0000FFu, "skin is blue now");
        CHECK(buff_names(*owner).find("Feelin' Blue (1 hour)") != std::string::npos, "/status shows it with time left");

        std::puts("\n[B3] Devil Horns on another player, Freeze Wand only on others");
        use(ev, *owner, 126, them_at);
        CHECK(has_buff(*visitor, "Devil Horns") && (visitor->state & 0x40), "visitor has horns");
        CHECK(!has_buff(*owner, "Devil Horns"), "not me");
        use(ev, *owner, 274, me_at); CHECK(said("another player") && held(*owner, 274) == 3, "Freeze Wand refuses yourself");
        use(ev, *owner, 274, ::pos{ 30, 20 }); CHECK(said("another player") && held(*owner, 274) == 3, "nobody there, kept");
        use(ev, *owner, 274, them_at); CHECK((visitor->state & 0x800) && held(*owner, 274) == 2, "visitor frozen");

        std::puts("\n[B4] staff can't be hit by players");
        visitor->role = MODERATOR;
        use(ev, *owner, 274, them_at); CHECK(said("staff") && held(*owner, 274) == 2, "refused, kept");
        visitor->role = PLAYER;

        std::puts("\n[B5] g-Virus and Antidote");
        use(ev, *owner, 782, me_at); CHECK(said("not sick") && held(*owner, 782) == 3, "Antidote needs a virus");
        use(ev, *owner, 764, me_at);
        CHECK((owner->state & S_ZOMBIE) && has_buff(*owner, "Infected!"), "zombie");
        CHECK(player_physics(*owner).x < 250.0f, "zombies are slow");
        use(ev, *owner, 782, me_at);
        CHECK(!(owner->state & S_ZOMBIE) && !has_buff(*owner, "Infected!") && held(*owner, 782) == 2, "cured");
        CHECK(has_buff(*owner, "Feelin' Blue"), "other mods stay");

        std::puts("\n[B6] Fire Wand, Apple");
        use(ev, *owner, 276, them_at); CHECK(said("OnKilled") && held(*owner, 276) == 2, "visitor dies");
        use(ev, *owner, 676, me_at); CHECK(buff_repel(*owner), "Doctor Repulsion");

        std::puts("\n[B7] mods run out");
        for (auto &[key, value] : visitor->profile)
            if (key.starts_with("buff:")) value = std::format("{}{}", std::time(nullptr) - 1, value.substr(value.find('/')));
        visitor->loaded = owner->loaded = true; // @note buff_tick skips players still logging in
        Sleep(1100); outbox.clear(); buff_tick();
        visitor->loaded = owner->loaded = false; // @note never saved over a real account
        CHECK(said("Off the naughty list") && said("thawed out"), "removed messages");
        CHECK(!(visitor->state & (0x40 | 0x800)) && buff_names(*visitor).empty(), "horns and ice gone");
        CHECK(has_buff(*owner, "Feelin' Blue"), "owner's hour isn't up");

        std::erase_if(owner->profile, [](const auto &kv) { return kv.first.starts_with("buff:"); });
        owner->update_effects(); visitor->update_effects();
        owner->role = owner_role; visitor->role = visitor_role;
    }

    /* ================= Special blocks: loot, fossils, Mystery Pinata ================= */
    {
        const auto owner_role = owner->role;
        owner->role = PLAYER;
        owner->pos = ::pos{ 11 * 32.0f, 20 * 32.0f };
        owner->slot_size = 60;
        const auto saved_hand = owner->clothing[clothing::HAND];
        auto punch = [&](const ::pos &at) { outbox.clear(); tile_change(ev, ::gamePacket{ .type = 3, .id = 18, .pos = owner->pos, .punch = at }); };
        auto on_tile = [&](const ::pos &at, short id) {
            int n = 0;
            for (const ::object &o : w.objects) if (o.id == id && o.pos.by_32(true) == at) n += o.count;
            return n;
        };
        auto clear_tile = [&](const ::pos &at) { std::erase_if(w.objects, [&](const ::object &o) { return o.pos.by_32(true) == at; }); };
        auto break_it = [&](const ::pos &at, short id) { for (int k = 0; k < 40 && w.blocks[cord(at.x_int(), at.y_int())].fg == id; ++k) punch(at); };

        std::puts("\n[X1] Mystery Block drops 1-50 gems, no block or seed");
        const ::pos lt{ 14, 20 };
        clear_tile(lt);
        w.blocks[cord(14, 20)].fg = 120;
        break_it(lt, 120);
        CHECK(w.blocks[cord(14, 20)].fg == 0, "it broke");
        CHECK(on_tile(lt, 112) >= 1 && on_tile(lt, 112) <= 50, "1-50 gems on the ground");
        CHECK(on_tile(lt, 120) == 0 && on_tile(lt, 121) == 0, "no Mystery Block or seed");
        CHECK(said("breaks open"), "tells what came out");

        std::puts("\n[X2] Summer Surprise gives 3 prizes, Deep Iron gives ore");
        clear_tile(lt);
        w.blocks[cord(14, 20)].fg = 836;
        break_it(lt, 836);
        {
            int prizes = 0;
            for (const ::object &o : w.objects) if (o.pos.by_32(true) == lt) ++prizes;
            const std::string t = all_text();
            const std::size_t at = t.find("breaks open: ");
            const std::string line = (at != std::string::npos) ? t.substr(at, t.find("``", at) - at) : "";
            std::printf("  %s\n", line.c_str());
            CHECK(std::ranges::count(line, ',') == 2, "3 prizes named");
            CHECK(prizes >= 1, "prizes on the ground");
        }
        clear_tile(lt);
        w.blocks[cord(14, 20)].fg = 3608;
        break_it(lt, 3608);
        CHECK(on_tile(lt, 3610) >= 1 && on_tile(lt, 3610) <= 5, "1-5 Deep Iron Ore");

        std::puts("\n[X3] fossil rock broken without tools: fossil smashed, plain rock stays");
        const ::pos ft{ 16, 20 };
        clear_tile(ft);
        ::block &fb = w.blocks[cord(16, 20)];
        owner->clothing[clothing::HAND] = 0;
        fb.fg = 3918; fb.hits[0] = 0;
        break_it(ft, 3918);
        CHECK(fb.fg == 10 && said("smashed"), "Fossil Rock -> Rock");
        CHECK(on_tile(ft, 3936) == 0, "no fossil");

        std::puts("\n[X4] Rock Hammer taps never break it, then unearths; another tap smashes it");
        fb.fg = 3918; fb.hits[0] = 0;
        owner->clothing[clothing::HAND] = 3932;
        bool loose = false;
        for (int k = 0; k < 300 && !loose; ++k) { punch(ft); loose = said("unearthed"); }
        CHECK(loose, "unearthed a fossil");
        CHECK(fb.fg == 3918 && fb.hits[0] == 0, "rock undamaged by the hammer");
        punch(ft);
        CHECK(fb.fg == 10 && said("smashed"), "hammering it again smashes it");

        std::puts("\n[X5] hammer then Rock Chisel -> a Fossil drops");
        fb.fg = 3918; fb.hits[0] = 0;
        loose = false;
        for (int k = 0; k < 300 && !loose; ++k) { punch(ft); loose = said("unearthed"); }
        owner->clothing[clothing::HAND] = 3934;
        break_it(ft, 3918);
        CHECK(fb.fg == 0, "rock gone");
        CHECK(on_tile(ft, 3936) == 1, "one Fossil on the ground");

        std::puts("\n[X6] Fossil Brush on the fresh fossil -> Polished Fossil");
        owner->clothing[clothing::HAND] = 0;
        owner->slots.push_back(::slot(4132, 2));
        use(ev, *owner, 4132, ft);
        CHECK(on_tile(ft, 4134) == 1 && on_tile(ft, 3936) == 0, "polished");
        CHECK(held(*owner, 4132) == 1, "one brush used");
        use(ev, *owner, 4132, ft);
        CHECK(said("nobody has picked up") && held(*owner, 4132) == 1, "nothing to brush, brush kept");

        std::puts("\n[X7] Fossil Prep Station");
        namespace fs = std::filesystem;
        const fs::path home = fs::current_path(), sandbox = home / "build/test/prep_sandbox";
        fs::create_directories(sandbox / "resources");
        {
            std::ifstream in(home / "resources/fossils.txt");
            std::ofstream out(sandbox / "resources/fossils.txt");
            for (std::string line; std::getline(in, line); ) out << (line.starts_with("prep_time|") ? "prep_time|1|1" : line) << '\n';
        }
        fs::copy_file(home / "resources/loot.txt", sandbox / "resources/loot.txt", fs::copy_options::overwrite_existing);
        fs::current_path(sandbox); // @note a 1 second prep time without touching the real file
        const ::pos pt{ 18, 20 };
        w.blocks[cord(18, 20)].fg = 3938;
        owner->slots.push_back(::slot(3936, 1));
        use(ev, *owner, 3936, ::pos{ 19, 20 });
        CHECK(said("Prep Station") && held(*owner, 3936) == 1, "Fossil only goes into a station");
        use(ev, *owner, 3936, pt);
        CHECK(said("dissolving") && held(*owner, 3936) == 0, "fossil put in");
        punch(pt);
        CHECK(said("being prepared") && w.blocks[cord(18, 20)].fg == 3938, "can't break it while it works");
        visitor->slots = { ::slot(4134, 1) };
        outbox.clear(); tile_change(ev2, ::gamePacket{ .type = 3, .id = 4134, .pos = visitor->pos, .punch = pt });
        CHECK(held(*visitor, 4134) == 1, "visitor can't put a fossil in the owner's station");
        Sleep(1100);
        punch(pt);
        CHECK(said("turned out to be"), "prize message");
        CHECK(w.blocks[cord(18, 20)].fg == 3938, "station stays");
        punch(pt);
        CHECK(!said("being prepared") && !said("turned out"), "empty again: normal punch");
        fs::current_path(home);

        std::puts("\n[X8] Mystery Pinata changes the items under it");
        const ::pos mp{ 20, 20 };
        clear_tile(mp);
        add_object(ev, ::slot(330/*Cactus*/, 3), ::pos{ 20 * 32.0f + 4, 20 * 32.0f + 4 }, w);
        add_object(ev, ::slot(2/*Dirt*/, 5), ::pos{ 20 * 32.0f + 6, 20 * 32.0f + 6 }, w);
        w.blocks[cord(20, 20)].fg = 1584;
        break_it(mp, 1584);
        CHECK(on_tile(mp, 1578) == 3 && on_tile(mp, 330) == 0, "3 Cactus -> 3 Party Cactus");
        CHECK(on_tile(mp, 2) >= 5, "Dirt stays Dirt");

        std::puts("\n[X9] a loading world can turn a rock into a fossil rock");
        {
            /* @note on MTTEST's own blocks (a new ::world would write a row to the database), put back afterwards */
            const std::vector<::block> saved_blocks = w.blocks;
            w.blocks.assign(6000, ::block{});
            w.blocks[cord(5, 40)].fg = 10;
            bool spawned = false;
            for (int k = 0; k < 400 && !spawned; ++k) { fossil_spawn(w); spawned = w.blocks[cord(5, 40)].fg == 3918; }
            CHECK(spawned, "Rock -> Fossil Rock");
            w.blocks[cord(5, 40)].fg = 10;
            w.name = "START";
            for (int k = 0; k < 400; ++k) fossil_spawn(w);
            w.name = "MTTEST";
            CHECK(w.blocks[cord(5, 40)].fg == 10, "never in START");
            w.blocks = saved_blocks;
        }

        owner->clothing[clothing::HAND] = saved_hand;
        owner->role = owner_role;
        ::hStmt del{ "DELETE FROM world_fossil_prep WHERE world = 'MTTEST'" }; del.execute();
    }

    /* ================= Special tiles: Harmonic Crystals, Bunny Egg, tile data ================= */
    {
        namespace fs = std::filesystem;
        const auto owner_role = owner->role;
        owner->role = PLAYER;
        owner->pos = ::pos{ 11 * 32.0f, 20 * 32.0f };
        owner->slot_size = 60;
        const bool was_public = w.is_public;
        w.is_public = false;
        auto use = [&](short id, const ::pos &at) { outbox.clear(); tile_change(ev, ::gamePacket{ .type = 3, .id = id, .pos = owner->pos, .punch = at }); };
        auto punch = [&](const ::pos &at) { use(18, at); };
        auto on_tile = [&](const ::pos &at, short id) {
            int n = 0;
            for (const ::object &o : w.objects) if (o.id == id && o.pos.by_32(true) == at) n += o.count;
            return n;
        };
        auto extra_text = [&](const ::pos &at) {
            ::blob b; special_extra(b, w, at, 0x14);
            const auto &d = b.data();
            short len{}; memcpy(&len, d.data(), 2);
            return std::string(reinterpret_cast<const char*>(d.data() + 2), len);
        };
        const fs::path home = fs::current_path(), sandbox = home / "build/test/crystal_sandbox";
        fs::create_directories(sandbox / "resources");
        for (const char *f : { "chi.txt", "bunny_egg.txt", "loot.txt", "fossils.txt" })
            fs::copy_file(home / "resources" / f, sandbox / "resources" / f, fs::copy_options::overwrite_existing);
        int bump = 0;
        auto crystals_txt = [&](int accuracy) { // @note every cluster wants 1000 of each Chi: only a huge accuracy matches
            const fs::path p = sandbox / "resources/crystals.txt";
            std::ifstream in(home / "resources/crystals.txt");
            std::ofstream out(p);
            for (std::string line; std::getline(in, line); )
                out << (line.starts_with("chi|") ? "chi|1000|1000" : line.starts_with("accuracy|") ? std::format("accuracy|{0}|{0}|{0}", accuracy) : line) << '\n';
            out.close();
            fs::last_write_time(p, fs::file_time_type::clock::now() + std::chrono::seconds(++bump)); // @note always seen as changed
        };
        crystals_txt(6);
        fs::current_path(sandbox);

        std::puts("\n[Y1] a crystal on an empty tile starts a cluster");
        const ::pos ct{ 24, 20 };
        w.blocks[cord(24, 20)] = ::block{};
        owner->slots.push_back(::slot(2242, 3)); owner->slots.push_back(::slot(2244, 2)); owner->slots.push_back(::slot(2248, 1));
        owner->slots.push_back(::slot(2246, 2)); owner->slots.push_back(::slot(2250, 4));
        use(2242, ct);
        CHECK(w.blocks[cord(24, 20)].fg == 2242 && held(*owner, 2242) == 2, "Red Crystal placed");
        CHECK(extra_text(ct) == "1", "tile data: \"1\"");
        CHECK(get_type(id_to_item(2242)) == 0x14, "crystals have extra type 0x14");

        std::puts("\n[Y2] not in a public world");
        w.is_public = true;
        use(2242, ct);
        CHECK(said("World Locked world you own") && held(*owner, 2242) == 2, "refused, crystal kept");
        w.is_public = false;

        std::puts("\n[Y3] five crystals: Red Red Green Green White = Yellow Diamond recipe");
        for (short id : { 2242, 2244, 2244 }) use(id, ct);
        CHECK(extra_text(ct) == "1122", "4 crystals in it");
        use(2248, ct);
        CHECK(said("highest vibration") && extra_text(ct) == "11224", "full and sorted");
        use(2246, ct);
        CHECK(said("already at its highest") && held(*owner, 2246) == 2, "a 6th is refused");
        CHECK(w.blocks[cord(24, 20)].fg == 2242, "still one tile");

        std::puts("\n[Y4] out of tune: the wrench tells what's missing, a punch only damages");
        outbox.clear(); tile_change(ev, ::gamePacket{ .type = 3, .id = 32, .pos = owner->pos, .punch = ct });
        CHECK(said("Crystal Cluster") && said("several red") && said("out of alignment") && said("more Earth"), "dialog");
        punch(ct);
        CHECK(!said("true essence") && on_tile(ct, 3424) == 0, "no prize");
        w.blocks[cord(24, 20)].hits[0] = 0;

        std::puts("\n[Y5] in harmony: one punch reveals the prize");
        crystals_txt(100000);
        outbox.clear(); tile_change(ev, ::gamePacket{ .type = 3, .id = 32, .pos = owner->pos, .punch = ct });
        CHECK(said("perfect harmonic resonance"), "wrench says it's ready");
        punch(ct);
        CHECK(said("true essence") && on_tile(ct, 3424) == 1, "Yellow Diamond dropped");
        CHECK(w.blocks[cord(24, 20)].fg == 0, "cluster gone");

        std::puts("\n[Y6] a combination without a recipe shatters");
        use(2246, ct);
        for (int k = 0; k < 4; ++k) use(2250, ct);
        CHECK(said("shattered") && w.blocks[cord(24, 20)].fg == 0, "Blue + 4 Black shatter");
        CHECK(held(*owner, 2250) == 0, "the crystals are used up");
        fs::current_path(home);

        std::puts("\n[Y7] Magic Egg on an empty tile -> Bunny Egg, more eggs grow it");
        const ::pos et{ 26, 20 };
        w.blocks[cord(26, 20)] = ::block{};
        std::erase_if(w.objects, [&](const ::object &o) { return o.pos.by_32(true) == et; });
        owner->slots.push_back(::slot(611, 200));
        use(611, et);
        CHECK(w.blocks[cord(26, 20)].fg == 610 && held(*owner, 611) == 199, "Bunny Egg");
        for (int k = 0; k < 150; ++k) use(611, et);
        CHECK(said("15.1%") && held(*owner, 611) == 49, "151 eggs = 15.1%");
        {
            ::blob b; special_extra(b, w, et, 0x0f);
            u_int eggs{}; memcpy(&eggs, b.data().data(), 4);
            CHECK(b.data().size() == 4 && eggs == 151, "tile data: u32 151");
        }
        outbox.clear(); tile_change(ev, ::gamePacket{ .type = 3, .id = 32, .pos = owner->pos, .punch = et });
        CHECK(said("Bunny Egg") && said("151"), "wrench shows the size");
        save_special_tiles(w);
        {
            ::hStmt q{ "SELECT COUNT(*) FROM world_special_tile WHERE world = 'MTTEST' AND data = 'e|151'" };
            long long n{}; MYSQL_BIND r = make_bind_out(n);
            mysql_stmt_bind_result(q.pStmt, &r); q.execute(); mysql_stmt_store_result(q.pStmt); mysql_stmt_fetch(q.pStmt);
            CHECK(n == 1, "saved in world_special_tile");
        }

        std::puts("\n[Y8] break it: a prize for 101-200 eggs (5 pastel blocks)");
        for (int k = 0; k < 40 && w.blocks[cord(26, 20)].fg == 610; ++k) punch(et);
        int pastel = 0;
        for (short id = 510; id <= 522; id += 2) pastel += on_tile(et, id);
        CHECK(w.blocks[cord(26, 20)].fg == 0 && pastel == 5, "5 pastel blocks");
        CHECK(on_tile(et, 610) == 0 && on_tile(et, 611) == 0 && on_tile(et, 112) == 0, "no egg, seed or gems");
        use(611, et);
        for (int k = 0; k < 40 && w.blocks[cord(26, 20)].fg == 610; ++k) punch(et);
        CHECK(said("SPLAT"), "a tiny egg only splats");

        std::puts("\n[Y9] tile data the client expects");
        auto size_of = [&](u_char type) { ::blob b; special_extra(b, w, et, type); return b.data().size(); };
        CHECK(get_type(id_to_item(656)) == 0x06 && size_of(0x06) == 7, "Mailbox: 3 empty strings + flags");
        CHECK(get_type(id_to_item(982)) == 0x0a && size_of(0x0a) == 5, "Achievement Block: owner + id");
        CHECK(get_type(id_to_item(456)) == 0x08 && size_of(0x08) == 1, "Dice: the face");
        CHECK(get_type(id_to_item(872)) == 0x09 && size_of(0x09) == 4, "Chicken: provider timer");
        CHECK(get_type(id_to_item(2)) == 0x00, "Dirt: none");

        std::puts("\n[Y10] a provider grows for its own time, then a punch harvests it");
        const ::pos pv{ 28, 20 };
        w.blocks[cord(28, 20)] = ::block{};
        std::erase_if(w.objects, [&](const ::object &o) { return o.pos.by_32(true) == pv; });
        owner->slots.push_back(::slot(866, 1));
        use(866, pv);
        CHECK(w.blocks[cord(28, 20)].fg == 866 && held(*owner, 866) == 0, "Cow placed");
        CHECK(size_of(0x09) == 4, "tile data: u32 seconds");
        punch(pv);
        CHECK(!said("harvested") && on_tile(pv, 868) == 0, "not ready: no milk");
        w.blocks[cord(28, 20)].hits[0] = 0;
        outbox.clear(); tile_change(ev, ::gamePacket{ .type = 3, .id = 32, .pos = owner->pos, .punch = pv });
        CHECK(said("ready in") && (said("1d 0h") || said("23h")), "wrench: ready in about a day");
        const int cow_tick = items[866].tick;
        items[866].tick = 0; // @note no waiting a day in a test
        punch(pv);
        CHECK(said("harvested the Cow") && on_tile(pv, 868) >= 1 && on_tile(pv, 868) <= 2, "1-2 Milk");
        CHECK(w.blocks[cord(28, 20)].fg == 866 && w.blocks[cord(28, 20)].hits[0] == 0, "the Cow stays, undamaged");
        items[866].tick = cow_tick;
        punch(pv);
        CHECK(!said("harvested"), "growing again after the harvest");
        w.blocks[cord(28, 20)].hits[0] = 0;
        save_special_tiles(w);
        {
            ::hStmt q{ "SELECT COUNT(*) FROM world_special_tile WHERE world = 'MTTEST' AND data LIKE 'p|%'" };
            long long n{}; MYSQL_BIND r = make_bind_out(n);
            mysql_stmt_bind_result(q.pStmt, &r); q.execute(); mysql_stmt_store_result(q.pStmt); mysql_stmt_fetch(q.pStmt);
            CHECK(n == 1, "planting time saved");
        }

        std::puts("\n[Y11] ATM gives gems, Sheep Shears double the Wool, unknown providers say so");
        auto harvest = [&](short id) {
            w.blocks[cord(28, 20)] = ::block{}; w.blocks[cord(28, 20)].fg = id;
            std::erase_if(w.objects, [&](const ::object &o) { return o.pos.by_32(true) == pv; });
            special_placed(w, pv, id_to_item(id));
            const int tick = items[id].tick; items[id].tick = 0;
            punch(pv);
            items[id].tick = tick;
        };
        harvest(1008);
        CHECK(on_tile(pv, 112) >= 1 && on_tile(pv, 112) <= 100, "ATM: 1-100 gems");
        const auto hand = owner->clothing[clothing::HAND];
        owner->clothing[clothing::HAND] = 3914;
        harvest(3888);
        CHECK(on_tile(pv, 3890) >= 2 && on_tile(pv, 3890) <= 6 && on_tile(pv, 3890) % 2 == 0, "Sheep + shears: 2-6 Wool");
        owner->clothing[clothing::HAND] = hand;
        harvest(16104);
        CHECK(said("doesn't know its prizes"), "Gumball Machine: no prize list yet");
        w.blocks[cord(28, 20)] = ::block{};

        w.is_public = was_public;
        owner->role = owner_role;
        ::hStmt del{ "DELETE FROM world_special_tile WHERE world = 'MTTEST'" }; del.execute();
    }

    /* ================= Rewards: redeem codes, Daily Spin, Blue Gem Lock ================= */
    {
        const auto owner_role = owner->role, visitor_role = visitor->role;
        auto run = [&](void (*cmd)(ENetEvent&, const std::string_view), ENetEvent &e, const char *text) { outbox.clear(); cmd(e, text); };
        { ::hStmt d{ "DELETE FROM redeem_code WHERE code LIKE 'MTTEST%'" }; d.execute(); }
        { ::hStmt d{ "DELETE FROM redeem_used WHERE code LIKE 'MTTEST%'" }; d.execute(); }

        std::puts("\n[R1] only the owner makes codes");
        visitor->role = PLAYER;
        run(code_command, ev2, "code");
        CHECK(said("Only the owner"), "player refused");
        owner->role = OWNER;
        auto code_ui = [&](const std::string &fields) { outbox.clear(); code_return(ev, ::hPipe("dialog_name|mt_code|" + fields)); };
        run(code_command, ev, "code");
        CHECK(said("Redeem Codes") && said("Make a new code"), "/code opens the window");
        code_ui("buttonClicked|new");
        CHECK(said("New Redeem Code") && said("add_item_picker|additem"), "new code form with an item picker");
        code_ui("step|new|code|mttest1|gems|500|uses|1|days|1|amount|1|additem|242");
        CHECK(said("New Redeem Code") && said("500 gems, 1 World Lock"), "picked World Lock x1 is in the prize");
        code_ui("step|new|code|mttest1|gems|500|uses|1|days|1|amount|1|buttonClicked|create");
        CHECK(said("Code `wMTTEST1`` made") && said("/redeem MTTEST1"), "owner made MTTEST1");
        code_ui("buttonClicked|new");
        code_ui("step|new|code|MTTEST1|gems|5|uses|1|days|1|amount|1|buttonClicked|create");
        CHECK(said("already exists"), "no duplicates");
        code_ui("buttonClicked|new");
        code_ui("step|new|code||gems|0|uses|1|days|1|amount|1|buttonClicked|create");
        CHECK(said("some gems or an item"), "an empty prize is refused");
        code_ui("step|new|code||gems|10|uses|1|days|1|amount|1|buttonClicked|create");
        CHECK(said("made!") && said("/redeem "), "empty code = random code");
        { ::hStmt d{ "DELETE FROM redeem_code WHERE created_by = 'MTOWNER' AND code NOT LIKE 'MTTEST%'" }; d.execute(); }

        std::puts("\n[R2] a player redeems it once");
        visitor->slot_size = 60;
        const int gems_before = visitor->gems, wl_before = held(*visitor, 242);
        run(redeem_command, ev2, "redeem mttest1");
        CHECK(said("redeemed") && visitor->gems == gems_before + 500 && held(*visitor, 242) == wl_before + 1, "+500 gems, +1 World Lock (code is case-insensitive)");
        pset(*visitor, "redeem_try", 0);
        run(redeem_command, ev2, "redeem MTTEST1");
        CHECK(said("already redeemed") && visitor->gems == gems_before + 500, "only once");
        run(redeem_command, ev2, "redeem MTTEST9");
        CHECK(said("Wait a moment"), "guessing is slowed down");
        pset(*visitor, "redeem_try", 0);
        run(redeem_command, ev2, "redeem MTTEST9");
        CHECK(said("doesn't exist"), "unknown code");

        std::puts("\n[R3] used up / expired / staff");
        owner->role = OWNER;
        pset(*owner, "redeem_try", 0);
        run(redeem_command, ev, "redeem MTTEST1");
        CHECK(said("used up"), "1 use: gone");
        code_ui("buttonClicked|new");
        code_ui("step|new|code|MTTEST3|gems|10|uses|5|days|7|amount|1|buttonClicked|create");
        { ::hStmt u{ "UPDATE redeem_code SET expires = 1 WHERE code = 'MTTEST3'" }; u.execute(); }
        pset(*owner, "redeem_try", 0);
        run(redeem_command, ev, "redeem MTTEST3");
        CHECK(said("expired"), "expired");
        visitor->role = MODERATOR;
        pset(*visitor, "redeem_try", 0);
        run(redeem_command, ev2, "redeem MTTEST3");
        CHECK(said("Staff accounts"), "moderators can't redeem");
        visitor->role = PLAYER;
        run(redeem_command, ev2, "redeem");
        CHECK(said("mt_redeem") && said("Redeem Code"), "no code: opens the window");
        outbox.clear(); redeem_return(ev2, ::hPipe("dialog_name|mt_redeem|code|MTTEST1"));
        CHECK(said("already redeemed") || said("Wait a moment"), "the window redeems too");
        run(code_command, ev, "code");
        CHECK(said("MTTEST1") && said("MTTEST3") && said("del_MTTEST3"), "the window lists the codes");
        code_ui("buttonClicked|del_MTTEST3");
        CHECK(said("Delete MTTEST3?") && !said("deleted"), "Delete asks first");
        code_ui("buttonClicked|back");
        CHECK(said("del_MTTEST3"), "No keeps it");
        code_ui("buttonClicked|yesdel_MTTEST3");
        CHECK(said("deleted") && !said("del_MTTEST3"), "Yes deletes it");

        std::puts("\n[R4] Daily Spin once a day");
        pset(*visitor, "spin_day", 0);
        const int g0 = visitor->gems;
        const std::size_t kinds0 = visitor->slots.size();
        int count0 = 0; for (const ::slot &s : visitor->slots) count0 += s.count;
        run(spin_command, ev2, "spin");
        int count1 = 0; for (const ::slot &s : visitor->slots) count1 += s.count;
        CHECK(said("Lucky Wheel") && (visitor->gems > g0 || count1 > count0 || visitor->slots.size() > kinds0), "won something");
        run(spin_command, ev2, "spin");
        CHECK(said("already spun"), "only once a day");

        std::puts("\n[R5] 100 Diamond Locks <-> Blue Gem Lock");
        std::erase_if(visitor->slots, [](const ::slot &s) { return s.id == 1796 || s.id == 7188; });
        visitor->slots.push_back(::slot(1796, 100));
        outbox.clear(); item_activate(ev2, ::gamePacket{ .id = 1796 });
        CHECK(held(*visitor, 7188) == 1 && held(*visitor, 1796) == 0, "100 DL -> 1 BGL");
        outbox.clear(); item_activate(ev2, ::gamePacket{ .id = 7188 });
        CHECK(held(*visitor, 7188) == 0 && held(*visitor, 1796) == 100, "1 BGL -> 100 DL");
        std::erase_if(visitor->slots, [](const ::slot &s) { return s.id == 1796 || s.id == 7188 || s.count == 0; });

        std::puts("\n[R6] ranks spin more: Super VIP 2 a day");
        load_ranks();
        const int rank_before = visitor->rank_level;
        visitor->rank_level = 2;
        pset(*visitor, "spin_day", 0);
        run(spin_command, ev2, "spin");
        CHECK(said("1`` more spin"), "first spin: 1 more today");
        run(spin_command, ev2, "spin");
        CHECK(said("Come back tomorrow"), "second spin");
        run(spin_command, ev2, "spin");
        CHECK(said("already spun"), "no third");

        std::puts("\n[R7] Lucky Box: costs gems, gives a prize, ranks get a discount");
        visitor->rank_level = 0;
        visitor->gems = 1000;
        run(lucky_box_command, ev2, "luckybox");
        CHECK(said("Lucky Box") && said("open1") && said("2000``") && said("World Lock"), "window with price and prizes");
        outbox.clear(); lucky_box_return(ev2, ::hPipe("dialog_name|mt_lucky_box|buttonClicked|open1"));
        CHECK(said("You need") && visitor->gems == 1000, "not enough gems: nothing happens");
        visitor->gems = 30000;
        int items_before = 0; for (const ::slot &s : visitor->slots) items_before += s.count;
        outbox.clear(); lucky_box_return(ev2, ::hPipe("dialog_name|mt_lucky_box|buttonClicked|open10"));
        int items_after = 0; for (const ::slot &s : visitor->slots) items_after += s.count;
        CHECK(said("You got:") && (visitor->gems > 10000 - 1 || items_after > items_before), "10 boxes opened");
        CHECK(visitor->gems < 30000 - 20000 + 10 * 1200 + 1, "paid 20000 (gem prizes at most 1200 each)");
        visitor->rank_level = 5;
        run(lucky_box_command, ev2, "luckybox");
        CHECK(said("1500``") && said("25% off"), "GOD pays 1500");
        visitor->rank_level = rank_before;
        visitor->role = MODERATOR;
        run(lucky_box_command, ev2, "luckybox");
        CHECK(said("Staff accounts"), "staff can't");
        visitor->role = PLAYER;

        std::puts("\n[R8] /top is a menu");
        run(top, ev2, "top");
        CHECK(said("Leaderboards") && said("top_played") && said("top_level"), "menu with buttons");
        outbox.clear(); top_return(ev2, ::hPipe("dialog_name|mt_top|buttonClicked|top_played"));
        CHECK(said("Top 10: Most Played") && said("You are `w#"), "a list and your place");

        owner->role = owner_role, visitor->role = visitor_role;
        { ::hStmt d{ "DELETE FROM redeem_code WHERE code LIKE 'MTTEST%'" }; d.execute(); }
        { ::hStmt d{ "DELETE FROM redeem_used WHERE code LIKE 'MTTEST%'" }; d.execute(); }
    }

    /* ================= Chemical Combiners ================= */
    {
        const auto owner_role = owner->role;
        owner->role = PLAYER;
        owner->pos = ::pos{ 11 * 32.0f, 20 * 32.0f };
        const ::pos lt{ 30, 20 };
        auto punch = [&]() { outbox.clear(); tile_change(ev, ::gamePacket{ .type = 3, .id = 18, .pos = owner->pos, .punch = lt }); };
        auto on_tile = [&](short id) { int n = 0; for (const ::object &o : w.objects) if (o.id == id && o.pos.by_32(true) == lt) n += o.count; return n; };
        auto drop = [&](short id, short count) { add_object(ev, ::slot(id, count), ::pos{ 30 * 32.0f + 8, 20 * 32.0f + 8 }, w); };
        ::block &lb = w.blocks[cord(30, 20)];
        lb = ::block{}; lb.fg = 912;
        std::erase_if(w.objects, [&](const ::object &o) { return o.pos.by_32(true) == lt; });

        std::puts("\n[K1] a punch opens the Laboratory");
        punch();
        CHECK(lb.state[2] & S_TOGGLE, "open");
        lb.hits[0] = 0;

        std::puts("\n[K2] Combover = 1 Chemical G + 5 Chemical R + 2 Chemical Y");
        drop(914, 3); drop(916, 5); drop(924, 2); drop(2, 4);
        punch();
        CHECK(said("made `2Combover") && on_tile(770) == 1, "Combover dropped");
        CHECK(on_tile(914) == 2 && on_tile(916) == 0 && on_tile(924) == 0, "used exactly the recipe, 2 Chemical G left");
        CHECK(on_tile(2) == 4 && (lb.state[2] & S_TOGGLE) && lb.hits[0] == 0, "other items stay, still open, no damage");

        std::puts("\n[K3] no recipe: the punch closes it");
        punch();
        CHECK(!said("made") && !(lb.state[2] & S_TOGGLE), "closed");
        lb.hits[0] = 0;

        std::puts("\n[K4] Chocolate Sprinkles Block only in a Microwave");
        std::erase_if(w.objects, [&](const ::object &o) { return o.pos.by_32(true) == lt; });
        lb.state[2] |= S_TOGGLE;
        drop(1358, 10); drop(5374, 10); drop(954, 25);
        punch();
        CHECK(said("melt the chocolate") && on_tile(5376) == 0 && on_tile(954) == 25, "Laboratory refuses, items kept");
        lb.fg = 1472; lb.hits[0] = 0;
        punch();
        CHECK(on_tile(5376) == 5 && on_tile(954) == 0, "Microwave makes 5");
        lb = ::block{};
        std::erase_if(w.objects, [&](const ::object &o) { return o.pos.by_32(true) == lt; });
        owner->role = owner_role;
    }

    /* ================= Titles survive leaving a world ================= */
    {
        std::puts("\n[T9] Legend, Dr. and Blue Name after leaving and joining a world again");
        const auto saved_titles = visitor->titles, saved_on = visitor->titles_on;
        const auto saved_role = visitor->role;
        const auto saved_rank = visitor->rank_level;
        visitor->role = PLAYER, visitor->rank_level = 0;
        visitor->titles = visitor->titles_on = T_LEGEND | T_DOCTOR | T_BLUENAME;
        w.visitors += 10; // @note leaving must never drop MTTEST from memory (the tests still use it)
        outbox.clear(); action::quit_to_exit(ev2, "", true);
        outbox.clear(); action::join_request(ev2, "", "MTTEST");
        CHECK(said("OnNameChanged") && said("Dr. MTVISIT of Legend"), "the name with its titles is sent again");
        CHECK(said("|maxLevel") && said("|doctor"), "blue name and Dr. effects are sent again");
        outbox.clear(); tile_change(ev2, ::gamePacket{ .type = 3, .id = 18, .pos = visitor->pos, .punch = ::pos{ 0, 0 } }); // @note anything, then leave/join once more
        outbox.clear(); action::quit_to_exit(ev2, "", true);
        outbox.clear(); action::join_request(ev2, "", "MTTEST");
        CHECK(said("of Legend") && said("|maxLevel") && said("|doctor"), "and again the next time");
        w.visitors -= 10;
        visitor->titles = saved_titles, visitor->titles_on = saved_on, visitor->role = saved_role, visitor->rank_level = saved_rank;
    }

    /* ================= /ghost is staff only ================= */
    {
        std::puts("\n[T10] /ghost only for moderators and above");
        const auto saved_role = visitor->role;
        visitor->role = PLAYER; visitor->state &= ~S_GHOST;
        outbox.clear(); ghost(ev2, "ghost");
        CHECK(said("moderators and above") && !(visitor->state & S_GHOST), "a player can't");
        visitor->role = MODERATOR;
        outbox.clear(); ghost(ev2, "ghost");
        CHECK(visitor->state & S_GHOST, "a moderator can");
        visitor->role = PLAYER; visitor->update_effects();
        CHECK(!(visitor->state & S_GHOST), "demoted: ghost is gone");
        visitor->role = saved_role;
    }

    /* ================= Bug scan 2026-10-17: exploits and crashes found in the full scan ================= */
    {
        std::puts("\n[BUG1] a faked trash dialog can't create items");
        std::erase_if(visitor->slots, [](const ::slot &s) { return s.id == 242; });
        outbox.clear(); trash_item(ev2, ::hPipe("dialog_name|trash_item|itemID|242|count|-200"));
        CHECK(held(*visitor, 242) == 0, "minus count of an item we don't have: nothing made (was 200 free World Locks)");
        visitor->slots.push_back(::slot(242, 5));
        trash_item(ev2, ::hPipe("dialog_name|trash_item|itemID|242|count|-200"));
        CHECK(held(*visitor, 242) == 5, "minus count of an item we have: nothing changes");
        trash_item(ev2, ::hPipe("dialog_name|trash_item|itemID|242|count|3"));
        CHECK(held(*visitor, 242) == 2, "trashing 3 of 5 leaves 2");
        std::erase_if(visitor->slots, [](const ::slot &s) { return s.id == 242 || s.id == 4; });
        visitor->emplace(::slot(4, -5));
        CHECK(std::ranges::find(visitor->slots, static_cast<short>(4), &::slot::id) == visitor->slots.end(), "taking an item we don't have makes no stack");
        CHECK(visitor->emplace(::slot(4, 250)) == 50 && held(*visitor, 4) == 200, "a new stack stops at 200, the rest comes back");
        std::erase_if(visitor->slots, [](const ::slot &s) { return s.id == 4; });

        std::puts("\n[BUG2] clothes can only be put on if they're in the backpack");
        std::erase_if(visitor->slots, [&](const ::slot &s) { return s.id == hat2; });
        visitor->clothing[clothing::HAIR] = 0;
        outbox.clear(); item_activate(ev2, ::gamePacket{ .type = 10, .id = hat2 });
        CHECK(visitor->clothing[clothing::HAIR] != hat2, "a hat we don't own stays off");
        visitor->slots.push_back(::slot(hat2, 1));
        item_activate(ev2, ::gamePacket{ .type = 10, .id = hat2 });
        CHECK(visitor->clothing[clothing::HAIR] == hat2, "an owned hat goes on");
        item_activate(ev2, ::gamePacket{ .type = 10, .id = hat2 });
        CHECK(visitor->clothing[clothing::HAIR] == 0, "and comes off again");
        std::erase_if(visitor->slots, [&](const ::slot &s) { return s.id == hat2; });

        std::puts("\n[BUG3] picking up a drop that only partly fits doesn't duplicate it");
        std::erase_if(visitor->slots, [](const ::slot &s) { return s.id == 2; });
        std::erase_if(w.objects, [](const ::object &o) { return o.id == 2; });
        visitor->slots.push_back(::slot(2, 150));
        visitor->pos = ::pos{ 11 * 32.0f, 20 * 32.0f };
        const int uid = add_object(ev2, ::slot(2, 100), visitor->pos, w);
        outbox.clear(); item_activate_object(ev2, ::gamePacket{ .type = 0x0b, .id = uid });
        auto drop = std::ranges::find(w.objects, static_cast<u_int>(uid), &::object::uid);
        CHECK(held(*visitor, 2) == 200, "only what fits is picked up");
        CHECK(drop != w.objects.end() && drop->count == 50, "the drop keeps the other 50 (it kept 100 before: free items)");
        item_activate_object(ev2, ::gamePacket{ .type = 0x0b, .id = uid });
        drop = std::ranges::find(w.objects, static_cast<u_int>(uid), &::object::uid);
        CHECK(held(*visitor, 2) == 200 && drop != w.objects.end() && drop->count == 50, "full stack: nothing more taken");
        std::erase_if(w.objects, [](const ::object &o) { return o.id == 2; });
        std::erase_if(visitor->slots, [](const ::slot &s) { return s.id == 2; });

        std::puts("\n[BUG4] Thermonuclear Blast never wipes an existing world");
        std::erase_if(visitor->slots, [](const ::slot &s) { return s.id == 1402; });
        outbox.clear(); create_blast(ev2, ::hPipe("dialog_name|create_blast|id|1402|name|MTBLASTX"));
        CHECK(!::world::exists("MTBLASTX"), "no blast in the backpack: no world made");
        visitor->slots.push_back(::slot(1402, 1));
        const short mannequin_before = w.blocks[cord(10, 20)].fg;
        outbox.clear(); create_blast(ev2, ::hPipe("dialog_name|create_blast|id|1402|name|mttest"));
        CHECK(said("already exists") && held(*visitor, 1402) == 1 && w.blocks[cord(10, 20)].fg == mannequin_before, "an existing world is refused and the blast kept");
        outbox.clear(); create_blast(ev2, ::hPipe("dialog_name|create_blast|id|1402|name|a b"));
        CHECK(said("letters and numbers") && held(*visitor, 1402) == 1, "a bad name is refused");
        std::erase_if(visitor->slots, [](const ::slot &s) { return s.id == 1402; });

        std::puts("\n[BUG5] short and garbage packets don't crash");
        outbox.clear();
        action::wrench(ev2, "action|wrench");
        action::trash(ev2, "action|trash");
        action::drop(ev2, "action|drop");
        action::itemfavourite(ev2, "action|itemfavourite|itemID|abc"); // @note stoi("abc") ended the server
        action::itemfavourite(ev2, "action|itemfavourite");
        action::info(ev2, "action|info|itemID");
        action::store(ev2, "action|store");
        CHECK(true, "wrench/trash/drop/itemfavourite/info/store with missing fields");

        std::puts("\n[BUG6] world save/load: a door, sign and seed without their data don't shift the rest of the world");
        {
            for (const char *table : { "world", "vending", "world_door", "world_area_lock", "world_box", "world_magplant", "world_mannequin", "world_fossil_prep", "world_special_tile" })
            {
                ::hStmt del{ std::format("DELETE FROM {} WHERE {} = 'MTSAVE'", table, std::string_view{ table } == "world" ? "name" : "world") }; del.execute();
            }
            short door_id = 0, seed_id = 0, sign_id = 0;
            for (const ::item &it : items)
            {
                if (!door_id && it.type == type::DOOR) door_id = it.id;
                if (!seed_id && it.type == type::SEED) seed_id = it.id;
                if (!sign_id && it.type == type::SIGN) sign_id = it.id;
            }
            ::world saved{ "MTSAVE" };
            saved.blocks[cord(5, 5)].fg = door_id; std::erase_if(saved.doors, [](const ::door &d) { return d.pos == ::pos{ 5, 5 }; });
            saved.blocks[cord(6, 5)].fg = seed_id; std::erase_if(saved.trees, [](const ::tree &t) { return t.pos == ::pos{ 6, 5 }; });
            saved.blocks[cord(7, 5)].fg = sign_id; saved.signs.emplace_back("HELLO", ::pos{ 7, 5 });
            saved.blocks[cord(8, 5)].fg = 2;
            saved.save();
            ::world loaded{ "MTSAVE" };
            bool same = loaded.blocks.size() == saved.blocks.size();
            for (std::size_t i = 0; same && i < saved.blocks.size(); ++i)
                same = (loaded.blocks[i].fg == saved.blocks[i].fg || saved.blocks[i].fg == 10/*a rock may turn into a fossil*/) && loaded.blocks[i].bg == saved.blocks[i].bg;
            CHECK(same, "every block loads back the same");
            auto sign = std::ranges::find(loaded.signs, ::pos{ 7, 5 }, &::sign::pos);
            CHECK(sign != loaded.signs.end() && sign->label == "HELLO", "the sign after them keeps its text");
            CHECK(std::ranges::find(loaded.trees, ::pos{ 6, 5 }, &::tree::pos) != loaded.trees.end(), "the seed got a timer");
            ::blob map = saved.serialize();
            CHECK(map.size() > 6000 * 8, "the world with them still serializes for the game");
            for (const char *table : { "world", "vending", "world_door", "world_area_lock", "world_box", "world_magplant", "world_mannequin", "world_fossil_prep", "world_special_tile" })
            {
                ::hStmt del{ std::format("DELETE FROM {} WHERE {} = 'MTSAVE'", table, std::string_view{ table } == "world" ? "name" : "world") }; del.execute();
            }
        }

        std::puts("\n[BUG7] logging in again on the same connection is ignored (it reloaded the backpack: duplication)");
        {
            const std::string growid = visitor->growid;
            visitor->growid = "MTVISITOR"; visitor->gems = 1234; visitor->loaded = true;
            outbox.clear(); action::tankIDName(ev2, "tankIDName|MTVISITOR|tankIDPass|madeup|protocol|225");
            CHECK(visitor->growid == "MTVISITOR" && visitor->gems == 1234 && outbox.empty(), "tankIDName after logging in does nothing");
            action::protocol(ev2, "protocol|225|ltoken|madeup");
            CHECK(visitor->growid == "MTVISITOR" && outbox.empty(), "protocol after logging in does nothing");
            visitor->loaded = false; // @note never saved over a real account
            visitor->growid = growid;
        }

        std::puts("\n[BUG8] trade: changing the offer after the other side confirmed cancels the trade");
        {
            owner->slots.push_back(::slot(4, 10));
            std::erase_if(visitor->slots, [](const ::slot &s) { return s.id == 242; });
            visitor->slots.push_back(::slot(242, 5));
            owner->trade_netid = visitor->netid; visitor->trade_netid = owner->netid;
            owner->trading = visitor->trading = true;
            owner->trade_items = { ::slot(4, 10) }; visitor->trade_items = { ::slot(242, 5) };
            owner->trade_accepted = visitor->trade_accepted = true;
            outbox.clear(); trade_confirm(ev, ::hPipe("dialog_name|trade_confirm|buttonClicked|accept"));
            CHECK(owner->trade_confirmed, "the owner confirmed the deal");
            action::rem_trade(ev2, "action|rem_trade|itemID|242"); // @note the visitor takes back their World Locks
            trade_confirm(ev2, ::hPipe("dialog_name|trade_confirm|buttonClicked|accept"));
            CHECK(held(*owner, 4) == 10 && held(*owner, 242) == 0 && held(*visitor, 242) == 5 && held(*visitor, 4) == 0, "nothing was swapped (the owner would have given 10 lava for nothing)");
            end_trade(enet_peers[0]);
            std::erase_if(owner->slots, [](const ::slot &s) { return s.id == 4; });
            std::erase_if(visitor->slots, [](const ::slot &s) { return s.id == 242; });
        }

        std::puts("\n[BUG9] punching or building in someone else's world says who locked it");
        {
            owner->growid = "MTOWNER"; const auto role = visitor->role; visitor->role = 0; w.is_public = false;
            outbox.clear(); tile_change(ev2, ::gamePacket{ .type = 3, .id = 18, .pos = visitor->pos, .punch = ::pos{ 10, 20 } });
            CHECK(said("World Locked") && said("`2MTOWNER"), "the visitor is told the world is locked by MTOWNER (green, the owner's color)");
            const auto owner_role = owner->role; owner->role = MODERATOR;
            std::this_thread::sleep_for(std::chrono::milliseconds(1100)); // @note the message shows once a second
            outbox.clear(); tile_change(ev2, ::gamePacket{ .type = 3, .id = 18, .pos = visitor->pos, .punch = ::pos{ 10, 20 } });
            CHECK(said("@MTOWNER"), "a staff owner shows with their @ tag");
            owner->role = owner_role;
            visitor->role = role;
        }

        std::puts("\n[BUG10] \"My Worlds\" comes back after logging in again (it was never saved)");
        {
            owner->my_worlds.fill("");
            owner->load_my_worlds();
            CHECK(std::ranges::find(owner->my_worlds, std::string{ "MTTEST" }) != owner->my_worlds.end(), "the owner's locked world is in My Worlds");
            visitor->my_worlds.fill("");
            visitor->load_my_worlds();
            CHECK(std::ranges::find(visitor->my_worlds, std::string{ "MTTEST" }) == visitor->my_worlds.end(), "not in someone else's");
        }
    }

    /* ================= Every item: place, wrench, punch until it breaks ================= */
    {
        std::puts("\n[Z1] every item in items.dat on a tile: no crash, no hang, the world still serializes");
        const auto owner_role = owner->role;
        owner->role = PLAYER;
        owner->pos = ::pos{ 11 * 32.0f, 20 * 32.0f };
        const bool was_public = w.is_public;
        w.is_public = false;
        const ::pos at{ 50, 10 };
        ::block &b = w.blocks[cord(50, 10)];
        int placed = 0, broke = 0, tried = 0;
        std::vector<int> unbreakable{};
        for (const ::item &it : items)
        {
            switch (it.type)
            {
                /* @note not placed on a tile: worn, used, or they change the whole world / account */
                case type::FIST: case type::WRENCH: case type::CONSUMEABLE: case type::CLOTHING: case type::LOCK:
                case type::MAIN_DOOR: case type::MAGPLANT_REMOTE: case type::GEM: continue;
            }
            if (it.id == 0 || it.raw_name.empty() || it.raw_name.starts_with("null_item")) continue;
            ++tried;
            if (tried % 1000 == 0) std::printf("  ... %d items (now %d %s)\n", tried, it.id, it.raw_name.c_str());
            b = ::block{};
            std::erase_if(w.objects, [&](const ::object &o) { return o.pos.by_32(true) == at; });
            std::erase_if(owner->slots, [&](const ::slot &s) { return s.id == it.id; });
            owner->slots.push_back(::slot(it.id, 1));
            outbox.clear(); tile_change(ev, ::gamePacket{ .type = 3, .id = it.id, .pos = owner->pos, .punch = at });
            if (b.fg == 0 && b.bg == 0) continue;
            ++placed;
            outbox.clear(); tile_change(ev, ::gamePacket{ .type = 3, .id = 32, .pos = owner->pos, .punch = at });
            for (int k = 0; k < 150 && (b.fg != 0 || b.bg != 0); ++k)
            {
                outbox.clear(); tile_change(ev, ::gamePacket{ .type = 3, .id = 18, .pos = owner->pos, .punch = at });
            }
            if (b.fg == 0 && b.bg == 0) ++broke;
            else unbreakable.push_back(it.id);
        }
        std::printf("  %d tried, %d placed, %d broken, %zu never broke (e.g.", tried, placed, broke, unbreakable.size());
        for (std::size_t k = 0; k < std::min<std::size_t>(unbreakable.size(), 8); ++k) std::printf(" %d %s,", unbreakable[k], id_to_item(unbreakable[k]).raw_name.c_str());
        std::puts(")");
        if (std::FILE *f = std::fopen("build/test/unbreakable.txt", "w")) { for (int id : unbreakable) std::fprintf(f, "%d|%d|%s\n", id, id_to_item(id).type, id_to_item(id).raw_name.c_str()); std::fclose(f); }
        CHECK(placed > 3000, "thousands of items placed and punched without a crash");
        b = ::block{};
        std::erase_if(w.objects, [&](const ::object &o) { return o.pos.by_32(true) == at; });
        { ::blob wb = w.serialize(); CHECK(wb.size() > 6000 * 8, "the world still serializes"); }
        w.is_public = was_public;
        owner->role = owner_role;
    }

    /* ================= World Lock access list, World Level, reach ================= */
    {
        const bool was_public = w.is_public;
        const auto saved_access = w.access;
        const auto saved_vslots = visitor->slots, saved_oslots = owner->slots;
        const auto saved_vrole = visitor->role, saved_orole = owner->role;
        const auto saved_vpos = visitor->pos, saved_opos = owner->pos;
        visitor->role = PLAYER, owner->role = PLAYER;
        w.is_public = false; w.access.fill(0);
        const ::pos lock{ 60, 10 }, spot{ 63, 20 };
        w.blocks[cord(60, 10)] = ::block{}; w.blocks[cord(60, 10)].fg = 242; // @note World Lock
        w.blocks[cord(63, 20)] = ::block{};
        visitor->pos = owner->pos = ::pos{ 61 * 32.0f, 20 * 32.0f };
        visitor->slots = { ::slot(2, 10) }; owner->slots = { ::slot(2, 10) }; // @note Dirt

        std::puts("\n[WL1] no access: a visitor can't build");
        outbox.clear(); tile_change(ev2, ::gamePacket{ .type = 3, .id = 2, .pos = visitor->pos, .punch = spot });
        CHECK(w.blocks[cord(63, 20)].fg == 0, "visitor refused");
        const std::size_t plain = w.serialize().size();

        std::puts("\n[WL2] owner adds the visitor with the player picker");
        outbox.clear(); lock_edit(ev, ::hPipe("dialog_name|lock_edit|tilex|60|tiley|10|playerNetID|2|checkbox_public|0|checkbox_disable_music|0|minimum_entry_level|1"));
        CHECK(std::ranges::count(w.access, 2) == 1, "visitor on the access list");
        CHECK(said("now has access"), "owner told");
        CHECK(w.serialize().size() == plain + 4, "the lock sends the user id (4 bytes)");
        outbox.clear(); lock_edit(ev, ::hPipe("dialog_name|lock_edit|tilex|60|tiley|10|access_2|1|playerNetID|2|checkbox_public|0|checkbox_disable_music|0|minimum_entry_level|1"));
        CHECK(std::ranges::count(w.access, 2) == 1, "never twice");
        outbox.clear(); lock_edit(ev2, ::hPipe("dialog_name|lock_edit|tilex|60|tiley|10|playerNetID|1|checkbox_public|1|checkbox_disable_music|0|minimum_entry_level|1"));
        CHECK(!w.is_public, "access is not ownership: the visitor can't edit the lock");
        outbox.clear(); tile_change(ev2, ::gamePacket{ .type = 3, .id = 2, .pos = visitor->pos, .punch = spot });
        CHECK(w.blocks[cord(63, 20)].fg == 2, "visitor with access builds");

        std::puts("\n[WL3] saved and loaded with the world, unticking removes");
        w.save();
        CHECK(w.mysql_select<std::string>("access") == "2", "access saved");
        outbox.clear(); lock_edit(ev, ::hPipe("dialog_name|lock_edit|tilex|60|tiley|10|access_2|0|checkbox_public|0|checkbox_disable_music|0|minimum_entry_level|5"));
        CHECK(std::ranges::count(w.access, 2) == 0, "unticked = removed");
        CHECK(w.minimum_entry_level == 5, "World Level 5");
        w.save();
        CHECK(w.mysql_select<std::string>("access").empty() && w.mysql_select<int>("min_level") == 5, "removal and level saved");

        std::puts("\n[WL4] World Level keeps lower players out");
        visitor->level[0] = 3;
        w.visitors += 10;
        outbox.clear(); action::quit_to_exit(ev2, "", true);
        outbox.clear(); action::join_request(ev2, "", "MTTEST");
        CHECK(said("OnFailedToEnterWorld") && said("lower than level"), "level 3 refused from a level 5 world");
        visitor->level[0] = 5;
        outbox.clear(); action::join_request(ev2, "", "MTTEST");
        CHECK(!said("OnFailedToEnterWorld"), "level 5 enters");
        w.access[0] = 2, visitor->level[0] = 1;
        outbox.clear(); action::quit_to_exit(ev2, "", true);
        outbox.clear(); action::join_request(ev2, "", "MTTEST");
        CHECK(!said("OnFailedToEnterWorld"), "access list always enters");
        w.visitors -= 10;
        w.minimum_entry_level = 1; w.access.fill(0);

        std::puts("\n[WL5] reach: no building across the world");
        max_reach = 7.0f;
        owner->pos = ::pos{ 61 * 32.0f, 20 * 32.0f };
        w.blocks[cord(90, 20)] = ::block{}; w.blocks[cord(64, 20)] = ::block{};
        outbox.clear(); tile_change(ev, ::gamePacket{ .type = 3, .id = 2, .pos = owner->pos, .punch = ::pos{ 90, 20 } });
        CHECK(w.blocks[cord(90, 20)].fg == 0, "29 tiles away refused");
        outbox.clear(); tile_change(ev, ::gamePacket{ .type = 3, .id = 2, .pos = owner->pos, .punch = ::pos{ 64, 20 } });
        CHECK(w.blocks[cord(64, 20)].fg == 2, "3 tiles away placed");
        max_reach = 1e9f;

        w.blocks[cord(60, 10)] = ::block{}; w.blocks[cord(63, 20)] = ::block{}; w.blocks[cord(64, 20)] = ::block{};
        w.is_public = was_public; w.access = saved_access;
        visitor->slots = saved_vslots, owner->slots = saved_oslots;
        visitor->role = saved_vrole, owner->role = saved_orole;
        visitor->pos = saved_vpos, owner->pos = saved_opos;
    }

    /* ================= Surgery (Surg-E) ================= */
    {
        const auto saved_slots = owner->slots;
        const ::pos st{ 30, 40 };
        constexpr short tools[]{ 1258, 1260, 1262, 1264, 1266, 1268, 1270, 4308, 4310, 4312, 4314, 4316, 4318 };
        auto surg = [&](const std::string &clicked) { outbox.clear(); surgery_return(ev, ::hPipe("dialog_name|surge|buttonClicked|" + clicked)); };
        auto dialog = [&]() { const std::string s = all_text(); const auto at = s.rfind("OnDialogRequest"); return at == std::string::npos ? std::string{} : s.substr(at); };
        auto has = [](const std::string &d, std::string_view n) { return d.find(n) != std::string::npos; };

        std::puts("\n[SG1] wrench a Surg-E -> 'are you sure?' with a low supply warning");
        w.blocks[cord(st.x_int(), st.y_int())] = ::block{}; w.blocks[cord(st.x_int(), st.y_int())].fg = 4296;
        owner->slots.clear(); owner->malpractice = 0;
        outbox.clear(); tile_change(ev, ::gamePacket{ .type = 3, .id = 32, .pos = owner->pos, .punch = st });
        CHECK(said("Surg-E Anatomical Dummy") && said("end_dialog|surge|Cancel|Okay!|"), "confirm dialog");
        CHECK(said("Low Supply Warning"), "warns about missing tools");

        std::puts("\n[SG2] Okay! with no tools -> operating dialog with an empty tray, Give up! = malpractice");
        surg("");
        CHECK(said("is performing surgery on Surg-E") && said("Empty Surgery Tray"), "started, empty tray");
        surg("cancel");
        CHECK(said("couldn't save them") && owner->malpractice > std::time(nullptr), "gave up = malpractice");
        CHECK(w.blocks[cord(st.x_int(), st.y_int())].fg == 0, "Surg-E destroyed");
        w.blocks[cord(st.x_int(), st.y_int())].fg = 4296;
        outbox.clear(); tile_change(ev, ::gamePacket{ .type = 3, .id = 32, .pos = owner->pos, .punch = st });
        CHECK(said("malpractice issues"), "malpractice blocks the next surgery");

        std::puts("\n[SG0] Surgery Pack in the store: Surg-E + 5 of every tool");
        if (shouhin_tachi.empty()) parse_store();
        owner->slots.clear(); owner->gems = 2500;
        outbox.clear(); action::buy(ev, "action|buy|item|surgery_pack", "");
        if (owner->gems != 500) std::printf("  store said: %s\n", all_text().c_str());
        CHECK(owner->gems == 500 && held(*owner, 4296) == 1, "bought: Surg-E, 2000 gems");
        CHECK(std::ranges::all_of(tools, [&](short id) { return held(*owner, id) == 5; }), "5 of all 13 tools (Stitches too)");
        owner->slots.clear(); owner->gems = 0;

        std::puts("\n[SG3] 30 surgeries played by the hints: they can be won");
        int won = 0, lost = 0, stuck = 0;
        for (int n = 0; n < 30; ++n)
        {
            owner->slots.clear(); owner->malpractice = 0;
            for (short id : tools) owner->slots.push_back(::slot(id, 200));
            w.blocks[cord(st.x_int(), st.y_int())] = ::block{}; w.blocks[cord(st.x_int(), st.y_int())].fg = 4296;
            outbox.clear(); tile_change(ev, ::gamePacket{ .type = 3, .id = 32, .pos = owner->pos, .punch = st });
            surg("");
            bool lab = false, fixed = false, done = false;
            for (int turn = 0; turn < 80 && !done; ++turn)
            {
                const std::string d = dialog();
                const auto ip = d.find("Incisions: ");
                const int cuts = (ip == std::string::npos) ? 0 : d[ip + 13] - '0';
                const int need = has(d, "3 incisions") ? 3 : 2;
                const bool flu = has(d, "Flu``") || has(d, "Chicken Feet");
                if (has(d, "is cured") || has(d, "Now stitch up") || has(d, "set in a splint")) fixed = true;
                if (has(d, "bacteria found")) lab = true;
                std::string t;
                if (has(d, "Heart Stopped")) t = "Surgical Defibrillator";
                else if (has(d, "Extremely Weak") || has(d, "Pulse: `3Weak")) t = "Surgical Transfusion";
                else if (has(d, "losing blood")) t = "Surgical Clamp";
                else if (has(d, "hard to see")) t = "Surgical Sponge";
                else if (has(d, "not been diagnosed")) t = has(d, "try a lab test") ? "Surgical Lab Kit" : "Surgical Ultrasound";
                else if (flu || has(d, "Temp: `4") || (has(d, "Infected") && has(d, "Temp: `3"))) t = lab ? "Surgical Antibiotics" : "Surgical Lab Kit";
                else if (has(d, "Broken Arm``")) t = "Surgical Splint";
                else if (fixed) t = "Surgical Stitches";
                else if (!has(d, "Sanitized")) t = "Surgical Antiseptic";
                else if (has(d, "Status: `4Awake")) t = "Surgical Anesthetic";
                else if (cuts < need) t = "Surgical Scalpel";
                else if (has(d, "Shattered Leg")) t = "Surgical Pins";
                else if (has(d, "|Fix It!|")) t = "Fix It!";
                else t = "Surgical Anesthetic";
                surg(t);
                if (said("You saved the patient")) { ++won; done = true; }
                else if (said("couldn't save them")) { ++lost; done = true; }
            }
            if (!done) { ++stuck; surg("cancel"); }
        }
        std::printf("  won %d, lost %d, stuck %d\n", won, lost, stuck);
        CHECK(won >= 15, "most surgeries can be won");
        CHECK(stuck == 0, "no surgery gets stuck");
        CHECK(held(*owner, 4298) >= 1, "Caduceus for a win");
        owner->slots = saved_slots; owner->malpractice = 0;
        w.blocks[cord(st.x_int(), st.y_int())] = ::block{};
        std::erase_if(w.objects, [&](const ::object &o) { return o.pos.by_32(true) == st; });
    }

    /* ================= Audit fixes: untradeables, vending price, /pay ================= */
    std::puts("\n[AF1] untradeables can't be traded or sold in a vending machine");
    {
        const auto saved_owner = owner->slots, saved_visitor = visitor->slots;
        owner->slots = { ::slot(hat1, 10), ::slot(untrade, 1) };
        owner->trade_items.clear();
        outbox.clear(); trade_add(ev, ::hPipe(std::format("dialog_name|trade_add|itemID|{}|count|1", untrade)));
        CHECK(owner->trade_items.empty(), "faked trade_add with an untradeable: refused");
        outbox.clear(); action::mod_trade(ev, std::format("action|mod_trade\nitemID|{}", untrade));
        CHECK(owner->trade_items.empty() && said("sorry if you lost"), "mod_trade with an untradeable: refused");
        outbox.clear(); trade_add(ev, ::hPipe(std::format("dialog_name|trade_add|itemID|{}|count|3", hat1)));
        CHECK(owner->trade_items.size() == 1 && owner->trade_items[0].count == 3, "tradeable item still goes in");
        owner->trade_items.clear();

        const ::pos vt{ 30, 20 };
        w.blocks[cord(vt.x_int(), vt.y_int())].fg = 2978;
        const std::string vat = std::format("|tilex|{}|tiley|{}", vt.x_int(), vt.y_int());
        outbox.clear(); vending_return(ev, ::hPipe(std::format("dialog_name|vending|stockitem|{}{}", untrade, vat)));
        CHECK(held(*owner, untrade) == 1 && said("can't put that"), "untradeable refused by the vending machine");

        std::puts("\n[AF2] vending: the buyer pays the price they saw, not a new one");
        vending_return(ev, ::hPipe(std::format("dialog_name|vending|stockitem|{}{}", hat1, vat)));
        vending_return(ev, ::hPipe(std::format("dialog_name|vending|setprice|2|chk_peritem|1{}", vat)));
        CHECK(held(*owner, hat1) == 0 && find_vending(w, vt) && find_vending(w, vt)->price == 2, "owner stocked 10 at 2 WL each");
        visitor->slots = { ::slot(242, 50) };
        outbox.clear(); send_vending_dialog(ev2, w, vt);
        CHECK(said("embed_data|expectprice|2"), "buy dialog carries the price");
        vending_return(ev, ::hPipe(std::format("dialog_name|vending|setprice|5|chk_peritem|1{}", vat))); // @note owner raises it while the window is open
        outbox.clear(); vending_return(ev2, ::hPipe(std::format("dialog_name|vending|buycount|3|expectitem|{}|expectprice|2|expectperlock|0{}", hat1, vat)));
        CHECK(held(*visitor, 242) == 50 && held(*visitor, hat1) == 0 && said("price has changed"), "old price: nothing bought, no locks taken");
        outbox.clear(); vending_return(ev2, ::hPipe(std::format("dialog_name|vending|buycount|3{}", vat)));
        CHECK(held(*visitor, 242) == 50, "no expected price at all (proxy): refused");
        outbox.clear(); vending_return(ev2, ::hPipe(std::format("dialog_name|vending|buycount|3|expectitem|{}|expectprice|5|expectperlock|0{}", hat1, vat)));
        CHECK(held(*visitor, 242) == 35 && held(*visitor, hat1) == 3, "current price: bought 3 for 15 WL");
        vending_return(ev, ::hPipe(std::format("dialog_name|vending|buttonClicked|pullstock{}", vat)));
        vending_return(ev, ::hPipe(std::format("dialog_name|vending|buttonClicked|withdraw{}", vat)));
        w.blocks[cord(vt.x_int(), vt.y_int())] = ::block{};
        owner->slots = saved_owner; visitor->slots = saved_visitor;
    }

    std::puts("\n[AF3] /pay can't push someone's gems past the int max");
    {
        const std::string og = owner->growid, vg = visitor->growid;
        const signed oge = owner->gems, vge = visitor->gems;
        owner->growid = "MTOWNER"; visitor->growid = "MTVISIT";
        owner->gems = 1000; visitor->gems = std::numeric_limits<int>::max() - 10;
        outbox.clear(); pay(ev, "pay MTVISIT 100");
        CHECK(owner->gems == 1000 && visitor->gems == std::numeric_limits<int>::max() - 10 && said("can't hold"), "overflowing pay refused");
        outbox.clear(); pay(ev, "pay MTVISIT 10");
        CHECK(owner->gems == 990 && visitor->gems == std::numeric_limits<int>::max(), "pay up to exactly the max works");
        owner->growid = og; visitor->growid = vg; owner->gems = oge; visitor->gems = vge;
    }

    std::puts("\n[AF3b] /me is cut at 120 characters like chat");
    {
        outbox.clear(); me(ev, "me " + std::string(5000, 'a') + "|x");
        CHECK(said(std::string(120, 'a')) && !said(std::string(121, 'a')), "5000 characters sent, 120 reach the world");
    }

    std::puts("\n[AF4] public blocks (Public Lava) can't be placed in someone else's locked world");
    {
        const auto so = owner->slots, sv = visitor->slots; const bool pub = w.is_public; const auto acc = w.access;
        w.is_public = false; w.access.fill(0);
        owner->slots = { ::slot(1000, 5) }; visitor->slots = { ::slot(1000, 5) };
        const ::pos pt{ 40, 20 };
        w.blocks[cord(pt.x_int(), pt.y_int())] = ::block{};
        outbox.clear(); tile_change(ev2, ::gamePacket{ .type = 3, .id = 1000, .pos = visitor->pos, .punch = pt });
        CHECK(w.blocks[cord(pt.x_int(), pt.y_int())].fg == 0 && held(*visitor, 1000) == 5, "visitor's Public Lava refused");
        tile_change(ev, ::gamePacket{ .type = 3, .id = 1000, .pos = owner->pos, .punch = pt });
        CHECK(w.blocks[cord(pt.x_int(), pt.y_int())].fg == 1000 && held(*owner, 1000) == 4, "owner can still place it");
        outbox.clear(); tile_change(ev2, ::gamePacket{ .type = 3, .id = 18, .pos = visitor->pos, .punch = pt });
        CHECK(!said("World Locked"), "visitor may still punch a public block");
        w.blocks[cord(pt.x_int(), pt.y_int())] = ::block{};
        owner->slots = so; visitor->slots = sv; w.is_public = pub; w.access = acc;
    }

    std::puts("\n[AF5] vending machine in an unlocked world: nobody can empty it or buy");
    {
        const auto so = owner->slots, sv = visitor->slots; const int wo = w.owner;
        const ::pos vt{ 32, 20 };
        const std::string vat = std::format("|tilex|{}|tiley|{}", vt.x_int(), vt.y_int());
        w.blocks[cord(vt.x_int(), vt.y_int())].fg = 2978;
        owner->slots = { ::slot(hat1, 10) };
        vending_return(ev, ::hPipe(std::format("dialog_name|vending|stockitem|{}{}", hat1, vat)));
        vending_return(ev, ::hPipe(std::format("dialog_name|vending|setprice|1|chk_peritem|1{}", vat)));
        CHECK(find_vending(w, vt) && find_vending(w, vt)->stock == 10, "stocked while the world was locked");
        w.owner = 0; // @note the World Lock is gone
        visitor->slots = { ::slot(242, 50) };
        outbox.clear(); send_vending_dialog(ev2, w, vt);
        CHECK(said("only works in a locked area") && !said("pullstock"), "visitor gets no owner menu");
        outbox.clear(); vending_return(ev2, ::hPipe(std::format("dialog_name|vending|buttonClicked|pullstock{}", vat)));
        CHECK(find_vending(w, vt)->stock == 10 && held(*visitor, hat1) == 0, "visitor can't empty it");
        outbox.clear(); vending_return(ev2, ::hPipe(std::format("dialog_name|vending|buycount|1|expectitem|{}|expectprice|1|expectperlock|0{}", hat1, vat)));
        CHECK(held(*visitor, 242) == 50 && held(*visitor, hat1) == 0, "no buying without a lock");
        w.owner = wo;
        vending_return(ev, ::hPipe(std::format("dialog_name|vending|buttonClicked|pullstock{}", vat)));
        CHECK(held(*owner, hat1) == 10, "owner gets it back once locked again");
        w.blocks[cord(vt.x_int(), vt.y_int())] = ::block{};
        owner->slots = so; visitor->slots = sv;
    }

    /* ================= News & rules ================= */
    std::puts("\n[N1] gazette in English with a rules button");
    outbox.clear(); on::RequestGazette(ev);
    CHECK(said("The SevenPS Gazette") && said("Latest Updates"), "English gazette");
    {
        constexpr std::array<std::string_view, 12> month{ "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
        std::tm t = localtime();
        CHECK(said(std::format("{} {}", month[t.tm_mon], t.tm_mday)), "date like 'October 16th'");
    }
    CHECK(said("add_button|mt_rules|`wServer Rules"), "Server Rules button");
    CHECK(!said("/find"), "no more 'free /find' (it's staff only now)");
    outbox.clear(); gazette_return(ev, ::hPipe("dialog_name|gazette|buttonClicked|mt_rules"));
    CHECK(said("SevenPS Rules") && said("I Understand"), "button opens the rules");
    outbox.clear(); gazette_return(ev, ::hPipe("dialog_name|gazette|buttonClicked|"));
    CHECK(outbox.empty(), "OK button does nothing");

    {
        ::hStmt del{ "DELETE FROM world WHERE name = 'MTTEST'" }; del.execute();
        ::hStmt del2{ "DELETE FROM world_mannequin WHERE world = 'MTTEST'" }; del2.execute();
        std::puts("test world MTTEST deleted");
    }
    std::printf("\n%d passed, %d failed\n", passes, fails);
    std::fflush(stdout);
    _exit(fails ? 1 : 0); // @note skip destructors (they talk to the database)
}
