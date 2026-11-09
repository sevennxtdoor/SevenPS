-- SevenPS Discord bot: its own MariaDB account that can only read what the bot needs.
-- It can't see passwords, inventories or IPs, and can't change any player data.
-- 1. Replace CHANGE_ME below with a new password (the same one goes in discord_bot/.env as DB_PASSWORD)
-- 2. Run it as the MariaDB root user, e.g. in HeidiSQL, or: mysql -u root -p < setup_db_user.sql
-- 3. Start the server once first, so the discord_outbox and server_status tables exist.

CREATE USER IF NOT EXISTS 'sevenps_bot'@'localhost' IDENTIFIED BY 'CHANGE_ME';
CREATE USER IF NOT EXISTS 'sevenps_bot'@'127.0.0.1' IDENTIFIED BY 'CHANGE_ME';

GRANT SELECT (growid, level, xp, gems, wl_bank, surgery_skill, playtime, rank_level, role, banned) ON sevenps.peer TO 'sevenps_bot'@'localhost', 'sevenps_bot'@'127.0.0.1';
GRANT SELECT ON sevenps.server_status TO 'sevenps_bot'@'localhost', 'sevenps_bot'@'127.0.0.1';
GRANT SELECT, DELETE ON sevenps.discord_outbox TO 'sevenps_bot'@'localhost', 'sevenps_bot'@'127.0.0.1';
FLUSH PRIVILEGES;
