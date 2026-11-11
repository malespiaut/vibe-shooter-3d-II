#include "game/weapon.h"
#include <stddef.h>

#include "game/ai.h"
#include "game/enemy_info.h"
#include "game/projectile.h"
#include "game/sound.h"
#include "game/state.h"

/* Fire sound per slot (audio.md Player); Claws: claw_swish on a miss,
 * claw_hit when they connect. The Lance also plays lance_charge on the
 * fire tic, before lance_fire (audio.md names no other trigger). */
static const int FIRE_SFX[WP_SLOTS] = {
    [WP_CLAWS] = SFX_claw_swish,
    [WP_PISTOL] = SFX_pistol_fire,
    [WP_SCATTERGUN] = SFX_scatter_fire,
    [WP_REPEATER] = SFX_repeater_fire,
    [WP_LAUNCHER] = SFX_scarab_launch,
    [WP_LANCE] = SFX_lance_fire,
    [WP_HOWLER] = SFX_howler_roar,
};

/* weapons.md Stats. Damage base..base+spread; spread in angle units. */
const weapon_info_t weapon_info[WP_SLOTS] = {
    [WP_CLAWS] = { AM_NONE, 0, 14, 1, 8, 12, 0, 0 },
    [WP_PISTOL] = { AM_CHARGES, 1, 12, 1, 9, 5, 255, 1 },
    [WP_SCATTERGUN] = { AM_SHELLS, 1, 32, 7, 5, 4, 1019, 1 },
    [WP_REPEATER] = { AM_CHARGES, 1, 4, 1, 9, 5, 510, 1 },
    /* 5 spawns a scarab; 6 and 7 fire through fire_lance / fire_howler
     * (pellets unused). */
    [WP_LAUNCHER] = { AM_SCARABS, 1, 20, 0, 30, 20, 0, 0 },
    [WP_LANCE] = { AM_CELLS, 5, 40, 0, 100, 40, 0, 1 },
    [WP_HOWLER] = { AM_CELLS, 25, 50, 0, 80, 0, 0, 0 },
};

const int32_t ammo_max[AM_TYPES] = { 200, 50, 50, 300 };

/* Ammo given by a weapon pickup, per slot (weapons.md; the pistol, not in
 * that list, gives a Charge Vial's 10). */
static const int32_t PICKUP_AMMO[WP_SLOTS] = { 0, 0, 10, 8, 40, 4, 60, 50 };

int32_t ammo_cap(int a, int satchel)
{
    return ammo_max[a] * (satchel ? 2 : 1);
}

int weapon_add_ammo(weapons_t *w, int a, int32_t n, int kitten, int satchel)
{
    const int32_t cap = ammo_cap(a, satchel);
    if (w->ammo[a] >= cap)
        return 0;
    w->ammo[a] += kitten ? n * 3 / 2 : n;
    if (w->ammo[a] > cap)
        w->ammo[a] = cap;
    return 1;
}

int weapon_give_ex(weapons_t *w, int wp, int kitten, int satchel)
{
    const weapon_info_t *wi = &weapon_info[wp];
    const int have = (w->owned >> wp) & 1;
    int took = 0;
    if (wi->ammo_type != AM_NONE)
        took = weapon_add_ammo(w, wi->ammo_type, PICKUP_AMMO[wp], kitten, satchel);
    if (!have) {
        w->owned |= (uint8_t)(1u << wp);
        took = 1;
    }
    return took;
}

int weapon_give(weapons_t *w, int wp)
{
    return weapon_give_ex(w, wp, 0, 0);
}

void weapons_init(weapons_t *w)
{
    *w = (weapons_t){ 0 };
    w->owned = (1u << WP_CLAWS) | (1u << WP_PISTOL);
    w->ready = WP_PISTOL;
    w->ammo[AM_CHARGES] = PLAYER_START_CHARGES;
}

int weapon_has_ammo(const weapons_t *w, int wp)
{
    const weapon_info_t *wi = &weapon_info[wp];
    return wi->ammo_type == AM_NONE || w->ammo[wi->ammo_type] >= wi->ammo_use;
}

