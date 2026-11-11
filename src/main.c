/* Vibe Shooter 3D II entry point. M1: runs the 35 Hz simulation on a map and
 * shows it in the 3D view (engine/render.h), or the dev top-down view with
 * --topdown.
 *
 *   game [--map <file.vmap>] [--warp N] [--skill N] [--god] [--topdown]
 *        [--demo <file.demo>] [--record <file.demo>] [--max-tics N]
 *        [--hash-log <file>] [--sound-log <file>]
 *   game --headless --demo <file.demo> [--frames-out <dir>] [--frame-every N] ...
 *
 * Flags and exit codes: docs/design/roadmap.md (Verification tooling
 * contract). With no --warp/--map/--record (and in demos with `start
 * title`) the run begins on the title screen and goes through the front-end
 * flow (game/flow.h, hud.md Flow); otherwise it starts straight in play.
 * Window mode: WASD / arrows move (left/right arrows turn), Shift runs, mouse
 * X turns, left click / Ctrl fires, E / Space uses, 1-7 select weapons, Esc
 * opens the pause menu; window close quits. The last stdout line of every
 * run is the STATS line.
 *
 * Sound: after every game tic the game's sound events (game/sound.h) go to
 * the mixer in window mode (distance volume and pan from the player),
 * nowhere in headless mode, and with --sound-log to a file, one line per
 * event: `tic name x y` (tic = the tic that made it, 0-based; x y in world
 * units with 4 decimals, `- -` for local player sounds). Menu ticks and
 * paused tics run no game tic and drain nothing. */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bast.h"
#include "engine/colormap.h"
#include "engine/mixer.h"
#include "engine/music.h"
#include "engine/png.h"
#include "engine/render.h"
#include "engine/video.h"
#include "game/demo.h"
#include "game/hud.h"
#include "game/flow.h"
#include "game/items.h"
#include "game/sound.h"
#include "game/state.h"
#include "game/topdown.h"
#include "platform/platform.h"

#ifndef VS3D_ASSETS_DIR
#define VS3D_ASSETS_DIR "assets/gen"
#endif
#ifndef VS3D_MAPS_DIR
#define VS3D_MAPS_DIR "maps"
#endif
#ifndef VS3D_MUSIC_DIR
#define VS3D_MUSIC_DIR "assets/src/music"
#endif

#define MOUSE_TURN 40 /* angle units per pixel of mouse X motion */
#define MAX_CATCHUP_TICS 10

enum { EXIT_OK = 0, EXIT_ERROR = 1, EXIT_EXPECT = 2 };

typedef struct {
    int headless;
    const char *demo_path;
    const char *record_path;
    const char *frames_dir;
    int frame_every;
    long max_tics; /* 0 = no limit */
    const char *hash_log;
    const char *sound_log;
    const char *map_path;
    int warp;  /* 0 = not given */
    int skill; /* 0 = not given */
    int god;
    int topdown;
} options;

static uint8_t framebuffer[SCREEN_W * SCREEN_H];

/* Audio thread: the platform pulls frames straight from the mixer. */
static void audio_fill(int16_t *out, int frames, void *user)
{
    (void)user;
    mixer_render(out, frames);
}

static int clamp127(int v)
{
    return v > TICCMD_MAX ? TICCMD_MAX : v < -TICCMD_MAX ? -TICCMD_MAX : v;
}

static void usage(void)
{
    fprintf(stderr,
            "usage: game [--headless] [--demo <file.demo>] [--record <file.demo>]\n"
            "            [--frames-out <dir>] [--frame-every N] [--max-tics N]\n"
            "            [--hash-log <file>] [--sound-log <file>] [--map <file.vmap>]\n"
            "            [--warp N] [--skill 1-3]\n"
            "            [--god] [--topdown]\n");
}

static int parse_long(const char *s, long lo, long hi, long *out)
{
    char *end;
    long v = strtol(s, &end, 10);
    if (*s == '\0' || *end != '\0' || v < lo || v > hi)
        return 0;
    *out = v;
    return 1;
}

