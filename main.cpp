/*
    @copyright SevenPS (c) 2026 @Seven
    @copyright gurotopia (c) 2024-05-25, Apache-2.0 (see LICENSE & NOTICE-SEVENPS.md)
    @version parent SHA: 8f2b5977fd057b99e2e2a6f0e65170145decc159 2026-9-30
*/
#include "include/pch.hpp"
#include "include/eventType/_eventType.hpp"

#include "include/database/shouhin.hpp" // @note init_shouhin_tachi()
#include "include/https/https.hpp" // @note https::listener()
#include "include/https/server_data.hpp" // @note gServer_data
#include "include/database/database_config.hpp" // @note load_database_config(), gDatabase_config
#include "include/automate/holiday.hpp" // @note holiday
#include "include/commands/restart.hpp" // @note restart_tick()
#include "include/tools/cps.hpp" // @note cps_tick()
#include "include/tools/rank.hpp" // @note load_ranks()
#include "include/tools/fishing.hpp" // @note fishing_tick()
#include "include/tools/item_hunt.hpp" // @note hunt_tick()
#include "include/tools/backup.hpp" // @note backup_tick()
#include "include/tools/guild.hpp" // @note load_guilds()
#include "include/tools/auth.hpp" // @note apply_password_reset()
#include "include/tools/web_login.hpp" // @note web_login_tick()
#include "include/tools/buffs.hpp" // @note buff_tick()
#include "include/tools/autosave.hpp" // @note autosave_tick()
#include <csignal>
#include <atomic>
#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h> // @note SetConsoleCtrlHandler (last include: its macros stay out of our headers)
#endif

namespace
{
    volatile std::sig_atomic_t gSignal = 0;
    std::atomic<bool> gSaved{ false }; // @note set once everything is saved, after the main loop
}
static void signal_handler(int signal) { gSignal = signal; }

#ifdef _WIN32
/* @note closing the window (X), logging off or shutting down never raised SIGINT: Windows just ended the process and
 * nothing was saved. this runs on its own thread; Windows waits for it (about 5 s), so stop the main loop and wait
 * until it has saved everyone */
static BOOL WINAPI console_handler(DWORD type)
{
    if (type != CTRL_CLOSE_EVENT && type != CTRL_LOGOFF_EVENT && type != CTRL_SHUTDOWN_EVENT) return FALSE; // @note Ctrl+C: SIGINT as before
    gSignal = SIGINT;
    for (int waited = 0; !gSaved && waited < 4500; waited += 50) Sleep(50);
    return TRUE;
}
#endif

int main()
{
    std::signal(SIGINT, signal_handler);
#ifdef _WIN32
    SetConsoleCtrlHandler(console_handler, TRUE);
#endif
#ifdef SIGHUP // @note unix
    std::signal(SIGHUP, signal_handler); // @note PuTTY, SSH problems
#endif

    mysql_library_init(0, NULL, NULL);
    enet_initialize();
    {
        gServer_data.init(); // @note ./server_data.php
        ENetAddress address{
            .type = ENET_ADDRESS_TYPE_IPV4, 
            .port = gServer_data.port
        };

        host = enet_host_create (ENET_ADDRESS_TYPE_IPV4, &address, 50ull/* max peer count */, 2ull, 0u, 0u);
        std::thread(&https::listener).detach();
    } // @note delete address
    host->duplicatePeers = 4;             // @note at most 4 connections at once from one IP (ENet's default allows all 50)
    host->maximumPacketSize = 64 * 1024;  // @note was 32 MB: one fake packet could make us allocate that much
    host->maximumWaitingData = 1024 * 1024;
    host->usingNewPacketForServer = true;
    host->checksum = enet_crc32;
    enet_host_compress_with_range_coder(host);

    gDb_config.init();
    mysql_connect();
    apply_password_reset(); // @note reset_password.txt (forgotten password)
    decode_items();      // @note reads items.dat into legible class members (id, item name, ect)
    parse_store();       // @todo thread loop this so the store can update without restarting server (stored in .\resource\store.txt)
    load_guilds();       // @note `guild` table
    load_ranks();        // @note .\resources\ranks.txt
    check_for_holiday(); // @note check for any holidays using local time (your VPS or local time) - @todo thread loop so it can change the holiday without restarting

    ENetEvent event{};
    bool restarting{};
    while (!gSignal && !restarting)
    {
        if (enet_host_service(host, &event, 100/*ms, was 1000: the login page waits for web_login_tick*/) > 0)
            if (const auto i = eventType_pool.find(event.type); i != eventType_pool.end())
                i->second(event);
        web_login_tick(); // @note answers the login page (password checks need the database, which lives on this thread)
        restarting = restart_tick(); // @note /restart countdown
        cps_tick(); // @note automatic events & broadcasts
        fishing_tick(); // @note fish bites
        hunt_tick(); // @note Item Hunt event
        backup_tick(); // @note daily database backup
        buff_tick(); // @note timed mods from consumables run out
        autosave_tick(); // @note players & worlds every 5 minutes
    }

    safe_disconnect_peers(gSignal);
    gSaved = true; // @note console_handler may let Windows close us now
    mysql_close(db); // @note deletes db (MYSQL* allocation)
    mysql_library_end();

    if (restarting)
    {
        puts("restarting SevenPS...");
        return 2; // @note jalankan_server.bat starts the server again on exit code 2
    }
    puts("killed SevenPS! [EXIT_SUCCESS]");
    return EXIT_SUCCESS;
}