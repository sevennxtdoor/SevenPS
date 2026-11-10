"""SevenPS Discord bot.

The game server never talks to Discord. It writes to these tables and this bot reads them:
  discord_outbox        messages for Discord (announcements, super broadcasts, staff log, joins), deleted once sent
  server_status         online count, player list, peak, last update time, written every 30 s (no update for 90 s = offline)
  discord_link_pending  /link codes the bot makes; the player types /link {code} in game and the server links them
  discord_link          GrowID <-> Discord account, used by /player and the automatic roles
Commands /top, /player, /world and /online read `peer`, `world` and `guild` (only the columns in setup_db_user.sql).
"""
import asyncio
import os
import re
import secrets
import time
from dataclasses import dataclass, field
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
ROLE_ENV = {1: "ROLE_MOD", 2: "ROLE_DEV", 3: "ROLE_OWNER"}  # in-game role -> the .env key holding its Discord role id
GUILD_RANKS = ["Member", "Elder", "Co-Leader", "Leader"]  # tools/guild.cpp RANK_NAME
LINK_CODE_CHARS = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"     # no 0/O or 1/I to mix up
LINK_CODE_SECONDS = 600
LIST_MAX = 25               # names shown in a list before "...and N more"

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


ENV = {**load_env(HERE / ".env"), **{k: v for k, v in os.environ.items() if k.startswith(("DISCORD_", "DB_", "CHANNEL_", "GUILD_", "ROLE_"))}}


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


# ---------- server status ----------

@dataclass
class Player:
    shown: str   # GrowID, or the nick of staff under /nick
    world: str   # "" in the world menu
    growid: str  # "" for staff under /nick: they can't be found by their real name


@dataclass
class Status:
    online: bool = False
    count: int = 0
    uptime: int = 0
    peak: int = 0
    worlds: int = 0
    players: list[Player] = field(default_factory=list)

    def find(self, growid: str) -> Player | None:
        return next((p for p in self.players if p.growid and p.growid.lower() == growid.lower()), None)

    def in_world(self, world: str) -> list[Player]:
        return [p for p in self.players if p.world and p.world.upper() == world.upper()]


def parse_players(text: str | None) -> list[Player]:
    """server lines: shown<TAB>world<TAB>growid (tools/discord.cpp discord_players)"""
    players = []
    for line in (text or "").split("\n"):
        parts = line.split("\t")
        if not parts[0].strip():
            continue
        parts += [""] * (3 - len(parts))
        players.append(Player(parts[0], parts[1], parts[2]))
    return players


def server_status() -> Status:
    rows = query("SELECT updated, online, started, players, peak, worlds FROM server_status WHERE id = 1")
    if not rows:
        return Status()
    updated, online, started, players, peak, worlds = rows[0]
    now = int(time.time())
    if now - (updated or 0) > OFFLINE_AFTER:
        return Status()
    return Status(True, int(online or 0), now - int(started or now), int(peak or 0), int(worlds or 0), parse_players(players))


def name_list(players: list[Player], with_world: bool) -> str:
    shown = [f"{plain(p.shown)} - {plain(p.world)}" if with_world and p.world else plain(p.shown) for p in players[:LIST_MAX]]
    if len(players) > LIST_MAX:
        shown.append(f"...and {len(players) - LIST_MAX} more")
    return "\n".join(shown)[:1024]


# ---------- players, guilds, links ----------

def guild_of(rows: list[tuple], growid: str) -> tuple[str, str] | None:
    """guild rows (name, members "growid:rank,...") -> (guild, rank name) of this player"""
    for name, members in rows:
        for member in (members or "").split(","):
            who, _, rank = member.partition(":")
            if who.strip().lower() == growid.lower():
                r = int(rank) if rank.strip().isdigit() else 0
                return name, GUILD_RANKS[r] if r < len(GUILD_RANKS) else "Member"
    return None


def like_escape(text: str) -> str:
    return text.replace("\\", "\\\\").replace("%", "\\%").replace("_", "\\_")


def new_link_code() -> str:
    return "".join(secrets.choice(LINK_CODE_CHARS) for _ in range(6))


def managed_roles() -> set[int]:
    """the Discord roles this bot gives and takes (only the ones set in .env)"""
    return {r for r in (env_id("ROLE_LINKED"), *(env_id(k) for k in ROLE_ENV.values())) if r}


