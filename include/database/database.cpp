#include "pch.hpp"

#include "database.hpp"
#include "database_config.hpp"

MYSQL *db;

void create_table_if_not_exist()
{
    std::string query_peer = 
        "CREATE TABLE IF NOT EXISTS peer ("
            "uid INT AUTO_INCREMENT PRIMARY KEY,"
            "growid VARCHAR(18) UNIQUE,"
            "password VARCHAR(18),"
            "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
            "inventory BLOB,"
            "role TINYINT UNSIGNED DEFAULT 0,"
            "clothing BLOB,"
            "banned TINYINT UNSIGNED DEFAULT 0,"
            "titles INT DEFAULT 0,"
            "titles_on INT DEFAULT 0,"
            "gems INT DEFAULT 0,"
            "surgery_skill INT DEFAULT 0,"
            "malpractice INT DEFAULT 0,"
            "notebook VARCHAR(255) DEFAULT '',"
            "wl_bank INT DEFAULT 0,"
            "playtime INT DEFAULT 0,"
            "profile TEXT,"
            "level INT DEFAULT 1,"
            "xp INT DEFAULT 0,"
            "rank_level INT DEFAULT 0"
        ")";
    /* @note older tables made before these columns existed */
    std::string query_peer_role =
        "ALTER TABLE peer "
            "ADD COLUMN IF NOT EXISTS role TINYINT UNSIGNED DEFAULT 0,"
            "ADD COLUMN IF NOT EXISTS clothing BLOB,"
            "ADD COLUMN IF NOT EXISTS banned TINYINT UNSIGNED DEFAULT 0,"
            "ADD COLUMN IF NOT EXISTS titles INT DEFAULT 0,"
            "ADD COLUMN IF NOT EXISTS titles_on INT DEFAULT 0,"
            "ADD COLUMN IF NOT EXISTS gems INT DEFAULT 0,"
            "ADD COLUMN IF NOT EXISTS surgery_skill INT DEFAULT 0,"
            "ADD COLUMN IF NOT EXISTS malpractice INT DEFAULT 0,"
            "ADD COLUMN IF NOT EXISTS notebook VARCHAR(255) DEFAULT '',"
            "ADD COLUMN IF NOT EXISTS wl_bank INT DEFAULT 0,"
            "ADD COLUMN IF NOT EXISTS playtime INT DEFAULT 0,"
            "ADD COLUMN IF NOT EXISTS profile TEXT,"
            "ADD COLUMN IF NOT EXISTS level INT DEFAULT 1,"
            "ADD COLUMN IF NOT EXISTS xp INT DEFAULT 0,"
            "ADD COLUMN IF NOT EXISTS rank_level INT DEFAULT 0";
    std::string query_nuked = "CREATE TABLE IF NOT EXISTS nuked (name VARCHAR(24) NOT NULL PRIMARY KEY)";
    std::string query_world = 
        "CREATE TABLE IF NOT EXISTS world ("
            "name VARCHAR(24) NOT NULL PRIMARY KEY,"
            "owner INT DEFAULT 0,"
            "blocks MEDIUMBLOB,"
            "objects MEDIUMBLOB"
        ")";
    std::string query_vending =
        "CREATE TABLE IF NOT EXISTS vending ("
            "world VARCHAR(24) NOT NULL,"
            "x INT NOT NULL,"
            "y INT NOT NULL,"
            "item INT DEFAULT 0,"
            "stock INT DEFAULT 0,"
            "price INT DEFAULT 0,"
            "per_lock TINYINT DEFAULT 0,"
            "earned INT DEFAULT 0,"
            "PRIMARY KEY (world, x, y)"
        ")";
    /* @note tools/social.cpp */
    std::string query_friends = "CREATE TABLE IF NOT EXISTS friends (a VARCHAR(18) NOT NULL, b VARCHAR(18) NOT NULL, PRIMARY KEY (a, b))";
    std::string query_reports =
        "CREATE TABLE IF NOT EXISTS reports ("
            "id INT AUTO_INCREMENT PRIMARY KEY, reporter VARCHAR(18), target VARCHAR(18), world VARCHAR(24), reason VARCHAR(128), created_at INT"
        ")";
    std::string query_trades =
        "CREATE TABLE IF NOT EXISTS trade_log ("
            "id INT AUTO_INCREMENT PRIMARY KEY, time INT, a VARCHAR(18), b VARCHAR(18), a_items VARCHAR(255), b_items VARCHAR(255), INDEX (a), INDEX (b)"
        ")";
    std::string query_doors = "CREATE TABLE IF NOT EXISTS world_door (world VARCHAR(24) NOT NULL, x INT NOT NULL, y INT NOT NULL, dest VARCHAR(64), door_id VARCHAR(64), PRIMARY KEY (world, x, y))"; // @note door destinations
    std::string query_door_password = "ALTER TABLE world_door ADD COLUMN IF NOT EXISTS password VARCHAR(24)"; // @note Password Door
    std::string query_magplant = "CREATE TABLE IF NOT EXISTS world_magplant (world VARCHAR(24) NOT NULL, x INT NOT NULL, y INT NOT NULL, item INT, count INT, flags INT DEFAULT 1, PRIMARY KEY (world, x, y))"; // @note Magplant 5000 & co
    std::string query_staff_log = "CREATE TABLE IF NOT EXISTS staff_log (id INT AUTO_INCREMENT PRIMARY KEY, time INT, staff VARCHAR(18), action VARCHAR(16), target VARCHAR(18), item INT, amount INT, INDEX (staff))"; // @note /stafflog
    std::string query_weather = "CREATE TABLE IF NOT EXISTS world_weather (world VARCHAR(24) NOT NULL PRIMARY KEY, weather INT DEFAULT 0)"; // @note /weather
    std::string query_area_lock = "CREATE TABLE IF NOT EXISTS world_area_lock (world VARCHAR(24) NOT NULL, x INT NOT NULL, y INT NOT NULL, item INT, owner INT, owner_name VARCHAR(18), access VARCHAR(1000), flags INT DEFAULT 0, tiles TEXT, PRIMARY KEY (world, x, y))"; // @note Small/Big/Huge/Builder's Lock
    std::string query_box = "CREATE TABLE IF NOT EXISTS world_box (world VARCHAR(24) NOT NULL, x INT NOT NULL, y INT NOT NULL, password VARCHAR(24), items TEXT, notes TEXT, PRIMARY KEY (world, x, y))"; // @note storage, vault, donation, mail, bulletin
    std::string query_password = "ALTER TABLE peer MODIFY COLUMN password VARCHAR(128)"; // @note PBKDF2 hashes (tools/auth.cpp)
    std::string query_guild = "CREATE TABLE IF NOT EXISTS guild (name VARCHAR(18) NOT NULL PRIMARY KEY, statement VARCHAR(128), fg INT NOT NULL DEFAULT 0, bg INT NOT NULL DEFAULT 0, members TEXT, created BIGINT NOT NULL DEFAULT 0)"; // @note Guilds
    std::string query_mannequin = "CREATE TABLE IF NOT EXISTS world_mannequin (world VARCHAR(24) NOT NULL, x INT NOT NULL, y INT NOT NULL, text VARCHAR(128), clothes VARCHAR(100), PRIMARY KEY (world, x, y))"; // @note Mannequin
    std::string query_fossil_prep = "CREATE TABLE IF NOT EXISTS world_fossil_prep (world VARCHAR(24) NOT NULL, x INT NOT NULL, y INT NOT NULL, fossil INT, ready BIGINT, PRIMARY KEY (world, x, y))"; // @note Fossil Prep Station
    std::string query_redeem = "CREATE TABLE IF NOT EXISTS redeem_code (code VARCHAR(20) NOT NULL PRIMARY KEY, gems INT NOT NULL DEFAULT 0, items VARCHAR(255), uses_left INT NOT NULL DEFAULT 1, expires BIGINT NOT NULL DEFAULT 0, created_by VARCHAR(18))"; // @note redeem codes (tools/rewards.cpp)
    std::string query_redeem_used = "CREATE TABLE IF NOT EXISTS redeem_used (code VARCHAR(20) NOT NULL, user_id INT NOT NULL, used BIGINT, PRIMARY KEY (code, user_id))";
    std::string query_special_tile = "CREATE TABLE IF NOT EXISTS world_special_tile (world VARCHAR(24) NOT NULL, x INT NOT NULL, y INT NOT NULL, data VARCHAR(64), PRIMARY KEY (world, x, y))"; // @note Harmonic Crystals, Bunny Egg
    /* @note BLOB holds 64 KB: a world full of trees/signs or with 4000+ drops didn't fit, so its save was refused and every change lost */
    std::string query_world_size = "ALTER TABLE world MODIFY COLUMN blocks MEDIUMBLOB, MODIFY COLUMN objects MEDIUMBLOB";
    if (mysql_query(db, query_peer.c_str()) || mysql_query(db, query_peer_role.c_str()) || mysql_query(db, query_world.c_str()) || mysql_query(db, query_nuked.c_str()) || mysql_query(db, query_vending.c_str()) || mysql_query(db, query_weather.c_str()) ||
        mysql_query(db, query_friends.c_str()) || mysql_query(db, query_reports.c_str()) || mysql_query(db, query_trades.c_str()) || mysql_query(db, query_doors.c_str()) || mysql_query(db, query_door_password.c_str()) || mysql_query(db, query_staff_log.c_str()) || mysql_query(db, query_area_lock.c_str()) || mysql_query(db, query_box.c_str()) || mysql_query(db, query_magplant.c_str()) || mysql_query(db, query_mannequin.c_str()) || mysql_query(db, query_guild.c_str()) || mysql_query(db, query_password.c_str()) || mysql_query(db, query_fossil_prep.c_str()) || mysql_query(db, query_special_tile.c_str()) || mysql_query(db, query_redeem.c_str()) || mysql_query(db, query_redeem_used.c_str()))
    {
        std::fprintf(stderr, "%s\n", mysql_error(db));
    }
    if (mysql_query(db, query_world_size.c_str())) std::fprintf(stderr, "%s\n", mysql_error(db)); // @note after CREATE: the table must exist
    /* @note World Lock: the access list, World Level and music setting were never saved (lost on every restart) */
    if (mysql_query(db, "ALTER TABLE world ADD COLUMN IF NOT EXISTS access VARCHAR(255) DEFAULT '', ADD COLUMN IF NOT EXISTS min_level INT DEFAULT 1, ADD COLUMN IF NOT EXISTS lock_state INT DEFAULT 0"))
        std::fprintf(stderr, "%s\n", mysql_error(db));
}

