#include "game/ai.h"

#include <string.h>

#include "game/door.h"
#include "game/enemy_info.h"
#include "game/items.h"
#include "game/projectile.h"
#include "game/sound.h"
#include "game/tags.h"

/* Compass steps, DI_EAST.. counter-clockwise (y grows north). */
static const int DIR_DX[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };
static const int DIR_DY[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };

#define UNREACHED 0x7fff
#define MAP_CELLS (64 * 64)

int32_t ai_windup_tics(const game_state *gs, uint16_t type)
{
    const enemy_info_t *info = enemy_info(type);
    if (!info)
        return 0;
    return gs->skill == 3 ? info->windup * 3 / 4 : info->windup; /* 25% off, rounded down */
}

static int32_t pain_chance(const game_state *gs, const enemy_info_t *info)
{
    return gs->skill == 3 ? info->pain / 2 : info->pain;
}

static uint16_t cell_flags(const game_state *gs, int tx, int ty)
{
    return gs->map->cells[ty * gs->map->width + tx].flags;
}

static int in_map(const game_state *gs, int tx, int ty)
{
    return tx >= 0 && ty >= 0 && tx < gs->map->width && ty < gs->map->height;
}

/* A closed door an enemy may open: no key, use-openable mode, not one-way. */
static int door_openable(const door_t *d)
{
    const int mode = DOOR_MODE(d->param);
    return DOOR_KEY(d->param) == 0 && (mode == DOOR_MODE_AUTO || mode == DOOR_MODE_STAY) &&
           !(d->flags & BAST_CELL_ONEWAY);
}

/* Tiles a chase may route through (doors it can open count as open). */
static int route_ok(const game_state *gs, int tx, int ty)
{
    if (!in_map(gs, tx, ty))
        return 0;
    const uint16_t f = cell_flags(gs, tx, ty);
    if (f & (BAST_CELL_SOLID | BAST_CELL_BLOCK_ALL | BAST_CELL_BLOCK_MONSTER))
        return 0;
    if (pushwall_blocks(gs, tx, ty)) /* where the wall is now, not where it spawned */
        return 0;
    if (f & BAST_CELL_DOOR) {
        int d = door_at(gs, tx, ty);
        return d >= 0 && (door_passable(&gs->doors[d]) || door_openable(&gs->doors[d]));
    }
    return 1;
}

/* Tiles an enemy body may overlap right now. */
static int stand_blocks(const game_state *gs, int tx, int ty)
{
    if (game_tile_blocks(gs, tx, ty))
        return 1;
    return (cell_flags(gs, tx, ty) & BAST_CELL_BLOCK_MONSTER) != 0;
}

int ai_circle_blocked(const game_state *gs, fix_t x, fix_t y, fix_t r)
{
    for (int ty = fix_to_int(y - r); ty <= fix_to_int(y + r); ty++)
        for (int tx = fix_to_int(x - r); tx <= fix_to_int(x + r); tx++)
            if (stand_blocks(gs, tx, ty) && game_circle_overlaps_tile(x, y, r, tx, ty))
                return 1;
    return 0;
}

/* Step from (tx, ty) in direction `dir` without cutting a corner. */
static int step_ok(const game_state *gs, int tx, int ty, int dir,
                   int (*ok)(const game_state *, int, int))
{
    const int nx = tx + DIR_DX[dir], ny = ty + DIR_DY[dir];
    if (!ok(gs, nx, ny))
        return 0;
    if (DIR_DX[dir] && DIR_DY[dir])
        return ok(gs, nx, ty) && ok(gs, tx, ny);
    return 1;
}

