#include "pch.hpp"
#include <unordered_map>
#include "onVariant/ConsoleMessage.hpp"
#include "tools/random.hpp"
#include "tools/profile.hpp"
#include "tools/cps.hpp"

#include "surgery.hpp"

namespace
{
    enum tool : short {
        SPONGE = 1258, SCALPEL = 1260, ANESTHETIC = 1262, ANTISEPTIC = 1264, ANTIBIOTICS = 1266,
        SPLINT = 1268, STITCHES = 1270, PINS = 4308, TRANSFUSION = 4310, DEFIBRILLATOR = 4312,
        CLAMP = 4314, ULTRASOUND = 4316, LAB_KIT = 4318
    };
    constexpr short TOOLS[] { SPONGE, SCALPEL, ANESTHETIC, ANTISEPTIC, ANTIBIOTICS, SPLINT, STITCHES, PINS, TRANSFUSION, DEFIBRILLATOR, CLAMP, ULTRASOUND, LAB_KIT };
    constexpr short SURG_E = 4296, CADUCEUS = 4298, GLOVES = 1296/* "Fix It!" icon */, EMPTY_TRAY = 4320;
    constexpr int MALPRACTICE_SECONDS = 60 * 60;

    enum diagnosis : u_char { NOSE_JOB, BROKEN_ARM, SHATTERED_LEG, FLU, SWALLOWED_LOCKET, HEART_ATTACK };

    struct operation
    {
        std::string world{};
        ::pos pos{};
        bool started{}; // @note false while the "are you sure?" dialog is open

        diagnosis diag{};
        std::string flu{};      // @note "Monkey Flu", "Bird Flu", ...
        bool diagnosed{};
        bool fixed{};           // @note the actual problem is solved, only stitching up may be left
        u_char incisions{};
        u_char needed{};        // @note incisions needed before the fix
        u_char sleep{};         // @note turns left unconscious (anesthetic)
        bool sterile{};         // @note antiseptic used. cutting a dirty site infects it
        bool infected{};        // @note temperature keeps rising
        bool lab{};             // @note lab kit used, antibiotics now work properly
        u_char bleeding{};      // @note 0-3
        u_char dirt{};          // @note blood on the site, 3+ = hard to see (more skill fails)
        int blood{100};         // @note 0 = the heart stops
        u_char heart_stopped{}; // @note turns left to restart it, 0 = beating
        float temp{98.6f};
        std::string note{};     // @note what happened this turn
    };

    /* @note user id -> their surgery. only lives in memory, a restart ends every surgery */
    std::unordered_map<int, operation> operations{};
}

static short owned(const ::peer &p, short id)
{
    auto it = std::ranges::find(p.slots, id, &::slot::id);
    return (it == p.slots.end()) ? 0 : it->count;
}

static void sfx(ENetPeer &p, std::string_view file) { send_action(p, "play_sfx", std::format("file|{}\ndelayMS|0\n", file)); }

static bool needs_fix_it(const operation &op) { return op.diag == NOSE_JOB || op.diag == SWALLOWED_LOCKET || op.diag == HEART_ATTACK; }

static bool fix_ready(const operation &op)
{
    return needs_fix_it(op) && op.diagnosed && !op.fixed && op.incisions >= op.needed && op.heart_stopped == 0;
}

static bool skill_fail(const ::peer &p, const operation &op)
{
    const int chance = std::max(5, 25 - p.surgery_skill / 5) + (op.dirt >= 3 ? 20 : 0);
    return RandomRange(0, 99) < chance;
}

static void bleed(operation &op) { op.bleeding = std::min(op.bleeding + 1, 3); }

static std::string diagnosis_text(const operation &op)
{
    if (!op.diagnosed) return "`4The patient has not been diagnosed``";
    switch (op.diag)
    {
        case NOSE_JOB:         return "Patient wants a `2Nose Job``";
        case BROKEN_ARM:       return "Patient has a `4Broken Arm``";
        case SHATTERED_LEG:    return "Patient has a `4Shattered Leg``";
        case FLU:              return std::format("Patient has `4{}``", op.flu);
        case SWALLOWED_LOCKET: return "Patient `4Swallowed a Locket``";
        case HEART_ATTACK:     return "Patient is having a `4Heart Attack``";
    }
    return "";
}

