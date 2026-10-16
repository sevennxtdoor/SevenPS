#pragma once

/* @note every few minutes: saves every online player and every loaded world. before, they were only saved on leaving
 * and on a clean shutdown (Ctrl+C), so a crash or closing the window lost everything since the players logged in */
extern void autosave_tick();

/* @note saves everything right now (autosave, closing the window) */
extern void save_everything();