/* 8-connected BFS distance (in steps) from the player's tile. */
static void route_field(const game_state *gs, int16_t *dist)
{
    static int16_t queue[MAP_CELLS];
    const int w = gs->map->width, h = gs->map->height;
    for (int i = 0; i < w * h; i++)
        dist[i] = UNREACHED;
    const int px = fix_to_int(gs->player.x), py = fix_to_int(gs->player.y);
    if (!in_map(gs, px, py))
        return;
    int head = 0, tail = 0;
    dist[py * w + px] = 0;
    queue[tail++] = (int16_t)(py * w + px);
    while (head < tail) {
        const int c = queue[head++], cx = c % w, cy = c / w;
        for (int dir = 0; dir < 8; dir++) {
            const int nx = cx + DIR_DX[dir], ny = cy + DIR_DY[dir];
            if (!in_map(gs, nx, ny) || dist[ny * w + nx] != UNREACHED)
                continue;
            if (!step_ok(gs, cx, cy, dir, route_ok))
                continue;
            dist[ny * w + nx] = (int16_t)(dist[c] + 1);
            queue[tail++] = (int16_t)(ny * w + nx);
        }
    }
}

static fix_t player_dist(const game_state *gs, const thinker_t *t)
{
    return fix_dist(gs->player.x - t->x, gs->player.y - t->y);
}

static angle_t angle_to_player(const game_state *gs, const thinker_t *t)
{
    return angle_of(gs->player.x - t->x, gs->player.y - t->y);
}

int ai_line_of_sight(const game_state *gs, const thinker_t *t)
{
    const angle_t a = angle_to_player(gs, t);
    const fix_t d = player_dist(gs, t);
    return game_trace_wall(gs, t->x, t->y, fine_cos(a), fine_sin(a), d) >= d;
}

static int sees_player(const game_state *gs, const thinker_t *t)
{
    const int16_t off = (int16_t)(angle_to_player(gs, t) - t->angle);
    if (off > (int32_t)AI_FOV_HALF || off < -(int32_t)AI_FOV_HALF)
        return 0;
    return ai_line_of_sight(gs, t);
}

static void wake(thinker_t *t)
{
    if (t->state != TH_STATE_STAND)
        return;
    t->state = TH_STATE_CHASE;
    t->movecount = 0;
    t->movedir = DI_NONE;
}

void ai_noise(game_state *gs)
{
    static uint8_t heard[MAP_CELLS];
    static int16_t queue[MAP_CELLS];
    const int w = gs->map->width, h = gs->map->height;
    const int px = fix_to_int(gs->player.x), py = fix_to_int(gs->player.y);
    if (!in_map(gs, px, py))
        return;
    memset(heard, 0, (size_t)(w * h));
    int head = 0, tail = 0;
    heard[py * w + px] = 1;
    queue[tail++] = (int16_t)(py * w + px);
    while (head < tail) {
        const int c = queue[head++], cx = c % w, cy = c / w;
        for (int dir = 0; dir < 8; dir++) {
            const int nx = cx + DIR_DX[dir], ny = cy + DIR_DY[dir];
            if (!in_map(gs, nx, ny) || heard[ny * w + nx] || game_tile_blocks(gs, nx, ny))
                continue;
            heard[ny * w + nx] = 1;
            queue[tail++] = (int16_t)(ny * w + nx);
        }
    }
    for (int i = 0; i < gs->things.count; i++) {
        thinker_t *t = &gs->things.list[i];
        /* Enemies only: a barrel is alive but never wakes. */
        if (!thinker_alive(t) || !enemy_info(t->type) || t->state != TH_STATE_STAND ||
            (t->flags & TH_FLAG_AMBUSH))
            continue;
        const int tx = fix_to_int(t->x), ty = fix_to_int(t->y);
        if (in_map(gs, tx, ty) && heard[ty * w + tx])
            wake(t);
    }
}

static void spawn_drop(game_state *gs, const thinker_t *t, const enemy_info_t *info)
{
    if (!info->drop || gs->pickup_count >= MAX_PICKUPS)
        return;
    pickup_t *pk = &gs->pickups[gs->pickup_count++];
    memset(pk, 0, sizeof *pk);
    pk->type = info->drop;
    pk->x = t->x;
    pk->y = t->y;
    pk->dropped = 1;
    pk->amount = info->drop_amount;
}

int ai_damage(game_state *gs, thinker_t *t, int32_t damage)
{
    return ai_damage_pain(gs, t, damage, 0);
}

