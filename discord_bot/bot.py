"""SevenPS Discord bot.

The game server never talks to Discord. It writes to two tables and this bot reads them:
  discord_outbox  messages for Discord (announcements, super broadcasts, staff log), deleted once sent
  server_status   online count + last update time, written every 30 s (no update for 90 s = offline)
Commands /top, /player and /online read the `peer` table (only the columns in setup_db_user.sql).
"""
import asyncio
import os
import re
import time
from pathlib import Path

import discord
import pymysql
from discord import app_commands
from discord.ext import tasks

HERE = Path(__file__).resolve().parent
RANKS_FILE = HERE.parent / "resources" / "ranks.txt"

OFFLINE_AFTER = 90          # seconds without a server_status update
OUTBOX_BATCH = 20           # messages sent per check (every 5 s)
ROLE_NAMES = {1: "Moderator", 2: "Developer", 3: "Owner"}

# same boards as the in-game /top (tools/cps.cpp). columns are fixed here, never taken from the user
BOARDS = {
    "level":   ("level",         "Highest Level"),
    "gems":    ("gems",          "Richest in Gems"),
    "rich":    ("wl_bank",       "Biggest World Lock Bank"),
    "surgeon": ("surgery_skill", "Best Surgeons"),
    "played":  ("playtime",      "Most Played"),
}


# ---------- config ----------

def load_env(path: Path) -> dict:
    """KEY=value lines, '#' comments. no extra package needed."""
    env = {}
    if path.exists():
        for line in path.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, value = line.split("=", 1)
            env[key.strip()] = value.strip().strip('"').strip("'")
    return env


ENV = {**load_env(HERE / ".env"), **{k: v for k, v in os.environ.items() if k.startswith(("DISCORD_", "DB_", "CHANNEL_", "GUILD_"))}}


def env_id(key: str) -> int | None:
    value = ENV.get(key, "")
    return int(value) if value.isdigit() else None


# ---------- text ----------

COLOR = re.compile(r"`.?")


def plain(text: str) -> str:
    """Growtopia color codes (`9, `w, ``) out, Discord markdown and @mentions made harmless."""
    text = COLOR.sub("", text)
    text = discord.utils.escape_markdown(text)
    return discord.utils.escape_mentions(text)