void mysql_connect()
{
    db = mysql_init(NULL);

    if (mysql_real_connect(db, gDb_config.host.c_str(), gDb_config.user.c_str(), gDb_config.passwd.c_str(), NULL, 3306u, NULL, 0ul) == NULL) 
    {
        std::fprintf(stderr, "[MariaDB] %s\n", mysql_error(db));
    }
    else printf("connected to MariaDB server on %s:%d\n", db->host, db->port);

    mysql_query(db, "CREATE DATABASE IF NOT EXISTS sevenps");
    mysql_select_db(db, "sevenps");

    create_table_if_not_exist();
}

/* hStmt */

hStmt::hStmt(const std::string &query)
{
    this->pStmt = mysql_stmt_init(db);
    if (!pStmt) 
    {
        std::fprintf(stderr, "%s\n", mysql_error(db));
    }
    if (mysql_stmt_prepare(pStmt, query.c_str(), (u_long)query.size()))
    {
        std::fprintf(stderr, "%s\n", mysql_error(db));
    }
}
hStmt::~hStmt() 
{
    if (mysql_stmt_close(pStmt))
    {
        std::fprintf(stderr, "%s\n", mysql_error(db));
    }
}

void hStmt::bind_param(MYSQL_BIND *param)
{
    if (mysql_stmt_bind_param(pStmt, param)) log_err();
}
void hStmt::execute()
{
    if (mysql_stmt_execute(pStmt)) log_err();
}
void hStmt::fetch()
{
    int value = mysql_stmt_fetch(pStmt);
    if (value == 1 || value == MYSQL_DATA_TRUNCATED) log_err();
    else if (value == 0 || value == MYSQL_NO_DATA) /*@todo do something later...*/; // @note success
}


