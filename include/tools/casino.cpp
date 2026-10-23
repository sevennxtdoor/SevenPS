#include "pch.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "tools/random.hpp"

#include "casino.hpp"

namespace
{
    constexpr auto SPIN_COOLDOWN = std::chrono::milliseconds(1500); // @note GTOS: one spin per 1.5 s
    constexpr int SPIN_DELAY = 2000; // @note ms: the result shows when the wheel stops

    /* @note the red numbers of a real roulette table, the rest (1-36) are black, 0 is green */
    constexpr int RED[] = { 1, 3, 5, 7, 9, 12, 14, 16, 18, 19, 21, 23, 25, 27, 30, 32, 34, 36 };

    char color_of(int number)
    {
        if (number == 0) return '2';
        return (std::ranges::find(RED, number) != std::end(RED)) ? '4' : 'b';
    }

    /* @note reme: add the digits, keep the last digit (19 -> 10 -> 0). 0, 19 and 28 are the best */
    int reme(int number) { return (number / 10 + number % 10) % 10; }
}

void roulette_spin(ENetEvent &event)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const auto now = std::chrono::steady_clock::now();
    if (now < pPeer->spin_ready) return; // @note still turning
    pPeer->spin_ready = now + SPIN_COOLDOWN;

    const int number = RandomRange(0, 37); // @note 0-36 (RandomRange never returns the upper bound)
    const char color = color_of(number);
    const std::string bubble  = std::format("`w[{} `wspun the wheel and got `{}{}`w!]``", pPeer->display_growid, color, number);
    const std::string console = std::format("`7[``{} `ospun the wheel and got `{}{}`o!`7]`` `o(reme `w{}`o)``", pPeer->display_growid, color, number, reme(number));

    /* @note everyone in the world sees it (the old code sent every copy to the spinner only) */
    peers(pPeer->recent_worlds.back(), PEER_SAME_WORLD, [&](ENetPeer &p)
    {
        send_varlist(&p, { "OnTalkBubble", pPeer->netid, bubble, 0u }, -1, SPIN_DELAY);
        on::ConsoleMessage(&p, console, 0, SPIN_DELAY);
    });
}

bool fake_spin(std::string_view text)
{
    std::string plain{};
    plain.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '`') { ++i; continue; } // @note `w, `4 ... color codes
        if (std::isspace(static_cast<u_char>(text[i]))) continue; // @note "s p u n" tricks
        plain += static_cast<char>(std::tolower(static_cast<u_char>(text[i])));
    }
    return plain.find("spunthewheel") != std::string::npos;
}
