#pragma once

/* @note Mentors & Apprentices (Social Portal > Show Apprentices), from ubisoft-mobile.helpshift.com (Growtopia > Mentorship Program)
 * and growtopiawiki.com/w/Guide:Mentorship:
 * a level 20+ player wrenches a level 1-2 player > Add Apprentice (1 per day, 50 active). an apprentice has one mentor,
 * may leave before level 20 (then waits 7 days for a new one), the mentor may remove them before level 30 and archive them after.
 * apprentices get an Apprentice Ticket at level 1/5/10/20/30; using it gives the mentor the Mentor Ticket of that level.
 * milestone rewards count the apprentices that reached level 20.
 * SevenPS: no Mentorship Certificate (GT doesn't sell it anymore), no invite links / recruits.
 * saved in the `mentorship` table, kept in memory while the server runs */

extern void load_mentorship(); // @note startup

/* @note wrench menu on another player: "Add Apprentice" when you may */
extern std::string mentor_wrench_lines(const ::peer &viewer, const ::peer &target);
extern bool mentor_wrench_button(ENetEvent &event, const ::hPipe &hPipe); // @note popup button mentor_add, true if handled

extern void send_apprentices(ENetEvent &event);                           // @note social portal "Show Apprentices"
extern void mentor_return(ENetEvent &event, const ::hPipe &hPipe);        // @note dialog_return "mt_mentor"
extern void mentor_invite_return(ENetEvent &event, const ::hPipe &hPipe); // @note dialog_return "mt_mentor_invite" (Accept / Decline)

/* @note peer::add_xp, after a level up */
extern void mentor_level_up(ENetEvent &event, int level);
/* @note login: tickets that are due (a full backpack, an offline mentor) */
extern void mentor_login(ENetEvent &event);

/* @note Apprentice / Mentor Ticket ids by level (1, 5, 10, 20, 30), -1 if `id` isn't one */
extern int apprentice_ticket_step(short id);
extern int mentor_ticket_step(short id);
/* @note an apprentice used their ticket of that step: their mentor gets the Mentor Ticket */
extern void apprentice_ticket_used(ENetEvent &event, int step);
