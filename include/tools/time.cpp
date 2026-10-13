#include "pch.hpp"

#include "time.hpp"

u_int ticks()
{
    return static_cast<u_int>(std::time(nullptr)); // @note real time: tree ages are saved, the monotonic clock restarted at every PC boot
}
