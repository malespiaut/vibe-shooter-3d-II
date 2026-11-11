/* SDL3 implementation of platform.h. The only file in the tree that
 * includes SDL. Conversion from palette indices to truecolor pixels happens
 * here and nowhere else. */
#include "platform/platform.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FB_W 320
#define FB_H 240

static SDL_Window *g_window;
static SDL_Surface *g_stage; /* XRGB8888, FB_W*scale x FB_H*scale */
static int g_stage_scale;
static float g_mouse_fx, g_mouse_fy; /* sub-pixel remainder of relative motion */

static SDL_AudioStream *g_audio;
static plat_audio_fn g_audio_fn;
static void *g_audio_user;

static char g_error[256];

static void set_error(const char *what)
{
    snprintf(g_error, sizeof g_error, "%s: %s", what, SDL_GetError());
}

const char *platform_error(void)
{
    return g_error;
}

bool platform_init(const char *title, int scale)
{
    if (scale < 1)
        scale = 1;
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        set_error("SDL_Init");
        return false;
    }
    g_window = SDL_CreateWindow(title, FB_W * scale, FB_H * scale, SDL_WINDOW_RESIZABLE);
    if (!g_window) {
        set_error("SDL_CreateWindow");
        SDL_Quit();
        return false;
    }
    return true;
}

void platform_shutdown(void)
{
    platform_audio_close();
    if (g_stage) {
        SDL_DestroySurface(g_stage);
        g_stage = NULL;
        g_stage_scale = 0;
    }
    if (g_window) {
        SDL_DestroyWindow(g_window);
        g_window = NULL;
    }
    SDL_Quit();
}

void platform_present(const uint8_t *fb, const uint8_t *palette)
{
    SDL_Surface *win = SDL_GetWindowSurface(g_window);
    if (!win)
        return;

    int scale = win->w / FB_W;
    if (win->h / FB_H < scale)
        scale = win->h / FB_H;
    if (scale < 1)
        scale = 1;

    if (scale != g_stage_scale) {
        if (g_stage)
            SDL_DestroySurface(g_stage);
        g_stage = SDL_CreateSurface(FB_W * scale, FB_H * scale, SDL_PIXELFORMAT_XRGB8888);
        g_stage_scale = g_stage ? scale : 0;
        if (!g_stage)
            return;
    }

    uint32_t lut[256];
    for (int i = 0; i < 256; i++)
        lut[i] = 0xFF000000u | ((uint32_t)palette[i * 3] << 16) |
                 ((uint32_t)palette[i * 3 + 1] << 8) | (uint32_t)palette[i * 3 + 2];

    /* Integer nearest-neighbour upscale: each source pixel becomes a
     * scale x scale block, so pixels stay crisp. */
    if (!SDL_LockSurface(g_stage))
        return;
    for (int y = 0; y < FB_H; y++) {
        uint32_t *row = (uint32_t *)((uint8_t *)g_stage->pixels + (size_t)y * scale * g_stage->pitch);
        const uint8_t *src = fb + y * FB_W;
        uint32_t *d = row;
        for (int x = 0; x < FB_W; x++) {
            uint32_t c = lut[src[x]];
            for (int s = 0; s < scale; s++)
                *d++ = c;
        }
        for (int s = 1; s < scale; s++)
            memcpy((uint8_t *)row + (size_t)s * g_stage->pitch, row, (size_t)FB_W * scale * 4);
    }
    SDL_UnlockSurface(g_stage);

    /* Letterbox: black borders, image centred. */
    SDL_FillSurfaceRect(win, NULL, SDL_MapSurfaceRGB(win, 0, 0, 0));
    SDL_Rect dst = {(win->w - g_stage->w) / 2, (win->h - g_stage->h) / 2, g_stage->w, g_stage->h};
    SDL_BlitSurface(g_stage, NULL, win, &dst);
    SDL_UpdateWindowSurface(g_window);
}

static int map_key(SDL_Keycode k)
{
    switch (k) {
    case SDLK_ESCAPE: return PKEY_ESCAPE;
    case SDLK_RETURN: return PKEY_ENTER;
    case SDLK_KP_ENTER: return PKEY_ENTER;
    case SDLK_TAB: return PKEY_TAB;
    case SDLK_BACKSPACE: return PKEY_BACKSPACE;
    case SDLK_UP: return PKEY_UP;
    case SDLK_DOWN: return PKEY_DOWN;
    case SDLK_LEFT: return PKEY_LEFT;
    case SDLK_RIGHT: return PKEY_RIGHT;
    case SDLK_LSHIFT: return PKEY_LSHIFT;
    case SDLK_RSHIFT: return PKEY_RSHIFT;
    case SDLK_LCTRL: return PKEY_LCTRL;
    case SDLK_RCTRL: return PKEY_RCTRL;
    case SDLK_LALT: return PKEY_LALT;
    case SDLK_RALT: return PKEY_RALT;
    default: break;
    }
    if (k >= SDLK_F1 && k <= SDLK_F12)
        return PKEY_F1 + (int)(k - SDLK_F1);
    if (k >= 32 && k < 127) /* SDL3 keycodes for printable keys are ASCII */
        return (int)k;
    return PKEY_NONE;
}