static int parse_args(int argc, char **argv, options *o)
{
    memset(o, 0, sizeof *o);
    o->frame_every = 1;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
        long n;
        if (strcmp(a, "--headless") == 0) {
            o->headless = 1;
        } else if (strcmp(a, "--god") == 0) {
            o->god = 1;
        } else if (strcmp(a, "--topdown") == 0) {
            o->topdown = 1; /* dev top-down view instead of the 3D view */
        } else if (!v) {
            fprintf(stderr, "game: unknown flag or missing value: %s\n", a);
            return 0;
        } else if (strcmp(a, "--demo") == 0) {
            o->demo_path = v;
            i++;
        } else if (strcmp(a, "--record") == 0) {
            o->record_path = v;
            i++;
        } else if (strcmp(a, "--frames-out") == 0) {
            o->frames_dir = v;
            i++;
        } else if (strcmp(a, "--hash-log") == 0) {
            o->hash_log = v;
            i++;
        } else if (strcmp(a, "--sound-log") == 0) {
            o->sound_log = v;
            i++;
        } else if (strcmp(a, "--map") == 0) {
            o->map_path = v;
            i++;
        } else if (strcmp(a, "--frame-every") == 0) {
            if (!parse_long(v, 1, 1000000, &n)) {
                fprintf(stderr, "game: --frame-every: bad value '%s'\n", v);
                return 0;
            }
            o->frame_every = (int)n;
            i++;
        } else if (strcmp(a, "--max-tics") == 0) {
            if (!parse_long(v, 1, 100000000, &n)) {
                fprintf(stderr, "game: --max-tics: bad value '%s'\n", v);
                return 0;
            }
            o->max_tics = n;
            i++;
        } else if (strcmp(a, "--warp") == 0) {
            if (!parse_long(v, 1, 99, &n)) {
                fprintf(stderr, "game: --warp: bad level '%s'\n", v);
                return 0;
            }
            o->warp = (int)n;
            i++;
        } else if (strcmp(a, "--skill") == 0) {
            if (!parse_long(v, 1, 3, &n)) {
                fprintf(stderr, "game: --skill: must be 1-3, got '%s'\n", v);
                return 0;
            }
            o->skill = (int)n;
            i++;
        } else {
            fprintf(stderr, "game: unknown flag: %s\n", a);
            return 0;
        }
    }
    if (o->headless && !o->demo_path && !o->max_tics) {
        fprintf(stderr, "game: --headless needs --demo <file> or --max-tics N\n");
        return 0;
    }
    if (o->headless && o->record_path) {
        fprintf(stderr, "game: --record is for window mode\n");
        return 0;
    }
    if (o->frames_dir && !o->headless) {
        fprintf(stderr, "game: --frames-out needs --headless\n");
        return 0;
    }
    return 1;
}

/* The run: everything both modes share. */
typedef struct {
    options opt;
    demo_t *demo;
    bast_map *map;
    char map_name[DEMO_MAP_NAME_MAX]; /* for the --record header */
    int level, skill;
    uint32_t seed;
    game_state gs;
    game_state entry; /* level entry state, for Restart Level */
    flow_t flow;
    flow_world world;
    flow_gfx *gfx;   /* menus; loaded when anything is drawn */
    uint32_t run_tic; /* tics run, menus included (frames, --max-tics) */
    int exit_flow;    /* main menu Quit chosen */
    FILE *hash_log;
    FILE *sound_log;
    int audio;                      /* window mode: sounds go to the mixer */
    mixer_sound *sfx[SFX_COUNT];    /* loaded effects, NULL if missing */
    demo_rec rec;
    hud_state hud; /* render side: timers, face (game/hud.h) */
    int status;
} run_t;

/* Map choice: --map, else --warp N (maps/levelN.vmap), else the demo's
 * `map` line (maps/<name>.vmap), else its `level` (maps/levelN.vmap), else
 * the M1 test map. Level/skill: flag, else demo, else 1 / 2. */
