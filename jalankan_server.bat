@echo off
title SevenPS - Seven Private Server
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
rem McAfee Firewall memegang main.exe (program yang diizinkan firewall), jadi main.exe tidak bisa diganti dengan move.
rem copy tetap bisa (isi file ditimpa, file-nya sendiri tidak diganti), lalu main_new.exe dihapus.
set tries=0
:install
copy /b /y main_new.exe main.exe >nul 2>nul && del main_new.exe >nul 2>nul && goto run
set /a tries+=1
if %tries% geq 30 (
    echo [!] Gagal memasang versi baru setelah 30 detik. Server versi lama tetap dijalankan.
    goto run
)
echo     file masih dikunci, coba lagi... (%tries%/30)
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
