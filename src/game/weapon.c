#include "game/weapon.h"
#include <stddef.h>

#include "game/state.h"

/* weapons.md Stats. Damage base..base+spread; spread in angle units. */
const weapon_info_t weapon_info[WP_SLOTS] = {
    [WP_CLAWS] = { AM_NONE, 0, 14, 1, 8, 12, 0, 0 },
    [WP_PISTOL] = { AM_CHARGES, 1, 12, 1, 9, 5, 255, 1 },
    [WP_SCATTERGUN] = { AM_SHELLS, 1, 32, 7, 5, 4, 1019, 1 },
    [WP_REPEATER] = { AM_CHARGES, 1, 4, 1, 9, 5, 510, 1 },
    /* 5-7 are owned by later issues; listed for ammo and auto-switch. */
    [WP_LAUNCHER] = { AM_SCARABS, 1, 20, 0, 30, 20, 0, 0 },
    [WP_LANCE] = { AM_CELLS, 5, 40, 0, 100, 40, 0, 1 },
    [WP_HOWLER] = { AM_CELLS, 25, 50, 0, 80, 0, 0, 0 },
};

const int32_t ammo_max[AM_TYPES] = { 200, 50, 50, 300 };

/* Ammo given by a weapon pickup, per slot (weapons.md; the pistol, not in
 * that list, gives a Charge Vial's 10). */
static const int32_t PICKUP_AMMO[WP_SLOTS] = { 0, 0, 10, 8, 40, 4, 60, 50 };

int weapon_give(weapons_t *w, int wp)
{
    const weapon_info_t *wi = &weapon_info[wp];
    const int have = (w->owned >> wp) & 1;
    int took = 0;
    if (wi->ammo_type != AM_NONE && w->ammo[wi->ammo_type] < ammo_max[wi->ammo_type]) {
        int32_t a = w->ammo[wi->ammo_type] + PICKUP_AMMO[wp];
        w->ammo[wi->ammo_type] = a < ammo_max[wi->ammo_type] ? a : ammo_max[wi->ammo_type];
        took = 1;
    }
    if (!have) {
        w->owned |= (uint8_t)(1u << wp);
        took = 1;
    }
    return took;
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
static fix_t trace_wall(const struct game_state *gs, fix_t x, fix_t y, fix_t dx, fix_t dy,
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

uint16_t weapon_trace(struct game_state *gs, fix_t x, fix_t y, angle_t a, fix_t range, int edge,
                      fix_t *wall_dist)
{
    const fix_t dx = fine_cos(a), dy = fine_sin(a);
    const fix_t wall = trace_wall(gs, x, y, dx, dy, HITSCAN_RANGE);
    if (wall_dist)
        *wall_dist = wall < range ? wall : range;

    uint16_t best = 0;
    int64_t best_t = 0;
    for (int i = 0; i < gs->things.count; i++) {
        const thinker_t *t = &gs->things.list[i];
        if (t->state != TH_STATE_STAND || !(t->flags & TH_FLAG_SHOOTABLE))
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
        if (!best || along < best_t) {
            best = (uint16_t)(i + 1);
            best_t = along;
        }
    }
    return best;
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

    const player_t *p = &gs->player;
    const fix_t range = wp == WP_CLAWS ? CLAW_REACH : HITSCAN_RANGE;
    for (int i = 0; i < wi->pellets; i++) {
        angle_t a = (angle_t)(p->angle + spread_offset(gs, wi->spread));
        uint16_t id = weapon_trace(gs, p->x, p->y, a, range, wp == WP_CLAWS, NULL);
        thinker_t *t = thinker_get(&gs->things, id);
        if (t) {
            int32_t dmg = wi->dmg_base + (int32_t)rng_range(&gs->rng, (uint32_t)wi->dmg_spread + 1);
            thinker_damage(&gs->things, t, dmg);
        }
    }
}

void weapon_tic(struct game_state *gs, int select_slot, int fire)
{
    weapons_t *w = &gs->player.weapons;
    player_t *p = &gs->player;

    /* Weapon pickups within reach of the player centre. */
    for (int i = 0; i < gs->things.count; i++) {
        thinker_t *t = &gs->things.list[i];
        if (!(t->flags & TH_FLAG_PICKUP) || t->state != TH_STATE_STAND)
            continue;
        if (fix_dist(t->x - p->x, t->y - p->y) > PICKUP_RADIUS)
            continue;
        if (weapon_give(w, t->type - BAST_ENT_ANKH_PISTOL + WP_PISTOL))
            t->state = TH_STATE_TAKEN;
    }

    if (select_slot)
        weapon_select(w, select_slot);

    /* View bob: the phase advances with the player's speed (a full cycle
     * per ~1 s at walk speed). */
    const fix_t speed = fix_dist(p->momx, p->momy);
    w->bob_phase = (angle_t)(w->bob_phase + (uint16_t)((int64_t)speed * 1872 / PLAYER_WALK_SPEED));

    if (w->flash_tics)
        w->flash_tics--;
    if (w->refire_tics)
        w->refire_tics--;
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
        weapon_select(w, weapon_auto_pick(w));
        return;
    }
    if (fire && w->refire_tics == 0) {
        fire_weapon(gs);
        if (!weapon_has_ammo(w, w->ready))
            weapon_select(w, weapon_auto_pick(w));
    }
}