bool platform_poll_event(plat_event *ev)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        memset(ev, 0, sizeof *ev);
        switch (e.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            ev->type = PEV_QUIT;
            return true;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            ev->key = map_key(e.key.key);
            if (ev->key == PKEY_NONE)
                continue;
            ev->type = e.type == SDL_EVENT_KEY_DOWN ? PEV_KEY_DOWN : PEV_KEY_UP;
            ev->repeat = e.key.repeat;
            return true;
        case SDL_EVENT_MOUSE_MOTION: {
            /* Floats stay inside the platform layer; the engine sees ints. */
            g_mouse_fx += e.motion.xrel;
            g_mouse_fy += e.motion.yrel;
            int dx = (int)g_mouse_fx, dy = (int)g_mouse_fy;
            g_mouse_fx -= (float)dx;
            g_mouse_fy -= (float)dy;
            if (dx == 0 && dy == 0)
                continue;
            ev->type = PEV_MOUSE_MOVE;
            ev->dx = dx;
            ev->dy = dy;
            return true;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (e.button.button == SDL_BUTTON_LEFT)
                ev->button = PMOUSE_LEFT;
            else if (e.button.button == SDL_BUTTON_MIDDLE)
                ev->button = PMOUSE_MIDDLE;
            else if (e.button.button == SDL_BUTTON_RIGHT)
                ev->button = PMOUSE_RIGHT;
            else
                continue;
            ev->type = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? PEV_MOUSE_DOWN : PEV_MOUSE_UP;
            return true;
        default:
            continue;
        }
    }
    return false;
}

void platform_set_relative_mouse(bool on)
{
    if (g_window)
        SDL_SetWindowRelativeMouseMode(g_window, on);
}

static void SDLCALL audio_cb(void *user, SDL_AudioStream *stream, int additional, int total)
{
    (void)user;
    (void)total;
    int16_t buf[1024 * PLAT_AUDIO_CHANNELS];
    int frames_left = additional / (int)(sizeof(int16_t) * PLAT_AUDIO_CHANNELS);
    while (frames_left > 0) {
        int n = frames_left < 1024 ? frames_left : 1024;
        g_audio_fn(buf, n, g_audio_user);
        SDL_PutAudioStreamData(stream, buf, n * (int)(sizeof(int16_t) * PLAT_AUDIO_CHANNELS));
        frames_left -= n;
    }
}

bool platform_audio_open(plat_audio_fn fn, void *user)
{
    if (g_audio)
        return true;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        set_error("SDL_InitSubSystem(audio)");
        return false;
    }
    g_audio_fn = fn;
    g_audio_user = user;
    SDL_AudioSpec spec = {SDL_AUDIO_S16, PLAT_AUDIO_CHANNELS, PLAT_AUDIO_RATE};
    g_audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audio_cb, NULL);
    if (!g_audio) {
        set_error("SDL_OpenAudioDeviceStream");
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }
    if (!SDL_ResumeAudioStreamDevice(g_audio)) {
        set_error("SDL_ResumeAudioStreamDevice");
        platform_audio_close();
        return false;
    }
    return true;
}

void platform_audio_close(void)
{
    if (!g_audio)
        return;
    SDL_DestroyAudioStream(g_audio);
    g_audio = NULL;
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void *platform_read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long len = ftell(f);
    if (len < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    char *buf = malloc((size_t)len + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    buf[len] = '\0';
    if (size)
        *size = (size_t)len;
    return buf;
}

bool platform_write_file(const char *path, const void *data, size_t size)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;
    bool ok = fwrite(data, 1, size, f) == size;
    if (fclose(f) != 0)
        ok = false;
    return ok;
}

bool platform_make_dir(const char *path)
{
    return SDL_CreateDirectory(path);
}

uint64_t platform_ticks_ms(void)
{
    return SDL_GetTicks();
}

void platform_sleep_ms(uint32_t ms)
{
    SDL_Delay(ms);
}
