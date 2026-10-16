@echo off
title MikeTopia GTPS
cd /d "%~dp0"
set PATH=%PATH%;C:\msys64\ucrt64\bin;C:\msys64\usr\bin

tasklist /fi "imagename eq main.exe" | find /i "main.exe" >nul
if not errorlevel 1 (
    echo [!] Server lama masih jalan. Tutup dulu jendela server yang lama, lalu jalankan file ini lagi.
    pause
    exit /b
)

:start
if not exist main_new.exe goto run
echo Memasang versi server terbaru...
rem antivirus sering mengunci file baru beberapa detik setelah dibuat: coba ulang, jangan langsung gagal
set tries=0
:install
move /y main_new.exe main.exe >nul 2>nul && goto run
set /a tries+=1
if %tries% geq 15 (
    echo [!] Gagal memasang versi baru setelah 15 detik. Server versi lama tetap dijalankan.
    goto run
)
echo     file masih dikunci, coba lagi... (%tries%/15)
timeout /t 1 /nobreak >nul
goto install

:run
main.exe
if %errorlevel%==2 (
    echo.
    echo === Server restart via /restart, menyalakan ulang dalam 3 detik... ===
    timeout /t 3 /nobreak >nul
    goto start
)
pause