/* @note a small hint once diagnosed, real growtopia players learn these by heart */
static std::string hint_text(const operation &op)
{
    if (!op.diagnosed) return "Use the `wUltrasound`` or `wLab Kit`` to find out what's wrong.";
    switch (op.diag)
    {
        case NOSE_JOB:         return "Anesthetic, 2 incisions, Fix It!, then stitch up.";
        case BROKEN_ARM:       return "Put a Splint on it.";
        case SHATTERED_LEG:    return "Anesthetic, 2 incisions, Pins, then stitch up.";
        case FLU:              return "Lab Kit, then Antibiotics until the fever is gone.";
        case SWALLOWED_LOCKET: return "Anesthetic, 3 incisions, Fix It!, then stitch up.";
        case HEART_ATTACK:     return "Restart the heart, Anesthetic, 2 incisions, Fix It!, then stitch up.";
    }
    return "";
}

static void show(ENetEvent &event, const operation &op)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    const std::string pulse =
        (op.heart_stopped) ? "`4None" :
        (op.blood > 70) ? "`2Strong" :
        (op.blood > 45) ? "`2Steady" :
        (op.blood > 20) ? "`3Weak" : "`4Extremely Weak";
    const std::string status =
        (op.heart_stopped) ? std::format("`4Heart Stopped! ({} turns left)", op.heart_stopped) :
        (op.sleep > 1) ? "`2Unconscious" :
        (op.sleep == 1) ? "`3Coming to" : "`4Awake";
    const std::string temp_color = (op.temp >= 106.0f) ? "`4" : (op.temp >= 101.0f) ? "`3" : "`2";
    const std::string site = op.infected ? "`4Infected" : op.sterile ? "`2Sanitized" : "`3Not sanitized";
    const std::string cut_color = (op.incisions > op.needed) ? "`4" : (op.incisions == op.needed) ? "`2" : "`3";

    std::string extra{};
    if (op.dirt >= 3) extra += "add_smalltext|`6It is becoming hard to see your work.``|left|\n";
    if (op.bleeding)  extra += std::format("add_smalltext|Patient is losing blood {}.``|left|\n",
        (op.bleeding == 1) ? "`3slowly" : (op.bleeding == 2) ? "`4quickly" : "`4very quickly");

    std::string tray{};
    bool any{};
    for (short id : TOOLS)
        if (const short have = owned(*pPeer, id); have > 0)
        {
            tray += std::format("add_button_with_icon|{}||noflags|{}|{}|\n", id_to_item(id).raw_name, id, have);
            any = true;
        }
    if (fix_ready(op)) tray += std::format("add_button_with_icon|Fix It!||noflags|{}||\n", GLOVES);
    else if (!any)     tray += std::format("add_button_with_icon|Empty Surgery Tray||noflags|{}||\n", EMPTY_TRAY);

    send_varlist(event.peer, { "OnDialogRequest", std::format(
        "set_default_color|`o\n"
        "add_label_with_icon|big|`wSurg-E``|left|{}|\n"
        "add_smalltext|{}.``|left|\n"
        "add_smalltext|Pulse: {}``    Status: {}``|left|\n"
        "add_smalltext|Temp: {}{:.1f}``    Operation site: {}``|left|\n"
        "add_smalltext|Incisions: {}{}``|left|\n"
        "{}"
        "add_smalltext|`5Tip:`` {}|left|\n"
        "add_spacer|small|\n"
        "add_smalltext|{}``|left|\n"
        "text_scaling_string|Defibrillator|\n"
        "{}"
        "add_button_with_icon||END_LIST|noflags|0||\n"
        "add_button|cancel|Give up!|noflags|0|0|\n"
        "end_dialog|surge|||\n",
        SURG_E, diagnosis_text(op), pulse, status, temp_color, op.temp, site, cut_color, op.incisions,
        extra, hint_text(op), op.note, tray) });
}

