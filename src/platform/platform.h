#ifndef PLATFORM_PLATFORM_H
#define PLATFORM_PLATFORM_H

/* Platform abstraction (GAME.md §1 Platform Abstraction).
 * The only module allowed to include SDL. It provides exactly:
 * a window presenting a 320x240 indexed framebuffer, keyboard and relative
 * mouse events, an audio callback, whole-file I/O and a pacing clock.
 * Nothing here is used by gameplay logic except through input events. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Platform-independent key codes. Printable keys use their lowercase ASCII
 * value ('a'..'z', '0'..'9', ' ', ...); the rest are listed here. */
enum {
    PKEY_NONE = 0,
    PKEY_TAB = 9,
    PKEY_ENTER = 13,
    PKEY_ESCAPE = 27,
    PKEY_SPACE = 32,
    PKEY_BACKSPACE = 127,
    PKEY_UP = 256,
    PKEY_DOWN,
    PKEY_LEFT,
    PKEY_RIGHT,
    PKEY_LSHIFT,
    PKEY_RSHIFT,
    PKEY_LCTRL,
    PKEY_RCTRL,
    PKEY_LALT,
    PKEY_RALT,
    PKEY_F1,
    PKEY_F2,
    PKEY_F3,
    PKEY_F4,
    PKEY_F5,
    PKEY_F6,
    PKEY_F7,
    PKEY_F8,
    PKEY_F9,
    PKEY_F10,
    PKEY_F11,
    PKEY_F12,
    PKEY_COUNT
};

/* Mouse buttons. */
enum { PMOUSE_LEFT = 1, PMOUSE_MIDDLE = 2, PMOUSE_RIGHT = 3 };

typedef enum {
    PEV_NONE = 0,
    PEV_QUIT,       /* window closed */
    PEV_KEY_DOWN,   /* key: PKEY_* or ASCII; repeat set for auto-repeat */
    PEV_KEY_UP,
    PEV_MOUSE_MOVE, /* dx, dy: relative motion in pixels */
    PEV_MOUSE_DOWN, /* button: PMOUSE_* */
    PEV_MOUSE_UP
} plat_event_type;

typedef struct {
    plat_event_type type;
    int key;
    bool repeat;
    int dx, dy;
    int button;
} plat_event;

/* Audio: the platform pulls `frames` interleaved stereo int16 frames
 * (L,R,L,R,...) at PLAT_AUDIO_RATE from the engine. Runs on the audio thread. */
enum { PLAT_AUDIO_RATE = 22050, PLAT_AUDIO_CHANNELS = 2 };
typedef void (*plat_audio_fn)(int16_t *out, int frames, void *user);

/* Opens the window (title, initial integer scale of 320x240). false on error,
 * message in platform_error(). */
bool platform_init(const char *title, int scale);
void platform_shutdown(void);
const char *platform_error(void);

/* Presents a 320x240 framebuffer of palette indices through a 768-byte RGB
 * palette, at the largest integer scale that fits the window, nearest
 * neighbour, centred with black borders. */
void platform_present(const uint8_t *fb, const uint8_t *palette);

/* Pops the next pending event; false when the queue is empty. */
bool platform_poll_event(plat_event *ev);

/* Relative mouse mode (pointer hidden and captured). */
void platform_set_relative_mouse(bool on);

/* Starts audio output calling fn(out, frames, user). false on error. */
bool platform_audio_open(plat_audio_fn fn, void *user);
void platform_audio_close(void);

/* Whole-file I/O. read returns a malloc'd buffer (free() it) with a trailing
 * NUL not counted in *size, or NULL on error. write returns false on error. */
void *platform_read_file(const char *path, size_t *size);
bool platform_write_file(const char *path, const void *data, size_t size);

/* Milliseconds since an arbitrary start. For frame pacing only, never for
 * gameplay (tech.md Simulation: gameplay runs on tics). */
uint64_t platform_ticks_ms(void);
void platform_sleep_ms(uint32_t ms);

#endif
