#include "pch.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "tools/buffs.hpp"
#include "tools/jammer.hpp"

#include "zombie.hpp"

namespace
{
    constexpr short G_VIRUS = 764; // @note its line in resources/consumables.txt is the "Infected!" mod passed on
}

void zombie_punch(ENetEvent &event, ::world &world, const ::pos &tile)
{
    ::peer &zombie = *static_cast<::peer*>(event.peer->data);
    const buff_def *def = buff_for_item(G_VIRUS);
    if (def == nullptr || !has_buff(zombie, def->name)) return;
    if (jammer_on(world, jammer::ZOMBIE)) return; // @note Zombie Jammer: "Zombie bites can't infect anyone here."

    ENetPeer *victim{};
    peers(world.name, PEER_SAME_WORLD, [&](ENetPeer &p)
    {
        if (&p != event.peer && victim == nullptr && static_cast<::peer*>(p.data)->pos.by_32(true) == tile) victim = &p;
    });
    if (victim == nullptr) return;

    ::peer &v = *static_cast<::peer*>(victim->data);
    if (v.role >= MODERATOR || has_buff(v, def->name)) return; // @note staff can't be infected, like use_buff() won't hit them

    ENetEvent ve{ .peer = victim };
    buff_add(ve, *def, ve); // @note `by` = the victim, else it says "You used it on..." to the zombie
    on::ConsoleMessage(event.peer, std::format("`2You infected ``{}`2!``", v.display_growid));
}