static void start(operation &op)
{
    op.started = true;
    op.diag = static_cast<diagnosis>(RandomRange(0, 5));
    switch (op.diag)
    {
        case NOSE_JOB:         op.needed = 2; op.diagnosed = true; break; // @note a cosmetic job, the patient tells you
        case BROKEN_ARM:       op.needed = 0; break;
        case SHATTERED_LEG:    op.needed = 2; break;
        case FLU:
        {
            constexpr std::array<std::string_view, 4> flus{ "Monkey Flu", "Bird Flu", "Turtle Flu", "Chicken Feet" };
            op.flu = flus[RandomRange(0, flus.size() - 1)];
            op.temp = 104.5f;
            break;
        }
        case SWALLOWED_LOCKET: op.needed = 3; break;
        case HEART_ATTACK:     op.needed = 2; op.heart_stopped = 4; op.blood = 30; break;
    }
    op.note = "`3The patient is prepped for surgery.";
}

/* @note one turn passes after every tool. @return false if the patient died */
static bool tick(operation &op)
{
    if (op.sleep > 0 && --op.sleep == 0 && op.incisions > 0)
    {
        bleed(op);
        op.note += " `4The patient woke up on the table!";
    }
    op.blood -= op.bleeding * 7;
    op.dirt = std::min(op.dirt + op.bleeding, 9);

    if (op.heart_stopped > 0)
    {
        if (--op.heart_stopped == 0) return false;
    }
    else if (op.blood <= 0)
    {
        op.blood = 0;
        op.heart_stopped = 3;
        op.note += " `4The patient's heart stopped!";
    }

    op.temp += ((op.diag == FLU && !op.fixed) ? 0.6f : 0.0f) + (op.infected ? 0.5f : 0.0f);
    return op.temp < 110.0f;
}

/* @note "The patient is awake!": cutting without anesthetic makes them thrash around */
static bool awake(operation &op)
{
    if (op.sleep > 0) return false;
    bleed(op);
    op.note = "`4The patient is awake! They thrash around and start bleeding.";
    return true;
}

