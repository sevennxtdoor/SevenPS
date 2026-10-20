# SevenPS

**Seven Private Server**: a Growtopia private server written in C++20, made by @Seven.

Everything SevenPS adds or changes (roles, trade, vending, surgery, ranks, guilds, events, login security,
backups...) is listed in [`NOTICE-SEVENPS.md`](NOTICE-SEVENPS.md).

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
| `items.dat` | The game's item list |
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

Apache License 2.0, see [`LICENSE`](LICENSE). SevenPS is a modified version of Gurotopia
(© gurotopia, 2024); the changes and other credits are in [`NOTICE-SEVENPS.md`](NOTICE-SEVENPS.md).
