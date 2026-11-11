#include "game/flow.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/colormap.h"
#include "engine/draw2d.h"
#include "engine/video.h"

static const char *const SCREEN_NAMES[FLOW_SCREEN_COUNT] = {
    "title", "menu", "skill", "controls", "play", "pause",
};

const char *flow_screen_name(int screen)
{
    return screen >= 0 && screen < FLOW_SCREEN_COUNT ? SCREEN_NAMES[screen] : NULL;
}

int flow_screen_from_name(const char *name)
{
    for (int i = 0; i < FLOW_SCREEN_COUNT; i++)
        if (strcmp(SCREEN_NAMES[i], name) == 0)
            return i;
    return -1;
}

static void go(flow_t *f, int screen, int cursor)
{
    f->screen = screen;
    f->cursor = cursor;
    f->timer = 0;
}

void flow_init(flow_t *f, int screen, flow_world *w)
{
    memset(f, 0, sizeof *f);
    f->back = FLOW_MENU;
    go(f, screen, 0);
    if (screen == FLOW_PLAY && w && w->gs && w->entry)
        *w->entry = *w->gs;
}

int flow_title_blink_on(const flow_t *f)
{
    return f->screen == FLOW_TITLE && (f->timer / FLOW_BLINK_TICS) % 2 == 0;
}

/* Up/down on a list of n items, wrapping both ways. */
static int move_cursor(flow_t *f, int key, int n)
{
    if (key == FKEY_UP)
        f->cursor = (f->cursor + n - 1) % n;
    else if (key == FKEY_DOWN)
        f->cursor = (f->cursor + 1) % n;
    else
        return 0;
    f->sound = FSND_MOVE;
    return 1;
}

int flow_tic(flow_t *f, int key, flow_world *w, const ticcmd *cmd)
{
    int start = f->screen;
    int result = 0;
    f->sound = FSND_NONE;
    switch (f->screen) {
    case FLOW_TITLE:
        if (key != FKEY_NONE) {
            go(f, FLOW_MENU, MENU_NEW_GAME);
            f->sound = FSND_SELECT;
        }
        break;
    case FLOW_MENU:
        if (move_cursor(f, key, MENU_COUNT))
            break;
        if (key == FKEY_ESC) {
            go(f, FLOW_TITLE, 0);
            f->sound = FSND_MOVE;
        } else if (key == FKEY_ENTER) {
            f->sound = FSND_SELECT;
            if (f->cursor == MENU_NEW_GAME) {
                go(f, FLOW_SKILL, FLOW_DEFAULT_SKILL - 1);
            } else if (f->cursor == MENU_CONTROLS) {
                f->back = FLOW_MENU;
                go(f, FLOW_CONTROLS, MENU_CONTROLS);
            } else {
                result |= FLOW_EXIT;
            }
        }
        break;
    case FLOW_SKILL:
        if (move_cursor(f, key, SKILL_COUNT))
            break;
        if (key == FKEY_ESC) {
            go(f, FLOW_MENU, MENU_NEW_GAME);
            f->sound = FSND_MOVE;
        } else if (key == FKEY_ENTER) {
            f->sound = FSND_SELECT;
            game_init(w->gs, w->map, w->level, f->cursor + 1, w->seed);
            *w->entry = *w->gs;
            go(f, FLOW_PLAY, 0);
            result |= FLOW_NEWLEVEL;
        }
        break;
    case FLOW_CONTROLS:
        if (key == FKEY_ESC || key == FKEY_ENTER) {
            go(f, f->back, f->back == FLOW_PAUSE ? PAUSE_CONTROLS : MENU_CONTROLS);
            f->sound = FSND_MOVE;
        }
        break;
    case FLOW_PLAY:
        if (key == FKEY_ESC) {
            go(f, FLOW_PAUSE, PAUSE_RESUME);
            f->sound = FSND_SELECT;
        }
        break;
    case FLOW_PAUSE:
        if (move_cursor(f, key, PAUSE_COUNT))
            break;
        if (key == FKEY_ESC) {
            go(f, FLOW_PLAY, 0);
            f->sound = FSND_MOVE;
        } else if (key == FKEY_ENTER) {
            f->sound = FSND_SELECT;
            switch (f->cursor) {
            case PAUSE_RESUME:
                go(f, FLOW_PLAY, 0);
                break;
            case PAUSE_RESTART:
                *w->gs = *w->entry;
                go(f, FLOW_PLAY, 0);
                result |= FLOW_NEWLEVEL;
                break;
            case PAUSE_CONTROLS:
                f->back = FLOW_PAUSE;
                go(f, FLOW_CONTROLS, 0);
                break;
            default:
                go(f, FLOW_TITLE, 0);
                break;
            }
        }
        break;
    }
    if (f->screen == start) {
        if (f->screen == FLOW_PLAY) {
            game_tic(w->gs, cmd);
            result |= FLOW_TICKED;
        }
        f->timer++;
    }
    return result;
}

/* ---- drawing ---- */

#define DARK_ROW 20  /* colormap row behind menus (issue #101) */
#define ITEM_X 104   /* hud.md: items at x=104, y=96 + 20*i */
#define ITEM_Y 96
#define ITEM_DY 20
#define HEAD_Y 70  /* clears the title apex sparkle (y 60..67), art director PR #136 */
#define BLINK_Y 204

struct flow_gfx {
    bast_tex *title;
    bast_tex *ankh;
    uint8_t dark[256]; /* colormap row DARK_ROW, fullbright dimmed too */
    font *menu;
    font *small;
};

