#include "game/flow.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/colormap.h"
#include "engine/draw2d.h"
#include "engine/video.h"
#include "game/story_text.h"

static const char *const SCREEN_NAMES[FLOW_SCREEN_COUNT] = {
    "title", "menu", "skill", "controls", "play", "pause",
    "intro", "tally", "story", "ending", "credits",
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
    f->page = 0;
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

/* ---- tally ---- */

int32_t tally_percent(int32_t found, int32_t total)
{
    if (total <= 0)
        return 100;
    if (found < 0)
        found = 0;
    return (int32_t)((int64_t)found * 100 / total);
}

void tally_format_time(int32_t seconds, char *buf, int cap)
{
    if (seconds < 0)
        seconds = 0;
    snprintf(buf, (size_t)cap, "%d:%02d", (int)(seconds / 60), (int)(seconds % 60));
}

void tally_init(tally_t *t, const game_state *gs)
{
    memset(t, 0, sizeof *t);
    t->target[TALLY_KILLS] = tally_percent(gs->things.kills, gs->things.kill_total);
    t->target[TALLY_ITEMS] = tally_percent(gs->items_got, gs->items_total);
    t->target[TALLY_SECRETS] = gs->secrets;
    t->target[TALLY_TIME] = (int32_t)(gs->tic / TICRATE);
    t->secret_total = gs->secret_total;
    t->par = gs->map ? gs->map->par_seconds : 0;
    const char *name = gs->map ? gs->map->name : "";
    size_t i = 0;
    for (; name[i] && i + 1 < sizeof t->name; i++)
        t->name[i] = (char)(name[i] >= 'a' && name[i] <= 'z' ? name[i] - 'a' + 'A' : name[i]);
    t->name[i] = '\0';
}

int tally_step(tally_t *t)
{
    for (int i = 0; i < TALLY_LINES; i++) {
        if (t->shown[i] < t->target[i]) {
            t->shown[i]++;
            return 1;
        }
    }
    t->done = 1;
    return 0;
}

static void tally_finish(tally_t *t)
{
    for (int i = 0; i < TALLY_LINES; i++)
        t->shown[i] = t->target[i];
    t->done = 1;
}

/* ---- credits ---- */

int flow_credits_height(void)
{
    return story_credits_lines * FLOW_CREDITS_LINE_H;
}

uint32_t flow_credits_tics(void)
{
    return 2u * (uint32_t)(flow_credits_height() + SCREEN_H);
}

/* ---- text screens ---- */

/* The screen's texts: intro and ending have two, the story after level N
 * has intermission N (none after the last level). */
static int screen_texts(const flow_t *f, const char *const **texts)
{
    switch (f->screen) {
    case FLOW_INTRO:
        *texts = story_intro;
        return STORY_INTRO_SCREENS;
    case FLOW_ENDING:
        *texts = story_ending;
        return STORY_ENDING_SCREENS;
    case FLOW_STORY:
        if (f->done_level >= 1 && f->done_level <= STORY_INTERMISSIONS) {
            *texts = &story_intermission[f->done_level - 1];
            return 1;
        }
        return 0;
    default:
        return 0;
    }
}

static int screen_pages(const flow_t *f)
{
    const char *const *texts = NULL;
    int n = screen_texts(f, &texts), pages = 0;
    for (int i = 0; i < n; i++)
        pages += story_page_count(texts[i]);
    return pages;
}

int flow_page_text(const flow_t *f, char *out, int cap)
{
    const char *const *texts = NULL;
    int n = screen_texts(f, &texts), page = f->page;
    for (int i = 0; i < n; i++) {
        int k = story_page_count(texts[i]);
        if (page < k)
            return story_page(texts[i], page, out, (size_t)cap);
        page -= k;
    }
    return 0;
}

/* Story -> the next level with the carried inventory. 0: no next level. */
static int next_level(flow_world *w)
{
    if (!w->load_level)
        return 0;
    inventory_t inv;
    game_carry(w->gs, &inv);
    const int skill = w->gs->skill;
    const bast_map *map = w->load_level(w->ctx, w->level + 1);
    if (!map)
        return 0;
    w->map = map;
    w->level++;
    game_init_with(w->gs, map, w->level, skill, w->seed, &inv);
    *w->entry = *w->gs;
    return 1;
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
            /* The level starts now; its first tic runs after the intro. */
            game_init(w->gs, w->map, w->level, f->cursor + 1, w->seed);
            go(f, FLOW_INTRO, 0);
            f->page = 0;
        }
        break;
    case FLOW_INTRO:
        if (key != FKEY_NONE) {
            f->sound = FSND_SELECT;
            if (++f->page >= screen_pages(f)) {
                *w->entry = *w->gs;
                go(f, FLOW_PLAY, 0);
                result |= FLOW_NEWLEVEL;
            }
        }
        break;
    case FLOW_TALLY:
        if (!f->tally.done) {
            if (key != FKEY_NONE) { /* any key skips the counting */
                tally_finish(&f->tally);
                f->sound = FSND_TALLY_DONE;
            } else if (tally_step(&f->tally)) {
                if (f->timer % 3 == 0) /* a tick every 3 tics, not a buzz */
                    f->sound = FSND_TALLY_TICK;
            } else {
                f->sound = FSND_TALLY_DONE;
            }
        } else if (key != FKEY_NONE) { /* a second key continues */
            f->sound = FSND_SELECT;
            if (f->done_level >= FLOW_LAST_LEVEL) {
                go(f, FLOW_ENDING, 0);
            } else {
                go(f, FLOW_STORY, 0);
                if (screen_pages(f) == 0) { /* no story text: straight on */
                    if (next_level(w)) {
                        go(f, FLOW_PLAY, 0);
                        result |= FLOW_NEWLEVEL;
                    } else {
                        go(f, FLOW_TITLE, 0);
                    }
                }
            }
        }
        break;
    case FLOW_STORY:
        if (key != FKEY_NONE) {
            f->sound = FSND_SELECT;
            if (++f->page >= screen_pages(f)) {
                if (next_level(w)) {
                    go(f, FLOW_PLAY, 0);
                    result |= FLOW_NEWLEVEL;
                } else {
                    go(f, FLOW_TITLE, 0);
                }
            }
        }
        break;
    case FLOW_ENDING:
        if (key != FKEY_NONE) {
            f->sound = FSND_SELECT;
            if (++f->page >= screen_pages(f))
                go(f, FLOW_CREDITS, 0);
        }
        break;
    case FLOW_CREDITS:
        if (key == FKEY_ESC || key == FKEY_ENTER || f->timer + 1 >= flow_credits_tics())
            go(f, FLOW_TITLE, 0);
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
        if (f->screen == FLOW_PLAY && w->gs->exit) { /* the exit: tally */
            tally_init(&f->tally, w->gs);
            f->done_level = w->level;
            go(f, FLOW_TALLY, 0);
            result |= FLOW_LEVELDONE;
        }
    }
    return result;
}