static int setup(run_t *r)
{
    const options *o = &r->opt;
    if (o->demo_path) {
        size_t size;
        char *text = platform_read_file(o->demo_path, &size);
        if (!text) {
            fprintf(stderr, "game: %s: cannot read\n", o->demo_path);
            return 0;
        }
        char err[256];
        r->demo = demo_parse(text, err, sizeof err);
        free(text);
        if (!r->demo) {
            fprintf(stderr, "game: %s: %s\n", o->demo_path, err);
            return 0;
        }
    }
    r->level = o->warp ? o->warp : r->demo ? r->demo->level : 1;
    r->skill = o->skill ? o->skill : r->demo ? r->demo->skill : 2;
    r->seed = r->demo ? r->demo->seed : 0;

    char path[512];
    const char *map_path = path;
    if (o->map_path) {
        map_path = o->map_path;
        /* Name for a recorded header: basename without .vmap. */
        const char *b = strrchr(o->map_path, '/');
        b = b ? b + 1 : o->map_path;
        snprintf(r->map_name, sizeof r->map_name, "%s", b);
        char *dot = strrchr(r->map_name, '.');
        if (dot)
            *dot = '\0';
    } else if (o->warp || (r->demo && !r->demo->map[0])) {
        snprintf(path, sizeof path, "%s/level%d.vmap", VS3D_MAPS_DIR, r->level);
    } else {
        snprintf(r->map_name, sizeof r->map_name, "%s",
                 r->demo && r->demo->map[0] ? r->demo->map : "m1test");
        snprintf(path, sizeof path, "%s/%s.vmap", VS3D_MAPS_DIR, r->map_name);
    }

    /* Palette + light colormap, built by `cmake --build build`. */
    if (!colormap_load(VS3D_ASSETS_DIR)) {
        fprintf(stderr, "game: %s\n", colormap_error());
        return 0;
    }
    r->map = bast_map_load(map_path);
    if (!r->map) {
        fprintf(stderr, "game: %s: %s\n", map_path, bast_error());
        return 0;
    }
    /* Wall textures, only needed when the 3D view draws: in a window, or
     * headless with --frames-out. A headless run without frames never draws. */
    if (!o->topdown && (!o->headless || o->frames_dir) && !render_init(r->map, VS3D_ASSETS_DIR)) {
        fprintf(stderr, "game: %s\n", render_error());
        return 0;
    }
    if (!o->topdown && (!o->headless || o->frames_dir) && !hud_init(VS3D_ASSETS_DIR)) {
        fprintf(stderr, "game: %s\n", hud_error());
        return 0;
    }
    if (!o->headless || o->frames_dir) {
        r->gfx = flow_gfx_load(VS3D_ASSETS_DIR);
        if (!r->gfx) {
            fprintf(stderr, "game: %s\n", flow_gfx_error());
            return 0;
        }
    }
    if (o->hash_log) {
        r->hash_log = fopen(o->hash_log, "w");
        if (!r->hash_log) {
            fprintf(stderr, "game: %s: cannot write\n", o->hash_log);
            return 0;
        }
    }
    if (o->sound_log) {
        r->sound_log = fopen(o->sound_log, "w");
        if (!r->sound_log) {
            fprintf(stderr, "game: %s: cannot write\n", o->sound_log);
            return 0;
        }
    }
    if (o->frames_dir && !platform_make_dir(o->frames_dir)) {
        fprintf(stderr, "game: %s: cannot create directory\n", o->frames_dir);
        return 0;
    }
    demo_rec_init(&r->rec);
    game_init(&r->gs, r->map, r->level, r->skill, r->seed);
    hud_state_init(&r->hud, &r->gs, o->god);
    /* Start screen: --warp, --map and --record jump straight into play;
     * else the demo's `start` line (default level); else the title. */
    int screen = FLOW_TITLE;
    if (o->warp || o->map_path || o->record_path)
        screen = FLOW_PLAY;
    else if (r->demo)
        screen = r->demo->start;
    else if (o->headless)
        screen = FLOW_PLAY; /* --headless --max-tics N: the M1 smoke run */
    r->world = (flow_world){ &r->gs, &r->entry, r->map, r->level, r->seed };
    flow_init(&r->flow, screen, &r->world);
    return 1;
}