int ai_damage_pain(game_state *gs, thinker_t *t, int32_t damage, int32_t pain_tics)
{
    const enemy_info_t *info = enemy_info(t->type);
    if (!thinker_alive(t))
        return 0;
    if (thinker_damage(&gs->things, t, damage)) {
        t->died_tic = gs->tic;
        if (info)
            spawn_drop(gs, t, info);
        return 1;
    }
    if (!info || damage <= 0)
        return 0;
    wake(t);
    if (pain_tics > 0) { /* forced: no pain roll */
        t->state = TH_STATE_PAIN;
        t->tics = pain_tics;
        t->movedir = DI_NONE;
        return 0;
    }
    const int32_t pain = pain_chance(gs, info);
    if (pain > 0 && (int32_t)rng_range(&gs->rng, 256) < pain) {
        t->state = TH_STATE_PAIN;
        t->tics = AI_PAIN_TICS;
    }
    return 0;
}

int ai_shield_up(const thinker_t *t)
{
    const enemy_info_t *info = enemy_info(t->type);
    return info && info->shield && thinker_alive(t) && t->state != TH_STATE_WINDUP &&
           t->state != TH_STATE_ATTACK && t->shield_tics == 0;
}

/* 1 when (sx, sy) lies within the shield arc in front of `t`. */
static int shield_faces(const thinker_t *t, fix_t sx, fix_t sy)
{
    if (sx == t->x && sy == t->y)
        return 1;
    const int16_t off = (int16_t)(angle_of(sx - t->x, sy - t->y) - t->angle);
    return off <= (int32_t)AI_SHIELD_HALF && off >= -(int32_t)AI_SHIELD_HALF;
}

int ai_shield_blocks(const thinker_t *t, fix_t sx, fix_t sy)
{
    return ai_shield_up(t) && shield_faces(t, sx, sy);
}

int ai_damage_from(game_state *gs, thinker_t *t, int32_t damage, int kind, fix_t sx, fix_t sy)
{
    if (damage > 0 && ai_shield_blocks(t, sx, sy)) {
        if (kind == DMG_SPLASH) {
            damage /= 2;
        } else {
            game_sound(gs, SFX_shield_block, t->x, t->y);
            wake(t);
            return 0;
        }
    }
    return ai_damage(gs, t, damage);
}

/* Telegraph and attack sounds (audio.md) of the enemies that play them;
 * -1 = none. */
static int windup_sfx(uint16_t type)
{
    switch (type) {
    case BAST_ENT_MASTIFF_JUGGERNAUT:
        return SFX_mastiff_windup;
    case BAST_ENT_SHEPHERD_SHIELDBEARER:
        return SFX_shep_windup;
    default:
        return -1;
    }
}

static int hit_sfx(uint16_t type)
{
    return type == BAST_ENT_SHEPHERD_SHIELDBEARER ? SFX_baton_hit : -1;
}

static uint16_t thinker_id(const game_state *gs, const thinker_t *t)
{
    return (uint16_t)(t - gs->things.list + 1);
}

/* Mastiff: one missile from the next shoulder, aimed at the player from
 * the launch point. Left = 90 degrees counter-clockwise of the facing. The
 * shoulder flips every shot, also when a wall forces the centre. */
static void launch_missile(game_state *gs, thinker_t *t, const enemy_info_t *info)
{
    const angle_t side = (angle_t)(t->angle + (t->shoulder ? (angle_t)(0u - ANG90) : ANG90));
    fix_t x = t->x + fix_mul(info->shoulder, fine_cos(side));
    fix_t y = t->y + fix_mul(info->shoulder, fine_sin(side));
    const projectile_info_t *pi = projectile_info(info->missile);
    if (pi && game_circle_blocked(gs, x, y, pi->radius)) {
        /* Shoulder against a wall (a 1-tile corridor): launch from the
         * centre, which the body keeps clear, rather than into the wall. */
        x = t->x;
        y = t->y;
    }
    const angle_t aim = angle_of(gs->player.x - x, gs->player.y - y);
    projectile_spawn(gs, info->missile, thinker_id(gs, t), x, y, AI_MISSILE_Z, aim);
    if (info->shoulder)
        t->shoulder ^= 1;
    game_sound(gs, SFX_bone_fire, x, y);
}