def duration(seconds: int) -> str:
    hours, minutes = divmod(max(seconds, 0) // 60, 60)
    days, hours = divmod(hours, 24)
    if days:
        return f"{days}d {hours}h"
    return f"{hours}h {minutes}m" if hours else f"{minutes}m"


def load_ranks() -> dict[int, str]:
    """resources/ranks.txt: level|name|color|price|..."""
    ranks = {}
    try:
        for line in RANKS_FILE.read_text(encoding="utf-8").splitlines():
            parts = line.split("|")
            if line.startswith("#") or len(parts) < 2 or not parts[0].strip().isdigit():
                continue
            ranks[int(parts[0])] = parts[1].strip()
    except OSError:
        pass
    return ranks


# ---------- database (blocking, run in a thread) ----------

def db():
    return pymysql.connect(
        host=ENV.get("DB_HOST", "127.0.0.1"), port=int(ENV.get("DB_PORT", "3306") or 3306),
        user=ENV.get("DB_USER", ""), password=ENV.get("DB_PASSWORD", ""), database=ENV.get("DB_NAME", "sevenps"),
        connect_timeout=5, read_timeout=10, write_timeout=10, autocommit=True, charset="utf8mb4",
    )


def query(sql: str, args=()) -> list[tuple]:
    with db() as conn, conn.cursor() as cur:
        cur.execute(sql, args)
        return list(cur.fetchall())


def execute(sql: str, args=()) -> None:
    with db() as conn, conn.cursor() as cur:
        cur.execute(sql, args)


def server_status() -> tuple[bool, int, int]:
    """(online, players, uptime seconds)"""
    rows = query("SELECT updated, online, started FROM server_status WHERE id = 1")
    if not rows:
        return False, 0, 0
    updated, online, started = rows[0]
    now = int(time.time())
    if now - (updated or 0) > OFFLINE_AFTER:
        return False, 0, 0
    return True, int(online or 0), now - int(started or now)


# ---------- bot ----------

intents = discord.Intents.none()
intents.guilds = True
client = discord.Client(intents=intents, allowed_mentions=discord.AllowedMentions.none())
tree = app_commands.CommandTree(client)
GUILD = discord.Object(id=env_id("GUILD_ID")) if env_id("GUILD_ID") else None
CHANNELS = {"announce": env_id("CHANNEL_ANNOUNCE"), "sb": env_id("CHANNEL_SB"), "staff": env_id("CHANNEL_STAFF")}
status_message: discord.Message | None = None


def status_embed(online: bool, players: int, uptime: int) -> discord.Embed:
    if online:
        embed = discord.Embed(title="🟢 SevenPS is online", color=0x2ECC71)
        embed.add_field(name="Players online", value=str(players))
        embed.add_field(name="Uptime", value=duration(uptime))
    else:
        embed = discord.Embed(title="🔴 SevenPS is offline", description="The server is down or restarting.", color=0xE74C3C)
    embed.set_footer(text="Updated")
    embed.timestamp = discord.utils.utcnow()
    return embed


@tasks.loop(seconds=30)
async def update_status():
    global status_message
    try:
        online, players, uptime = await asyncio.to_thread(server_status)
    except pymysql.MySQLError as e:
        print(f"[status] database error: {e}")
        return
    activity = discord.Activity(type=discord.ActivityType.watching, name=f"{players} players" if online else "server offline")
    await client.change_presence(status=discord.Status.online if online else discord.Status.idle, activity=activity)

    channel_id = env_id("CHANNEL_STATUS")
    channel = client.get_channel(channel_id) if channel_id else None
    if channel is None:
        return
    embed = status_embed(online, players, uptime)
    try:
        if status_message is None:  # @note after a bot restart: reuse our last status message instead of posting a new one
            async for message in channel.history(limit=20):
                if message.author == client.user:
                    status_message = message
                    break
        if status_message is None:
            status_message = await channel.send(embed=embed)
        else:
            await status_message.edit(embed=embed)
    except discord.NotFound:
        status_message = None
    except discord.HTTPException as e:
        print(f"[status] discord error: {e}")


@tasks.loop(seconds=5)
async def send_outbox():
    try:
        rows = await asyncio.to_thread(query, "SELECT id, channel, text FROM discord_outbox ORDER BY id LIMIT %s", (OUTBOX_BATCH,))
    except pymysql.MySQLError as e:
        print(f"[outbox] database error: {e}")
        return
    if not rows:
        return
    done = []
    for row_id, channel_name, text in rows:
        channel_id = CHANNELS.get(channel_name)
        channel = client.get_channel(channel_id) if channel_id else None
        if channel is not None:
            prefix = {"sb": "📢 ", "staff": "🛡️ "}.get(channel_name, "")
            try:
                await channel.send((prefix + plain(text))[:2000])
            except discord.HTTPException as e:
                print(f"[outbox] couldn't send to #{channel_name}: {e}")
        done.append(row_id)  # @note sent, or no channel set for it: either way it's done
    placeholders = ",".join(["%s"] * len(done))
    try:
        await asyncio.to_thread(execute, f"DELETE FROM discord_outbox WHERE id IN ({placeholders})", tuple(done))
    except pymysql.MySQLError as e:
        print(f"[outbox] database error: {e}")


@tree.command(name="online", description="Is SevenPS online, and how many players are on?")
async def online_command(interaction: discord.Interaction):
    online, players, uptime = await asyncio.to_thread(server_status)
    await interaction.response.send_message(embed=status_embed(online, players, uptime))


@tree.command(name="top", description="SevenPS leaderboards")
@app_commands.describe(board="Which leaderboard")
@app_commands.choices(board=[app_commands.Choice(name=title, value=key) for key, (_, title) in BOARDS.items()])
async def top_command(interaction: discord.Interaction, board: app_commands.Choice[str]):
    column, title = BOARDS[board.value]
    rows = await asyncio.to_thread(query, f"SELECT growid, {column} FROM peer WHERE banned = 0 AND role < 1 ORDER BY {column} DESC LIMIT 10")
    medals = ["🥇", "🥈", "🥉"]
    lines = []
    for i, (growid, value) in enumerate(rows):
        shown = duration(int(value or 0)) if column == "playtime" else f"{int(value or 0):,}"
        lines.append(f"{medals[i] if i < 3 else f'`{i + 1}.`'} **{plain(growid)}** - {shown}")
    embed = discord.Embed(title=f"🏆 {title}", description="\n".join(lines) or "Nobody yet.", color=0xF1C40F)
    await interaction.response.send_message(embed=embed)


@tree.command(name="player", description="Look up a SevenPS player")
@app_commands.describe(growid="Their GrowID")
async def player_command(interaction: discord.Interaction, growid: str):
    growid = growid.strip()[:18]
    rows = await asyncio.to_thread(query, "SELECT growid, level, xp, rank_level, role, banned, playtime FROM peer WHERE growid = %s LIMIT 1", (growid,))
    if not rows:
        await interaction.response.send_message(f"No player named **{plain(growid)}**.", ephemeral=True)
        return
    name, level, xp, rank_level, role, banned, playtime = rows[0]
    embed = discord.Embed(title=plain(name), color=0x95A5A6 if banned else 0x3498DB)
    embed.add_field(name="Level", value=f"{level} ({int(xp or 0):,} XP)")
    embed.add_field(name="Rank", value=load_ranks().get(int(rank_level or 0), "None") if rank_level else "None")
    if role:
        embed.add_field(name="Staff", value=ROLE_NAMES.get(int(role), "Staff"))
    embed.add_field(name="Played", value=duration(int(playtime or 0)))
    if banned:
        embed.set_footer(text="This account is banned.")
    await interaction.response.send_message(embed=embed)


@tree.error
async def on_command_error(interaction: discord.Interaction, error: app_commands.AppCommandError):
    print(f"[command] {error}")
    text = "Couldn't reach the SevenPS database, try again later." if isinstance(getattr(error, "original", None), pymysql.MySQLError) else "Something went wrong."
    if interaction.response.is_done():
        await interaction.followup.send(text, ephemeral=True)
    else:
        await interaction.response.send_message(text, ephemeral=True)


@client.event
async def on_ready():
    if GUILD:  # @note commands copied to your server show up right away (global ones can take an hour)
        tree.copy_global_to(guild=GUILD)
        await tree.sync(guild=GUILD)
    else:
        await tree.sync()
    for loop in (update_status, send_outbox):
        if not loop.is_running():
            loop.start()
    print(f"[bot] logged in as {client.user}")


def main():
    token = ENV.get("DISCORD_TOKEN", "")
    if not token:
        raise SystemExit("No DISCORD_TOKEN: copy env-template.txt to .env and fill it in.")
    client.run(token)


if __name__ == "__main__":
    main()
