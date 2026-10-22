#include "pch.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "onVariant/SetBux.hpp"
#include "action/quit_to_exit.hpp"
#include "tools/profile.hpp"
#include "tools/rank.hpp"
#include "tools/cps.hpp"
#include "tools/economy.hpp"

#include "system_worlds.hpp"

/* @note item ids (checked against items.dat) */
namespace id
{
    constexpr short DIRT = 2, MAIN_DOOR = 6, BEDROCK = 8, DOOR = 12, CAVE_BG = 14, GRASS = 16, SIGN = 20, DAISY = 22;
    constexpr short WOOD_PLATFORM = 102, DEATH_SPIKES = 162, STEEL = 186, ROSE = 190, BUSH = 192;
    constexpr short GOLDEN_BLOCK = 260, PICKET_FENCE = 264, RAINBOW_BLOCK = 308, CHANDELIER = 340, CHECKPOINT = 410;
    constexpr short MARBLE = 668, TORCH = 696, PALM_TREE = 854, STREETLAMP_BG = 992, PILLAR = 1100;
    constexpr short GLOWY_BLOCK = 2010, FOUNTAIN = 2964, RED_ROYAL_BG = 2988, LAVA_LAMP = 5044;
    constexpr short CHERRY_TREE = 7046, SUNSET_CLOUDS_BG = 7238;
}

namespace
{
    constexpr int W = 100, H = 60;
    constexpr int GROUND = 37; // @note first solid row, players walk on row 36 like a normal world

    const ::pos PARKOUR_FINISH{ 96, 13 };
    constexpr int PARKOUR_REWARD = 1000; // @note gems, once a day (+ rank bonus)
}

bool is_system_world(std::string_view name)
{
    return name == "START" || name == "PARKOUR";
}

namespace
{
    constexpr std::string_view GET_STARTED = "`2Get started:`` type `$/tutorial``! Lock your own world, break and place blocks, plant and harvest, then shop. Every step gives gems.";
}

void refresh_system_signs(::world &world)
{
    if (world.name != "START") return;
    for (::sign &s : world.signs)
        if (s.label.starts_with("`2Get started:``")) s.label = GET_STARTED; // @note only our own sign, anything staff wrote stays
}

/* ---------- drawing helpers ---------- */

static void fg(::world &w, int x, int y, short item) { if (x >= 0 && x < W && y >= 0 && y < H) w.blocks[cord(x, y)].fg = item; }
static void bg(::world &w, int x, int y, short item) { if (x >= 0 && x < W && y >= 0 && y < H) w.blocks[cord(x, y)].bg = item; }

static void put_sign(::world &w, int x, int y, const std::string &text)
{
    fg(w, x, y, id::SIGN);
    w.signs.emplace_back(::sign(text, ::pos{ x, y }));
}

static void put_door(::world &w, int x, int y, const std::string &label, const std::string &dest)
{
    fg(w, x, y, id::DOOR);
    w.doors.emplace_back(::door(label, dest, "", ::pos{ x, y }));
}

static void main_door(::world &w, int x)
{
    fg(w, x, GROUND - 1, id::MAIN_DOOR);
    fg(w, x, GROUND, id::BEDROCK);
    w.doors.emplace_back(::door("EXIT", "", "", ::pos{ x, GROUND - 1 }));
}

/* @note empty world with dirt, a cave background and bedrock at the bottom (no lava or rocks) */
static void clear_world(::world &w)
{
    w.blocks.assign(cord(0, H), ::block{ 0, 0 });
    w.doors.clear();
    w.signs.clear();
    w.trees.clear();
    w.displays.clear();
    w.random_blocks.clear();
    w.objects.clear();
    w.last_object_uid = 0;

    for (int y = GROUND; y < H; ++y)
        for (int x = 0; x < W; ++x)
        {
            bg(w, x, y, id::CAVE_BG);
            fg(w, x, y, (y >= 54) ? id::BEDROCK : id::DIRT);
        }
}

