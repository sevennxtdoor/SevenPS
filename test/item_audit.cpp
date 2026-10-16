// MikeTopia item audit: reads items.dat with the real parser and prints how many items there are per type,
// with a few example names. run: test/item_audit.sh (after make). output: build/test/item_audit.txt
#include "pch.hpp"
#include <cstdio>
#include <map>

int main()
{
    decode_items();
    std::map<int, std::vector<const ::item*>> by_type{};
    std::size_t real{};
    for (const ::item &it : items)
    {
        if (it.raw_name.empty() || it.id % 2 == 1) continue; // @note odd ids are the seeds of the item before
        by_type[it.type].push_back(&it);
        ++real;
    }
    std::printf("items (no seeds): %zu, seeds: %zu\n", real, items.size() - real);
    for (const auto &[type, list] : by_type)
    {
        std::printf("type %3d  %5zu  ", type, list.size());
        for (std::size_t i = 0; i < list.size() && i < 6; ++i) std::printf("%s%s", i ? ", " : "", list[i * (list.size() / std::min<std::size_t>(list.size(), 6))]->raw_name.c_str());
        std::printf("\n");
    }
    /* @note full list for scripts: id|type|cloth_type|name */
    if (FILE *f = std::fopen("build/test/items.tsv", "w"))
    {
        for (const ::item &it : items) std::fprintf(f, "%u|%u|%u|%s\n", it.id, it.type, it.cloth_type, it.raw_name.c_str());
        std::fclose(f);
    }
    std::fflush(stdout);
    _exit(0);
}