/* Draws the current view into the framebuffer. Reads game state only. */
static void draw(const run_t *r)
{
    static render_thing things[RENDER_MAX_SPRITES];
    int screen = r->flow.screen;
    if (screen == FLOW_PLAY || screen == FLOW_PAUSE) {
        if (r->opt.topdown)
            topdown_draw(&r->gs, framebuffer);
        else {
            /* Live door state (game/door.h) as the per-cell table the
             * renderer reads: only DOOR cells are looked at, the rest may
             * stay 0. Door cells past MAX_DOORS are not tracked by the game
             * (doors_init skips them), so they keep 0 here and draw closed. */
            static fix_t door_open[BAST_MAP_MAX_DIM * BAST_MAP_MAX_DIM];
            for (int i = 0; i < r->gs.door_count; i++) {
                const door_t *d = &r->gs.doors[i];
                door_open[d->ty * r->map->width + d->tx] = d->pos;
            }
            render_set_doors(door_open);
            /* Pickups from live state: a taken one is gone the same tic. The
             * tic is only read (shimmer frame), so the state hash is
             * unaffected. Under pause the tic is frozen, so is the shimmer. */
            const int n = items_render_things(&r->gs, things, RENDER_MAX_SPRITES);
            render_view_things(r->map, r->gs.player.x, r->gs.player.y, r->gs.player.angle,
                               things, n, r->gs.tic, framebuffer);
            hud_draw(framebuffer, &r->hud, &r->gs);
        }
    }
    /* Pause darkens the view and the status bar in place. */
    flow_draw(&r->flow, r->gfx, framebuffer);
}

/* Writes a 16.16 value as decimal world units with 4 decimals (integer
 * maths: the log must not depend on float formatting). */
static void log_fix(FILE *f, fix_t v)
{
    int64_t a = v < 0 ? -(int64_t)v : v;
    int64_t frac = ((a & 0xffff) * 10000 + 0x8000) >> 16;
    int64_t whole = a >> 16;
    if (frac == 10000) {
        whole++;
        frac = 0;
    }
    fprintf(f, "%s%" PRId64 ".%04" PRId64, v < 0 ? "-" : "", whole, frac);
}

/* Sends the tic's sound events to the log and, in window mode, the mixer:
 * volume from the distance to the player, pan from the angle between the
 * view and the source. Reads game state only. */
static void drain_sounds(run_t *r)
{
    const game_state *gs = &r->gs;
    const player_t *p = &gs->player;
    for (int i = 0; i < gs->sounds.count; i++) {
        const sound_event *e = &gs->sounds.ev[i];
        if (r->sound_log) {
            fprintf(r->sound_log, "%" PRIu32 " %s ", gs->tic - 1, sfx_name(e->id));
            if (e->local) {
                fputs("- -", r->sound_log);
            } else {
                log_fix(r->sound_log, e->x);
                fputc(' ', r->sound_log);
                log_fix(r->sound_log, e->y);
            }
            fputc('\n', r->sound_log);
        }
        if (!r->audio || !r->sfx[e->id])
            continue;
        int vol = MIXER_VOL_MAX, pan = MIXER_PAN_CENTER;
        if (!e->local) {
            const fix_t dx = e->x - p->x, dy = e->y - p->y;
            vol = mixer_dist_volume(fix_dist(dx, dy));
            pan = mixer_pan(p->angle, angle_of(dx, dy));
        }
        if (vol > 0)
            mixer_start(r->sfx[e->id], MIXER_SOURCE_NONE, vol, pan);
    }
}

/* Runs one tic of the flow with menu key `key` and `cmd` (a game tic when
 * the screen is PLAY); logs the hash, records game tics, dumps a frame.
 * Frames are named by the run tic, which equals the game tic for runs that
 * start in play. */
