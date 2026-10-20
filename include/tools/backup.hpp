#pragma once

/* @note automatic database backups: 2 minutes after start and then every 24 hours the whole `sevenps` database is
 * dumped with mariadb-dump into backup/sevenps_YYYY-MM-DD_HHMM.sql, the newest 7 are kept.
 * the password goes to the dump tool through its environment (MYSQL_PWD), never on the command line, because other
 * programs can read a process' command line. backup/ is in .gitignore: it holds password hashes and must never be pushed.
 * restore: mariadb -u root -p < backup\sevenps_....sql (with the server stopped). older dumps are gurotopia_....sql:
 * the database was called `gurotopia` then, so they restore into that name */

extern void backup_tick(); // @note main loop

extern void backup_command(ENetEvent &event, const std::string_view text); // @note /backup (owner): back up now / show the last result
