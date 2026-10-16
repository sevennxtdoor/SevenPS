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
- Mannequin (`include/tools/mannequin.cpp`, `world_mannequin` table): use a clothing item on it to dress it (Yes/No first, the old piece comes back), punch takes one piece back (it only breaks when empty), wrench writes its sign and removes chosen/all clothes; locked spots only, owner + access list; worn last copies and untradeable items refused
- Fishing (`include/tools/fishing.cpp`): wear a Fishing Rod (or E-Z/Licorice/Cursed/Goldenrod), use Wiggly Worm / Fishing Fly / Shrimp Lure on water next to you, punch within 3 s of the splash; fish weight = rod max, better bait = rarer fish, a few gems per catch (`econ` source "fishing"); store: Fishin' Pack, worms, flies, lures. Packet GONE_FISHIN (31) from GTOS/GTopia/GrowServer
- Casino (`include/tools/casino.cpp`): Roulette Wheel rewritten - 0-36 with the real red/black table, reme shown, the result reaches everyone in the world (old code sent every copy to the spinner), 1.5 s cooldown (GTOS); chat, /me, /sb and /msg that fake "spun the wheel" are blocked for players. Store: Vegas Pack (20000 gems, contents from GTOS)
- Item Hunt event (`include/tools/item_hunt.cpp`): every 3 hours 5 Treasure Chests are hidden in START for 20 minutes, one per player, 750-1500 gems each (`econ` source "item hunt"); `/hunt` hints, `/hunt start|stop` for staff. Chests are never saved and come back if START reloads. Drop removal packet (netid -2) from GTOS
- Fixes: picking up a drop with an unknown uid crashed the server, drops could be collected from any distance (now 5 tiles); `RandomRange(a, b)` never returns `b`, so Surgery Day never started automatically, the quiz was always math, Heart Attack never happened in surgery, dice never rolled a 6, and the last loot entry of chests/cookies never dropped
- Daily database backups (`include/tools/backup.cpp`): mariadb-dump into `backup/` (git-ignored), newest 7 kept, `/backup` for the owner; the password reaches the dump tool only through its environment, never the command line
- Staff item leak closed: MOD/DEV only place blocks/seeds in their own private world (or START/PARKOUR) and can't make a world public. Pocket Lighter message reaches everyone. Broadcasts, news and /rules are in English
- New player tutorial (`include/tools/tutorial.cpp`, `/tutorial`): 6 steps driven by profile stats (lock a world, break, place, plant, harvest, shop), 100 gems each + 400 bonus once; START's "Get started" sign is refreshed on load
- Guilds (`include/tools/guild.cpp`, table `guild`): /guild create (50000 gems, level 5), invite/accept, ranks, kick, promote/demote, leader transfer, statement, flag, disband, /gc chat. Flag visuals use OnGuildDataChanged + "|showGuild" from GTOS (unverified in game)
- Item effects, part 1: punch effects moved to `resources/punch_effects.txt` (313 items, was 97 in code; reloads on change). playmods.txt extras: `pr+N`/`br+N` punch/build reach (character state bytes 2/3, 128 = normal), and looks/abilities `eyes, horns, halo, highjump, fireproof, spikeproof, stinky, sparkle, shadow, irradiated, spotlight`; fireproof stops lava damage. `test/item_audit.sh` lists items per type
- Item effects, part 2: timed consumables in `resources/consumables.txt` (28 items: Blueberry, Devil Horns, Angelic Halo, Freeze/Curse/Fire Wand, Lucky Clover, Apple, g-Virus, Egg, Perfume...; reloads on change). `tools/buffs.cpp` keeps each mod in the profile as `buff:{name}` = `{ends}/{item}` so it survives logouts; mods give skin color, speed/gravity, looks (horns, halo, frozen, stinky, shadow, zombie), Lucky! (10% of gem drops x2-5), Doctor Repulsion (hides "of Legend"), Curse (HELL, same lock as /curse) and Fire Wand (dies). Used on yourself or on the player standing on the tile; players cannot use them on staff. Antidote ends the g-Virus. `/status` lists active mods. Durations and messages from the Growtopia wiki
- Item effects, part 3 (`include/tools/special_blocks.cpp`): loot blocks in `resources/loot.txt` (Mystery Block, Pot O' Gold, Golden Treasure Hoard, Deep Iron, Hidden Treasure, Locke's Mystery Box, Thanksgiving Turkey, Summer Surprise (3 prizes), Ultra & Lava Pinata; rare prizes announced), Mystery Pinata turns the items under it into festive ones; fossils in `resources/fossils.txt`: a loading world may turn a rock into a fossil rock, Rock Hammer taps unearth it (a second tap smashes it), Rock Chisel breaks it out as a Fossil (Amber Necklace 5% double), Fossil Brush makes a fresh Fossil a Polished Fossil, Fossil Prep Station (1-24 h, table `world_fossil_prep`) gives a fossil prize. Prize lists from the Growtopia wiki, chances are MikeTopia's (the wiki has none)
- Item effects, part 3b (`include/tools/special_tiles.cpp`): Harmonic Crystals (`resources/crystals.txt`, recipes from GTopia (MIT, keichira) HarmonicCrystal.cpp checked against the Growtopia wiki; Chi of 3144 items in `resources/chi.txt` from the wiki "Chi - Earth/Fire/Wind/Water" categories): stack 5 crystals (only in your own, non-public World-Locked world), the wrench tells which Chi is missing, a tap in harmony gives the prize (Chi Harmonizer / Harmonic Lock make it easier, Rainbow Crystal Block may give a crystal back), a combination without a recipe shatters. Bunny Egg (`resources/bunny_egg.txt`, wiki table): Magic Eggs grow it 0.1% each, break it for a prize by size, past 100% it may burst. Kept in table `world_special_tile`. Also fixed: the client reads extra tile data for crystals, bunny eggs, mailboxes, bulletin boards, donation boxes, achievement blocks, heart monitors, dice and providers (Chicken, Cow...) - the server sent none (or only the type byte), which could garble the rest of the world (formats from GTopia TileExtra.cpp)
- Machines, part 1 - providers (`include/tools/special_tiles.cpp`, prizes in `resources/providers.txt` from the Growtopia wiki): Cow, Chicken, Buffalo, Well, Tea Set, Coffee Maker, Science Station, ATM, Sheep (Sheep Shears double the Wool), Tackle Box, Surgical Tool Bag, Star Tool Droid, Awkward Friendly Unicorn and the 9 Awesome Item-O-Matics grow for the item's own grow time (items.dat), a punch when ready harvests, the wrench shows the time left, planting time kept in `world_special_tile`, the client gets the real growth in the tile data. Fixed: the old code gave a provider's prizes on EVERY punch (an ATM gave up to 100 gems a punch) and providers could never be broken
- Own login page (`resources/login.html`, `include/tools/web_login.cpp`, routes in `include/https/https.cpp`): loginurl is now www.growtopia1.com (our own server) instead of login-gurotopia.vercel.app, which received every password inside the token. Log in / New account tabs; the server checks the password and gives the game a random 64-hex session token (7 days, max 5 per account, only its SHA-256 kept), so the game never sees the password. Logins are answered by the main loop (the database isn't thread safe). Also fixed: tankIDName crashed on "user|x" (std::stoi) and read past the last pipe
- Bot & hacker protection (`include/tools/guard.cpp`): packet floods kick + block the IP 5 min, max 20 new connections/min and 4 at once per IP, 10 wrong passwords from one IP = 15 min login block, max 3 new accounts per IP per day; passwords/ltoken hidden in the console log
- Made-up packets: empty/short packets crashed the server; tiles and positions outside the world read/wrote memory past the world; actions and game packets before logging in are ignored; ENet max packet size 64 KB (was 32 MB)
- Logins: a refused login no longer saves an empty player over the account; the hand-over (first) connection never saves; logging in twice closes the old session first (no duplicated inventories)
- Login server (port 443): 5 s timeouts so one silent connection can't block every login, fixed an SSL leak per request
- Offline test harness (`test/`): real server code with a fake network, 272 checks for Mannequin, Password Door, Magplant, Fishing, Casino, pickups, Item Hunt, backups, staff building, tutorial, guilds, the news, security and the login page
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
- **GTopia** (https://github.com/keichira/GTopia, MIT, Copyright (c) 2026 keichira) - Magplant tile extra layout (type 0x3e), OnPlanterActivated and the machine rules (capacity, allowed items); Mannequin tile extra layout (type 0x0e) and dialog flow. Rewritten in Gurotopia's style.
- **GrowServer** (https://github.com/JadlionHD/GrowServer, MIT, Copyright (c) 2022-2025 JadlionHD) - punch effect ids per item (PUNCH_ITEMS) merged into resources/punch_effects.txt; login page flow (/player/login/dashboard, validate, checktoken).
- **GTopia playmods / character states** (MIT, (c) keichira) - character state bit names, punch/build range bytes, and a few item playmods (The One Ring, Fire Hose, Neutron Gun, Diamond Horns).
- **CreativePS** - inspiration for the /find rule, events and season gazette (no code copied).
- Growtopia is a game by Ubisoft. This project is not affiliated with Ubisoft.
