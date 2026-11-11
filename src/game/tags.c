#include "game/tags.h"
#include "game/items.h"

#include "game/sound.h"
#include "game/state.h"

static const bast_map_cell *cell_at(const game_state *gs, int tx, int ty)
{
    const bast_map *m = gs->map;
    if (tx < 0 || ty < 0 || tx >= m->width || ty >= m->height)
        return 0;
    return &m->cells[ty * m->width + tx];
}

void tags_init(game_state *gs)
{
    const bast_map *m = gs->map;
    gs->pushwall_count = 0;
    gs->secret_total = 0;
    for (int ty = 0; ty < m->height; ty++) {
        for (int tx = 0; tx < m->width; tx++) {
            const uint16_t f = m->cells[ty * m->width + tx].flags;
            if (f & BAST_CELL_SECRET)
                gs->secret_total++;
            if (!(f & BAST_CELL_PUSHWALL) || gs->pushwall_count == MAX_PUSHWALLS)
                continue;
            pushwall_t *pw = &gs->pushwalls[gs->pushwall_count++];
            pw->tx = (uint8_t)tx;
            pw->ty = (uint8_t)ty;
            pw->dx = pw->dy = 0;
            pw->state = PUSHWALL_IDLE;
            pw->moved = 0;
            pw->pos = 0;
        }
    }
}

int pushwall_at(const game_state *gs, int tx, int ty)
{
    for (int i = 0; i < gs->pushwall_count; i++)
        if (gs->pushwalls[i].tx == tx && gs->pushwalls[i].ty == ty)
            return i;
    return -1;
}

int pushwall_blocks(const game_state *gs, int tx, int ty)
{
    for (int i = 0; i < gs->pushwall_count; i++) {
        const pushwall_t *pw = &gs->pushwalls[i];
        if (pw->tx == tx && pw->ty == ty)
            return 1;
        if (pw->state == PUSHWALL_MOVING && pw->tx + pw->dx == tx && pw->ty + pw->dy == ty)
            return 1;
    }
    return 0;
}

/* The tile the pushwall would enter next is free: inside the map, not
 * SOLID/BLOCK_ALL, not a shut door, no other pushwall (game_tile_blocks,
 * with this pushwall itself idle so its own reach does not count), and not
 * overlapped by the player or a live enemy (a tag-fired pushwall never
 * slides into them). */
static int next_free(game_state *gs, pushwall_t *pw)
{
    const uint8_t state = pw->state;
    pw->state = PUSHWALL_IDLE;
    const int nx = pw->tx + pw->dx, ny = pw->ty + pw->dy;
    int free = !game_tile_blocks(gs, nx, ny) &&
                     !game_circle_overlaps_tile(gs->player.x, gs->player.y, PLAYER_RADIUS, nx, ny);
    for (int i = 0; free && i < gs->things.count; i++) {
        const thinker_t *t = &gs->things.list[i];
        if (thinker_alive(t) && game_circle_overlaps_tile(t->x, t->y, t->radius, nx, ny))
            free = 0;
    }
    pw->state = state;
    return free;
}

/* Starts an idle pushwall sliding (sx, sy). Returns 1 when it moves; a
 * pushwall whose first tile is blocked stays idle and usable. */
static int pushwall_start(game_state *gs, int idx, int sx, int sy)
{
    pushwall_t *pw = &gs->pushwalls[idx];
    if (pw->state != PUSHWALL_IDLE || (sx == 0) == (sy == 0))
        return 0;
    pw->dx = (int8_t)sx;
    pw->dy = (int8_t)sy;
    if (!next_free(gs, pw)) {
        pw->dx = pw->dy = 0;
        return 0;
    }
    pw->state = PUSHWALL_MOVING;
    pw->pos = 0;
    game_sound(gs, SFX_pushwall_grind, fix_from_int(pw->tx) + FIX_ONE / 2,
               fix_from_int(pw->ty) + FIX_ONE / 2);
    return 1;
}

/* Direction away from the player along the dominant axis (ties: x). */
static void away_from_player(const game_state *gs, const pushwall_t *pw, int *sx, int *sy)
{
    const fix_t dx = fix_from_int(pw->tx) + FIX_ONE / 2 - gs->player.x;
    const fix_t dy = fix_from_int(pw->ty) + FIX_ONE / 2 - gs->player.y;
    *sx = *sy = 0;
    if (fix_abs(dx) >= fix_abs(dy))
        *sx = dx < 0 ? -1 : 1;
    else
        *sy = dy < 0 ? -1 : 1;
}

/* close_tics: the timed-door delay of the switch that fired (0: none). */
static void fire_tag(game_state *gs, int tag, int32_t close_tics)
{
    if (tag <= 0)
        return;
    gs->tags_fired++;
    gs->last_tag = tag;
    for (int i = 0; i < gs->door_count; i++) {
        const door_t *d = &gs->doors[i];
        if (cell_at(gs, d->tx, d->ty)->tag == tag)
            door_activate_timed(gs, i, close_tics);
    }
    for (int i = 0; i < gs->pushwall_count; i++) {
        const pushwall_t *pw = &gs->pushwalls[i];
        if (pw->state != PUSHWALL_IDLE || cell_at(gs, pw->tx, pw->ty)->tag != tag)
            continue;
        int sx, sy;
        away_from_player(gs, pw, &sx, &sy);
        pushwall_start(gs, i, sx, sy);
    }
    const bast_map *m = gs->map;
    for (int i = 0; i < m->entity_count; i++)
        if (m->entities[i].type == BAST_ENT_LIGHTS && m->entities[i].tag == tag)
            gs->lights_alt = 1;
}