/* ---- drawing ---- */

#define DARK_ROW 20  /* colormap row behind menus (issue #101) */
#define CREDITS_DARK_ROW 27 /* darker behind the credits: small font legible */
#define ITEM_X 104   /* hud.md: items at x=104, y=96 + 20*i */
#define ITEM_Y 96
#define ITEM_DY 20
#define HEAD_Y 70  /* clears the title apex sparkle (y 60..67), art director PR #136 */
#define BLINK_Y 204

struct flow_gfx {
    bast_tex *title;
    bast_tex *ankh;
    bast_tex *papyrus; /* intro, story, ending (issue #156) */
    bast_tex *tally;   /* intermission tally */
    uint8_t ink[256];  /* menu font gold -> brown ink on the papyrus */
    uint8_t credits_dark[256]; /* colormap row CREDITS_DARK_ROW */
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
    colormap_dim_remap(CREDITS_DARK_ROW, g->credits_dark);
    g->title = load_tex(dir, "ui/title.tex");
    g->ankh = g->title ? load_tex(dir, "hud/menu_ankh.tex") : NULL;
    g->papyrus = g->ankh ? load_tex(dir, "ui/papyrus_panel.tex") : NULL;
    g->tally = g->papyrus ? load_tex(dir, "ui/tally_panel.tex") : NULL;
    remap_ramp(g->ink, "gold", "brown");
    if (g->tally) {
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
    bast_tex_free(g->papyrus);
    bast_tex_free(g->tally);
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

/* Tally layout on tally_panel (issue #156 hand-off): level name centred
 * on x=160 at y=27; labels at x=44, values right-aligned to x=279, lines
 * at y = 84 + 24*i; TIME right-aligned to x=163, PAR at x=192. A line's
 * value shows once its count has started. */
static void draw_tally(const tally_t *t, const flow_gfx *g, uint8_t *fb)
{
    static const char *const LABEL[TALLY_LINES] = { "KILLS", "ITEMS", "SECRETS", "TIME" };
    char buf[32];
    draw_patch(fb, g->tally, 0, 0);
    draw_text(fb, g->menu, 160 - text_width(g->menu, t->name) / 2, 27, t->name, NULL);
    int counting = 0; /* first line still counting */
    while (counting < TALLY_LINES && t->shown[counting] >= t->target[counting])
        counting++;
    for (int i = 0; i < TALLY_LINES; i++) {
        const int y = 84 + 24 * i;
        draw_text(fb, g->menu, 44, y, LABEL[i], NULL);
        if (i > counting)
            continue;
        switch (i) {
        case TALLY_KILLS:
        case TALLY_ITEMS:
            snprintf(buf, sizeof buf, "%d%%", (int)t->shown[i]);
            draw_text_right(fb, g->menu, 279, y, buf, NULL);
            break;
        case TALLY_SECRETS:
            snprintf(buf, sizeof buf, "%d/%d", (int)t->shown[i], (int)t->secret_total);
            draw_text_right(fb, g->menu, 279, y, buf, NULL);
            break;
        default:
            tally_format_time(t->shown[i], buf, sizeof buf);
            draw_text_right(fb, g->menu, 163, y, buf, NULL);
            draw_text(fb, g->menu, 192, y, "PAR", NULL);
            tally_format_time(t->par, buf, sizeof buf);
            draw_text_right(fb, g->menu, 279, y, buf, NULL);
            break;
        }
    }
}

/* Credits over the darkened title: the two title lines in the menu font,
 * the rest in the small font (the widest line is 42 characters), centred,
 * 14 px apart, starting at y=240 and moving up 1 px every 2 tics. */
static void draw_credits(const flow_t *f, const flow_gfx *g, uint8_t *fb)
{
    draw_patch_remap(fb, g->title, 0, 0, g->credits_dark);
    const int top = SCREEN_H - (int)(f->timer / 2);
    for (int i = 0; i < story_credits_lines; i++) {
        const font *ft = i < 2 ? g->menu : g->small;
        const int y = top + i * FLOW_CREDITS_LINE_H + (FLOW_CREDITS_LINE_H - ft->cell_h) / 2;
        if (y <= -FLOW_CREDITS_LINE_H || y >= SCREEN_H)
            continue;
        centred(fb, ft, y, story_credits[i]);
    }
}

void flow_draw(const flow_t *f, const flow_gfx *g, uint8_t *fb)
{
    const uint8_t *dark = g->dark;
    switch (f->screen) {
    case FLOW_PLAY:
        return;
    case FLOW_INTRO:
    case FLOW_STORY:
    case FLOW_ENDING: {
        static char text[2048];
        draw_patch(fb, g->papyrus, 0, 0);
        if (flow_page_text(f, text, sizeof text))
            draw_text(fb, g->menu, STORY_BOX_X, STORY_BOX_Y, text, g->ink);
        return;
    }
    case FLOW_TALLY:
        draw_tally(&f->tally, g, fb);
        return;
    case FLOW_CREDITS:
        draw_credits(f, g, fb);
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