static void use(::peer &p, operation &op, short tool)
{
    switch (tool)
    {
        case SPONGE:
            op.dirt = 0;
            op.note = "You cleaned up the blood.";
            break;
        case ANESTHETIC:
            if (op.sleep > 0)
            {
                op.blood -= 15;
                op.note = "`4Overdose! The patient's pulse drops.";
            }
            else op.note = "The patient is out cold.";
            op.sleep = 6;
            break;
        case ANTISEPTIC:
            op.sterile = true;
            op.note = op.infected ? "The site is clean, but the infection needs `wAntibiotics``." : "The operation site is now sanitized.";
            break;
        case SCALPEL:
            if (awake(op)) break;
            if (skill_fail(p, op))
            {
                bleed(op);
                op.note = "`3[`4Skill Fail`3] `6You slipped and cut the wrong place.";
                break;
            }
            ++op.incisions;
            if (!op.sterile) op.infected = true;
            if (RandomRange(0, 99) < 35) bleed(op);
            if (op.incisions > op.needed)
            {
                bleed(op);
                op.note = "`6That incision wasn't needed!";
            }
            else op.note = "You made an incision.";
            break;
        case STITCHES:
            if (op.incisions == 0) { op.note = "`6There's nothing to stitch."; break; }
            if (skill_fail(p, op)) { op.note = "`3[`4Skill Fail`3] `6You stitched your glove to the patient."; break; }
            --op.incisions;
            if (op.bleeding) --op.bleeding;
            op.note = "You closed an incision.";
            break;
        case CLAMP:
            if (op.bleeding == 0) { op.note = "`6Nothing is bleeding."; break; }
            --op.bleeding;
            op.note = "You clamped a blood vessel.";
            break;
        case TRANSFUSION:
            op.blood = std::min(op.blood + 40, 100);
            op.note = op.heart_stopped ? "`6New blood, but the heart isn't pumping it!" : "The patient's pulse gets stronger.";
            break;
        case DEFIBRILLATOR:
            if (op.heart_stopped == 0)
            {
                op.blood -= 25;
                op.note = "`4You shocked a patient whose heart was beating fine!";
                break;
            }
            if (skill_fail(p, op)) { op.note = "`3[`4Skill Fail`3] `6You shocked yourself!"; break; }
            op.heart_stopped = 0;
            op.blood = std::max(op.blood, 35);
            if (op.diag == HEART_ATTACK) op.diagnosed = true;
            op.note = "`2The heart is beating again!";
            break;
        case ULTRASOUND:
            if (skill_fail(p, op)) { op.note = "`3[`4Skill Fail`3] `6You scanned the nurse with your ultrasound!"; break; }
            if (op.diag == FLU) { op.note = "The ultrasound shows nothing broken... try a lab test."; break; }
            op.diagnosed = true;
            op.note = "The ultrasound shows what's wrong.";
            break;
        case LAB_KIT:
            if (skill_fail(p, op)) { op.note = "`3[`4Skill Fail`3] `6You contaminated the sample."; break; }
            op.lab = true;
            if (op.diag == FLU) op.diagnosed = true;
            op.note = (op.diag == FLU || op.infected) ? "Lab results: `4bacteria found``. Antibiotics will work now." : "Lab results: nothing unusual.";
            break;
        case ANTIBIOTICS:
            if (skill_fail(p, op))
            {
                op.temp += 0.5f;
                op.note = "`3[`4Skill Fail`3] `6Wrong medication! The bacteria like it.";
                break;
            }
            if (op.lab)
            {
                op.temp = std::max(op.temp - 2.5f, 98.6f);
                op.infected = false;
                op.note = "The fever is going down.";
            }
            else
            {
                op.temp = std::max(op.temp - 0.6f, 98.6f);
                op.note = "Without a lab test you're guessing... it helps a little.";
            }
            if (op.diag == FLU && op.lab && op.temp <= 99.5f)
            {
                op.fixed = op.diagnosed = true;
                op.note = std::format("`2The {} is cured!", op.flu);
            }
            break;
        case SPLINT:
            if (op.diag != BROKEN_ARM || !op.diagnosed) { op.note = "`6You splint a random limb. It doesn't help."; break; }
            if (skill_fail(p, op)) { op.note = "`3[`4Skill Fail`3] `6You put the splint on backwards."; break; }
            op.fixed = true;
            op.note = "`2The arm is set in a splint!";
            break;
        case PINS:
            if (op.diag != SHATTERED_LEG || !op.diagnosed) { op.note = "`6There are no bones to pin."; break; }
            if (op.fixed) { op.note = "`6The bones are already pinned."; break; }
            if (op.incisions < op.needed) { op.note = std::format("`6You need to cut in first ({} incisions).", op.needed); break; }
            if (awake(op)) break;
            if (skill_fail(p, op)) { op.note = "`3[`4Skill Fail`3] `6You dropped the pins inside the patient!"; break; }
            op.fixed = true;
            op.note = "`2The bones are pinned together! Now stitch up.";
            break;
        case GLOVES: // @note "Fix It!"
            if (awake(op)) break;
            if (skill_fail(p, op)) { op.note = "`3[`4Skill Fail`3] `6You screwed it up! Try again."; break; }
            op.fixed = true;
            op.note =
                (op.diag == NOSE_JOB) ? "`2The new nose looks great! Now stitch up." :
                (op.diag == SWALLOWED_LOCKET) ? "`2You pulled out the locket! Now stitch up." : "`2You repaired the heart! Now stitch up.";
            break;
    }
    op.blood = std::max(op.blood, 0);
}

/* @note give or drop it next to the Surg-E if it doesn't fit */
static void give(ENetEvent &event, ::world &world, const ::pos &pos, ::slot slot)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const short have = owned(*pPeer, slot.id);
    const bool room = (have > 0) || (int)pPeer->slots.size() < pPeer->slot_size;
    if (room && have + slot.count <= 200) modify_item_inventory(event, slot);
    else add_drop(event, slot, pos.by_32(), world);
}