static int step(run_t *r, int key, const ticcmd *cmd)
{
    int res = flow_tic(&r->flow, key, &r->world, cmd);
    r->run_tic++;
    if (res & FLOW_NEWLEVEL) /* New Game / Restart Level: fresh face, no stale flashes */
        hud_state_init(&r->hud, &r->gs, r->opt.god);
    if (res & FLOW_TICKED) /* the HUD only advances with the game (pause freezes it) */
        hud_tic(&r->hud, &r->gs);
    if (res & FLOW_TICKED) /* the queue holds the last game tic's events */
        drain_sounds(r);
    if (res & FLOW_EXIT)
        r->exit_flow = 1;
    if (r->hash_log)
        fprintf(r->hash_log, "%016" PRIx64 "\n", game_hash(&r->gs));
    if ((res & FLOW_TICKED) && r->opt.record_path && !demo_rec_add(&r->rec, cmd)) {
        fprintf(stderr, "game: out of memory recording\n");
        return 0;
    }
    if (r->opt.frames_dir && r->run_tic % (uint32_t)r->opt.frame_every == 0) {
        char path[1024];
        snprintf(path, sizeof path, "%s/frame_%06" PRIu32 ".png", r->opt.frames_dir, r->run_tic);
        draw(r);
        if (!png_write_indexed(path, framebuffer, SCREEN_W, SCREEN_H, colormap_palette())) {
            fprintf(stderr, "game: %s: cannot write\n", path);
            return 0;
        }
    }
    return 1;
}

/* Input for the next tic from the demo. 0 = stop (end, failure). */
static int demo_input(run_t *r, ticcmd *cmd, int *key)
{
    char err[256] = "";
    r->demo->screen = r->flow.screen;
    int s = demo_next(r->demo, &r->gs, cmd, err, sizeof err);
    *key = r->demo->key;
    if (s == DEMO_FAIL) {
        fprintf(stderr, "game: %s: %s\n", r->opt.demo_path, err);
        r->status = EXIT_EXPECT;
    }
    return s == DEMO_TIC;
}

static int tic_limit_hit(const run_t *r)
{
    return r->opt.max_tics && (long)r->run_tic >= r->opt.max_tics;
}

static void run_headless(run_t *r)
{
    while (!tic_limit_hit(r) && !r->exit_flow) {
        ticcmd cmd = { 0, 0, 0, 0 };
        int key = FKEY_NONE;
        if (r->demo && !demo_input(r, &cmd, &key))
            return;
        if (!step(r, key, &cmd)) {
            r->status = EXIT_ERROR;
            return;
        }
        if (r->gs.exit) /* an EXIT cell ends the level run */
            return;
        if (r->gs.player.dead)
            return; /* death ends the run; STATS shows health=0 */
    }
}

/* Window keys -> menu keys. Every key counts as "any" on the title. */
static int menu_key(int key)
{
    switch (key) {
    case PKEY_UP:
    case 'w':
        return FKEY_UP;
    case PKEY_DOWN:
    case 's':
        return FKEY_DOWN;
    case PKEY_ENTER:
    case PKEY_SPACE:
        return FKEY_ENTER;
    case PKEY_ESCAPE:
        return FKEY_ESC;
    default:
        return FKEY_ANY;
    }
}

#define KEY_QUEUE 8