int weapon_auto_pick(const weapons_t *w)
{
    for (int wp = WP_SLOTS - 1; wp >= WP_CLAWS; wp--) {
        if (wp == WP_LAUNCHER || wp == WP_HOWLER)
            continue;
        if ((w->owned & (1u << wp)) && weapon_has_ammo(w, wp))
            return wp;
    }
    return WP_CLAWS;
}

void weapon_select(weapons_t *w, int wp)
{
    if (wp < WP_CLAWS || wp >= WP_SLOTS || !(w->owned & (1u << wp)))
        return;
    if (w->switch_tics ? wp == w->pending : wp == w->ready)
        return;
    w->pending = (uint8_t)wp;
    w->switch_tics = WEAPON_SWITCH_TICS;
}

/* Distance along the unit ray (dx, dy) from (x, y) to the first blocking
 * tile, or `range` if none is reached within it. Grid DDA, fixed point. */
fix_t game_trace_wall(const struct game_state *gs, fix_t x, fix_t y, fix_t dx, fix_t dy,
                      fix_t range)
{
    int tx = fix_to_int(x), ty = fix_to_int(y);
    if (game_tile_blocks(gs, tx, ty))
        return 0;
    const int stepx = dx > 0 ? 1 : -1, stepy = dy > 0 ? 1 : -1;
    const int64_t BIG = INT64_MAX / 4;
    const int64_t adx = dx < 0 ? -(int64_t)dx : dx, ady = dy < 0 ? -(int64_t)dy : dy;
    /* Distances along the ray, 16.16 in int64. */
    int64_t deltax = adx ? ((int64_t)FIX_ONE << 16) / adx : BIG;
    int64_t deltay = ady ? ((int64_t)FIX_ONE << 16) / ady : BIG;
    int64_t sidex, sidey;
    if (!adx)
        sidex = BIG;
    else if (dx > 0)
        sidex = (((int64_t)fix_from_int(tx + 1) - x) << 16) / adx;
    else
        sidex = (((int64_t)x - fix_from_int(tx)) << 16) / adx;
    if (!ady)
        sidey = BIG;
    else if (dy > 0)
        sidey = (((int64_t)fix_from_int(ty + 1) - y) << 16) / ady;
    else
        sidey = (((int64_t)y - fix_from_int(ty)) << 16) / ady;

    for (;;) {
        int64_t dist;
        if (sidex < sidey) {
            dist = sidex;
            sidex += deltax;
            tx += stepx;
        } else {
            dist = sidey;
            sidey += deltay;
            ty += stepy;
        }
        if (dist >= range)
            return range;
        if (game_tile_blocks(gs, tx, ty))
            return (fix_t)dist;
    }
}

int weapon_trace_multi(struct game_state *gs, fix_t x, fix_t y, angle_t a, fix_t range, int edge,
                       uint16_t *ids, int max, fix_t *wall_dist)
{
    const fix_t dx = fine_cos(a), dy = fine_sin(a);
    const fix_t wall = game_trace_wall(gs, x, y, dx, dy, HITSCAN_RANGE);
    if (wall_dist)
        *wall_dist = wall < range ? wall : range;

    /* ids[0..n) sorted by distance; equal distances keep creation order. */
    int64_t along_of[WEAPON_PIERCE_MAX];
    int n = 0;
    if (max > WEAPON_PIERCE_MAX)
        max = WEAPON_PIERCE_MAX;
    for (int i = 0; i < gs->things.count; i++) {
        const thinker_t *t = &gs->things.list[i];
        if (!thinker_alive(t) || !(t->flags & TH_FLAG_SHOOTABLE))
            continue;
        const int64_t ox = (int64_t)t->x - x, oy = (int64_t)t->y - y;
        const int64_t along = (ox * dx + oy * dy) / FIX_ONE; /* projection, 16.16 */
        if (along <= 0 || along >= wall)
            continue;
        if ((edge ? along - t->radius : along) > range)
            continue;
        int64_t perp = (ox * dy - oy * dx) / FIX_ONE;
        if (perp < 0)
            perp = -perp;
        if (perp >= t->radius)
            continue;
        int k = n;
        while (k > 0 && along < along_of[k - 1])
            k--;
        if (k >= max)
            continue;
        if (n < max)
            n++;
        for (int j = n - 1; j > k; j--) {
            ids[j] = ids[j - 1];
            along_of[j] = along_of[j - 1];
        }
        ids[k] = (uint16_t)(i + 1);
        along_of[k] = along;
    }
    return n;
}