/* Player near edge within the melee reach of `t`. */
static int in_reach(const game_state *gs, const thinker_t *t, const enemy_info_t *info)
{
    return player_dist(gs, t) - PLAYER_RADIUS <= info->range;
}

/* A plasma bolt at the player's position now. */
static void fire_bolt(game_state *gs, thinker_t *t)
{
    t->angle = angle_to_player(gs, t);
    projectile_spawn(gs, TH_TYPE_PLASMA_BOLT, thinker_id(gs, t), t->x, t->y, AI_POM_BOLT_Z,
                     t->angle);
    game_sound(gs, SFX_plasma_fire, t->x, t->y);
}

/* 1 when `tics` (counting down through the Pomeranian's attack state) is
 * the fire tic of bolt 2..AI_POM_BOLTS: bolt k fires (k - 1) gaps after
 * bolt 1, which resolve_attack fires as the state starts. */
static int pom_volley_tic(int32_t tics)
{
    const int32_t start = AI_ATTACK_TICS + (AI_POM_BOLTS - 1) * AI_POM_BOLT_GAP;
    for (int k = 2; k <= AI_POM_BOLTS; k++)
        if (tics == start - (k - 1) * AI_POM_BOLT_GAP)
            return 1;
    return 0;
}

static void resolve_attack(game_state *gs, thinker_t *t, const enemy_info_t *info)
{
    int hit = 0;
    if (info->attack == ATK_MISSILE) {
        launch_missile(gs, t, info);
        return;
    }
    if (info->attack == ATK_HITSCAN) {
        hit = player_dist(gs, t) <= info->range && ai_line_of_sight(gs, t);
    } else if (info->attack == ATK_MELEE) {
        hit = in_reach(gs, t, info);
    } else if (info->attack == ATK_PLASMA) {
        fire_bolt(gs, t);
    } else if (info->attack == ATK_MORTAR) {
        hit = in_reach(gs, t, info); /* headbutt */
        if (!hit) {
            game_sound(gs, SFX_mortar_launch, t->x, t->y);
            reticle_spawn(gs, thinker_id(gs, t), gs->player.x, gs->player.y);
        }
    }
    if (hit) {
        const int32_t dmg = info->dmg_base + (int32_t)rng_range(&gs->rng, (uint32_t)info->dmg_spread + 1);
        player_damage_from(gs, dmg, t->x, t->y);
        if (hit_sfx(t->type) >= 0)
            game_sound(gs, hit_sfx(t->type), t->x, t->y);
    }
}

static void start_windup(game_state *gs, thinker_t *t)
{
    t->state = TH_STATE_WINDUP;
    t->tics = ai_windup_tics(gs, t->type);
    t->angle = angle_to_player(gs, t);
    t->movedir = DI_NONE;
    if (windup_sfx(t->type) >= 0)
        game_sound(gs, windup_sfx(t->type), t->x, t->y);
}

/* Picks the next tile down the route field; DI_NONE when none (or when in
 * the player's tile, where the enemy walks straight at the player). */
static void choose_step(game_state *gs, thinker_t *t, const int16_t *dist)
{
    const int w = gs->map->width;
    const int tx = fix_to_int(t->x), ty = fix_to_int(t->y);
    t->movedir = DI_NONE;
    if (!in_map(gs, tx, ty))
        return;
    const int here = dist[ty * w + tx];
    if (here == 0)
        return;
    /* Prefer the step closest in angle to the player, then the lowest DI_. */
    const angle_t want = angle_to_player(gs, t);
    int best = DI_NONE, best_d = here == UNREACHED ? UNREACHED : here;
    int32_t best_off = 0;
    for (int dir = 0; dir < 8; dir++) {
        if (!step_ok(gs, tx, ty, dir, route_ok))
            continue;
        const int d = dist[(ty + DIR_DY[dir]) * w + tx + DIR_DX[dir]];
        int16_t off16 = (int16_t)((angle_t)(dir * ANG45) - want);
        const int32_t off = off16 < 0 ? -(int32_t)off16 : off16;
        if (d < best_d || (d == best_d && best != DI_NONE && off < best_off)) {
            best = dir;
            best_d = d;
            best_off = off;
        }
    }
    if (best == DI_NONE)
        return;
    t->movedir = (uint8_t)best;
    t->goal_tx = (uint8_t)(tx + DIR_DX[best]);
    t->goal_ty = (uint8_t)(ty + DIR_DY[best]);
}