void game_fire_tag(game_state *gs, int tag)
{
    fire_tag(gs, tag, 0);
}

void game_use(game_state *gs)
{
    int tx, ty, sx, sy;
    if (!use_trace(gs, &tx, &ty, &sx, &sy))
        return;
    const int d = door_at(gs, tx, ty);
    if (d >= 0) {
        door_use(gs, d);
        return;
    }
    const int pw = pushwall_at(gs, tx, ty);
    if (pw >= 0) {
        if (!pushwall_start(gs, pw, sx, sy))
            game_sound_local(gs, SFX_noway);
        return;
    }
    const bast_map_cell *c = cell_at(gs, tx, ty);
    const fix_t cx = fix_from_int(tx) + FIX_ONE / 2, cy = fix_from_int(ty) + FIX_ONE / 2;
    int used = 0;
    if (c) {
        uint8_t *fired = &gs->cell_fired[ty * gs->map->width + tx];
        if ((c->flags & BAST_CELL_SWITCH) && !(*fired & FIRED_SWITCH)) {
            *fired |= FIRED_SWITCH;
            if (!(c->flags & BAST_CELL_EXIT))
                game_sound(gs, SFX_switch_click, cx, cy);
            fire_tag(gs, c->tag, (int32_t)c->param * TICRATE);
            used = 1;
        }
        if (c->flags & BAST_CELL_EXIT) {
            game_sound(gs, SFX_exit_switch, cx, cy);
            gs->exit = 1;
            used = 1;
        }
    }
    if (!used) /* a plain wall, or a switch already on */
        game_sound_local(gs, SFX_noway);
}

void pushwalls_tic(game_state *gs)
{
    for (int i = 0; i < gs->pushwall_count; i++) {
        pushwall_t *pw = &gs->pushwalls[i];
        if (pw->state != PUSHWALL_MOVING)
            continue;
        pw->pos += PUSHWALL_SPEED;
        if (pw->pos < FIX_ONE)
            continue;
        pw->pos = 0;
        pw->tx = (uint8_t)(pw->tx + pw->dx);
        pw->ty = (uint8_t)(pw->ty + pw->dy);
        pw->moved++;
        /* Each tile is checked when the previous one is reached. */
        if (pw->moved >= PUSHWALL_TILES || !next_free(gs, pw))
            pw->state = PUSHWALL_DONE;
    }
}

void tags_player_cell(game_state *gs)
{
    const int tx = fix_to_int(gs->player.x), ty = fix_to_int(gs->player.y);
    const bast_map_cell *c = cell_at(gs, tx, ty);
    if (!c)
        return;
    uint8_t *fired = &gs->cell_fired[ty * gs->map->width + tx];
    if ((c->flags & BAST_CELL_TRIGGER) && !(*fired & FIRED_TRIGGER)) {
        *fired |= FIRED_TRIGGER;
        game_fire_tag(gs, c->tag);
    }
    if ((c->flags & BAST_CELL_SECRET) && !(*fired & FIRED_SECRET)) {
        *fired |= FIRED_SECRET;
        gs->secrets++;
        game_sound_local(gs, SFX_secret_found);
    }
    /* Through the shared damage path: skill, armour, Ninth Life, death. */
    if ((c->flags & BAST_CELL_DAMAGE) && c->param > 0 && gs->tic % DAMAGE_PERIOD == 0)
        player_damage(gs, c->param);
}

int game_cell_dark(const game_state *gs, int tx, int ty)
{
    const bast_map_cell *c = cell_at(gs, tx, ty);
    if (!c)
        return 0;
    return gs->lights_alt ? c->dark_alt : c->dark;
}

void tags_hash(const game_state *gs, hash_t *h)
{
    hash_u32(h, gs->exit);
    hash_u32(h, gs->lights_alt);
    hash_i32(h, gs->secrets);
    hash_i32(h, gs->secret_total);
    hash_u32(h, gs->tags_fired);
    hash_i32(h, gs->pushwall_count);
    for (int i = 0; i < gs->pushwall_count; i++) {
        const pushwall_t *pw = &gs->pushwalls[i];
        hash_u32(h, pw->tx);
        hash_u32(h, pw->ty);
        hash_i32(h, pw->dx);
        hash_i32(h, pw->dy);
        hash_u32(h, pw->state);
        hash_u32(h, pw->moved);
        hash_i32(h, pw->pos);
    }
    /* One-shot cell states (switch on/off, trigger, secret), row-major over
     * the cells that can carry them. */
    const bast_map *m = gs->map;
    const uint16_t oneshot = BAST_CELL_SWITCH | BAST_CELL_TRIGGER | BAST_CELL_SECRET;
    for (int i = 0; i < m->width * m->height; i++)
        if (m->cells[i].flags & oneshot)
            hash_u32(h, gs->cell_fired[i]);
}