def wanted_roles(role, banned) -> set[int]:
    """the roles a linked player should have: Linked, plus their staff role. none while banned"""
    if banned:
        return set()
    wanted = {env_id("ROLE_LINKED"), env_id(ROLE_ENV.get(int(role or 0), ""))}
    return {r for r in wanted if r}


# ---------- bot ----------

intents = discord.Intents.none()
intents.guilds = True
client = discord.Client(intents=intents, allowed_mentions=discord.AllowedMentions.none())
tree = app_commands.CommandTree(client)
GUILD = discord.Object(id=env_id("GUILD_ID")) if env_id("GUILD_ID") else None
CHANNELS = {"announce": env_id("CHANNEL_ANNOUNCE"), "sb": env_id("CHANNEL_SB"), "staff": env_id("CHANNEL_STAFF")}
status_message: discord.Message | None = None


def status_embed(status: Status) -> discord.Embed:
    if status.online:
        embed = discord.Embed(title="🟢 SevenPS is online", color=0x2ECC71)
        embed.add_field(name="Players online", value=str(status.count))
        embed.add_field(name="Peak today", value=str(max(status.peak, status.count)))
        embed.add_field(name="Uptime", value=duration(status.uptime))
        embed.add_field(name="Active worlds", value=str(status.worlds))
        if status.players:
            embed.add_field(name="Who's on", value=name_list(status.players, with_world=False), inline=False)
    else:
        embed = discord.Embed(title="🔴 SevenPS is offline", description="The server is down or restarting.", color=0xE74C3C)
    join_info = ENV.get("JOIN_INFO", "").replace("\\n", "\n").strip()  # @note one .env line: \n starts a new line
    if join_info:
        embed.add_field(name="How to join", value=join_info[:1024], inline=False)
    logo = ENV.get("LOGO_URL", "")
    if logo.startswith("https://"):
        embed.set_thumbnail(url=logo)
    embed.set_footer(text="Updated")
    embed.timestamp = discord.utils.utcnow()
    return embed


@tasks.loop(seconds=30)
async def update_status():
    global status_message
    try:
        status = await asyncio.to_thread(server_status)
    except pymysql.MySQLError as e:
        print(f"[status] database error: {e}")
        return
    activity = discord.Activity(type=discord.ActivityType.watching, name=f"{status.count} players" if status.online else "server offline")
    await client.change_presence(status=discord.Status.online if status.online else discord.Status.idle, activity=activity)

    channel_id = env_id("CHANNEL_STATUS")
    channel = client.get_channel(channel_id) if channel_id else None
    if channel is None:
        return
    embed = status_embed(status)
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


OUTBOX_STYLE = {  # channel -> (author line, color)
    "announce": ("📣 SevenPS", 0xF1C40F),
    "sb":       ("📢 Super Broadcast", 0x3498DB),
    "staff":    ("🛡️ Staff log", 0x95A5A6),
}


def outbox_embed(channel_name: str, text: str) -> discord.Embed:
    author, color = OUTBOX_STYLE.get(channel_name, ("SevenPS", 0x95A5A6))
    if channel_name == "staff":  # @note texts from tankIDName.cpp, action/quit.cpp and web_login.cpp
        if text.startswith("New player registered: "):
            author, color = "🌱 New player", 0x1ABC9C
        elif re.fullmatch(r"\S+ joined \(level \d+\)", text):
            author, color = "➡️ Joined", 0x2ECC71
        elif re.fullmatch(r"\S+ left \(played \d+ min\)", text):
            author, color = "⬅️ Left", 0x7F8C8D
    embed = discord.Embed(description=plain(text)[:4000], color=color)
    embed.set_author(name=author)
    embed.timestamp = discord.utils.utcnow()
    return embed


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
            try:
                await channel.send(embed=outbox_embed(channel_name, text))
            except discord.HTTPException as e:
                print(f"[outbox] couldn't send to #{channel_name}: {e}")
        done.append(row_id)  # @note sent, or no channel set for it: either way it's done
    placeholders = ",".join(["%s"] * len(done))
    try:
        await asyncio.to_thread(execute, f"DELETE FROM discord_outbox WHERE id IN ({placeholders})", tuple(done))
    except pymysql.MySQLError as e:
        print(f"[outbox] database error: {e}")


