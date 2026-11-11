/* Cat face selection (hud.md "Cat face"). */
#include "game/hud_face.h"

#include <stdio.h>

static const char *const expr_name[FACE_EXPR_COUNT] = {
    "idle0", "idle1", "idle2", "hurtl", "hurtr", "hurtf",
    "bighit", "fury", "smug", "frenzy", "dead", "god",
};

int hud_face_tier(int32_t health)
{
    if (health <= 0)
        return 0;
    if (health >= 80)
        return 1;
    if (health >= 60)
        return 2;
    if (health >= 40)
        return 3;
    if (health >= 20)
        return 4;
    return 5;
}

int hud_face_hurt_dir(angle_t view, fix_t px, fix_t py, int has_src, fix_t sx, fix_t sy)
{
    if (!has_src || (sx == px && sy == py))
        return FACE_HURTF;
    const angle_t to_src = angle_of(sx - px, sy - py);
    const int16_t diff = (int16_t)(uint16_t)(to_src - view);
    if (diff >= -(int)ANG45 && diff <= (int)ANG45)
        return FACE_HURTF;
    return diff > 0 ? FACE_HURTL : FACE_HURTR;
}

static void next_glance_wait(hud_face *f)
{
    f->glance_wait =
        FACE_GLANCE_MIN + (int)rng_range(&f->rng, FACE_GLANCE_MAX - FACE_GLANCE_MIN + 1);
}

void hud_face_init(hud_face *f, uint32_t seed)
{
    f->tier = 1;
    f->expr = FACE_IDLE0;
    f->timed = -1;
    f->timed_tics = 0;
    f->fire_held = 0;
    f->glance = FACE_IDLE0;
    f->glance_tics = 0;
    rng_seed(&f->rng, seed);
    next_glance_wait(f);
}

static int timed_rank(int e)
{
    return e == FACE_BIGHIT ? 3 : (e >= FACE_HURTL && e <= FACE_HURTF) ? 2 : e == FACE_SMUG ? 1 : 0;
}

/* Starts timed expression `e` unless a stronger one is still showing. */
static void start_timed(hud_face *f, int e, int tics)
{
    if (f->timed >= 0 && f->timed_tics > 0 && timed_rank(f->timed) > timed_rank(e))
        return;
    f->timed = e;
    f->timed_tics = tics;
}

void hud_face_tic(hud_face *f, const hud_face_input *in)
{
    /* Timers age first, so an event of this tic shows for its full length. */
    if (f->timed_tics > 0 && --f->timed_tics == 0)
        f->timed = -1;
    f->fire_held = in->fire ? f->fire_held + 1 : 0;

    /* Idle glances run on their own clock from the render RNG. */
    if (f->glance_tics > 0) {
        if (--f->glance_tics == 0) {
            f->glance = FACE_IDLE0;
            next_glance_wait(f);
        }
    } else if (--f->glance_wait <= 0) {
        f->glance = rng_range(&f->rng, 2) ? FACE_IDLE2 : FACE_IDLE1;
        f->glance_tics = FACE_GLANCE_TICS;
    }

    if (in->smug)
        start_timed(f, FACE_SMUG, FACE_SMUG_TICS);
    if (in->hurt) {
        if (in->hurt_amount >= FACE_BIGHIT_DAMAGE)
            start_timed(f, FACE_BIGHIT, FACE_BIGHIT_TICS);
        else
            start_timed(f, in->hurt, FACE_HURT_TICS);
    }

    f->tier = hud_face_tier(in->health);
    if (in->dead || f->tier == 0) {
        f->tier = 0;
        f->expr = FACE_DEAD;
    } else if (in->god) {
        f->expr = FACE_GOD;
    } else if (f->timed >= 0) {
        f->expr = f->timed;
    } else if (f->fire_held >= FACE_FURY_TICS) {
        f->expr = FACE_FURY;
    } else if (in->frenzy) {
        f->expr = FACE_FRENZY;
    } else {
        f->expr = f->glance;
    }
}

void hud_face_name(const hud_face *f, char *buf, int size)
{
    if (f->expr == FACE_DEAD || f->expr == FACE_GOD)
        snprintf(buf, (size_t)size, "face_%s", expr_name[f->expr]);
    else
        snprintf(buf, (size_t)size, "face_t%d_%s", f->tier, expr_name[f->expr]);
}
