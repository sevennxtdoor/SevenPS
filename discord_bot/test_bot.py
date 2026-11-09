"""Offline checks for bot.py (no Discord, no database). run: uv run python test_bot.py"""
import time
from unittest import mock

import bot

passes = fails = 0


def check(cond, msg):
    global passes, fails
    if cond:
        passes += 1
        print(f"  ok   {msg}")
    else:
        fails += 1
        print(f"  FAIL {msg}")


print("[text]")
check(bot.plain("`9** ``Mike `9reached level `w50``!") == "\\*\\* Mike reached level 50!", "color codes out, markdown escaped")
check("@everyone" not in bot.plain("hi @everyone"), "@everyone made harmless")
check(bot.plain("a`") == "a", "a lone backtick at the end")
check(bot.duration(59) == "0m" and bot.duration(3600 + 120) == "1h 2m" and bot.duration(90000) == "1d 1h", "durations")

print("[config]")
with mock.patch.dict(bot.ENV, {"CHANNEL_SB": "123", "CHANNEL_STAFF": "abc"}):
    check(bot.env_id("CHANNEL_SB") == 123 and bot.env_id("CHANNEL_STAFF") is None and bot.env_id("NOPE") is None, "channel ids: number or off")
env = bot.load_env(bot.HERE / "env-template.txt")
check(env.get("DB_USER") == "sevenps_bot" and env.get("DISCORD_TOKEN") == "", "env-template.txt parses")

print("[ranks]")
ranks = bot.load_ranks()
check(len(ranks) > 0 and all(isinstance(k, int) and v for k, v in ranks.items()), f"resources/ranks.txt read ({len(ranks)} ranks)")

print("[status]")
now = int(time.time())
with mock.patch.object(bot, "query", return_value=[(now - 10, 7, now - 3600)]):
    check(bot.server_status() == (True, 7, 3600), "fresh status: online")
with mock.patch.object(bot, "query", return_value=[(now - 200, 7, now - 3600)]):
    check(bot.server_status() == (False, 0, 0), "no update for 200 s: offline")
with mock.patch.object(bot, "query", return_value=[]):
    check(bot.server_status() == (False, 0, 0), "server never ran: offline")
check(bot.status_embed(True, 3, 60).title.startswith("🟢") and bot.status_embed(False, 0, 0).title.startswith("🔴"), "status embeds")

print("[boards]")
check(all(col.replace("_", "").isalpha() for col, _ in bot.BOARDS.values()), "leaderboard columns are fixed names")
grants = (bot.HERE / "setup_db_user.sql").read_text(encoding="utf-8")
check(all(col in grants for col, _ in bot.BOARDS.values()), "every leaderboard column is granted to the bot account")
check("password" not in grants.split("ON sevenps.peer")[0].split("GRANT SELECT (")[1], "the bot account can't read passwords")

print(f"\n{passes} passed, {fails} failed")
raise SystemExit(1 if fails else 0)
