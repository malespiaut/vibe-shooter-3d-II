#include "game/projectile.h"

#include <stddef.h>

#include "game/ai.h"
#include "game/items.h"
#include "game/sound.h"
#include "game/state.h"

/* weapons.md: Scarab Launcher, 14 u/s, direct 30..50, splash 96 r1.5.
 * Radius 0.125 u: the 12x12 px scarab_proj sprite is 0.1875 u across. */
static const projectile_info_t SCARAB = { 26214, 8192, 30, 20, 96, SPLASH_RADIUS_1_5,
                                          TH_TYPE_EXPLOSION_SMALL, SFX_scarab_explode };

/* enemies.md Pomeranian: 8 u/s, 8..16, no splash. Radius as the scarab's
 * (same 12x12 px sprite). */
static const projectile_info_t PLASMA = { 14980, 8192, 8, 8, 0, 0, TH_TYPE_PLASMA_HIT, SFX_plasma_hit };

/* enemies.md Mastiff Juggernaut: bone missile 6 u/s (11235), direct 20..40,
 * splash 32 r1.0. "Slow and big": radius 0.25 u, twice the scarab's. */
static const projectile_info_t BONE = { 11235, 16384, 20, 20, 32, SPLASH_RADIUS_1_0,
                                        TH_TYPE_EXPLOSION_SMALL, SFX_bone_explode };

const projectile_info_t *projectile_info(uint16_t type)
{
    switch (type) {
    case TH_TYPE_SCARAB_PROJ:
        return &SCARAB;
    case TH_TYPE_PLASMA_BOLT:
        return &PLASMA;
    case TH_TYPE_BONE_PROJ:
        return &BONE;
    default:
        return NULL;
    }
}

int32_t fx_tics(uint16_t fx)
{
    /* Frame counts of assets/src/fx/{explosion_small,explosion_big,
     * plasma_hit}.json. */
    switch (fx) {
    case TH_TYPE_PLASMA_HIT:
        return 5 * FX_TICS_PER_FRAME;
    case TH_TYPE_EXPLOSION_SMALL:
        return 8 * FX_TICS_PER_FRAME;
    case TH_TYPE_EXPLOSION_BIG:
        return 10 * FX_TICS_PER_FRAME;
    default:
        return 0;
    }
}

uint16_t projectile_spawn(game_state *gs, uint16_t type, uint16_t owner, fix_t x, fix_t y, fix_t z,
                          angle_t angle)
{
    const projectile_info_t *pi = projectile_info(type);
    if (!pi)
        return 0;
    const uint16_t id = thinker_alloc(&gs->things);
    thinker_t *t = thinker_get(&gs->things, id);
    if (!t)
        return 0;
    t->type = type;
    t->state = TH_STATE_STAND;
    t->x = x;
    t->y = y;
    t->z = z;
    t->angle = angle;
    t->radius = pi->radius;
    t->momx = fix_mul(pi->speed, fine_cos(angle));
    t->momy = fix_mul(pi->speed, fine_sin(angle));
    t->owner = owner;
    return id;
}

uint16_t reticle_spawn(game_state *gs, uint16_t owner, fix_t x, fix_t y)
{
    const uint16_t id = thinker_alloc(&gs->things);
    thinker_t *t = thinker_get(&gs->things, id);
    if (!t)
        return 0;
    t->type = TH_TYPE_RETICLE;
    t->state = TH_STATE_STAND;
    t->x = x;
    t->y = y;
    t->tics = MORTAR_DELAY_TICS;
    t->owner = owner;
    game_sound(gs, SFX_reticle_beep, x, y);
    return id;
}

/* 1 when circles (ax, ay, ar) and (bx, by, br) overlap (touching is not). */
static int circles_overlap(fix_t ax, fix_t ay, fix_t ar, fix_t bx, fix_t by, fix_t br)
{
    const int64_t dx = (int64_t)ax - bx, dy = (int64_t)ay - by, r = (int64_t)ar + br;
    return dx * dx + dy * dy < r * r;
}

/* 1 when no blocking tile lies on the segment between the two points. */
static int clear_line(const game_state *gs, fix_t x0, fix_t y0, fix_t x1, fix_t y1, fix_t d)
{
    if (d == 0)
        return !game_tile_blocks(gs, fix_to_int(x0), fix_to_int(y0));
    const fix_t ux = (fix_t)(((int64_t)x1 - x0) * FIX_ONE / d);
    const fix_t uy = (fix_t)(((int64_t)y1 - y0) * FIX_ONE / d);
    return game_trace_wall(gs, x0, y0, ux, uy, d) >= d;
}

/* Splash amount at distance from the centre, 0 outside the radius; *d gets
 * the distance. */
static int32_t splash_at(fix_t x, fix_t y, fix_t tx, fix_t ty, int32_t amount, fix_t radius, fix_t *d)
{
    const int64_t dx = (int64_t)tx - x, dy = (int64_t)ty - y;
    if (dx >= radius || -dx >= radius || dy >= radius || -dy >= radius)
        return 0;
    *d = (fix_t)isqrt64((uint64_t)(dx * dx + dy * dy));
    if (*d >= radius)
        return 0;
    return (int32_t)((int64_t)amount * (radius - *d) / radius);
}

