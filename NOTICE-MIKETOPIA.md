# MikeTopia

MikeTopia is a modified version of **Gurotopia** (https://github.com/gurotopia/Gurotopia),
licensed under the Apache License 2.0 (see `LICENSE`). As the license asks, this file lists what was changed.

Made by @Mike, for local/private use.

## Changes from Gurotopia

**Roles & staff**
- Roles saved in the database: Player, Moderator, Developer, Owner (`peer.role`), with `@` name tags and colors
- Super Supporter for staff: bigger name + green wrench (character state bit 24), `/hidestatus`, `/nick`
- Titles: Dr. (red name), of Legend, Blue Name (`|maxLevel`), chosen from wrench > Title or `/title`
- Staff chat colors, Owner/Developer broadcasts, "Restart By" notices

**Commands** (`include/commands/`)
- `/give`, `/givegems`, `/setrole`, `/givetitle`, `/restart`, `/kick`, `/ban`, `/unban`, `/nuke`, `/unnuke`, `/wban`, `/uba`, `/nick`, `/title`, `/hidestatus`
- `/weather` now works for the whole world (owner/staff) and weather is sent when entering a world

**Gameplay**
- Trade (`include/action/trade.cpp`)
- Vending machines (`include/tools/vending.cpp`, own `vending` table)
- World ban / pull / kick from the wrench menu (`include/tools/worldban.cpp`)
- CreativePS-style (`include/tools/cps.cpp`, own code): automatic events every 2 hours (Gems x2, XP x2, Lucky Drops, Surgery Day) + `/event`, rotating broadcasts from `resources/broadcast.txt`, join announcements, `/top` leaderboards, `/start`, and the /find rule (store & special items can't be found)
- Level and XP are now saved
- Ranks VIP..GOD bought with gems (`include/tools/rank.cpp`, `resources/ranks.txt`, `/rank`, `/giverank`): name tag & color, gem/XP bonus, backpack slots, free /sb, titles, /nick
- Chat quiz every 15 minutes, 300 gems per 30 minutes online, `/msg`, `/online`, /sb costs 1000 gems for players without a rank
- Profile (`include/tools/profile.cpp`): stats & 25 achievements, daily Goals & Quests, Grow Pass daily bonus, Marvelous Missions, Personalize Profile (bio), Wardrobe outfits, Card Battle License, Battle Pets, Wrench Customization, Growmojis list
- Social (`include/tools/social.cpp`): friends (`friends` table), ignore (chat & PM), reports to staff (`reports` table), Trade-Scan history (`trade_log` table)
- Wrench menu (`include/tools/wrench_menu.cpp`): Notebook, World Lock Bank, online status, view worn clothes, send message; real surgeon level, play time and account age
- Weather machines: SFX/sprite machines (Heatwave, Stuff...) work, only one machine runs at a time, `/weather` is saved (`world_weather` table)
- Surgery on Surg-E (`include/tools/surgery.cpp`): 6 diagnoses, all 13 surgical tools, surgeon skill and malpractice saved to the database
- 76 more punch effects, double jump from back items, bedrock breakable by developers
- System worlds (`include/tools/system_worlds.cpp`): START (hub with the MikeTopia sky title, hall, signs, doors to PARKOUR/TRADE, garden, platform climb) and PARKOUR (jump course with checkpoints, daily 2500 gems at the finish) are built with a design when first opened; only staff can edit them; `/rebuild {START|PARKOUR}` (owner), `/parkour`
- Jammers (`include/tools/jammer.cpp`): Signal, Punch, Zombie, Balloon Jammer and Antigravity Generator turn on/off by punching (tile update + message); Signal Jammer hides the world from Top Worlds, /sb and friend lists (staff still see it); "World X [JAMMED, NOPUNCH...] entered" tags
- Consumables (`include/tools/consumables.cpp`, item list from GTOS): Grow Spray & Deluxe Grow Spray, Fortune & Lucky Fortune Cookie, Winter Gift, Bronze/Silver/Gold Architect Chest, Winter & Spring Chest, Small Seed Pack, Surgery Gatcha, Gift of Growganoth, fragments that combine (Devil Wings, Teeny Devil Wings, Vampire Cape, Black Cat, Skeletal Dragon Claw, Nightmare Magnifying Glass, Emerald & Ruby Lock, Cupid), Valentine's Card, Green Beer, g-Virus & Antidote; GrowScan 9000 world stats (wrench)
- Playmods (`include/tools/playmods.cpp`, `resources/playmods.txt`): speed & gravity of worn items, zombies are slow, Antigravity Generator lowers gravity for everyone in the world; ~50 items listed (Air Robinsons & other Speedy rides, High Jump packs), `dj` = double jump on non-back items (Pegasus, Riding Raven), `dig` = Enhanced Digging (Pickaxe, Cybernetic Arm: blocks need one hit less)
- Area locks (`include/tools/area_lock.cpp`, `world_area_lock` table): Small (10 tiles), Big (48), Huge (200) and Builder's Lock (200) spread over connected tiles like real Growtopia; owner + access list (player picker, max 20) only; "Ignore empty air" and "Re-apply lock"; Builder's Lock can limit access to only build / only break; World Locks and area locks of other players can't be mixed; lock_edit now checks the world owner (a faked dialog could change someone's World Lock)
- Boxes (`include/tools/boxes.cpp`, `world_box` table), wrench to use, need a World Lock or area lock: Storage Box Xtreme (20/40/90 stacks) and Untrade-a-Box (owner only), Safe Vault (owner + password), Donation Box (anyone gives items with a note, owner collects), Mailbox (private letters, owner gets notified), Bulletin Board (public messages, owner can clear); boxes with items can't be broken
- Doors (`include/tools/password_door.cpp`): destinations `WORLD`, `WORLD:ID` and `:ID` (door-to-door in the same world) now work; Password Door (762) asks for its password (kept only on the server, `world_door.password`), 5 wrong tries = 30 s wait; placing a door no longer inherits the old broken door's label/destination
- Magplant 5000, Gaia's Beacon, Unstable Tesseract (`include/tools/magplant.cpp`, `world_magplant` table): collects one chosen item from block/tree/provider drops, owner adds/retrieves with the wrench, punch toggles building mode, Magplant 5000 Remote builds/plants from the machine (owner + access list only, removed when you change worlds)
- Placing/using an item now checks you really have it (a modded client could place items it never owned and break them for drops; the backpack also went to -1)
- Security fixes: `gateway_edit` (sign/door/entrance dialogs) checked nothing - a faked dialog could rename doors in any world (even START) or write outside the world's tiles; now bounds + build permission. Visitors can wrench in World-Locked worlds again (vending buyers were blocked), edit dialogs stay builder-only. Vending machines in an area lock belong to the lock owner
- Commands from GTOS' list: /pay /go /rules /mods /radio /info /status, staff /warpto /mute /unmute /curse /uncurse (stuck in HELL) /warn /kickall
- Economy like real Growtopia (`include/tools/economy.cpp`): /find is staff only, World Lock = 2000 gems is the price reference, starter pack for new players (My First World Lock, Pepper Tree & Laser Grid seeds, Small Locks), online reward only for active players and max 6 a day, smaller quiz rewards, parkour 1000 gems; every gem made/spent is counted per source and shown with `/economy` (developer+)
- Staff accounts (`include/tools/economy.cpp`): /find and /give are developer+, moderator & developer accounts can't drop, trade, use vending machines or /pay; /givegems to others and /giverank are owner only; every spawn is logged in `staff_log`, read with `/stafflog`
- System broadcasts: placeholders in `resources/broadcast.txt` ({online} {players} {worlds} {event} {next_event}), "event in 5 minutes" warning, event popup notification, hourly server stats, new player welcome, level milestones (every 10 levels) and achievements announced to everyone

**Saving & fixes**
- Saved to the database: clothing, gems, titles, ban status
- Server shutdown now saves online players and worlds
- Display block and vending machine tile data is now sent to the client
- Door destinations are saved (`world_door` table), so doors still work after a restart
- Drop dialog: dropping an item you didn't own (with a proxy) created items out of nothing
- Store: buying an item a second time gave nothing (the store entry was emptied after each purchase)
- Tree growth uses the real clock (the old monotonic clock reset on every PC reboot)
- "X left" message now goes to the other players instead of the one leaving
- MikeTopia gazette and login messages

## Credits
- **Gurotopia** - the server this is based on (Apache-2.0)
- **GTOS** (https://github.com/zKaan/GTOSLatest, GPL) - reference for the Super Supporter state bit,
  punch effect ids, the trade / vending flow and the Surg-E dialog format. MikeTopia's code is written separately in Gurotopia's style.
- **GTopia** (https://github.com/keichira/GTopia, MIT, Copyright (c) 2026 keichira) - Magplant tile extra layout (type 0x3e), OnPlanterActivated and the machine rules (capacity, allowed items). Rewritten in Gurotopia's style.
- **CreativePS** - inspiration for the /find rule, events and season gazette (no code copied).
- Growtopia is a game by Ubisoft. This project is not affiliated with Ubisoft.