uint16_t weapon_trace(struct game_state *gs, fix_t x, fix_t y, angle_t a, fix_t range, int edge,
                      fix_t *wall_dist)
{
    uint16_t id = 0;
    weapon_trace_multi(gs, x, y, a, range, edge, &id, 1, wall_dist);
    return id;
}

int weapon_howler_targets(struct game_state *gs, uint16_t *ids, int max)
{
    const player_t *p = &gs->player;
    int n = 0;
    for (int i = 0; i < gs->things.count && n < max; i++) {
        const thinker_t *t = &gs->things.list[i];
        if (!thinker_alive(t) || !(t->flags & TH_FLAG_SHOOTABLE))
            continue;
        const fix_t ox = t->x - p->x, oy = t->y - p->y;
        const fix_t d = fix_dist(ox, oy);
        if (d > HOWLER_RANGE)
            continue;
        if (d > 0) { /* a thinker on the player's centre is always in the cone */
            const angle_t a = angle_of(ox, oy);
            const int16_t off = (int16_t)(a - p->angle);
            if (off > (int32_t)HOWLER_HALF_CONE || off < -(int32_t)HOWLER_HALF_CONE)
                continue;
            if (game_trace_wall(gs, p->x, p->y, fine_cos(a), fine_sin(a), d) < d)
                continue;
        }
        ids[n++] = (uint16_t)(i + 1);
    }
    return n;
}

static int32_t roll_damage(game_state *gs, const weapon_info_t *wi)
{
    return wi->dmg_base + (int32_t)rng_range(&gs->rng, (uint32_t)wi->dmg_spread + 1);
}

/* Eye of Ra Lance: one beam, the WEAPON_PIERCE_MAX nearest thinkers it
 * crosses before the first wall, nearest first, one damage roll each. */
static void fire_lance(game_state *gs, const weapon_info_t *wi)
{
    const player_t *p = &gs->player;
    uint16_t ids[WEAPON_PIERCE_MAX];
    const int n = weapon_trace_multi(gs, p->x, p->y, p->angle, HITSCAN_RANGE, 0, ids,
                                     WEAPON_PIERCE_MAX, NULL);
    for (int i = 0; i < n; i++)
        ai_damage(gs, thinker_get(&gs->things, ids[i]), roll_damage(gs, wi));
}

/* Sphinx Howler: every target of weapon_howler_targets, in creation order:
 * damage, then (if it lives) knockback 1 u straight away from the player
 * and a forced flinch. Only enemies flinch or move: a kibble barrel (no
 * enemy_info) just takes the damage, and 80 always lights its fuse
 * (BARREL_HP 20). Never touches the player. */
static void fire_howler(game_state *gs, const weapon_info_t *wi)
{
    const player_t *p = &gs->player;
    uint16_t ids[MAX_THINKERS];
    const int n = weapon_howler_targets(gs, ids, MAX_THINKERS);
    for (int i = 0; i < n; i++) {
        thinker_t *t = thinker_get(&gs->things, ids[i]);
        t->howler_hit = 1;
        if (ai_damage_pain(gs, t, roll_damage(gs, wi), HOWLER_PAIN_TICS) || !enemy_info(t->type))
            continue;
        const fix_t ox = t->x - p->x, oy = t->y - p->y;
        const angle_t a = (ox || oy) ? angle_of(ox, oy) : p->angle;
        ai_push(gs, t, fix_mul(fine_cos(a), HOWLER_KNOCKBACK), fix_mul(fine_sin(a), HOWLER_KNOCKBACK));
    }
}

/* Horizontal offset in -spread..spread from the gameplay RNG. */
static angle_t spread_offset(game_state *gs, angle_t spread)
{
    if (!spread)
        return 0;
    return (angle_t)((int32_t)rng_range(&gs->rng, 2u * spread + 1) - spread);
}