/* ~ hStmt ~ */


MYSQL_BIND make_bind_in(const signed &buffer)
{
    return { .buffer = (void*)&buffer, .buffer_type = MYSQL_TYPE_LONG };
}
MYSQL_BIND make_bind_in(const unsigned &buffer)
{
    return { .buffer = (void*)&buffer, .buffer_type = MYSQL_TYPE_LONG, .is_unsigned = true };
}
MYSQL_BIND make_bind_in(const long &buffer)
{
    return { .buffer = (void*)&buffer, .buffer_type = MYSQL_TYPE_LONG };
}
MYSQL_BIND make_bind_in(const long long &buffer)
{
    return { .buffer = (void*)&buffer, .buffer_type = MYSQL_TYPE_LONGLONG };
}
MYSQL_BIND make_bind_in(const float &buffer)
{
    return { .buffer = (void*)&buffer, .buffer_type = MYSQL_TYPE_FLOAT };
}
MYSQL_BIND make_bind_in(const std::string &buffer)
{
    return { .buffer = (void*)buffer.c_str(), .buffer_length = (u_long)buffer.size(), .buffer_type = MYSQL_TYPE_STRING };
}
MYSQL_BIND make_bind_in(const std::vector<u_char> &buffer)
{
    return { .buffer = (void*)buffer.data(), .buffer_length = (u_long)buffer.size(), .buffer_type = MYSQL_TYPE_BLOB };
}
MYSQL_BIND make_bind_in(const ::blob &buffer)
{
    return { .buffer = (void*)buffer.data().data(), .buffer_length = (u_long)buffer.size(), .buffer_type = MYSQL_TYPE_BLOB };
}

