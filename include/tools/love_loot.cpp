#include "pch.hpp"
#include "tools/random.hpp"

#include "love_loot.hpp"

namespace
{
    /* @note growtopiawiki.com/w/Golden_Booty_Chest, amounts as listed there. found by name in items.dat */
    constexpr std::pair<std::string_view, short> wiki[]{
        { "Angel Wings", 1 }, { "Bear That Cares", 1 }, { "Booty Chest", 1 }, { "Bridal Dress with Train", 1 },
        { "Candy Heart", 1 }, { "Chocolate Heart", 1 }, { "Cotton Candy Swirl", 1 }, { "Cotton Candy Swirl Background", 1 },
        { "Dancing Candle", 1 }, { "Datemaster's Bling", 1 }, { "Datemaster's Rose", 1 }, { "Dating Dandy's Hair", 1 },
        { "Dating Dandy's Suit", 1 }, { "Dating Vixen's Dress", 1 }, { "Dating Vixen's Hair", 1 }, { "Diaper", 1 },
        { "Formal Rose Jacket", 1 }, { "Formal Rose Pants", 1 }, { "Formal Rose Skirt", 1 }, { "Good Witch's Wand", 1 },
        { "Groom's Coat", 1 }, { "Half Up Hairstyle", 1 }, { "Harp of Love", 1 }, { "Heart Awning", 10 },
        { "Heartbow", 1 }, { "Heartbreaker Hammer", 1 }, { "Heartcastle Stone", 10 }, { "Heartcastle Stone Background", 10 },
        { "Heart Glasses", 1 }, { "Heartking's Crown", 1 }, { "Heartking's Robes", 1 }, { "Heart Necklace", 1 },
        { "Heartqueen's Crown", 1 }, { "Heartqueen's Robes", 1 }, { "Heart Shirt", 1 }, { "Heartstone Block", 10 },
        { "Heartstaff", 1 }, { "Heartsword", 1 }, { "Heartthrob Helm", 1 }, { "Love Birds", 1 },
        { "Lovebird Pendant", 1 }, { "Love Bug", 1 }, { "Love Charged Hands", 1 }, { "Love Curtain", 2 },
        { "Love Portal", 1 }, { "Love Potion #8", 1 }, { "Loveseat", 2 }, { "Pegasus", 1 },
        { "Perfume", 1 }, { "Pink Marble Arch", 10 }, { "Rink of Love", 1 }, { "Romantic Pic-Nic Basket", 1 },
        { "Rose Cape", 1 }, { "Rose Rifle", 1 }, { "Ruby Necklace", 1 }, { "Silk Scarf", 1 },
        { "Sour Lollipop", 10 }, { "Strawberry Dress", 1 }, { "Strawberry Hair", 1 }, { "Sweet Lollipop", 1 },
        { "Tattooed Muscles", 1 }, { "Teeny Angel Wings", 1 }, { "Teddy Bear Block", 1 }, { "USMom's Talaria", 1 },
        { "Valensign", 1 }, { "Valentine", 1 }, { "Valentine's Card", 1 }, { "Valentine Dress", 1 }
        // @note the 69th, Golden Heart Crystal, stays tile_change.cpp's 1% roll
    };

    const std::vector<::slot> &loot()
    {
        static const std::vector<::slot> list = []
        {
            std::vector<::slot> out{};
            for (const auto &[name, amount] : wiki)
            {
                auto it = std::ranges::find(items, name, &::item::raw_name);
                if (it == items.end()) { std::printf("[love loot] \"%.*s\" is not in items.dat\n", static_cast<int>(name.size()), name.data()); continue; }
                out.emplace_back(static_cast<short>(it->id), amount);
            }
            return out;
        }();
        return list;
    }
}

::slot random_love_item()
{
    const auto &list = loot();
    if (list.empty()) return ::slot{ 388, 1 }; // @note Perfume, the old prize
    return list[RandomRange(0, static_cast<int>(list.size()))];
}

std::size_t love_loot_size() { return loot().size(); }