@tree.command(name="online", description="Is SevenPS online, and who is playing?")
async def online_command(interaction: discord.Interaction):
    status = await asyncio.to_thread(server_status)
    embed = status_embed(status)
    for i, f in enumerate(embed.fields):  # @note here with the world each player is in
        if f.name == "Who's on":
            embed.set_field_at(i, name="Who's on", value=name_list(status.players, with_world=True), inline=False)
    await interaction.response.send_message(embed=embed)


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


def player_info(growid: str):
    """everything /player shows (blocking)"""
    rows = query("SELECT growid, level, xp, rank_level, role, banned, playtime, gems, wl_bank, last_online FROM peer WHERE growid = %s LIMIT 1", (growid,))
    if not rows:
        return None
    name = rows[0][0]
    guild = guild_of(query("SELECT name, members FROM guild WHERE members LIKE %s", ("%" + like_escape(name) + ":%",)), name)
    link = query("SELECT discord_id FROM discord_link WHERE growid = %s", (name,))
    return rows[0], guild, (int(link[0][0]) if link else None), server_status().find(name)


@tree.command(name="player", description="Look up a SevenPS player")
@app_commands.describe(growid="Their GrowID")
async def player_command(interaction: discord.Interaction, growid: str):
    growid = growid.strip()[:18]
    info = await asyncio.to_thread(player_info, growid)
    if info is None:
        await interaction.response.send_message(f"No player named **{plain(growid)}**.", ephemeral=True)
        return
    (name, level, xp, rank_level, role, banned, playtime, gems, wl_bank, last_online), guild, discord_id, now_on = info
    embed = discord.Embed(title=plain(name), color=0x95A5A6 if banned else (0x2ECC71 if now_on else 0x3498DB))
    if now_on:
        embed.description = "🟢 Online" + (f" in **{plain(now_on.world)}**" if now_on.world else "")
    elif last_online:
        embed.description = f"Last online <t:{int(last_online)}:R>"
    embed.add_field(name="Level", value=f"{level} ({int(xp or 0):,} XP)")
    embed.add_field(name="Rank", value=load_ranks().get(int(rank_level or 0), "None") if rank_level else "None")
    if role:
        embed.add_field(name="Staff", value=ROLE_NAMES.get(int(role), "Staff"))
    embed.add_field(name="Gems", value=f"{int(gems or 0):,}")
    embed.add_field(name="World Lock Bank", value=f"{int(wl_bank or 0):,}")
    embed.add_field(name="Played", value=duration(int(playtime or 0)))
    if guild:
        embed.add_field(name="Guild", value=f"{plain(guild[0])} ({guild[1]})")
    if discord_id:
        embed.add_field(name="Discord", value=f"<@{discord_id}>")  # @note shows the name, never pings (allowed_mentions none)
    if banned:
        embed.set_footer(text="This account is banned.")
    await interaction.response.send_message(embed=embed)


def world_info(name: str):
    """(name, owner GrowID or None, World Level, players inside) or None (blocking)"""
    rows = query("SELECT name, owner, min_level FROM world WHERE name = %s LIMIT 1", (name,))
    if not rows:
        return None
    world, owner, min_level = rows[0]
    owner_rows = query("SELECT growid FROM peer WHERE uid = %s", (owner,)) if owner else []
    return world, (owner_rows[0][0] if owner_rows else None), int(min_level or 1), server_status().in_world(world)


@tree.command(name="world", description="Look up a SevenPS world")
@app_commands.describe(name="World name")
async def world_command(interaction: discord.Interaction, name: str):
    name = "".join(c for c in name.upper() if c.isascii() and c.isalnum())[:24]
    info = await asyncio.to_thread(world_info, name) if name else None
    if info is None:
        await interaction.response.send_message(f"No world named **{plain(name) or '?'}**.", ephemeral=True)
        return
    world, owner, min_level, inside = info
    embed = discord.Embed(title=f"🌍 {plain(world)}", color=0x9B59B6)
    embed.add_field(name="Owner", value=f"🔒 {plain(owner)}" if owner else "🔓 Not locked")
    if min_level > 1:
        embed.add_field(name="World Level", value=str(min_level))
    embed.add_field(name=f"Players inside ({len(inside)})", value=name_list(inside, with_world=False) or "Nobody right now.", inline=False)
    await interaction.response.send_message(embed=embed)