/* Dachshund backing off: the step that raises the route distance from the
 * player (any step pointing away when the player's tile is unreachable),
 * the one closest in angle to straight away first. DI_NONE when cornered. */
static void choose_retreat(game_state *gs, thinker_t *t, const int16_t *dist)
{
    const int w = gs->map->width;
    const int tx = fix_to_int(t->x), ty = fix_to_int(t->y);
    t->movedir = DI_NONE;
    if (!in_map(gs, tx, ty))
        return;
    const int here = dist[ty * w + tx];
    const angle_t away = (angle_t)(angle_to_player(gs, t) + ANG180);
    int best = DI_NONE;
    int32_t best_off = 0;
    for (int dir = 0; dir < 8; dir++) {
        if (!step_ok(gs, tx, ty, dir, route_ok))
            continue;
        const int d = dist[(ty + DIR_DY[dir]) * w + tx + DIR_DX[dir]];
        const int16_t off16 = (int16_t)((angle_t)(dir * ANG45) - away);
        const int32_t off = off16 < 0 ? -(int32_t)off16 : off16;
        if (here == UNREACHED ? off >= (int32_t)ANG90 : (d == UNREACHED || d <= here))
            continue;
        if (best == DI_NONE || off < best_off) {
            best = dir;
            best_off = off;
        }
    }
    if (best == DI_NONE)
        return;
    t->movedir = (uint8_t)best;
    t->goal_tx = (uint8_t)(tx + DIR_DX[best]);
    t->goal_ty = (uint8_t)(ty + DIR_DY[best]);
}

static int overlaps_player(const game_state *gs, fix_t x, fix_t y, fix_t r)
{
    const int64_t dx = (int64_t)gs->player.x - x, dy = (int64_t)gs->player.y - y;
    const int64_t rr = (int64_t)r + PLAYER_RADIUS;
    return dx * dx + dy * dy < rr * rr;
}

/* Moves `t` by `delta` along one axis: the largest part of it (halving)
 * that does not overlap a blocking tile, nor bring it closer into the
 * player's circle. */
static void move_axis(const game_state *gs, thinker_t *t, fix_t delta, int axis_y)
{
    fix_t *pos = axis_y ? &t->y : &t->x;
    const fix_t start = *pos;
    const int was_on_player = overlaps_player(gs, t->x, t->y, t->radius);
    for (; delta != 0; delta /= 2) {
        *pos = start + delta;
        if (!ai_circle_blocked(gs, t->x, t->y, t->radius) &&
            (was_on_player || !overlaps_player(gs, t->x, t->y, t->radius)))
            return;
    }
    *pos = start;
}

/* Moves `t` by `delta` along one axis: the largest part of it (halving)
 * that does not overlap a tile enemies may not stand in. */
static void push_axis(const game_state *gs, thinker_t *t, fix_t delta, int axis_y)
{
    fix_t *pos = axis_y ? &t->y : &t->x;
    const fix_t start = *pos;
    for (; delta != 0; delta /= 2) {
        *pos = start + delta;
        if (!ai_circle_blocked(gs, t->x, t->y, t->radius))
            return;
    }
    *pos = start;
}

