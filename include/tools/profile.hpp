#pragma once

/* @note SevenPS's profile systems behind the wrench menu buttons:
 * stats & achievements, daily goals, grow pass (daily bonus), marvelous missions, personalize profile,
 * wardrobe, card battle license, battle pets, wrench customization and growmojis.
 * everything is kept in peer::profile (key=value;...) and saved in the `profile` column */

/* @note peer::profile access. strings are cleaned of the separators ; = , | and new lines */
extern int  pget(const ::peer &p, const std::string &key, int fallback = 0);
extern void pset(::peer &p, const std::string &key, int value);
extern std::string pget_str(const ::peer &p, const std::string &key);
extern void pset_str(::peer &p, const std::string &key, const std::string &value);
extern std::vector<std::string> pget_list(const ::peer &p, const std::string &key); // @note comma separated
extern void pset_list(::peer &p, const std::string &key, const std::vector<std::string> &list);

extern std::string profile_serialize(const ::peer &p);
extern void profile_load(::peer &p, const std::string &data);

/* @note count something the player did ("broken", "placed", "harvested", "surgeries", "trades", "fires", "goals")
 * and announce achievements that got unlocked */
extern void add_stat(ENetEvent &event, const std::string &key, int amount = 1);
extern int achievements_unlocked(const ::peer &p);
extern int achievements_total();

/* @note wrench menu (yourself) buttons handled here. @return true if handled */
extern bool profile_button(ENetEvent &event, const std::string &clicked);
extern void profile_return(ENetEvent &event, const ::hPipe &hPipe); // @note dialog_return "mt_profile"

/* @note what other players see when they wrench this player (bio, pet, license, chosen info) */
extern std::string profile_public_lines(const ::peer &p);