/* @note 5x7 pixel letters for the sky title */
static void letter(::world &w, int left, int top, char c, short item)
{
    static const std::unordered_map<char, std::array<std::string_view, 7>> font{
        { 'M', { "X...X", "XX.XX", "X.X.X", "X...X", "X...X", "X...X", "X...X" } },
        { 'I', { "XXXXX", "..X..", "..X..", "..X..", "..X..", "..X..", "XXXXX" } },
        { 'K', { "X...X", "X..X.", "X.X..", "XX...", "X.X..", "X..X.", "X...X" } },
        { 'E', { "XXXXX", "X....", "X....", "XXXX.", "X....", "X....", "XXXXX" } },
        { 'T', { "XXXXX", "..X..", "..X..", "..X..", "..X..", "..X..", "..X.." } },
        { 'O', { ".XXX.", "X...X", "X...X", "X...X", "X...X", "X...X", ".XXX." } },
        { 'P', { "XXXX.", "X...X", "X...X", "XXXX.", "X....", "X....", "X...." } },
        { 'A', { ".XXX.", "X...X", "X...X", "XXXXX", "X...X", "X...X", "X...X" } },
        { 'S', { ".XXXX", "X....", "X....", ".XXX.", "....X", "....X", "XXXX." } },
        { 'V', { "X...X", "X...X", "X...X", "X...X", "X...X", ".X.X.", "..X.." } },
        { 'N', { "X...X", "XX..X", "X.X.X", "X..XX", "X...X", "X...X", "X...X" } },
    };
    auto it = font.find(c);
    if (it == font.end()) return;
    for (int row = 0; row < 7; ++row)
        for (int col = 0; col < 5; ++col)
            if (it->second[row][col] == 'X') fg(w, left + col, top + row, item);
}

/* ---------- START ---------- */

static void build_start(::world &w)
{
    clear_world(w);

    /* @note "SEVENPS" in the sky with a glowing line under it */
    constexpr std::string_view title{ "SEVENPS" };
    for (std::size_t i = 0; i < title.size(); ++i) letter(w, 50 - (static_cast<int>(title.size()) * 6 - 1) / 2 + static_cast<int>(i) * 6, 6, title[i], id::RAINBOW_BLOCK);
    for (int x = 22; x <= 78; ++x) fg(w, x, 15, id::GLOWY_BLOCK);

    /* @note the hall: golden roof, pillars (with a gap to walk through), royal wallpaper, marble floor */
    for (int x = 34; x <= 66; ++x) fg(w, x, 26, id::GOLDEN_BLOCK);
    for (int y = 27; y <= 33; ++y) { fg(w, 35, y, id::PILLAR); fg(w, 65, y, id::PILLAR); }
    for (int y = 27; y <= 36; ++y)
        for (int x = 36; x <= 64; ++x) bg(w, x, y, id::RED_ROYAL_BG);
    for (int x = 30; x <= 70; ++x) fg(w, x, GROUND, id::MARBLE);
    for (int x : { 40, 50, 60 }) fg(w, x, 27, id::CHANDELIER);
    for (int x : { 37, 63 }) fg(w, x, 32, id::TORCH);
    for (int x : { 45, 55 }) fg(w, x, GROUND - 1, id::LAVA_LAMP);

    main_door(w, 50);

    put_sign(w, 48, GROUND - 1, "`9Welcome to SevenPS!`` Type `$/help`` for all commands. Have fun and be nice!");
    put_sign(w, 52, GROUND - 1, std::string{ GET_STARTED });
    put_sign(w, 43, GROUND - 1, "`4Rules:`` no scamming, no spamming, no bad words. Staff decisions are final. Scammed? Report with wrench > Report.");
    put_sign(w, 57, GROUND - 1, "`5Daily:`` wrench yourself for Grow Pass, Goals & Quests and Achievements. Online 30 minutes = free gems!");
    put_door(w, 39, GROUND - 1, "`2PARKOUR``", "PARKOUR");
    put_door(w, 61, GROUND - 1, "`9TRADE``", "TRADE");

    /* @note left garden */
    for (int x = 1; x <= 3; ++x) fg(w, x, GROUND - 1, id::PICKET_FENCE);
    for (int x : { 8, 20 }) fg(w, x, GROUND - 1, id::PALM_TREE);
    for (int x : { 14, 27 }) fg(w, x, GROUND - 1, id::CHERRY_TREE);
    for (int x : { 5, 17, 31 }) bg(w, x, GROUND - 1, id::STREETLAMP_BG);
    for (int x = 4; x <= 29; ++x)
        if (w.blocks[cord(x, GROUND - 1)].fg == 0)
            fg(w, x, GROUND - 1, (x % 5 == 0) ? id::ROSE : (x % 5 == 2) ? id::DAISY : (x % 5 == 4) ? id::BUSH : id::GRASS);
    put_sign(w, 11, GROUND - 1, "`2Tip:`` ask a friend for help, trade fairly and check `$/top`` to see the best players!");

    /* @note right side: fountains, trees and a platform climb up to the title */
    for (int x : { 76, 88 }) fg(w, x, GROUND - 1, id::FOUNTAIN);
    for (int x : { 82, 94 }) fg(w, x, GROUND - 1, id::CHERRY_TREE);
    for (int x : { 71, 85, 97 }) bg(w, x, GROUND - 1, id::STREETLAMP_BG);
    for (int x = 72; x <= 98; ++x)
        if (w.blocks[cord(x, GROUND - 1)].fg == 0 && x % 3 == 0) fg(w, x, GROUND - 1, (x % 2) ? id::ROSE : id::DAISY);
    for (int x = 98; x <= 99; ++x) fg(w, x, GROUND - 1, id::PICKET_FENCE);
    put_sign(w, 79, GROUND - 1, "`9Secret:`` climb the platforms up to the SevenPS sign... if you can!");

    constexpr std::array<std::pair<int, int>, 10> climb{ { { 68, 34 }, { 71, 32 }, { 74, 30 }, { 71, 28 }, { 68, 26 },
                                                           { 71, 24 }, { 74, 22 }, { 77, 20 }, { 80, 18 }, { 82, 16 } } };
    for (const auto &[x, y] : climb) { fg(w, x, y, id::WOOD_PLATFORM); fg(w, x + 1, y, id::WOOD_PLATFORM); }
}

