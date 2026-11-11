#include "game/state.h"

#include <string.h>

#include "engine/hash.h"
#include "game/ai.h"
#include "game/items.h"
#include "game/projectile.h"

static void inventory_take(inventory_t *inv, const player_t *p)
{
    memset(inv, 0, sizeof *inv);
    inv->health = p->health;
    inv->armour = p->armour;
    inv->armour_class = p->armour_class;
    inv->satchel = p->satchel;
    inv->weapons_owned = p->weapons.owned;
    inv->weapon_ready = p->weapons.ready;
    for (int i = 0; i < AM_TYPES; i++)
        inv->ammo[i] = p->weapons.ammo[i];
}

static void inventory_give(player_t *p, const inventory_t *inv)
{
    p->health = inv->health;
    p->armour = inv->armour;
    p->armour_class = inv->armour_class;
    p->satchel = inv->satchel;
    weapons_init(&p->weapons); /* timers, switch and bob at rest */
    p->weapons.owned = (uint8_t)inv->weapons_owned;
    p->weapons.ready = (uint8_t)inv->weapon_ready;
    for (int i = 0; i < AM_TYPES; i++)
        p->weapons.ammo[i] = inv->ammo[i];
}

void game_init(game_state *gs, const bast_map *map, int level, int skill, uint32_t demo_seed)
{
    game_init_with(gs, map, level, skill, demo_seed, NULL);
}

void game_init_with(game_state *gs, const bast_map *map, int level, int skill,
                    uint32_t demo_seed, const inventory_t *inv)
{
    memset(gs, 0, sizeof *gs);
    gs->level = level;
    gs->demo_seed = demo_seed;
    gs->map = map;
    gs->skill = skill < 1 ? 1 : skill > 3 ? 3 : skill;
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
    weapons_init(&p->weapons);
    player_start_inventory(p);
    if (inv)
        inventory_give(p, inv);
    inventory_take(&gs->entry, p);
    thinkers_spawn_map(&gs->things, map, gs->skill);
    doors_init(gs);
    items_spawn(gs);
    tags_init(gs);
}

void game_restart(game_state *gs)
{
    const inventory_t inv = gs->entry;
    const uint32_t restarts = gs->restarts;
    game_init_with(gs, gs->map, gs->level, gs->skill, gs->demo_seed, &inv);
    gs->restarts = restarts + 1;
}

fix_t game_eye_height(const game_state *gs)
{
    const player_t *p = &gs->player;
    if (!p->dead)
        return PLAYER_EYE_HEIGHT;
    int32_t t = p->death_tics > DEATH_VIEW_TICS ? DEATH_VIEW_TICS : p->death_tics;
    return PLAYER_EYE_HEIGHT -
           (PLAYER_EYE_HEIGHT - PLAYER_DEAD_EYE_HEIGHT) * t / DEATH_VIEW_TICS;
}

int game_restart_ready(const game_state *gs)
{
    return gs->player.dead && gs->player.death_tics >= DEATH_VIEW_TICS;
}

/* A dead player's tic: no input but use, which restarts the level once the
 * death view is over. The body slides to a stop (no thrust), and the world
 * (doors, pushwalls, projectiles, enemies) keeps running. Returns 1 when it
 * restarted. */
static int dead_tic(game_state *gs, const ticcmd *cmd)
{
    player_t *p = &gs->player;
    const int use_edge = (cmd->buttons & BT_USE) && !(gs->prev_buttons & BT_USE);
    if (use_edge && game_restart_ready(gs)) {
        game_restart(gs);
        return 1;
    }
    gs->prev_buttons = cmd->buttons;
    if (p->death_tics < DEATH_VIEW_TICS)
        p->death_tics++;
    return 0;
}

void game_give_keys(game_state *gs, uint32_t keys)
{
    gs->player.keys |= keys & (KEY_GREEN | KEY_BLUE | KEY_VIOLET);
}

int game_tile_blocks(const game_state *gs, int tx, int ty)
{
    const bast_map *map = gs->map;
    if (tx < 0 || ty < 0 || tx >= map->width || ty >= map->height)
        return 1;
    uint16_t f = map->cells[ty * map->width + tx].flags;
    if (f & (BAST_CELL_SOLID | BAST_CELL_BLOCK_ALL))
        return 1;
    if (pushwall_blocks(gs, tx, ty))
        return 1;
    if (f & BAST_CELL_DOOR) {
        int d = door_at(gs, tx, ty);
        return d < 0 || !door_passable(&gs->doors[d]);
    }
    return 0;
}

