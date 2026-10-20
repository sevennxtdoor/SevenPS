#include "pch.hpp"
#include "database/database_config.hpp"
#include "onVariant/ConsoleMessage.hpp"

#include "backup.hpp"

#include <atomic>
#include <cstring>
#include <filesystem>
#include <mutex>
#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#endif

namespace
{
    namespace fs = std::filesystem;

    constexpr int BACKUP_GAP = 24 * 60 * 60;
    constexpr std::size_t KEEP = 7;
    /* @note the test harness sets MIKETOPIA_BACKUP_DIR: its test backups used to rotate the real ones out of backup/ */
    const fs::path FOLDER = std::getenv("MIKETOPIA_BACKUP_DIR") ? std::getenv("MIKETOPIA_BACKUP_DIR") : "backup";

    std::atomic<bool> busy{};
    std::mutex result_mutex;
    std::string last_result = "no backup yet";
    std::time_t next_backup = std::time(nullptr) + 2 * 60; // @note not during startup

    void set_result(const std::string &r)
    {
        std::lock_guard lock(result_mutex);
        last_result = r;
        std::printf("[backup] %s\n", r.c_str());
    }

    /* @note C:\Program Files\MariaDB xx.x\bin\mariadb-dump.exe (or the old mysqldump.exe name) */
    fs::path find_dump_tool()
    {
        std::error_code ec;
        for (const fs::directory_entry &dir : fs::directory_iterator("C:/Program Files", ec))
        {
            if (!dir.path().filename().string().starts_with("MariaDB")) continue;
            for (const char *exe : { "mariadb-dump.exe", "mysqldump.exe" })
                if (fs::exists(dir.path() / "bin" / exe, ec)) return dir.path() / "bin" / exe;
        }
        return {};
    }

    void prune()
    {
        std::error_code ec;
        std::vector<fs::path> files{};
        for (const fs::directory_entry &f : fs::directory_iterator(FOLDER, ec))
            if (f.path().filename().string().starts_with("sevenps_") && f.path().extension() == ".sql") files.push_back(f.path());
        std::ranges::sort(files); // @note the date is in the name, so oldest first
        while (files.size() > KEEP) { fs::remove(files.front(), ec); files.erase(files.begin()); }
    }

#ifdef _WIN32
    /* @note our environment + MYSQL_PWD, only for the child process */
    std::vector<char> child_environment(const std::string &password)
    {
        std::vector<char> env{};
        if (char *block = GetEnvironmentStringsA())
        {
            for (char *s = block; *s; s += std::strlen(s) + 1)
            {
                if (std::string_view{ s }.starts_with("MYSQL_PWD=")) continue;
                env.insert(env.end(), s, s + std::strlen(s) + 1);
            }
            FreeEnvironmentStringsA(block);
        }
        const std::string pwd = "MYSQL_PWD=" + password;
        env.insert(env.end(), pwd.begin(), pwd.end());
        env.push_back('\0');
        env.push_back('\0');
        return env;
    }

    void run_backup(std::string host, std::string user, std::string password)
    {
        const fs::path tool = find_dump_tool();
        if (tool.empty()) return set_result("failed: mariadb-dump.exe not found in C:\\Program Files\\MariaDB*");

        std::error_code ec;
        fs::create_directories(FOLDER, ec);
        const std::time_t now = std::time(nullptr);
        std::tm tm{}; localtime_s(&tm, &now);
        char stamp[32]; std::strftime(stamp, sizeof(stamp), "%Y-%m-%d_%H%M", &tm);
        const fs::path out = FOLDER / std::format("sevenps_{}.sql", stamp);

        SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE }; // @note the child writes straight into the file
        HANDLE file = CreateFileW(out.c_str(), GENERIC_WRITE, 0, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return set_result("failed: can't create " + out.string());

        STARTUPINFOA si{ sizeof(si) };
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = file;
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        si.hStdInput = nullptr;
        PROCESS_INFORMATION pi{};

        /* @note no password here: command lines are visible to every program on the PC */
        std::string cmd = std::format("\"{}\" --host={} --user={} --single-transaction --routines --databases sevenps", tool.string(), host, user);
        std::vector<char> env = child_environment(password);
        const BOOL ok = CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, env.data(), nullptr, &si, &pi);
        std::fill(env.begin(), env.end(), '\0'); // @note don't leave the password lying around in memory longer than needed
        if (!ok)
        {
            CloseHandle(file);
            fs::remove(out, ec);
            return set_result("failed: couldn't start mariadb-dump");
        }
        WaitForSingleObject(pi.hProcess, 10 * 60 * 1000);
        DWORD code = 1;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread); CloseHandle(file);

        const auto size = fs::file_size(out, ec);
        if (code != 0 || ec || size < 100)
        {
            fs::remove(out, ec);
            return set_result(std::format("failed: mariadb-dump exit code {}", code));
        }
        prune();
        set_result(std::format("ok: {} ({} KB)", out.string(), size / 1024));
    }
#else
    void run_backup(std::string, std::string, std::string) { set_result("automatic backups are Windows-only for now, use mariadb-dump yourself"); }
#endif

    void start_backup()
    {
        if (busy.exchange(true)) return; // @note one at a time
        std::thread([host = gDb_config.host, user = gDb_config.user, pwd = gDb_config.passwd]() mutable
        {
            run_backup(host, user, pwd); // @note in the background: the game keeps running while it dumps
            std::fill(pwd.begin(), pwd.end(), '\0');
            busy = false;
        }).detach();
    }
}

void backup_tick()
{
    if (std::time(nullptr) < next_backup) return;
    next_backup = std::time(nullptr) + BACKUP_GAP;
    start_backup();
}

void backup_command(ENetEvent &event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (pPeer->role < OWNER) return on::ConsoleMessage(event.peer, "`4Only the owner can use /backup.``");

    std::string result;
    { std::lock_guard lock(result_mutex); result = last_result; }
    if (text.find("now") != std::string_view::npos)
    {
        if (busy) return on::ConsoleMessage(event.peer, "`oA backup is already running.``");
        start_backup();
        return on::ConsoleMessage(event.peer, "`oBackup started in the background. Type `$/backup`` in a minute to see the result.``");
    }
    on::ConsoleMessage(event.peer, std::format("`oLast backup: `w{}``. Next automatic one in `w{}`` minutes. `$/backup now`` to back up right away.``",
        result, std::max<long long>(0, (next_backup - std::time(nullptr) + 59) / 60)));
}
