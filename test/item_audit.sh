#!/bin/bash
# item audit: items per type in items.dat. build the server first (make binary=main_new.exe).
# run from MSYS2: MSYSTEM=UCRT64 /c/msys64/usr/bin/bash.exe -lc "bash /c/Users/acer/Belajar/GTPS/Gurotopia/test/item_audit.sh"
export OS=Windows_NT
G=$(cd "$(dirname "$0")/.." && pwd)
O="$G/build/test"
mkdir -p "$O"
cd "$G"
g++ -std=c++20 -g -Iinclude -Ibuild/include -include include/pch.hpp -c test/item_audit.cpp -o "$O/item_audit.o" || exit 1
g++ "$O/item_audit.o" $(find build/include -name '*.o') -o "$O/item_audit.exe" -Wl,--allow-multiple-definition -L./include/enet/lib -L./include/mysql/lib -lssl -lcrypto -lmariadb -lenet_32 -lws2_32 -lwinmm || exit 1
cp -n libmariadb.dll "$O/" 2>/dev/null
"$O/item_audit.exe" | tee "$O/item_audit.txt"