static void run_window(run_t *r)
{
    if (!platform_init("Vibe Shooter 3D II", 3)) {
        fprintf(stderr, "game: %s\n", platform_error());
        r->status = EXIT_ERROR;
        return;
    }
    /* Mixer, every effect and the title music before the device opens. A
     * missing sound or song is not fatal: it stays silent. */
    mixer_init(VS3D_ASSETS_DIR);
    for (int i = 0; i < SFX_COUNT; i++) {
        r->sfx[i] = mixer_load_snd(sfx_name(i));
        if (!r->sfx[i])
            fprintf(stderr, "game: %s\n", mixer_error());
    }
    char err[256];
    music_song *title_song = music_load(VS3D_MUSIC_DIR "/title.vmus", err, sizeof err);
    if (!title_song)
        fprintf(stderr, "game: title music: %s\n", err);
    music_attach();
    int music_on = 0;
    if (platform_audio_open(audio_fill, NULL))
        r->audio = 1;
    else
        fprintf(stderr, "game: audio disabled: %s\n", platform_error());
    platform_set_relative_mouse(true);

    uint8_t held[PKEY_COUNT] = { 0 };
    int mouse_fire = 0;
    int mouse_dx = 0;
    int keyq[KEY_QUEUE], keyq_len = 0; /* menu keys, one consumed per tic */
    int weapon_key = 0;                /* 1-7 pressed since the last tic */
    uint64_t start = platform_ticks_ms();
    uint32_t tics_done = 0; /* tics run since `start` */

    int running = 1;
    while (running) {
        plat_event ev;
        while (platform_poll_event(&ev)) {
            if (ev.type == PEV_QUIT) {
                running = 0;
            } else if ((ev.type == PEV_KEY_DOWN || ev.type == PEV_KEY_UP) && ev.key > 0 &&
                       ev.key < PKEY_COUNT) {
                held[ev.key] = ev.type == PEV_KEY_DOWN;
                if (ev.type == PEV_KEY_DOWN) {
                    int mk = menu_key(ev.key);
                    /* Auto-repeat only scrolls menus. */
                    if ((!ev.repeat || mk == FKEY_UP || mk == FKEY_DOWN) && keyq_len < KEY_QUEUE)
                        keyq[keyq_len++] = mk;
                    if (ev.key >= '1' && ev.key <= '7')
                        weapon_key = ev.key - '0';
                }
            } else if (ev.type == PEV_MOUSE_DOWN || ev.type == PEV_MOUSE_UP) {
                if (ev.button == PMOUSE_LEFT)
                    mouse_fire = ev.type == PEV_MOUSE_DOWN;
                if (ev.type == PEV_MOUSE_DOWN && r->flow.screen == FLOW_TITLE &&
                    keyq_len < KEY_QUEUE)
                    keyq[keyq_len++] = FKEY_ANY;
            } else if (ev.type == PEV_MOUSE_MOVE) {
                mouse_dx += ev.dx;
            }
        }

        /* Whole tics owed at 35 Hz since start. After a stall, drop the
         * backlog instead of fast-forwarding. */
        uint64_t due = (platform_ticks_ms() - start) * TICRATE / 1000;
        if (due > tics_done + MAX_CATCHUP_TICS) {
            tics_done = (uint32_t)due - 1;
        }
        while (running && tics_done < due) {
            ticcmd cmd = { 0, 0, 0, 0 };
            int key = FKEY_NONE;
            if (tic_limit_hit(r)) {
                running = 0;
                break;
            }
            if (r->demo) {
                if (!demo_input(r, &cmd, &key)) {
                    running = 0;
                    break;
                }
            } else {
                if (keyq_len > 0) {
                    key = keyq[0];
                    keyq_len--;
                    memmove(keyq, keyq + 1, (size_t)keyq_len * sizeof keyq[0]);
                }
                int fwd = 0, side = 0, turn = 0;
                if (held['w'] || held[PKEY_UP])
                    fwd += TICCMD_MAX;
                if (held['s'] || held[PKEY_DOWN])
                    fwd -= TICCMD_MAX;
                if (held['d'])
                    side += TICCMD_MAX;
                if (held['a'])
                    side -= TICCMD_MAX;
                if (held[PKEY_LEFT])
                    turn += PLAYER_TURN_SPEED;
                if (held[PKEY_RIGHT])
                    turn -= PLAYER_TURN_SPEED;
                turn -= mouse_dx * MOUSE_TURN; /* mouse right = clockwise */
                mouse_dx = 0;
                if (turn > INT16_MAX)
                    turn = INT16_MAX;
                if (turn < INT16_MIN)
                    turn = INT16_MIN;
                cmd.fwd = (int8_t)clamp127(fwd);
                cmd.side = (int8_t)clamp127(side);
                cmd.turn = (int16_t)turn;
                if (held[PKEY_LSHIFT] || held[PKEY_RSHIFT])
                    cmd.buttons |= BT_RUN;
                if (mouse_fire || held[PKEY_LCTRL] || held[PKEY_RCTRL])
                    cmd.buttons |= BT_FIRE;
                if (held['e'] || held[PKEY_SPACE])
                    cmd.buttons |= BT_USE;
                if (weapon_key)
                    cmd.buttons |= (uint8_t)(weapon_key << BT_WEAPON_SHIFT);
                weapon_key = 0;
            }
            if (!step(r, key, &cmd)) {
                r->status = EXIT_ERROR;
                running = 0;
                break;
            }
            {
                mixer_sound *ms = r->flow.sound == FSND_MOVE     ? r->sfx[SFX_menu_move]
                                  : r->flow.sound == FSND_SELECT ? r->sfx[SFX_menu_select]
                                                                 : NULL;
                if (ms)
                    mixer_start(ms, MIXER_SOURCE_NONE, MIXER_VOL_MAX, MIXER_PAN_CENTER);
            }
            if (r->exit_flow)
                running = 0;
            tics_done++;
            if (r->gs.exit) { /* an EXIT cell ends the level run */
                running = 0;
                break;
            }
        }

        /* Title music on the title and menu screens (hud.md Flow). The
         * pause menu keeps the level silent until level music exists. */
        int want_music = r->flow.screen != FLOW_PLAY && r->flow.screen != FLOW_PAUSE &&
                         !(r->flow.screen == FLOW_CONTROLS && r->flow.back == FLOW_PAUSE);
        if (title_song && want_music != music_on) {
            if (want_music)
                music_play(title_song);
            else
                music_stop();
            music_on = want_music;
        }

        draw(r);
        platform_present(framebuffer, colormap_palette());
        platform_sleep_ms(5);
    }
    platform_shutdown();
    mixer_shutdown();
    music_free(title_song);

    if (r->opt.record_path) {
        if (demo_rec_write(&r->rec, r->opt.record_path, r->level, r->gs.skill, 0, r->map_name))
            fprintf(stderr, "game: recorded %" PRIu32 " tics to %s\n", r->gs.tic,
                    r->opt.record_path);
        else {
            fprintf(stderr, "game: %s: cannot write\n", r->opt.record_path);
            r->status = EXIT_ERROR;
        }
    }
}