static ::slot roll_reward()
{
    if (!RandomRange(0, 49)) // @note rare
    {
        constexpr short rare[]{ 1506/*Medical Scarf*/, 4324/*Biotronic Brain Enhancer*/, 4326/*Plague Doctor Mask*/, 4328/*Plague Doctor Hat*/, 4330/*Plague Doctor Robe*/ };
        return { rare[RandomRange(0, std::size(rare) - 1)], 1 };
    }
    if (!RandomRange(0, 9)) // @note uncommon clothes
    {
        constexpr short uncommon[]{ 1242/*Scrub Cap*/, 1244/*Scrub Mask*/, 1246/*Scrub Top*/, 1248/*Scrub Pants*/, 1282/*Candy Striper Cap*/,
            1284/*Candy Striper Uniform*/, 1286/*Candy Striper Skirt*/, 3172/*Legal Briefs*/, 1252/*Magnifying Glass*/, 4334/*Spare Body Part*/ };
        return { uncommon[RandomRange(0, std::size(uncommon) - 1)], 1 };
    }
    if (!RandomRange(0, 2)) // @note hospital blocks
    {
        static const ::slot blocks[]{ {1256/*Hospital Bed*/, 1}, {1288/*Hospital Curtain*/, 5}, {1290/*Hospital Wall*/, 10},
            {1292/*Hospital Window*/, 5}, {1294/*Hospital Door*/, 1}, {4322/*Autoclave*/, 1}, {SURG_E, 1} };
        return blocks[RandomRange(0, std::size(blocks) - 1)];
    }
    return { TOOLS[RandomRange(0, std::size(TOOLS) - 1)], 5 }; // @note more supplies
}

/* @note destroy the Surg-E, reward or punish, and forget the surgery */
static void finish(ENetEvent &event, ::world &world, int user_id, bool success)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const ::pos pos = operations[user_id].pos;
    operations.erase(user_id);

    ::block &block = world.blocks[cord(pos.x_int(), pos.y_int())];
    if (block.fg == SURG_E)
    {
        block.fg = 0, block.hits[0] = 0, block.state[2] = 0x00;
        ::gamePacket gamePacket{};
        gamePacket.punch = pos;
        send_tile_update(event, gamePacket, block, world);
    }

    std::string broadcast{};
    if (success)
    {
        pPeer->surgery_skill = std::min(pPeer->surgery_skill + 1, 100);
        add_stat(event, "surgeries");
        ::slot reward = roll_reward();
        if (event_on(event_type::SURGERY)) reward.count = std::min<short>(reward.count * 2, 200); // @note Surgery Day
        give(event, world, pos, reward);
        give(event, world, pos, ::slot{ CADUCEUS, 1 });

        sfx(*event.peer, "audio/double_chance.wav");
        on::ConsoleMessage(event.peer, std::format("`2Success!`` You saved the patient! (`wSurgeon Skill: {}``)", pPeer->surgery_skill));
        on::ConsoleMessage(event.peer, std::format("`oYou got `w{}`` `2{}`` and a `3Caduceus``!", reward.count, id_to_item(reward.id).raw_name));
        send_varlist(event.peer, { "OnTalkBubble", pPeer->netid,
            std::format("`wAfter a surgery like that, you decide you deserve `w{} {}``.", reward.count, id_to_item(reward.id).raw_name) });
        broadcast = std::format("`7[``{} `ohas successfully operated on Surg-E!`7]``", pPeer->display_growid);
    }
    else
    {
        pPeer->malpractice = static_cast<int>(std::time(nullptr)) + (event_on(event_type::SURGERY) ? MALPRACTICE_SECONDS / 4 : MALPRACTICE_SECONDS);
        on::ConsoleMessage(event.peer, "`4You couldn't save them!``");
        on::ConsoleMessage(event.peer, std::format("You are not allowed to perform surgery for a while! (`$Malpractice`` mod added, `${}`` left)", event_on(event_type::SURGERY) ? "15 minutes" : "1 hour"));
        broadcast = std::format("`7[``{} `4lost the patient!`7]``", pPeer->display_growid);
    }
    peers(world.name, PEER_SAME_WORLD, [&broadcast](ENetPeer &p)
    {
        sfx(p, "audio/metal_destroy.wav");
        on::ConsoleMessage(&p, broadcast);
    });
}

bool surgery_in_use(const ::world &world, const ::pos &pos)
{
    return std::ranges::any_of(operations, [&](const auto &it) {
        return it.second.started && it.second.world == world.name && it.second.pos == pos;
    });
}