void ai_push(game_state *gs, thinker_t *t, fix_t dx, fix_t dy)
{
    /* Quarter steps so no step is longer than a tile edge can stop. */
    for (int i = 0; i < 4; i++) {
        const fix_t ox = t->x, oy = t->y;
        const fix_t sx = (fix_t)(((int64_t)dx * (i + 1)) / 4 - ((int64_t)dx * i) / 4);
        const fix_t sy = (fix_t)(((int64_t)dy * (i + 1)) / 4 - ((int64_t)dy * i) / 4);
        push_axis(gs, t, sx, 0);
        push_axis(gs, t, sy, 1);
        if (t->x == ox && t->y == oy)
            return; /* pinned against a wall */
    }
}

static void chase_move(game_state *gs, thinker_t *t, const enemy_info_t *info)
{
    fix_t gx, gy;
    const int tx = fix_to_int(t->x), ty = fix_to_int(t->y);
    if (t->movedir == DI_NONE) {
        if (tx != fix_to_int(gs->player.x) || ty != fix_to_int(gs->player.y))
            return;
        gx = gs->player.x; /* same tile: straight at the player */
        gy = gs->player.y;
    } else {
        gx = fix_from_int(t->goal_tx) + FIX_ONE / 2;
        gy = fix_from_int(t->goal_ty) + FIX_ONE / 2;
        /* Walking into a door: open it if allowed. */
        int d = door_at(gs, t->goal_tx, t->goal_ty);
        if (d >= 0 && !door_passable(&gs->doors[d]))
            door_monster_open(gs, d);
    }
    const fix_t vx = gx - t->x, vy = gy - t->y;
    const fix_t len = fix_dist(vx, vy);
    if (len == 0) {
        t->movedir = DI_NONE;
        return;
    }
    t->angle = angle_of(vx, vy);
    const fix_t step = len < info->speed ? len : info->speed;
    move_axis(gs, t, (fix_t)((int64_t)vx * step / len), 0);
    move_axis(gs, t, (fix_t)((int64_t)vy * step / len), 1);
    if (t->movedir != DI_NONE && t->x == gx && t->y == gy)
        t->movedir = DI_NONE; /* reached the tile centre: choose again */
}

static void need_route(game_state *gs, int16_t *dist, int *have_dist)
{
    if (!*have_dist) {
        route_field(gs, dist);
        *have_dist = 1;
    }
}

/* Dachshund chase (enemies.md): closer than AI_DACH_FLEE it backs off, and
 * keeps backing off until AI_DACH_NEAR; within AI_DACH_FAR with line of
 * sight it holds its ground facing the player; otherwise it walks in like
 * any enemy. Every AI_DECIDE_TICS, unless backing off, it lobs a mortar
 * when it sees the player within AI_DACH_MORTAR_RANGE and rolls under its
 * aggression. A cornered Dachshund (no step away) holds instead. */
static void dach_chase(game_state *gs, thinker_t *t, const enemy_info_t *info, int16_t *dist,
                       int *have_dist)
{
    const fix_t d = player_dist(gs, t);
    const int was_retreating = (t->flags & TH_FLAG_RETREAT) != 0;
    int retreat = d < AI_DACH_FLEE || (was_retreating && d < AI_DACH_NEAR);
    if (retreat != was_retreating)
        t->movedir = DI_NONE; /* the current step was for the other mode */
    if (retreat && t->movedir == DI_NONE) {
        need_route(gs, dist, have_dist);
        choose_retreat(gs, t, dist);
        if (t->movedir == DI_NONE)
            retreat = 0; /* cornered */
    }
    if (retreat)
        t->flags |= TH_FLAG_RETREAT;
    else
        t->flags &= (uint8_t)~TH_FLAG_RETREAT;

    const int los = ai_line_of_sight(gs, t);
    if (--t->movecount <= 0) {
        t->movecount = AI_DECIDE_TICS;
        if (!retreat && d <= AI_DACH_MORTAR_RANGE && los &&
            (int32_t)rng_range(&gs->rng, 256) < info->aggression) {
            start_windup(gs, t);
            return;
        }
        if (!retreat)
            t->movedir = DI_NONE; /* re-evaluate the route */
    }
    if (retreat) {
        chase_move(gs, t, info);
        return;
    }
    if (d <= AI_DACH_FAR && los) { /* in its band: hold, aim */
        t->movedir = DI_NONE;
        t->angle = angle_to_player(gs, t);
        return;
    }
    if (t->movedir == DI_NONE) {
        need_route(gs, dist, have_dist);
        choose_step(gs, t, dist);
    }
    chase_move(gs, t, info);
}