static char g_err[1024];

const char *flow_gfx_error(void)
{
    return g_err;
}

static bast_tex *load_tex(const char *dir, const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    bast_tex *t = bast_tex_load(path);
    if (!t)
        snprintf(g_err, sizeof g_err, "%s: %s", path, bast_error());
    return t;
}

flow_gfx *flow_gfx_load(const char *dir)
{
    flow_gfx *g = calloc(1, sizeof *g);
    if (!g) {
        snprintf(g_err, sizeof g_err, "out of memory");
        return NULL;
    }
    colormap_dim_remap(DARK_ROW, g->dark);
    g->title = load_tex(dir, "ui/title.tex");
    g->ankh = g->title ? load_tex(dir, "hud/menu_ankh.tex") : NULL;
    if (g->ankh) {
        g->menu = font_load(dir, "menu");
        g->small = g->menu ? font_load(dir, "small") : NULL;
        if (!g->small)
            snprintf(g_err, sizeof g_err, "%s/fonts: cannot load the menu or small font", dir);
    }
    if (!g->small) {
        flow_gfx_free(g);
        return NULL;
    }
    return g;
}

void flow_gfx_free(flow_gfx *g)
{
    if (!g)
        return;
    bast_tex_free(g->title);
    bast_tex_free(g->ankh);
    font_free(g->menu);
    font_free(g->small);
    free(g);
}

static void centred(uint8_t *fb, const font *ft, int y, const char *s)
{
    draw_text(fb, ft, (SCREEN_W - text_width(ft, s)) / 2, y, s, NULL);
}

/* Items at x=104, y=96+20*i, the ankh left of the selected one, centred on
 * the text line. */
static void items(uint8_t *fb, const flow_gfx *g, const char *const *names, int n, int cursor)
{
    for (int i = 0; i < n; i++)
        draw_text(fb, g->menu, ITEM_X, ITEM_Y + ITEM_DY * i, names[i], NULL);
    int ax = ITEM_X - g->ankh->width - 4;
    int ay = ITEM_Y + ITEM_DY * cursor + (g->menu->cell_h - g->ankh->height) / 2;
    draw_patch(fb, g->ankh, ax, ay);
}

static const char *const MENU_ITEMS[MENU_COUNT] = { "NEW GAME", "CONTROLS", "QUIT" };
static const char *const PAUSE_ITEMS[PAUSE_COUNT] = { "RESUME", "RESTART LEVEL", "CONTROLS",
                                                      "QUIT TO TITLE" };
/* hud.md Difficulty. */
static const char *const SKILL_ITEMS[SKILL_COUNT] = { "KITTEN", "TOMCAT", "SABRETOOTH" };
static const char *const SKILL_LINES[SKILL_COUNT] = { "\"I CAN HAS MERCY\"", "\"LET'S DO THIS\"",
                                                      "\"NINE LIVES IS PLENTY\"" };
/* hud.md Controls: the static binding list. */
static const char *const CONTROLS[][2] = {
    { "MOVE", "W S A D / ARROWS" },
    { "TURN", "MOUSE / LEFT RIGHT" },
    { "FIRE", "LEFT CLICK / CTRL" },
    { "USE", "E / SPACE" },
    { "RUN", "SHIFT" },
    { "WEAPONS", "1 - 7" },
    { "PAUSE", "ESC" },
};
#define CONTROLS_N (int)(sizeof CONTROLS / sizeof CONTROLS[0])

void flow_draw(const flow_t *f, const flow_gfx *g, uint8_t *fb)
{
    const uint8_t *dark = g->dark;
    switch (f->screen) {
    case FLOW_PLAY:
        return;
    case FLOW_TITLE:
        draw_patch(fb, g->title, 0, 0);
        if (flow_title_blink_on(f))
            centred(fb, g->menu, BLINK_Y, "PRESS ANY KEY");
        return;
    case FLOW_PAUSE:
        for (int i = 0; i < SCREEN_W * SCREEN_H; i++)
            fb[i] = dark[fb[i]];
        break;
    default:
        draw_patch_remap(fb, g->title, 0, 0, dark);
        break;
    }
    switch (f->screen) {
    case FLOW_MENU:
        items(fb, g, MENU_ITEMS, MENU_COUNT, f->cursor);
        break;
    case FLOW_SKILL:
        centred(fb, g->menu, HEAD_Y, "CHOOSE YOUR FURY");
        items(fb, g, SKILL_ITEMS, SKILL_COUNT, f->cursor);
        centred(fb, g->small, ITEM_Y + ITEM_DY * SKILL_COUNT + 8, SKILL_LINES[f->cursor]);
        break;
    case FLOW_CONTROLS:
        centred(fb, g->menu, HEAD_Y, "CONTROLS");
        for (int i = 0; i < CONTROLS_N; i++) {
            draw_text(fb, g->small, 72, ITEM_Y + 12 * i, CONTROLS[i][0], NULL);
            draw_text(fb, g->small, 136, ITEM_Y + 12 * i, CONTROLS[i][1], NULL);
        }
        centred(fb, g->small, ITEM_Y + 12 * CONTROLS_N + 16, "ESC: BACK");
        break;
    case FLOW_PAUSE:
        centred(fb, g->menu, HEAD_Y, "PAUSED");
        items(fb, g, PAUSE_ITEMS, PAUSE_COUNT, f->cursor);
        break;
    default:
        break;
    }
}