/* ---------- PARKOUR ---------- */

static void build_parkour(::world &w)
{
    clear_world(w);
    for (int y = 0; y < GROUND; ++y)
        for (int x = 0; x < W; ++x) bg(w, x, y, id::SUNSET_CLOUDS_BG);
    for (int x = 0; x < W; ++x) fg(w, x, GROUND, id::BEDROCK);

    main_door(w, 2);
    put_sign(w, 4, GROUND - 1, std::format("`9PARKOUR!`` Jump to the `2FINISH`` at the top right for `w{}`` gems (once a day). Spikes send you back to your last checkpoint.", PARKOUR_REWARD));
    for (int x = 5; x < W; ++x) fg(w, x, GROUND - 1, id::DEATH_SPIKES);

    /* @note {x, y, width}: every gap is 2 tiles wide and at most 1 tile higher (the first one 2, from the ground) */
    struct step { int x, y, width; bool checkpoint = false; };
    constexpr step steps[]{
        { 6, 35, 2 }, { 10, 34, 2 }, { 14, 33, 2 }, { 18, 33, 1 }, { 21, 32, 1 }, { 24, 31, 2 }, { 28, 31, 3, true },
        { 33, 30, 1 }, { 36, 29, 1 }, { 39, 28, 2 }, { 43, 27, 1 }, { 46, 26, 1 }, { 49, 25, 2 }, { 53, 25, 3, true },
        { 58, 24, 1 }, { 61, 23, 1 }, { 64, 22, 1 }, { 67, 21, 1 }, { 70, 20, 2 }, { 74, 20, 3, true },
        { 79, 19, 1 }, { 82, 18, 1 }, { 85, 17, 1 }, { 88, 16, 1 }, { 91, 15, 1 },
    };
    for (const step &s : steps)
    {
        for (int x = s.x; x < s.x + s.width; ++x) fg(w, x, s.y, id::STEEL);
        if (s.checkpoint) fg(w, s.x + 1, s.y - 1, id::CHECKPOINT);
    }

    /* @note finish line */
    for (int x = 94; x <= 98; ++x) fg(w, x, 14, id::GOLDEN_BLOCK);
    fg(w, PARKOUR_FINISH.x_int(), PARKOUR_FINISH.y_int(), id::CHECKPOINT);
    put_sign(w, 98, 13, "`2FINISH!`` Touch the checkpoint to claim your reward. See you tomorrow!");
}

bool build_system_world(::world &world)
{
    if (world.name == "START") build_start(world);
    else if (world.name == "PARKOUR") build_parkour(world);
    else return false;
    return true;
}

/* ---------- parkour reward ---------- */

void system_checkpoint(ENetEvent &event, ::world &world, const ::pos &tile)
{
    if (world.name != "PARKOUR" || tile.x_int() != PARKOUR_FINISH.x_int() || tile.y_int() != PARKOUR_FINISH.y_int()) return;

    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const int today = static_cast<int>(std::time(nullptr) / 86400);
    if (pget(*pPeer, "parkour_day") == today)
        return on::ConsoleMessage(event.peer, "`oYou already finished the parkour today. Come back tomorrow for another reward!``");

    pset(*pPeer, "parkour_day", today);
    add_stat(event, "parkour");
    const int gems = gem_bonus(*pPeer, PARKOUR_REWARD);
    pPeer->gems += gems;
    econ("parkour", gems);
    on::SetBux(event);
    send_action(*event.peer, "play_sfx", "file|audio/cash_register.wav\ndelayMS|0\n");
    send_varlist(event.peer, { "OnTextOverlay", std::format("`2Parkour finished! ``+{} gems", gems) });
    system_broadcast(std::format("`2** ``{} `2finished the `wPARKOUR`` `2and won `w{}`` gems! Type `$/parkour`` to try it.``", pPeer->display_growid, gems));
}

