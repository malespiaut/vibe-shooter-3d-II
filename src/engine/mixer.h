#ifndef ENGINE_MIXER_H
#define ENGINE_MIXER_H

/* Integer software mixer (docs/design/audio.md, "Mixer").
 *
 * Output: 22050 Hz, stereo interleaved signed 16-bit. 16 sound-effect voices
 * are summed into 32-bit accumulators, then clipped to int16. Four further
 * music channels are reserved: the sequencer (follow-up issue) renders into
 * the same accumulator through mixer_set_music().
 *
 * Gain law (exact, integer):
 *   out = (sample << 8) * vol * pan_gain / (64 * 64)     per voice, per channel
 * with vol in 0..MIXER_VOL_MAX (64) and pan_gain from the pan step:
 *   left  gain = 64 for pan <= 0, else 64 * (4 - pan) / 4
 *   right gain = 64 for pan >= 0, else 64 * (4 + pan) / 4
 * So a centred voice at full volume reproduces `sample << 8` exactly in both
 * channels, and hard left (pan -4) is silent on the right.
 *
 * Threads: the game thread calls mixer_start / mixer_update /
 * mixer_stop_source, which only post commands into a lock-free single-producer
 * single-consumer ring. The audio thread drains the ring at the start of
 * mixer_render. The audio thread never allocates. Sounds are loaded on the
 * game thread before use and stay alive until mixer_shutdown.
 *
 * Audio never changes game state (determinism). */

#include <stdbool.h>
#include <stdint.h>

#include "bast.h"
#include "engine/angle.h"
#include "engine/fixed.h"

#define MIXER_RATE 22050
#define MIXER_VOICES 16
#define MIXER_MUSIC_CHANNELS 4 /* reserved for the sequencer */
#define MIXER_VOL_MAX 64
#define MIXER_PAN_LEFT (-4)  /* hard left */
#define MIXER_PAN_CENTER 0
#define MIXER_PAN_RIGHT 4    /* hard right; -4..4 = 8 steps */
#define MIXER_MAX_SOUNDS 128
#define MIXER_SOURCE_NONE 0  /* anonymous source: never cuts another voice */

typedef struct mixer_sound mixer_sound;

/* Resets all voices and the command ring and sets the directory holding
 * `sfx/<name>.snd` (normally assets/gen). Frees sounds from a previous init. */
void mixer_init(const char *assets_dir);
/* Frees every loaded sound. Call after the audio device is closed. */
void mixer_shutdown(void);

/* Loads <assets_dir>/sfx/<name>.snd through libbast. Cached by name. NULL on
 * error (message in bast_error()). Game thread only. */
mixer_sound *mixer_load_snd(const char *name);
/* Registers an already loaded sound under `name`; the mixer takes ownership
 * of `snd` (also on failure). Used by mixer_load_snd and the tests. */
mixer_sound *mixer_add_sound(const char *name, bast_snd *snd);
uint32_t mixer_sound_length(const mixer_sound *s); /* in samples */

/* Game thread. vol 0..64, pan -4..4 (clamped). source_id identifies the
 * emitter (actor, door, ...): restarting a source cuts its previous sound;
 * MIXER_SOURCE_NONE never does. Returns false if the command ring is full. */
bool mixer_start(mixer_sound *sound, int source_id, int vol, int pan);
bool mixer_update(int source_id, int vol, int pan);
bool mixer_stop_source(int source_id);

/* Audio thread: renders `frames` stereo frames (L,R,L,R...) into out. */
void mixer_render(int16_t *out, int frames);

/* Music hook: fn adds `frames` stereo frames into acc (L,R,... int32) at the
 * same scale as the voices (int16 range per channel). Set before the audio
 * device opens, or NULL to disable. */
typedef void (*mixer_music_fn)(int32_t *acc, int frames, void *user);
void mixer_set_music(mixer_music_fn fn, void *user);

/* Spatialisation helpers, integer only. */
/* 64-entry table, 0.5 u per entry: 64 within 2 u, 0 at 24 u and beyond. */
int mixer_dist_volume(fix_t dist);
/* Lateral pan step -4 (hard left) .. 4 (hard right) of a source seen from a
 * view angle: round(-4 * sin(to_source - view)). Front and back are centred. */
int mixer_pan(angle_t view, angle_t to_source);

/* Inspection for tests: state of voice i as of the last mixer_render.
 * Not synchronised with the audio thread. */
typedef struct {
    bool active;
    int source_id;
    int vol, pan;
    uint32_t pos;
    uint32_t serial; /* start order: lower = older */
    const mixer_sound *sound;
} mixer_voice_info;
void mixer_get_voice(int i, mixer_voice_info *out);

#endif
