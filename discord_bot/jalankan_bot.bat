@echo off
rem SevenPS Discord bot. Needs uv (https://docs.astral.sh/uv/) and discord_bot\.env (copy env-template.txt).
rem Restarts itself if it stops (internet down, Discord hiccup). Close this window to stop the bot.
cd /d "%~dp0"
:loop
uv run --quiet python bot.py
echo [bot] stopped, starting again in 10 seconds...
timeout /t 10 /nobreak >nul
goto loop
