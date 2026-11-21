#include "game/viewmodel.h"

#include <stdio.h>
#include <string.h>

#include "bast.h"
#include "engine/draw2d.h"
#include "engine/render.h"
#include "engine/video.h"

/* Fire sequences: (frame, tics) shown from the fire tic on, then idle.
 * Each covers the weapon's fire interval (weapon_info). The scattergun
 * pumps from e = 16, the tic its pump sound plays (weapon.c). Weapons 5-7
 * use the frame names of weapons.md for their art to come. */
typedef struct {
    const char *name;
    int tics;
} vm_step;

#define VM_MAX_STEPS 5

static const vm_step FRAMES[WP_SLOTS][VM_MAX_STEPS] = {
    [WP_CLAWS] = { { "swipe0", 3 }, { "swipe1", 3 }, { "swipe2", 4 } },             /* 14 */
    [WP_PISTOL] = { { "fire0", 2 }, { "fire1", 4 } },                                /* 12 */
    [WP_SCATTERGUN] = { { "fire0", 3 }, { "fire1", 5 }, { "idle", 8 }, { "pump0", 6 },
                        { "pump1", 6 } },                                            /* 32 */
    [WP_REPEATER] = { { "fire0", 2 }, { "fire1", 2 } },                              /* 4 */
    [WP_LAUNCHER] = { { "fire0", 3 }, { "fire1", 5 }, { "reload0", 8 } },            /* 20 */
    [WP_LANCE] = { { "fire0", 3 }, { "fire1", 6 }, { "charge0", 10 }, { "charge1", 10 } }, /* 40 */
    [WP_HOWLER] = { { "roar0", 4 }, { "roar1", 10 }, { "roar2", 16 } },              /* 50 */
};

/* Round-to-nearest of n * s / 65536 (s 16.16, may be negative). */
static int scale_round(int n, int64_t s)
{
    const int64_t v = (int64_t)n * s;
    return (int)(v >= 0 ? (v + 32768) >> 16 : -((-v + 32768) >> 16));
}

void viewmodel_bob(angle_t phase, fix_t speed, int *dx, int *dy)
{
    int64_t amp = speed < 0 ? 0 : speed >= PLAYER_WALK_SPEED
                                      ? FIX_ONE
                                      : ((int64_t)speed << 16) / PLAYER_WALK_SPEED;
    const int64_t sx = (int64_t)fine_sin(phase) * amp >> 16;
    const int64_t sy = ((int64_t)FIX_ONE + fine_sin((angle_t)(phase * 2))) * amp >> 16;
    *dx = scale_round(VM_BOB_X, sx);
    *dy = scale_round(VM_BOB_Y, sy);
}

int viewmodel_drop(int h, int switch_tics)
{
    const int half = WEAPON_SWITCH_TICS / 2;
    if (switch_tics <= 0)
        return 0;
    if (switch_tics > WEAPON_SWITCH_TICS)
        switch_tics = WEAPON_SWITCH_TICS;
    const int k = switch_tics > half ? WEAPON_SWITCH_TICS - switch_tics : switch_tics;
    return h * k / half;
}

int viewmodel_weapon(const weapons_t *w)
{
    return w->ready;
}

const char *viewmodel_frame(int wp, int elapsed)
{
    if (wp < WP_CLAWS || wp >= WP_SLOTS || elapsed < 0)
        return "idle";
    for (int i = 0; i < VM_MAX_STEPS && FRAMES[wp][i].name; i++) {
        if (elapsed < FRAMES[wp][i].tics)
            return FRAMES[wp][i].name;
        elapsed -= FRAMES[wp][i].tics;
    }
    return "idle";
}

const char *viewmodel_frame_of(const weapons_t *w)
{
    const int wp = viewmodel_weapon(w);
    if (wp < WP_CLAWS || wp >= WP_SLOTS || w->refire_tics == 0)
        return "idle";
    return viewmodel_frame(wp, weapon_info[wp].interval - w->refire_tics);
}

int viewmodel_light_shift(const weapons_t *w)
{
    return w->flash_tics ? -VM_FLASH_ROWS : 0;
}

/* ---- loading and drawing ---- */

#define VM_MAX_FRAMES (VM_MAX_STEPS + 1)

static struct {
    const char *name[WP_SLOTS][VM_MAX_FRAMES];
    bast_tex *tex[WP_SLOTS][VM_MAX_FRAMES];
} V;

static void add_name(int wp, const char *name)
{
    for (int i = 0; i < VM_MAX_FRAMES; i++) {
        if (V.name[wp][i] && strcmp(V.name[wp][i], name) == 0)
            return;
        if (!V.name[wp][i]) {
            V.name[wp][i] = name;
            return;
        }
    }
}

int viewmodel_init(const char *dir)
{
    viewmodel_shutdown();
    for (int wp = WP_CLAWS; wp < WP_SLOTS; wp++) {
        add_name(wp, "idle");
        for (int i = 0; i < VM_MAX_STEPS && FRAMES[wp][i].name; i++)
            add_name(wp, FRAMES[wp][i].name);
        int missing = 0;
        for (int i = 0; i < VM_MAX_FRAMES && V.name[wp][i]; i++) {
            char path[512];
            snprintf(path, sizeof path, "%s/viewmodels/vm%d_%s.tex", dir, wp, V.name[wp][i]);
            V.tex[wp][i] = bast_tex_load(path);
            missing += !V.tex[wp][i];
        }
        static uint8_t warned; /* bit wp: warned once per run, not per level */
        if (missing && !(warned & (1u << wp))) {
            warned |= (uint8_t)(1u << wp);
            fprintf(stderr, "viewmodel: weapon %d: %d frame(s) missing in %s/viewmodels, not drawn\n",
                    wp, missing, dir);
        }
    }
    return 1;
}

void viewmodel_shutdown(void)
{
    for (int wp = 0; wp < WP_SLOTS; wp++)
        for (int i = 0; i < VM_MAX_FRAMES; i++)
            bast_tex_free(V.tex[wp][i]);
    memset(&V, 0, sizeof V);
}

static const bast_tex *find(int wp, const char *name)
{
    if (wp < WP_CLAWS || wp >= WP_SLOTS)
        return NULL;
    for (int i = 0; i < VM_MAX_FRAMES && V.name[wp][i]; i++)
        if (strcmp(V.name[wp][i], name) == 0)
            return V.tex[wp][i];
    return NULL;
}

void viewmodel_draw(uint8_t *fb, const game_state *gs)
{
    const player_t *p = &gs->player;
    if (p->health <= 0)
        return;
    const weapons_t *w = &p->weapons;
    const bast_tex *t = find(viewmodel_weapon(w), viewmodel_frame_of(w));
    if (!t)
        return;
    int dx, dy;
    viewmodel_bob(w->bob_phase, fix_dist(p->momx, p->momy), &dx, &dy);
    const int x = VIEW_W / 2 + dx - t->origin_x;
    const int y = VIEW_H + dy + viewmodel_drop(t->height, w->switch_tics) - t->origin_y;
    const int h = VIEW_H - y < t->height ? VIEW_H - y : t->height;
    if (h > 0)
        draw_patch_region(fb, t, 0, 0, t->width, h, x, y, NULL);
}
