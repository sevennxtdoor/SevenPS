#!/bin/bash
# SevenPS offline tests: real server code, fake network, local MariaDB (world MTTEST is made then deleted).
# run from MSYS2: MSYSTEM=UCRT64 /c/msys64/usr/bin/bash.exe -lc "bash /c/Users/acer/Belajar/GTPS/Gurotopia/test/run.sh"
# build the server first (make binary=main_new.exe) so build/include/*.o are fresh.
export OS=Windows_NT
export MIKETOPIA_BACKUP_DIR=build/test/backup # @note never touch the real backup/ folder
G=$(cd "$(dirname "$0")/.." && pwd)
O="$G/build/test"
mkdir -p "$O"
cd "$G"
g++ -std=c++20 -g -Iinclude -Ibuild/include -include include/pch.hpp -c test/harness.cpp -o "$O/harness.o" || exit 1
# world.cpp again with -ffunction-sections, so the fake ~world() in harness.cpp wins
g++ -std=c++20 -g -ffunction-sections -Iinclude -Ibuild/include -include include/pch.hpp -c include/database/world.cpp -o "$O/world_fs.o" || exit 1
objs="$O/world_fs.o $(find build/include -name '*.o' ! -path '*/database/world.o')"
g++ "$O/harness.o" $objs -o "$O/harness.exe" -Wl,--allow-multiple-definition -L./include/enet/lib -L./include/mysql/lib -lssl -lcrypto -lmariadb -lenet_32 -lws2_32 -lwinmm || exit 1
cp -n libmariadb.dll "$O/" 2>/dev/null
"$O/harness.exe"
