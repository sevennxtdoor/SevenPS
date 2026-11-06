#pragma once

/* @note SevenPS rude-name filter (resources/bad_words.txt): world names, GrowIDs and guild names.
 * the list is read again when the file changes (checked at most once a minute), so it can be edited while the server runs.
 * main thread only (the web login asks through on_main) */

/* @return true if this name (letters and numbers) contains a word from resources/bad_words.txt.
 * case doesn't matter, 0 1 3 4 5 7 count as o i e a s t, and repeated letters don't hide a word (PEEPEK, P3P3K) */
extern bool is_bad_name(std::string_view name);

/* @return the text with every word from the list turned into '*' (chat, signs, door names, notes...).
 * "=word" lines only hit a whole word here (TAI, not MOUNTAIN). color codes (`4) are kept and can't split a word (kon`4tol) */
extern std::string censor(std::string text);