static void think(game_state *gs, thinker_t *t, const enemy_info_t *info, int16_t *dist,
                  int *have_dist)
{
    const int player_alive = !gs->player.dead;
    switch (t->state) {
    case TH_STATE_STAND:
        if (player_alive && sees_player(gs, t))
            wake(t);
        return;
    case TH_STATE_PAIN:
    case TH_STATE_ATTACK:
        --t->tics;
        if (t->state == TH_STATE_ATTACK && info->attack == ATK_PLASMA && player_alive &&
            pom_volley_tic(t->tics))
            fire_bolt(gs, t); /* the next bolt of the volley */
        if (t->tics <= 0) {
            t->state = TH_STATE_CHASE;
            t->tics = 0;
            t->movedir = DI_NONE;
        }
        return;
    case TH_STATE_WINDUP:
        t->angle = angle_to_player(gs, t);
        if (--t->tics <= 0) {
            resolve_attack(gs, t, info);
            t->state = TH_STATE_ATTACK;
            t->tics = AI_ATTACK_TICS;
            if (info->attack == ATK_PLASMA)
                t->tics += (AI_POM_BOLTS - 1) * AI_POM_BOLT_GAP;
            t->movecount = AI_DECIDE_TICS;
        }
        return;
    case TH_STATE_CHASE:
        break;
    default:
        return;
    }
    if (!player_alive)
        return;
    if ((info->attack == ATK_MELEE || info->attack == ATK_MORTAR) && in_reach(gs, t, info)) {
        start_windup(gs, t); /* bite, headbutt */
        return;
    }
    if (info->attack == ATK_MORTAR) {
        dach_chase(gs, t, info, dist, have_dist);
        return;
    }
    if (--t->movecount <= 0) {
        t->movecount = AI_DECIDE_TICS;
        if ((info->attack == ATK_HITSCAN || info->attack == ATK_PLASMA ||
             info->attack == ATK_MISSILE) &&
            player_dist(gs, t) <= info->range && ai_line_of_sight(gs, t) &&
            (int32_t)rng_range(&gs->rng, 256) < info->aggression) {
            start_windup(gs, t);
            return;
        }
        t->movedir = DI_NONE; /* re-evaluate the route */
    }
    if (t->movedir == DI_NONE) {
        need_route(gs, dist, have_dist);
        choose_step(gs, t, dist);
    }
    chase_move(gs, t, info);
}

void ai_tic(game_state *gs)
{
    static int16_t dist[MAP_CELLS];
    int have_dist = 0;
    for (int i = 0; i < gs->things.count; i++) {
        thinker_t *t = &gs->things.list[i];
        if (!thinker_alive(t))
            continue;
        const enemy_info_t *info = enemy_info(t->type);
        if (!info)
            continue;
        if (info->shield) { /* Howler hook (thinker_t.howler_hit) */
            if (t->shield_tics > 0)
                t->shield_tics--;
            if (t->howler_hit) {
                t->howler_hit = 0;
                t->shield_tics = AI_SHIELD_DROP_TICS;
            }
        }
        think(gs, t, info, dist, &have_dist);
    }
}

int ai_render_things(const game_state *gs, render_thing *out, int max)
{
    int n = 0;
    for (int i = 0; i < gs->things.count && n < max; i++) {
        const thinker_t *t = &gs->things.list[i];
        if (!enemy_info(t->type) || (t->flags & TH_FLAG_PICKUP))
            continue;
        int anim, frame;
        thinker_anim(t, gs->tic, &anim, &frame);
        out[n++] = (render_thing){ .type = t->type, .x = t->x, .y = t->y, .angle = t->angle,
                                   .anim = (uint8_t)anim, .frame = (uint8_t)frame };
    }
    return n;
}
