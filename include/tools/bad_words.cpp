#include "pch.hpp"
#include <filesystem>
#include <fstream>

#include "bad_words.hpp"

namespace
{
    constexpr const char *FILE_PATH = "resources/bad_words.txt";
    constexpr std::time_t RECHECK_SECONDS = 60;

    /* @note a name as runs of one letter: "PEEPEK" = p1 e2 p1 e1 k1. a word matches when its letters line up and every run in
     * the name is at least as long as in the word, so extra repeats don't hide it (PEEEPEK) but NIGERIA (one g) isn't "nigger" */
    struct run { char c; int n; };
    using runs = std::vector<run>;

    char plain(char c)
    {
        switch (c)
        {
            case '0': return 'o';
            case '1': return 'i';
            case '3': return 'e';
            case '4': return 'a';
            case '5': return 's';
            case '7': return 't';
            default:  return static_cast<char>(std::tolower(static_cast<u_char>(c)));
        }
    }

    runs to_runs(std::string_view s)
    {
        runs r{};
        for (char c : s)
        {
            if (!std::isalnum(static_cast<u_char>(c))) continue;
            const char p = plain(c);
            if (!r.empty() && r.back().c == p) ++r.back().n;
            else r.push_back({ p, 1 });
        }
        return r;
    }

    bool fits(const runs &name, std::size_t at, const runs &word)
    {
        for (std::size_t i = 0; i < word.size(); ++i)
            if (name[at + i].c != word[i].c || name[at + i].n < word[i].n) return false;
        return true;
    }

    struct entry { runs word; bool exact; };
    std::vector<entry> list{};
    std::filesystem::file_time_type loaded_time{};
    std::time_t next_check = 0;

    /* @note one word per line, '#' = comment. "=word" only matches the whole name (short words that hide inside normal ones) */
    void refresh()
    {
        const std::time_t now = std::time(nullptr);
        if (now < next_check) return;
        next_check = now + RECHECK_SECONDS;

        std::error_code ec{};
        const auto time = std::filesystem::last_write_time(FILE_PATH, ec);
        if (ec || (time == loaded_time && !list.empty())) return; // @note missing file: keep the last list
        loaded_time = time;

        std::vector<entry> fresh{};
        std::ifstream file(FILE_PATH);
        for (std::string line; std::getline(file, line); )
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line.starts_with('#')) continue;
            const bool exact = line.starts_with('=');
            runs word = to_runs(exact ? std::string_view(line).substr(1) : std::string_view(line));
            if (!word.empty()) fresh.push_back({ std::move(word), exact });
        }
        list = std::move(fresh);
        std::printf("[bad words] %zu words loaded\n", list.size());
    }
}

bool is_bad_name(std::string_view name)
{
    refresh();
    const runs n = to_runs(name);
    for (const entry &e : list)
    {
        if (e.word.size() > n.size()) continue;
        if (e.exact) { if (e.word.size() == n.size() && fits(n, 0, e.word)) return true; continue; }
        for (std::size_t at = 0; at + e.word.size() <= n.size(); ++at)
            if (fits(n, at, e.word)) return true;
    }
    return false;
}