void splash_damage(game_state *gs, fix_t x, fix_t y, int32_t amount, fix_t radius,
                   uint16_t source_id)
{
    thinkers_t *th = &gs->things;
    fix_t d;
    for (int i = 0; i < th->count; i++) {
        thinker_t *t = &th->list[i];
        if (i + 1 == source_id || !thinker_alive(t) || !(t->flags & TH_FLAG_SHOOTABLE))
            continue;
        const int32_t dmg = splash_at(x, y, t->x, t->y, amount, radius, &d);
        if (dmg > 0 && clear_line(gs, x, y, t->x, t->y, d))
            ai_damage_from(gs, t, dmg, DMG_SPLASH, x, y);
    }
    player_t *p = &gs->player;
    if (p->dead)
        return;
    const int32_t dmg = splash_at(x, y, p->x, p->y, amount, radius, &d);
    if (dmg > 0 && clear_line(gs, x, y, p->x, p->y, d))
        player_damage(gs, dmg);
}

static int32_t roll(game_state *gs, int32_t base, int32_t spread)
{
    return base + (int32_t)rng_range(&gs->rng, (uint32_t)spread + 1);
}

/* Moves projectile `t` (id `id`) one tic; on impact deals the damage and
 * turns it into its effect at the impact point. */
static void projectile_move(game_state *gs, thinker_t *t, uint16_t id)
{
    const projectile_info_t *pi = projectile_info(t->type);
    const fix_t x0 = t->x, y0 = t->y;
    thinker_t *hit = NULL;
    int hit_player = 0, hit_wall = 0;
    for (int s = 1; s <= PROJ_SUBSTEPS && !hit && !hit_player && !hit_wall; s++) {
        const fix_t nx = x0 + (fix_t)((int64_t)t->momx * s / PROJ_SUBSTEPS);
        const fix_t ny = y0 + (fix_t)((int64_t)t->momy * s / PROJ_SUBSTEPS);
        if (game_circle_blocked(gs, nx, ny, t->radius)) {
            hit_wall = 1; /* explode at the last free position */
            break;
        }
        t->x = nx;
        t->y = ny;
        for (int i = 0; i < gs->things.count && !hit; i++) {
            thinker_t *o = &gs->things.list[i];
            if (i + 1 != t->owner && thinker_alive(o) && (o->flags & TH_FLAG_SHOOTABLE) &&
                circles_overlap(nx, ny, t->radius, o->x, o->y, o->radius))
                hit = o;
        }
        if (!hit && t->owner != TH_ID_PLAYER && !gs->player.dead &&
            circles_overlap(nx, ny, t->radius, gs->player.x, gs->player.y, PLAYER_RADIUS))
            hit_player = 1;
    }
    if (!hit && !hit_player && !hit_wall)
        return;

    if (hit) /* it came from where it was one tic ago */
        ai_damage_from(gs, hit, roll(gs, pi->dmg_base, pi->dmg_spread), DMG_DIRECT, x0 - t->momx,
                       y0 - t->momy);
    else if (hit_player)
        player_damage(gs, roll(gs, pi->dmg_base, pi->dmg_spread));
    if (pi->splash)
        splash_damage(gs, t->x, t->y, pi->splash, pi->splash_radius, id);
    game_sound(gs, pi->sfx, t->x, t->y);

    t->type = pi->fx;
    t->tics = fx_tics(pi->fx);
    t->momx = t->momy = 0;
}

void projectiles_tic(game_state *gs)
{
    thinkers_t *th = &gs->things;
    uint16_t boom[MAX_THINKERS];
    int nboom = 0;
    const int n = th->count;

    for (int i = 0; i < n; i++) {
        thinker_t *t = &th->list[i];
        if (t->state == TH_STATE_DYING && --t->tics <= 0) {
            t->state = TH_STATE_DEAD;
            t->tics = 0;
            boom[nboom++] = (uint16_t)(i + 1);
        }
    }

    for (int i = 0; i < n; i++) {
        thinker_t *t = &th->list[i];
        if (t->state != TH_STATE_STAND)
            continue;
        if (projectile_info(t->type)) {
            projectile_move(gs, t, (uint16_t)(i + 1));
        } else if (t->type == TH_TYPE_RETICLE) {
            if (--t->tics <= 0) { /* the shell lands on the mark */
                game_sound(gs, SFX_mortar_land, t->x, t->y);
                splash_damage(gs, t->x, t->y, MORTAR_SPLASH, SPLASH_RADIUS_1_5, (uint16_t)(i + 1));
                t->type = TH_TYPE_EXPLOSION_SMALL;
                t->tics = fx_tics(TH_TYPE_EXPLOSION_SMALL);
            } else if ((MORTAR_DELAY_TICS - t->tics) % RETICLE_BEEP_TICS == 0) {
                game_sound(gs, SFX_reticle_beep, t->x, t->y); /* first beep at spawn */
            }
        } else if (fx_tics(t->type)) {
            if (--t->tics <= 0)
                t->state = TH_STATE_FREE;
        }
    }

    for (int i = 0; i < nboom; i++) {
        const thinker_t *b = thinker_get(th, boom[i]);
        const fix_t x = b->x, y = b->y;
        thinker_t *fx = thinker_get(th, thinker_alloc(th));
        if (fx) {
            fx->type = TH_TYPE_EXPLOSION_BIG;
            fx->state = TH_STATE_STAND;
            fx->x = x;
            fx->y = y;
            fx->tics = fx_tics(TH_TYPE_EXPLOSION_BIG);
        }
        game_sound(gs, SFX_barrel_explode, x, y);
        splash_damage(gs, x, y, BARREL_SPLASH, SPLASH_RADIUS_1_5, boom[i]);
    }
}