MYSQL_BIND make_bind_out(signed &buffer)
{
    return { .buffer = &buffer, .buffer_type = MYSQL_TYPE_LONG };
}
MYSQL_BIND make_bind_out(unsigned &buffer)
{
    return { .buffer = &buffer, .buffer_type = MYSQL_TYPE_LONG, .is_unsigned = true };
}
MYSQL_BIND make_bind_out(long &buffer)
{
    return { .buffer = &buffer, .buffer_type = MYSQL_TYPE_LONG };
}
MYSQL_BIND make_bind_out(long long &buffer)
{
    return { .buffer = &buffer, .buffer_type = MYSQL_TYPE_LONGLONG };
}
MYSQL_BIND make_bind_out(float &buffer)
{
    return { .buffer = &buffer, .buffer_type = MYSQL_TYPE_FLOAT };
}
/* @note out buffers: a longer value is cut off at this size. it was 1 KB for text (a big `profile` lost its end) and 60 KB
 * for blobs (the blocks of a world full of trees/signs didn't fit, so the bottom of the world loaded empty) */
MYSQL_BIND make_bind_out(std::string &buffer)
{
    buffer.resize(64 * 1024, '\0');

    return { .buffer = buffer.data(), .buffer_length = (u_long)buffer.size(), .buffer_type = MYSQL_TYPE_STRING };
}
MYSQL_BIND make_bind_out(std::vector<u_char> &buffer)
{
    buffer.resize(1024 * 1024);

    return { .buffer = buffer.data(), .buffer_length = (u_long)buffer.size(), .buffer_type = MYSQL_TYPE_BLOB };
}
MYSQL_BIND make_bind_out(::blob &buffer)
{
    buffer.resize(1024 * 1024);

    return { .buffer = buffer.data().data(), .buffer_length = (u_long)buffer.size(), .buffer_type = MYSQL_TYPE_BLOB };
}
