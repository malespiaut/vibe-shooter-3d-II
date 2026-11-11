#ifndef GAME_JUKEBOX_H
#define GAME_JUKEBOX_H

/* Music in game (docs/design/audio.md, "Music"). Picks the track for a level
 * and plays tracks by name: loads assets/src/music/<name>.vmus on first use
 * (kept for the run), starts it on the sequencer (engine/music.h) when audio
 * is on, and writes `<tic> music <name>` (`music -` on stop) to the
 * --sound-log file on every call. A missing or broken track prints
 * `music: <name> missing` once on stderr and plays silence: the composition
 * issues own the content. Presentation only: never touches game state or
 * game_hash. Game thread only. */

#include <stdint.h>
#include <stdio.h>

/* Track for level 1..8 ("level_eg", ..., "boss"); NULL outside. */
const char *music_track_for_level(int level);

/* dir: where <name>.vmus lives. audio: drive the sequencer (window mode,
 * after music_attach). log: --sound-log file or NULL. */
void jukebox_init(const char *dir, int audio, FILE *log);

/* (Re)starts track `name` from its beginning; `tic` is for the log line.
 * 1 = playing, 0 = silent (missing, broken or invalid name). */
int jukebox_play(const char *name, uint32_t tic);
void jukebox_stop(uint32_t tic);

/* Name last asked for (also when missing), NULL when stopped. */
const char *jukebox_current(void);
/* `music: <name> missing` messages printed so far. */
int jukebox_missing_reports(void);

/* Stops the music and frees every loaded track. Call after the audio
 * device is closed. */
void jukebox_shutdown(void);

#endif
