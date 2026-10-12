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
if exist main_new.exe (
    echo Memasang versi server terbaru...
    move /y main_new.exe main.exe >nul || (
        echo [!] Gagal memasang versi baru. Coba lagi beberapa detik lagi.
        pause
        exit /b
    )
)
main.exe
if %errorlevel%==2 (
    echo.
    echo === Server restart via /restart, menyalakan ulang dalam 3 detik... ===
    timeout /t 3 /nobreak >nul
    goto start
)
pause