def make_link_code(discord_id: int) -> tuple[str, str | None]:
    """(new code, GrowID this Discord account is linked to now, or None) (blocking)"""
    linked = query("SELECT growid FROM discord_link WHERE discord_id = %s", (discord_id,))
    execute("DELETE FROM discord_link_pending WHERE discord_id = %s OR expires < %s", (discord_id, int(time.time())))
    for _ in range(10):
        code = new_link_code()
        try:
            execute("INSERT INTO discord_link_pending (code, discord_id, expires) VALUES (%s, %s, %s)",
                    (code, discord_id, int(time.time()) + LINK_CODE_SECONDS))
            return code, (linked[0][0] if linked else None)
        except pymysql.IntegrityError:  # @note that code is someone else's right now: pick another
            continue
    raise pymysql.MySQLError("no free link code")


@tree.command(name="link", description="Link your Discord account to your SevenPS GrowID")
async def link_command(interaction: discord.Interaction):
    code, linked = await asyncio.to_thread(make_link_code, interaction.user.id)
    text = f"In game, type **`/link {code}`** within 10 minutes. Don't share this code."
    if linked:
        text += f"\nYou're linked to **{plain(linked)}** now: using the code moves the link to that GrowID."
    await interaction.response.send_message(text, ephemeral=True)


@tree.command(name="unlink", description="Unlink your Discord account from your SevenPS GrowID")
async def unlink_command(interaction: discord.Interaction):
    rows = await asyncio.to_thread(query, "SELECT growid FROM discord_link WHERE discord_id = %s", (interaction.user.id,))
    if not rows:
        await interaction.response.send_message("Your Discord account isn't linked.", ephemeral=True)
        return
    await asyncio.to_thread(execute, "DELETE FROM discord_link WHERE discord_id = %s", (interaction.user.id,))
    await interaction.response.send_message(f"Unlinked from **{plain(rows[0][0])}**.", ephemeral=True)
    await apply_roles(interaction.user.id, set())


# ---------- linked roles ----------

applied_roles: dict[int, set[int]] = {}  # discord id -> roles we last made sure they have (memory only)


async def apply_roles(discord_id: int, wanted: set[int]) -> None:
    guild = client.get_guild(env_id("GUILD_ID") or 0)
    if guild is None:
        return
    try:
        member = guild.get_member(discord_id) or await guild.fetch_member(discord_id)
    except discord.NotFound:  # @note not in the Discord server (yet): tried again next minute
        applied_roles.pop(discord_id, None)
        return
    except discord.HTTPException as e:
        print(f"[roles] discord error: {e}")
        return
    have = {r.id for r in member.roles} & managed_roles()
    add = [role for r in wanted - have if (role := guild.get_role(r))]
    remove = [role for r in have - wanted if (role := guild.get_role(r))]
    try:
        if add:
            await member.add_roles(*add, reason="SevenPS account link")
        if remove:
            await member.remove_roles(*remove, reason="SevenPS account link")
        applied_roles[discord_id] = wanted
    except discord.Forbidden:
        print("[roles] missing permission: give the bot Manage Roles and put its role above the Linked/staff roles")
    except discord.HTTPException as e:
        print(f"[roles] discord error: {e}")


@tasks.loop(seconds=60)
async def sync_roles():
    if not managed_roles():
        return
    try:
        rows = await asyncio.to_thread(query, "SELECT l.discord_id, p.role, p.banned FROM discord_link l JOIN peer p ON p.growid = l.growid")
    except pymysql.MySQLError as e:
        print(f"[roles] database error: {e}")
        return
    wanted = {int(discord_id): wanted_roles(role, banned) for discord_id, role, banned in rows}
    for discord_id in set(applied_roles) - set(wanted):  # @note unlinked since the last check
        wanted[discord_id] = set()
    for discord_id, roles in wanted.items():
        if applied_roles.get(discord_id) != roles:
            await apply_roles(discord_id, roles)


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
    for loop in (update_status, send_outbox, sync_roles):
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