static void fire_weapon(game_state *gs)
{
    weapons_t *w = &gs->player.weapons;
    const int wp = w->ready;
    const weapon_info_t *wi = &weapon_info[wp];
    if (wi->ammo_type != AM_NONE)
        w->ammo[wi->ammo_type] -= wi->ammo_use;
    w->refire_tics = wi->interval;
    w->shots++;
    if (wi->flash)
        w->flash_tics = WEAPON_FLASH_TICS;

    if (wp == WP_LANCE) {
        game_sound_local(gs, SFX_lance_charge);
        game_sound_local(gs, FIRE_SFX[wp]);
        fire_lance(gs, wi);
        return;
    }
    if (wp == WP_HOWLER) {
        game_sound_local(gs, FIRE_SFX[wp]);
        fire_howler(gs, wi);
        return;
    }
    const player_t *p = &gs->player;
    if (wp == WP_LAUNCHER) { /* one scarab from the eye */
        projectile_spawn(gs, TH_TYPE_SCARAB_PROJ, TH_ID_PLAYER, p->x, p->y, PLAYER_EYE_Z, p->angle);
        game_sound_local(gs, FIRE_SFX[wp]);
        return;
    }
    const fix_t range = wp == WP_CLAWS ? CLAW_REACH : HITSCAN_RANGE;
    int hit = 0;
    for (int i = 0; i < wi->pellets; i++) {
        angle_t a = (angle_t)(p->angle + spread_offset(gs, wi->spread));
        uint16_t id = weapon_trace(gs, p->x, p->y, a, range, wp == WP_CLAWS, NULL);
        thinker_t *t = thinker_get(&gs->things, id);
        if (t) {
            ai_damage(gs, t, roll_damage(gs, wi));
            hit = 1;
        }
    }
    if (FIRE_SFX[wp])
        game_sound_local(gs, wp == WP_CLAWS && hit ? SFX_claw_hit : FIRE_SFX[wp]);
}

/* weapon_select, with weapon_switch when a switch actually starts. */
static void select_sound(game_state *gs, int wp)
{
    weapons_t *w = &gs->player.weapons;
    const uint8_t pending = w->pending, tics = w->switch_tics;
    weapon_select(w, wp);
    if (w->pending != pending || w->switch_tics != tics)
        game_sound_local(gs, SFX_weapon_switch);
}

void weapon_tic(struct game_state *gs, int select_slot, int fire)
{
    weapons_t *w = &gs->player.weapons;
    player_t *p = &gs->player;

    if (select_slot)
        select_sound(gs, select_slot);

    /* View bob: the phase advances with the player's speed (a full cycle
     * per ~1 s at walk speed). */
    const fix_t speed = fix_dist(p->momx, p->momy);
    w->bob_phase = (angle_t)(w->bob_phase + (uint16_t)((int64_t)speed * 1872 / PLAYER_WALK_SPEED));

    if (w->flash_tics)
        w->flash_tics--;
    if (w->refire_tics) {
        w->refire_tics--;
        /* Scattergun: fire x2, pump x2 (weapons.md), pump half way. */
        if (w->ready == WP_SCATTERGUN && w->refire_tics == weapon_info[WP_SCATTERGUN].interval / 2)
            game_sound_local(gs, SFX_scatter_pump);
    }
    if (w->switch_tics) {
        w->switch_tics--;
        if (w->switch_tics == WEAPON_SWITCH_TICS / 2) { /* lowered: raise the new one */
            w->ready = w->pending;
            w->refire_tics = 0;
        }
        if (w->switch_tics == 0)
            w->pending = WP_NONE;
        return; /* fire blocked while switching */
    }

    if (!weapon_has_ammo(w, w->ready)) {
        select_sound(gs, weapon_auto_pick(w));
        return;
    }
    if (fire && w->refire_tics == 0) {
        fire_weapon(gs);
        if (!weapon_has_ammo(w, w->ready))
            select_sound(gs, weapon_auto_pick(w));
    }
}
