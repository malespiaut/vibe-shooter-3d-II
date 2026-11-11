#include "game/state.h"

#include <string.h>

#include "engine/hash.h"

void game_init(game_state *gs, const bast_map *map, int level, uint32_t demo_seed)
{
    memset(gs, 0, sizeof *gs);
    gs->map = map;
    rng_seed_level(&gs->rng, level, demo_seed);

    player_t *p = &gs->player;
    p->x = fix_from_int(map->width) / 2;
    p->y = fix_from_int(map->height) / 2;
    for (int i = 0; i < map->entity_count; i++) {
        const bast_map_entity *e = &map->entities[i];
        if (e->type == BAST_ENT_PLAYER_START) {
            p->x = e->x;
            p->y = e->y;
            p->angle = e->angle;
            break;
        }
    }
    p->health = PLAYER_START_HEALTH;
    p->armour = 0;
}

int game_tile_blocks(const bast_map *map, int tx, int ty)
{
    if (tx < 0 || ty < 0 || tx >= map->width || ty >= map->height)
        return 1;
    uint16_t f = map->cells[ty * map->width + tx].flags;
    if (f & (BAST_CELL_SOLID | BAST_CELL_BLOCK_ALL | BAST_CELL_PUSHWALL))
        return 1;
    if ((f & BAST_CELL_DOOR) && !(f & BAST_CELL_START_OPEN))
        return 1;
    return 0;
}

static fix_t clamp_fix(fix_t v, fix_t lo, fix_t hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

int game_circle_blocked(const bast_map *map, fix_t x, fix_t y, fix_t r)
{
    const int64_t r2 = (int64_t)r * r;
    for (int ty = fix_to_int(y - r); ty <= fix_to_int(y + r); ty++) {
        for (int tx = fix_to_int(x - r); tx <= fix_to_int(x + r); tx++) {
            if (!game_tile_blocks(map, tx, ty))
                continue;
            /* Closest point of the tile square to the centre. */
            fix_t x0 = fix_from_int(tx), y0 = fix_from_int(ty);
            int64_t dx = (int64_t)x - clamp_fix(x, x0, x0 + FIX_ONE);
            int64_t dy = (int64_t)y - clamp_fix(y, y0, y0 + FIX_ONE);
            if (dx * dx + dy * dy < r2)
                return 1;
        }
    }
    return 0;
}

/* Moves *pos by `delta` along one axis, stopping at the last fixed unit
 * where the player circle does not overlap a blocking tile. Returns 1 when
 * the move was cut short (the caller then kills momentum on that axis). */
static int move_axis(const bast_map *map, fix_t *x, fix_t *y, fix_t delta, int axis_y)
{
    if (delta == 0)
        return 0;
    fix_t *pos = axis_y ? y : x;
    const fix_t start = *pos;
    *pos = start + delta;
    if (!game_circle_blocked(map, *x, *y, PLAYER_RADIUS))
        return 0;

    /* Binary search the largest free step in [0, |delta|). Speeds are below
     * the radius, so nothing between start and the target is skipped. */
    const fix_t sign = delta < 0 ? -1 : 1;
    fix_t lo = 0, hi = fix_abs(delta);
    while (hi - lo > 1) {
        fix_t mid = lo + (hi - lo) / 2;
        *pos = start + sign * mid;
        if (game_circle_blocked(map, *x, *y, PLAYER_RADIUS))
            hi = mid;
        else
            lo = mid;
    }
    *pos = start + sign * lo;
    return 1;
}

/* Scales v by num/den through int64, truncating toward zero. */
static fix_t scale(fix_t v, int64_t num, int64_t den)
{
    return (fix_t)((int64_t)v * num / den);
}

static int8_t clamp_cmd(int8_t v)
{
    return v < -TICCMD_MAX ? -TICCMD_MAX : v;
}

static void player_thrust(player_t *p, const ticcmd *cmd)
{
    const fix_t cap = (cmd->buttons & BT_RUN) ? PLAYER_RUN_SPEED : PLAYER_WALK_SPEED;
    const fix_t f = scale(cap, clamp_cmd(cmd->fwd), TICCMD_MAX);
    const fix_t s = scale(cap, clamp_cmd(cmd->side), TICCMD_MAX);
    const fix_t c = fine_cos(p->angle), sn = fine_sin(p->angle);

    /* Forward is (cos, sin); right is (sin, -cos). */
    fix_t wantx = fix_mul(f, c) + fix_mul(s, sn);
    fix_t wanty = fix_mul(f, sn) - fix_mul(s, c);
    fix_t want = fix_dist(wantx, wanty);
    if (want > cap) { /* no faster diagonal */
        wantx = scale(wantx, cap, want);
        wanty = scale(wanty, cap, want);
        want = cap;
    }

    /* Velocity moves toward the wanted velocity by at most `step` per tic:
     * speeding up reaches the cap in PLAYER_ACCEL_TICS; slowing down uses the
     * run step, so friction stops the player within PLAYER_ACCEL_TICS from
     * any speed. */
    const fix_t speed = fix_dist(p->momx, p->momy);
    const fix_t base = want >= speed ? cap : PLAYER_RUN_SPEED;
    const fix_t step = (base + PLAYER_ACCEL_TICS - 1) / PLAYER_ACCEL_TICS;

    const fix_t dx = wantx - p->momx, dy = wanty - p->momy;
    const fix_t dlen = fix_dist(dx, dy);
    if (dlen <= step) {
        p->momx = wantx;
        p->momy = wanty;
    } else {
        p->momx += scale(dx, step, dlen);
        p->momy += scale(dy, step, dlen);
    }
}

void game_tic(game_state *gs, const ticcmd *cmd)
{
    player_t *p = &gs->player;

    p->angle = (angle_t)(p->angle + (uint16_t)cmd->turn);
    player_thrust(p, cmd);

    /* Axis-separated slide: x first, then y. */
    if (move_axis(gs->map, &p->x, &p->y, p->momx, 0))
        p->momx = 0;
    if (move_axis(gs->map, &p->x, &p->y, p->momy, 1))
        p->momy = 0;

    gs->tic++;
}

uint64_t game_hash(const game_state *gs)
{
    hash_t h;
    hash_init(&h);
    hash_u32(&h, gs->tic);
    hash_u32(&h, gs->rng.state);

    const player_t *p = &gs->player;
    hash_i32(&h, p->x);
    hash_i32(&h, p->y);
    hash_u32(&h, p->angle);
    hash_i32(&h, p->momx);
    hash_i32(&h, p->momy);
    hash_i32(&h, p->health);
    hash_i32(&h, p->armour);
    return h.value;
}
