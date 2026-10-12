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
- 76 more punch effects, double jump from back items, bedrock breakable by developers

**Saving & fixes**
- Saved to the database: clothing, gems, titles, ban status
- Server shutdown now saves online players and worlds
- Display block and vending machine tile data is now sent to the client
- MikeTopia gazette and login messages

## Credits
- **Gurotopia** - the server this is based on (Apache-2.0)
- **GTOS** (https://github.com/zKaan/GTOSLatest, GPL) - reference for the Super Supporter state bit,
  punch effect ids, and the trade / vending flow. MikeTopia's code is written separately in Gurotopia's style.
- Growtopia is a game by Ubisoft. This project is not affiliated with Ubisoft.
