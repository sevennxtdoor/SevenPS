#include "pch.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "onVariant/SetBux.hpp"
#include "tools/economy.hpp"
#include "tools/profile.hpp"

#include "tutorial.hpp"

namespace
{
    struct step { std::string_view stat; int need; std::string_view title, hint; };

    constexpr step STEPS[] = {
        { "worlds_locked", 1,  "Lock your own world",       "Open the world menu, type a new world name and place your `wMy First World Lock`` in it." },
        { "broken",        10, "Break 10 blocks",           "Punch blocks in your world until they break. Blocks drop gems and seeds!" },
        { "placed",        10, "Place 10 blocks",           "Choose a block in your backpack and tap an empty spot to place it." },
        { "planted",       1,  "Plant a seed",              "Place a seed on the ground. It grows into a tree." },
        { "harvested",     1,  "Harvest a tree",            "When the tree is fully grown, punch it to collect its fruit." },
        { "store_buys",    1,  "Buy something in the Store", "Open the Store and buy anything. A `wWorld Lock`` costs 2000 gems." },
    };
    constexpr int STEP_COUNT = static_cast<int>(std::size(STEPS));
    constexpr int STEP_GEMS = 100, FINISH_GEMS = 400; // @note 1000 gems once per account: a push, not a farm

    ::peer &me(ENetEvent &event) { return *static_cast<::peer*>(event.peer->data); }

    void show_step(ENetEvent &event, int index)
    {
        const step &s = STEPS[index];
        on::ConsoleMessage(event.peer, std::format("`9Tutorial {}/{}:`` `w{}`` - {} `o(`$/tutorial`` for the list)``",
            index + 1, STEP_COUNT, s.title, s.hint));
    }
}

void tutorial_check(ENetEvent &event)
{
    ::peer &p = me(event);
    int index = pget(p, "tut_step");
    if (index >= STEP_COUNT) return;

    bool advanced = false;
    while (index < STEP_COUNT && pget(p, std::string{ STEPS[index].stat }) >= STEPS[index].need)
    {
        p.gems += STEP_GEMS;
        econ("tutorial", STEP_GEMS);
        on::ConsoleMessage(event.peer, std::format("`2Tutorial step done:`` {} `2+{} gems``", STEPS[index].title, STEP_GEMS));
        pset(p, "tut_step", ++index);
        advanced = true;
    }
    if (!advanced) return;

    send_action(*event.peer, "play_sfx", "file|audio/cash_register.wav\ndelayMS|0\n");
    if (index >= STEP_COUNT)
    {
        p.gems += FINISH_GEMS;
        econ("tutorial", FINISH_GEMS);
        send_varlist(event.peer, { "OnTextOverlay", std::format("`9Tutorial complete!`` +{} bonus gems", FINISH_GEMS) });
        on::ConsoleMessage(event.peer, std::format("`9** Tutorial complete! ``You got a `w{}`` gem bonus. Now try `$/parkour``, fishing, surgery and the `$/hunt``. Have fun!", FINISH_GEMS));
    }
    else
    {
        send_varlist(event.peer, { "OnTextOverlay", std::format("`2Tutorial step done!`` Next: {}", STEPS[index].title) });
        show_step(event, index);
    }
    on::SetBux(event);
}

void tutorial_on_join(ENetEvent &event)
{
    tutorial_check(event); // @note steps finished before the tutorial existed count right away
    const int index = pget(me(event), "tut_step");
    if (index < STEP_COUNT) show_step(event, index);
}

void tutorial_command(ENetEvent &event, const std::string_view text)
{
    ::peer &p = me(event);
    const int index = pget(p, "tut_step");

    std::string rows{};
    for (int i = 0; i < STEP_COUNT; ++i)
    {
        const step &s = STEPS[i];
        const int have = std::min(pget(p, std::string{ s.stat }), s.need);
        if (i < index)       rows += std::format("add_textbox|`2{}. {} - done!``|left|\n", i + 1, s.title);
        else if (i == index) rows += std::format("add_textbox|`9>> {}. {}`` `o({}/{})``|left|\nadd_smalltext|{}|left|\n", i + 1, s.title, have, s.need, s.hint);
        else                 rows += std::format("add_textbox|`s{}. {}``|left|\n", i + 1, s.title);
    }
    send_varlist(event.peer, { "OnDialogRequest", std::format(
        "set_default_color|`o\n"
        "add_label_with_icon|big|`wMikeTopia Tutorial``|left|242|\n"
        "add_textbox|{}|left|\n"
        "add_spacer|small|\n"
        "{}"
        "add_spacer|small|\n"
        "add_smalltext|Every step gives `w{}`` gems, finishing all of them gives `w{}`` more.|left|\n"
        "end_dialog|mt_tutorial||Close|\n",
        (index >= STEP_COUNT) ? "`2You finished the tutorial!`` Great job." : "Follow the steps to learn the basics and earn gems:",
        rows, STEP_GEMS, FINISH_GEMS) });
}
