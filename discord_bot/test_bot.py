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
lines = "Mike\tSTART\tMike\n`6@Hidden``\tSECRET\t\nBob\t\tBob"
with mock.patch.object(bot, "query", return_value=[(now - 10, 3, now - 3600, lines, 9, 4)]):
    s = bot.server_status()
    check(s.online and s.count == 3 and s.uptime == 3600 and s.peak == 9 and s.worlds == 4, "fresh status: online, peak, worlds")
    check(len(s.players) == 3 and s.find("mike").world == "START" and s.find("Bob").world == "", "player list: case-insensitive, world menu = no world")
    check(s.find("Hidden") is None and [p.shown for p in s.in_world("secret")] == ["`6@Hidden``"], "a /nick staff can't be found by name, but shows in the world")
with mock.patch.object(bot, "query", return_value=[(now - 200, 7, now - 3600, lines, 9, 4)]):
    check(bot.server_status() == bot.Status(), "no update for 200 s: offline")
with mock.patch.object(bot, "query", return_value=[]):
    check(bot.server_status() == bot.Status(), "server never ran: offline")
check(bot.parse_players(None) == [] and bot.parse_players("") == [] and len(bot.parse_players("A")) == 1, "empty / old-style player text")
check(bot.status_embed(s).title.startswith("🟢") and bot.status_embed(bot.Status()).title.startswith("🔴"), "status embeds")
check(any(f.name == "Who's on" and "@Hidden" in f.value and "`" not in f.value for f in bot.status_embed(s).fields), "who's on list, color codes out")
many = [bot.Player(f"P{i}", "W", f"P{i}") for i in range(40)]
check(bot.name_list(many, False).endswith("...and 15 more"), "long lists are cut")
with mock.patch.dict(bot.ENV, {"JOIN_INFO": "Line 1\\nLine 2", "LOGO_URL": "http://insecure"}):
    e = bot.status_embed(bot.Status())
    check(any(f.name == "How to join" and f.value == "Line 1\nLine 2" for f in e.fields) and not e.thumbnail.url, "join info with new lines, http logo ignored")

print("[outbox]")
check(bot.outbox_embed("staff", "Mike joined (level 12)").author.name.startswith("➡️"), "join embed")
check(bot.outbox_embed("staff", "Mike left (played 5 min)").author.name.startswith("⬅️"), "leave embed")
check(bot.outbox_embed("staff", "New player registered: Bob").author.name.startswith("🌱"), "new player embed")
check(bot.outbox_embed("staff", "Mike /ban Bob joined (level 1) x").author.name.startswith("🛡️"), "a staff action isn't mistaken for a join")
check(bot.outbox_embed("sb", "`9hi @everyone").description == "hi @​everyone", "sb embed: colors out, no ping")

print("[guild & link]")
rows = [("Alpha", "Bobby:0,Ann:3"), ("Beta", "bob:1")]
check(bot.guild_of(rows, "Bob") == ("Beta", "Elder") and bot.guild_of(rows, "ann") == ("Alpha", "Leader") and bot.guild_of(rows, "Zed") is None, "guild and rank, exact name only")
check(bot.like_escape("a_b%") == "a\\_b\\%", "LIKE wildcards escaped")
codes = {bot.new_link_code() for _ in range(200)}
check(all(len(c) == 6 and set(c) <= set(bot.LINK_CODE_CHARS) for c in codes) and len(codes) > 190, "link codes: 6 clear characters, random")
with mock.patch.dict(bot.ENV, {"ROLE_LINKED": "10", "ROLE_MOD": "11", "ROLE_DEV": "", "ROLE_OWNER": "13"}):
    check(bot.managed_roles() == {10, 11, 13}, "managed roles: only the ones set")
    check(bot.wanted_roles(0, 0) == {10} and bot.wanted_roles(1, 0) == {10, 11} and bot.wanted_roles(3, 0) == {10, 13}, "linked + staff role")
    check(bot.wanted_roles(2, 0) == {10} and bot.wanted_roles(1, 1) == set(), "unset role skipped, banned gets none")

print("[boards]")
check(all(col.replace("_", "").isalpha() for col, _ in bot.BOARDS.values()), "leaderboard columns are fixed names")
grants = (bot.HERE / "setup_db_user.sql").read_text(encoding="utf-8")
check(all(col in grants for col, _ in bot.BOARDS.values()), "every leaderboard column is granted to the bot account")
check("password" not in grants.split("ON sevenps.peer")[0].split("GRANT SELECT (")[1], "the bot account can't read passwords")
check("last_online" in grants and "uid," in grants and "sevenps.world" in grants and "sevenps.guild" in grants, "/player and /world columns granted")
check("INSERT, DELETE ON sevenps.discord_link_pending" in grants and "INSERT" not in grants.split("ON sevenps.discord_link TO")[0].splitlines()[-1],
      "the bot only makes codes; the server writes the link itself")

print(f"\n{passes} passed, {fails} failed")
raise SystemExit(1 if fails else 0)