/* ---------- /rebuild ---------- */

void rebuild_command(ENetEvent &event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (pPeer->role < OWNER) return on::ConsoleMessage(event.peer, "`4Only the owner can use this command.``");

    std::vector<std::string> args = readch(std::string{ text }, ' ');
    std::string name = (args.size() >= 2) ? args[1] : "";
    for (char &c : name) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (!is_system_world(name)) return on::ConsoleMessage(event.peer, "`oUsage: `w/rebuild START`` or `w/rebuild PARKOUR`` (everything built there is replaced!)``");

    /* @note everyone leaves first so nobody sees half a world. the last one out saves and unloads it */
    for (ENetPeer *p : peers())
    {
        ::peer *pp = static_cast<::peer*>(p->data);
        if (pp == nullptr || pp->netid == 0 || pp->recent_worlds.back() != name) continue;
        on::ConsoleMessage(p, std::format("`9{} is being rebuilt, please enter again in a moment.``", name));
        ENetEvent e{ .peer = p };
        action::quit_to_exit(e, "", false);
    }

    auto it = std::ranges::find(worlds, name, &::world::name);
    if (it != worlds.end()) build_system_world(*it); // @note still loaded somehow, saved when it unloads
    else
    {
        ::world world{ name }; // @note loads it from the database and saves it back when it goes out of scope
        build_system_world(world);
    }
    on::ConsoleMessage(event.peer, std::format("`2{} was rebuilt.``", name));
}

/* ---------- door destinations ---------- */

void load_doors(::world &world)
{
    ::hStmt hStmt{ "SELECT x, y, dest, door_id, password FROM world_door WHERE world = ?" };
    MYSQL_BIND param = make_bind_in(world.name);
    hStmt.bind_param(&param);

    signed x{}, y{};
    char dest[64]{}, door_id[64]{}, password[24]{};
    u_long dest_length{}, id_length{}, password_length{};
    MYSQL_BIND result[5] = { make_bind_out(x), make_bind_out(y) };
    result[2].buffer_type = MYSQL_TYPE_STRING;
    result[2].buffer = dest;
    result[2].buffer_length = sizeof(dest);
    result[2].length = &dest_length;
    result[3].buffer_type = MYSQL_TYPE_STRING;
    result[3].buffer = door_id;
    result[3].buffer_length = sizeof(door_id);
    result[3].length = &id_length;
    result[4].buffer_type = MYSQL_TYPE_STRING;
    result[4].buffer = password;
    result[4].buffer_length = sizeof(password);
    result[4].length = &password_length;
    mysql_stmt_bind_result(hStmt.pStmt, result);
    hStmt.execute();
    mysql_stmt_store_result(hStmt.pStmt);

    while (mysql_stmt_fetch(hStmt.pStmt) == 0)
    {
        auto door = std::ranges::find(world.doors, ::pos{ x, y }, &::door::pos);
        if (door == world.doors.end()) continue;
        door->dest.assign(dest, std::min<u_long>(dest_length, sizeof(dest)));
        door->id.assign(door_id, std::min<u_long>(id_length, sizeof(door_id)));
        door->password.assign(password, std::min<u_long>(password_length, sizeof(password)));
    }
}

void save_doors(::world &world)
{
    {
        ::hStmt hStmt{ "DELETE FROM world_door WHERE world = ?" };
        MYSQL_BIND param = make_bind_in(world.name);
        hStmt.bind_param(&param);
        hStmt.execute();
    }
    for (const ::door &door : world.doors)
    {
        if (door.dest.empty() && door.id.empty() && door.password.empty()) continue;
        const signed x = door.pos.x_int(), y = door.pos.y_int();
        if (x < 0 || y < 0 || cord(x, y) >= static_cast<int>(world.blocks.size())) continue;
        if (id_to_item(world.blocks[cord(x, y)].fg).type != type::DOOR && id_to_item(world.blocks[cord(x, y)].fg).type != type::PORTAL) continue; // @note door was broken

        const std::string dest = door.dest.substr(0, 63), door_id = door.id.substr(0, 63), password = door.password.substr(0, 23);
        ::hStmt hStmt{ "INSERT INTO world_door (world, x, y, dest, door_id, password) VALUES (?, ?, ?, ?, ?, ?)" };
        MYSQL_BIND params[6] = { make_bind_in(world.name), make_bind_in(x), make_bind_in(y), make_bind_in(dest), make_bind_in(door_id), make_bind_in(password) };
        hStmt.bind_param(params);
        hStmt.execute();
    }
}
