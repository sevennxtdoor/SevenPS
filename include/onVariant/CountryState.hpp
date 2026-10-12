#pragma once

namespace on
{
    /* @note "us" or "us|maxLevel" (blue name) */
    extern std::string country_state(const ::peer &peer);

    /* @note sends this peer's country state to everyone in their world */
    extern void CountryState(ENetEvent& event);
}