void send_surgery_dialog(ENetEvent &event, ::world &world, const ::pos &pos)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    if (auto it = operations.find(pPeer->user_id); it != operations.end() && it->second.started)
    {
        if (it->second.world == world.name && it->second.pos == pos) return show(event, it->second); // @note closed the dialog, carry on
        send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, "You're already operating on another Surg-E!" });
        return;
    }
    if (surgery_in_use(world, pos))
    {
        send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, "Someone is already operating on this Surg-E!" });
        return;
    }
    if (const int left = pPeer->malpractice - static_cast<int>(std::time(nullptr)); left > 0)
    {
        send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, "I know it's just a robot, but the authorities don't even trust you operating on that with your malpractice issues." });
        on::ConsoleMessage(event.peer, std::format("`oYou have `$Malpractice``, `w{}`` minutes left.``", (left + 59) / 60));
        return;
    }
    operations[pPeer->user_id] = operation{ .world = world.name, .pos = pos };

    std::string low{};
    for (short id : TOOLS)
        if (const short have = owned(*pPeer, id); have < 5)
            low += std::format("{}`4{}`` {}", low.empty() ? "" : ", ", have, id_to_item(id).raw_name);

    send_varlist(event.peer, { "OnDialogRequest", std::format(
        "set_default_color|`o\n"
        "add_label_with_icon|big|`9Surg-E Anatomical Dummy``|left|{}|\n"
        "add_spacer|small|\n"
        "add_smalltext|Surgeon Skill: {}|left|\n"
        "add_textbox|Are you sure you want to perform surgery on this robot? Whether you succeed or fail, the robot will be destroyed in the process.|left|\n"
        "{}"
        "end_dialog|surge|Cancel|Okay!|\n",
        SURG_E, pPeer->surgery_skill, low.empty() ? "" : std::format("add_smalltext|`9Low Supply Warning: ``{}|left|\n", low)) });
}

void surgery_return(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    auto it = operations.find(pPeer->user_id);
    if (it == operations.end()) return;
    operation &op = it->second;

    auto world = std::ranges::find(worlds, pPeer->recent_worlds.back(), &::world::name);
    if (pPeer->netid == 0 || world == worlds.end() || op.world != world->name ||
        world->blocks[cord(op.pos.x_int(), op.pos.y_int())].fg != SURG_E)
    {
        operations.erase(it); // @note the Surg-E is gone or we're elsewhere
        return;
    }

    const std::string clicked = hPipe["buttonClicked"];
    if (!op.started) // @note pressed "Okay!" (Cancel never reaches the server)
    {
        start(op);
        const std::string message = std::format("`7[``{} `ois performing surgery on Surg-E!`7]``", pPeer->display_growid);
        peers(world->name, PEER_SAME_WORLD, [&](ENetPeer &p)
        {
            on::ConsoleMessage(&p, message);
            send_varlist(&p, { "OnTalkBubble", pPeer->netid, message });
        });
        return show(event, op);
    }
    if (clicked == "cancel") return finish(event, *world, pPeer->user_id, false); // @note "Give up!"

    short tool{};
    if (clicked == "Fix It!") { if (!fix_ready(op)) return show(event, op); tool = GLOVES; }
    else
    {
        for (short id : TOOLS)
            if (clicked == id_to_item(id).raw_name) { tool = id; break; }
        if (tool == 0 || owned(*pPeer, tool) <= 0) return show(event, op); // @note empty tray / used up
        modify_item_inventory(event, ::slot{ tool, -1 });
    }

    use(*pPeer, op, tool);
    if (!tick(op)) return finish(event, *world, pPeer->user_id, false);
    if (op.fixed && op.incisions == 0 && op.heart_stopped == 0) return finish(event, *world, pPeer->user_id, true);
    show(event, op);
}

void abandon_surgery(ENetEvent &event)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    auto it = operations.find(pPeer->user_id);
    if (it == operations.end()) return;
    if (!it->second.started) { operations.erase(it); return; }

    auto world = std::ranges::find(worlds, it->second.world, &::world::name);
    if (world == worlds.end()) { operations.erase(it); return; }
    finish(event, *world, pPeer->user_id, false);
}