/* items is pickups taken / pickups spawned for the skill (weapons
 * included); secrets is SECRET cells entered / in the map. */
static void print_stats(const run_t *r)
{
    const player_t *p = &r->gs.player;
    printf("STATS level=%d tic=%" PRIu32 " hash=%016" PRIx64
           " health=%d armor=%d kills=%d/%d items=%d/%d secrets=%d/%d exit=%d god=%d\n",
           r->level, r->gs.tic, game_hash(&r->gs), (int)p->health, (int)p->armour,
           (int)r->gs.things.kills, (int)r->gs.things.kill_total, (int)r->gs.items_got,
           (int)r->gs.items_total, (int)r->gs.secrets, (int)r->gs.secret_total,
           (int)r->gs.exit, r->opt.god);
    fflush(stdout);
}

int main(int argc, char **argv)
{
    static run_t r;
    if (!parse_args(argc, argv, &r.opt)) {
        usage();
        return EXIT_ERROR;
    }
    r.status = EXIT_OK;
    if (!setup(&r)) {
        r.status = EXIT_ERROR;
    } else {
        if (r.opt.headless)
            run_headless(&r);
        else
            run_window(&r);
        if (r.status != EXIT_ERROR)
            print_stats(&r);
    }
    if (r.sound_log && fclose(r.sound_log) != 0) {
        fprintf(stderr, "game: %s: write failed\n", r.opt.sound_log);
        r.status = EXIT_ERROR;
    }
    if (r.hash_log && fclose(r.hash_log) != 0) {
        fprintf(stderr, "game: %s: write failed\n", r.opt.hash_log);
        r.status = EXIT_ERROR;
    }
    demo_rec_free(&r.rec);
    demo_free(r.demo);
    render_shutdown();
    hud_shutdown();
    flow_gfx_free(r.gfx);
    bast_map_free(r.map);
    return r.status;
}
