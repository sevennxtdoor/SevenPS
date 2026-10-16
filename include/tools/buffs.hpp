#pragma once

/* @note timed mods from consumables (resources/consumables.txt): Blueberry, Devil Horns, Freeze Wand, Lucky Clover...
 * a buff is kept in peer::profile as "buff:{name}" = "{ends}/{item id}", so it survives logging out and restarts.
 * its effects (skin, speed, looks, lucky...) are read from the item's line in consumables.txt while it lasts */

constexpr int BUFF_STATES = 0x40 | 0x80 | 0x800 | 0x4000 | 0x8000 | 0x10000 | 0x40000; // @note horns halo frozen stinky sparkle zombie shadow

struct buff_def
{
    short id{};
    std::string target{}; // @note self, player, other
    int seconds{};
    std::string name{}, added{}, removed{};
    int states{};
    bool has_skin{}; u_int skin{}; // @note in the game's order (b<<24 | g<<16 | r<<8 | a)
    float speed{}, gravity{};
    bool lucky{}, repel{}, curse{}, kill{}, burp{};
};

/* @return the buff this consumable gives, nullptr if it isn't in consumables.txt */
extern const buff_def *buff_for_item(short id);

/* @note gives `def` to `target` (or refreshes its time), with the "mod added" messages. `by`: who used the item */
extern void buff_add(ENetEvent &target, const buff_def &def, ENetEvent &by);

/* @note ends a buff right away (Antidote). @return true if it was active */
extern bool buff_remove(ENetEvent &event, const std::string &name);

extern bool has_buff(const ::peer &p, const std::string &name);

/* @note what the active buffs do right now */
extern int buff_states(const ::peer &p);
extern std::optional<u_int> buff_skin(const ::peer &p);
extern void buff_physics(const ::peer &p, float &speed, float &gravity);
extern bool buff_lucky(const ::peer &p);
extern bool buff_repel(const ::peer &p);

/* @note "Feelin' Blue (12 min), Lucky! (29 min)" for /status, empty if none */
extern std::string buff_names(const ::peer &p);

/* @note main loop: ends buffs whose time is up (once a second) */
extern void buff_tick();
