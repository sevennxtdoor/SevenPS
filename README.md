<p align="center">
  <img src="docs/banner.svg" alt="SevenPS" width="560">
</p>

<p align="center">
  <img src="https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus&logoColor=white" alt="C++20">
  <img src="https://img.shields.io/badge/MariaDB-database-003545?logo=mariadb&logoColor=white" alt="MariaDB">
  <img src="https://img.shields.io/badge/platform-Windows%20%7C%20Linux-555555" alt="Windows | Linux">
  <a href="#license"><img src="https://img.shields.io/badge/license-Apache%202.0%20base%20%2B%20SevenPS%20terms-blue" alt="License"></a>
  <br>
  <a href="https://sevennxtdoor.github.io/SevenPS/"><img src="https://img.shields.io/badge/website-sevennxtdoor.github.io-2bb377" alt="Website"></a>
  <a href="NOTICE-SEVENPS.md"><img src="https://img.shields.io/badge/changelog-NOTICE--SEVENPS.md-orange" alt="Changes"></a>
</p>

<p align="center">
  <b>Seven Private Server</b>: a Growtopia private server written in C++20, made by @Seven.
</p>

- **Built to behave like the real game.** Trade, vending, surgery, fishing, guilds, area locks, magplants,
  providers, combiners and hundreds of item effects, with numbers checked against the Growtopia wiki.
- **Staff tools that stay safe.** Roles saved in the database, role checks on every staff command, staff log,
  item-leak rules for moderators.
- **Secure login.** HTTPS login server, PBKDF2-SHA256 password hashes, rate limits, daily database backups.
- **Tested offline.** A harness runs the real server code against a fake network, hundreds of tests per build.
- **Discord bot.** Live server status, announcements, staff log and account linking in Discord.

Everything SevenPS adds or changes is listed in [`NOTICE-SEVENPS.md`](NOTICE-SEVENPS.md).

> **Disclaimer:** SevenPS is a fan-made, non-commercial project for learning. It is not affiliated with,
> endorsed by or connected to Ubisoft or Growtopia. Growtopia and its names are trademarks of their owners.
> This repository contains no game files or assets from Ubisoft (no `items.dat`, images or sounds), and
> nothing in SevenPS is sold for real money.

## Requirements

- [MSYS2](https://www.msys2.org/) (Windows) with the UCRT64 toolchain:
  ```bash
  pacman -S --needed mingw-w64-ucrt-x86_64-{gcc,openssl} make
  ```
- [MariaDB Server](https://mariadb.org/download/) (the server stores players and worlds in it)
- Linux instead: `build-essential libssl-dev libmariadb-dev` (Debian/Ubuntu) or `base-devel openssl mariadb-libs` (Arch)

## Database

Give the server its own MariaDB user, never `root`:

```sql
CREATE USER 'sevenps'@'localhost' IDENTIFIED BY 'a-long-random-password';
CREATE USER 'sevenps'@'127.0.0.1' IDENTIFIED BY 'a-long-random-password';
GRANT ALL PRIVILEGES ON sevenps.* TO 'sevenps'@'localhost';
GRANT ALL PRIVILEGES ON sevenps.* TO 'sevenps'@'127.0.0.1';
FLUSH PRIVILEGES;
```

Then put the login in `mysql_login.txt` next to the server (the password may not contain `|`):

```
host|127.0.0.1
user|sevenps
password|a-long-random-password
```

The server makes the `sevenps` database and its tables on the first start. Keep MariaDB on
`bind-address=127.0.0.1` so it can't be reached from the internet.

## Build

**Windows** (from the project folder, in any bash):

```bash
MSYSTEM=UCRT64 /c/msys64/usr/bin/bash.exe -lc "cd '$PWD' && make -j\$(nproc) OS=Windows_NT binary=main_new.exe"
```

`OS=Windows_NT` is needed when the shell doesn't set it, or the Makefile links the Linux ENet library.
Building `main_new.exe` works while `main.exe` is running. In VS Code, **Ctrl+Shift+B** runs "Build SevenPS".

**Linux**: `make -j$(nproc)` builds `main.out`.

## Run

- **Windows**: `jalankan_server.bat`. It installs a fresh `main_new.exe` as `main.exe`, starts it,
  and starts it again after an in-game `/restart`.
- **Linux**: `./main.out`

Ports: **UDP 17091** for the game (ENet; the `port` in `server_data.php`) and **TCP 443** for the login server.

To play on your own PC, point the game's server addresses at it in the **hosts** file
(`C:\Windows\System32\drivers\etc\hosts`, or `/etc/hosts`):

```
127.0.0.1 www.growtopia1.com
127.0.0.1 www.growtopia2.com
```

## Files next to the server

| File | What it is |
|---|---|
| `mysql_login.txt` | MariaDB login (see above). Secret, git-ignored |
| `server_data.php` | Address, port, maintenance text and login page the game gets. Made on the first start |
| `items.dat` | The game's item list. Not included (it belongs to Ubisoft): copy it from your own Growtopia install |
| `resources/` | Login page, store, ranks, prize tables, punch effects and other data the server reads |
| `resources/ctx/` | TLS certificate and key for the login server. The keys are git-ignored, never upload them |
| `reset_password.txt` | Forgotten password: write `growid|new password` in it and start the server. It is deleted after use |
| `backup/` | Automatic database dumps (`sevenps_*.sql`, newest 7 kept). Git-ignored: they hold password hashes |

Passwords are stored as PBKDF2-SHA256 hashes and can't be read back, not even by the owner; use
`reset_password.txt` instead.

The Owner role is only given in the database:
`UPDATE peer SET role = 3 WHERE growid = 'YourName';` (1 = Moderator, 2 = Developer, 3 = Owner).
Owners and Developers can then use `/setrole` in game.

## Tests

An offline harness runs the real server code with a fake network against the local MariaDB
(it makes a test world `MTTEST` and deletes it again). Build the server first, then:

```bash
MSYSTEM=UCRT64 /c/msys64/usr/bin/bash.exe -lc "bash '$PWD/test/run.sh'"
```

`test/item_audit.sh` lists the items in `items.dat` per type.

## License

SevenPS is a modified version of Gurotopia (© gurotopia, 2024). Gurotopia's original code stays
under the Apache License 2.0, see [`LICENSE`](LICENSE).

The SevenPS changes (© 2026 Seven, all rights reserved) may be read and studied, but **using, copying,
modifying or hosting them needs permission first**. Ask on Discord: **feelingsarefake or iseven_99**.

The changes and other credits are in [`NOTICE-SEVENPS.md`](NOTICE-SEVENPS.md).
