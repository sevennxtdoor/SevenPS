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
- System broadcasts: placeholders in `resources/broadcast.txt` ({online} {players} {worlds} {event} {next_event}), "event in 5 minutes" warning, event popup notification, hourly server stats, new player welcome, level milestones (every 10 levels) and achievements announced to everyone

**Saving & fixes**
- Saved to the database: clothing, gems, titles, ban status
- Server shutdown now saves online players and worlds
- Display block and vending machine tile data is now sent to the client
- Door destinations are saved (`world_door` table), so doors still work after a restart
- "X left" message now goes to the other players instead of the one leaving
- MikeTopia gazette and login messages

## Credits
- **Gurotopia** - the server this is based on (Apache-2.0)
- **GTOS** (https://github.com/zKaan/GTOSLatest, GPL) - reference for the Super Supporter state bit,
  punch effect ids, the trade / vending flow and the Surg-E dialog format. MikeTopia's code is written separately in Gurotopia's style.
- **CreativePS** - inspiration for the /find rule, events and season gazette (no code copied).
- Growtopia is a game by Ubisoft. This project is not affiliated with Ubisoft.