static fix_t clamp_fix(fix_t v, fix_t lo, fix_t hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

int game_circle_overlaps_tile(fix_t x, fix_t y, fix_t r, int tx, int ty)
{
    /* Closest point of the tile square to the centre. */
    fix_t x0 = fix_from_int(tx), y0 = fix_from_int(ty);
    int64_t dx = (int64_t)x - clamp_fix(x, x0, x0 + FIX_ONE);
    int64_t dy = (int64_t)y - clamp_fix(y, y0, y0 + FIX_ONE);
    return dx * dx + dy * dy < (int64_t)r * r;
}

int game_circle_blocked(const game_state *gs, fix_t x, fix_t y, fix_t r)
{
    for (int ty = fix_to_int(y - r); ty <= fix_to_int(y + r); ty++)
        for (int tx = fix_to_int(x - r); tx <= fix_to_int(x + r); tx++)
            if (game_tile_blocks(gs, tx, ty) && game_circle_overlaps_tile(x, y, r, tx, ty))
                return 1;
    return 0;
}

/* A live enemy or barrel (thinker_alive) the player circle at (x, y)
 * overlaps, unless it already overlapped it at (x0, y0) (so an overlap can always be walked out of). */
static int player_hits_enemy(const game_state *gs, fix_t x0, fix_t y0, fix_t x, fix_t y)
{
    for (int i = 0; i < gs->things.count; i++) {
        const thinker_t *t = &gs->things.list[i];
        if (!thinker_alive(t))
            continue;
        const int64_t rr = (int64_t)t->radius + PLAYER_RADIUS;
        const int64_t dx = (int64_t)x - t->x, dy = (int64_t)y - t->y;
        if (dx * dx + dy * dy >= rr * rr)
            continue;
        const int64_t ex = (int64_t)x0 - t->x, ey = (int64_t)y0 - t->y;
        if (ex * ex + ey * ey >= rr * rr)
            return 1;
    }
    return 0;
}

static int player_blocked(const game_state *gs, fix_t x0, fix_t y0, fix_t x, fix_t y)
{
    return game_circle_blocked(gs, x, y, PLAYER_RADIUS) || player_hits_enemy(gs, x0, y0, x, y);
}

/* Moves *pos by `delta` along one axis, stopping at the last fixed unit
 * where the player circle does not overlap a blocking tile or a live enemy. Returns 1 when
 * the move was cut short (the caller then kills momentum on that axis). */
static int move_axis(const game_state *gs, fix_t *x, fix_t *y, fix_t delta, int axis_y)
{
    if (delta == 0)
        return 0;
    fix_t *pos = axis_y ? y : x;
    const fix_t start = *pos, x0 = *x, y0 = *y;
    *pos = start + delta;
    if (!player_blocked(gs, x0, y0, *x, *y))
        return 0;

    /* Binary search the largest free step in [0, |delta|). Speeds are below
     * the radius, so nothing between start and the target is skipped. */
    const fix_t sign = delta < 0 ? -1 : 1;
    fix_t lo = 0, hi = fix_abs(delta);
    while (hi - lo > 1) {
        fix_t mid = lo + (hi - lo) / 2;
        *pos = start + sign * mid;
        if (player_blocked(gs, x0, y0, *x, *y))
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

    gs->sounds.count = 0; /* the main loop drained last tic's events */
    if (p->dead) {
        if (dead_tic(gs, cmd))
            return;
        items_tic(gs);
        doors_tic(gs);
        pushwalls_tic(gs);
        static const ticcmd none = { 0, 0, 0, 0 };
        player_thrust(p, &none);
        if (move_axis(gs, &p->x, &p->y, p->momx, 0))
            p->momx = 0;
        if (move_axis(gs, &p->x, &p->y, p->momy, 1))
            p->momy = 0;
        projectiles_tic(gs);
        ai_tic(gs);
        gs->tic++;
        return;
    }

    items_tic(gs);
    p->angle = (angle_t)(p->angle + (uint16_t)cmd->turn);

    /* Use acts on the rising edge, along the view after this tic's turn. */
    if ((cmd->buttons & BT_USE) && !(gs->prev_buttons & BT_USE)) {
        game_use(gs);
    }
    gs->prev_buttons = cmd->buttons;

    /* Doors move before the player, who sees this tic's door state. */
    doors_tic(gs);
    pushwalls_tic(gs);

    player_thrust(p, cmd);

    /* Axis-separated slide: x first, then y. */
    if (move_axis(gs, &p->x, &p->y, p->momx, 0))
        p->momx = 0;
    if (move_axis(gs, &p->x, &p->y, p->momy, 1))
        p->momy = 0;

    items_touch(gs);
    /* Triggers, secrets and damage floors under the player's new position. */
    tags_player_cell(gs);

    const uint32_t shots = p->weapons.shots;
    /* Fuses, projectile moves and effects, before the player fires and
     * before enemies think (see projectile.h). */
    projectiles_tic(gs);
    weapon_tic(gs, (cmd->buttons & BT_WEAPON_MASK) >> BT_WEAPON_SHIFT, cmd->buttons & BT_FIRE);
    if (p->weapons.shots != shots && p->weapons.ready != WP_CLAWS)
        ai_noise(gs); /* gunfire wakes the area; Claws are silent */

    ai_tic(gs);
    kill_triggers_tic(gs);

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

    const weapons_t *w = &p->weapons;
    hash_u32(&h, w->owned);
    hash_u32(&h, w->ready);
    hash_u32(&h, w->pending);
    hash_u32(&h, w->switch_tics);
    hash_u32(&h, w->refire_tics);
    hash_u32(&h, w->flash_tics);
    hash_u32(&h, w->bob_phase);
    for (int i = 0; i < AM_TYPES; i++)
        hash_i32(&h, w->ammo[i]);
    hash_u32(&h, w->shots);

    hash_i32(&h, gs->things.count);
    for (int i = 0; i < gs->things.count; i++) {
        const thinker_t *t = &gs->things.list[i];
        hash_u32(&h, t->type);
        hash_i32(&h, t->x);
        hash_i32(&h, t->y);
        hash_u32(&h, t->angle);
        hash_u32(&h, t->state);
        hash_u32(&h, t->flags);
        hash_i32(&h, t->tics);
        hash_i32(&h, t->health);
        hash_u32(&h, t->target);
        hash_i32(&h, t->momx);
        hash_i32(&h, t->momy);
        hash_u32(&h, t->owner);
    }
    hash_i32(&h, gs->things.kills);
    hash_i32(&h, gs->things.kill_total);
    hash_u32(&h, p->keys);
    hash_u32(&h, gs->prev_buttons);
    hash_i32(&h, gs->door_count);
    for (int i = 0; i < gs->door_count; i++) {
        const door_t *d = &gs->doors[i];
        hash_u32(&h, d->state);
        hash_i32(&h, d->pos);
        hash_i32(&h, d->timer);
    }

    hash_i32(&h, p->armour_class);
    hash_i32(&h, p->satchel);
    hash_i32(&h, p->catnip_tics);
    hash_i32(&h, p->catseye_tics);
    hash_i32(&h, p->ninth_life);
    hash_i32(&h, p->invuln_tics);
    hash_i32(&h, p->dead);
    hash_i32(&h, gs->skill);
    hash_i32(&h, gs->pickup_count);
    for (int i = 0; i < gs->pickup_count; i++)
        hash_u32(&h, gs->pickups[i].taken);
    hash_i32(&h, gs->items_got);
    tags_hash(gs, &h);
    for (int i = 0; i < gs->things.count; i++) {
        const thinker_t *t = &gs->things.list[i];
        hash_u32(&h, t->movedir);
        hash_u32(&h, t->goal_tx);
        hash_u32(&h, t->goal_ty);
        hash_i32(&h, t->movecount);
        /* Only a dead enemy's death tic means anything (its corpse
         * animation); hashing it then keeps kill-free demos unchanged. */
        if (t->state == TH_STATE_DEAD)
            hash_u32(&h, t->died_tic);
    }
    for (int i = 0; i < gs->things.count; i++)
        hash_u32(&h, gs->things.list[i].howler_hit);
    hash_i32(&h, p->death_tics);
    const inventory_t *e = &gs->entry;
    hash_i32(&h, e->health);
    hash_i32(&h, e->armour);
    hash_i32(&h, e->armour_class);
    hash_i32(&h, e->satchel);
    hash_u32(&h, e->weapons_owned);
    hash_u32(&h, e->weapon_ready);
    for (int i = 0; i < AM_TYPES; i++)
        hash_i32(&h, e->ammo[i]);
    return h.value;
}

void game_post_event(game_state *gs, const game_event *ev)
{
    gs->events[gs->event_seq % GAME_EVENT_RING] = *ev;
    gs->event_seq++;
}
