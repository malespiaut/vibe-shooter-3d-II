#include "game/door.h"

#include "engine/angle.h"
#include "game/state.h"

void doors_init(game_state *gs)
{
    const bast_map *m = gs->map;
    gs->door_count = 0;
    for (int i = 0; i < 64 * 64; i++)
        gs->door_index[i] = -1;
    for (int ty = 0; ty < m->height; ty++) {
        for (int tx = 0; tx < m->width; tx++) {
            const bast_map_cell *c = &m->cells[ty * m->width + tx];
            if (!(c->flags & BAST_CELL_DOOR) || gs->door_count == MAX_DOORS)
                continue;
            door_t *d = &gs->doors[gs->door_count];
            d->tx = (uint8_t)tx;
            d->ty = (uint8_t)ty;
            d->param = c->param;
            d->flags = c->flags;
            d->timer = 0;
            if (c->flags & BAST_CELL_START_OPEN) {
                d->state = DOOR_OPEN;
                d->pos = FIX_ONE;
            } else {
                d->state = DOOR_CLOSED;
                d->pos = 0;
            }
            gs->door_index[ty * m->width + tx] = (int16_t)gs->door_count++;
        }
    }
}

int door_at(const game_state *gs, int tx, int ty)
{
    const bast_map *m = gs->map;
    if (tx < 0 || ty < 0 || tx >= m->width || ty >= m->height)
        return -1;
    return gs->door_index[ty * m->width + tx];
}

int door_passable(const door_t *d)
{
    return d->pos >= FIX_ONE;
}

/* Is the player on the door's ONEWAY side? The side is the half-plane past
 * the door's edge in that direction (north: y >= top edge, ...). */
static int on_side(const door_t *d, const player_t *p)
{
    const fix_t x0 = fix_from_int(d->tx), y0 = fix_from_int(d->ty);
    switch (DOOR_ONEWAY_SIDE(d->param)) {
    case DOOR_SIDE_NORTH: return p->y >= y0 + FIX_ONE;
    case DOOR_SIDE_EAST: return p->x >= x0 + FIX_ONE;
    case DOOR_SIDE_SOUTH: return p->y <= y0;
    default: return p->x <= x0;
    }
}

int door_use(game_state *gs, int idx)
{
    door_t *d = &gs->doors[idx];
    const player_t *p = &gs->player;
    if (d->state != DOOR_CLOSED && d->state != DOOR_CLOSING)
        return 0;
    const int mode = DOOR_MODE(d->param);
    if (mode == DOOR_MODE_SWITCH || mode == DOOR_MODE_TIMED)
        return 0;
    const int key = DOOR_KEY(d->param);
    if (key && !(p->keys & (1u << (key - 1))))
        return 0;
    if ((d->flags & BAST_CELL_ONEWAY) && !on_side(d, p))
        return 0;
    d->state = DOOR_OPENING;
    return 1;
}

void door_activate(game_state *gs, int idx)
{
    door_t *d = &gs->doors[idx];
    if (d->state == DOOR_OPEN || d->state == DOOR_OPENING) {
        if (d->flags & BAST_CELL_START_OPEN) {
            d->state = DOOR_CLOSING;
            d->flags &= (uint16_t)~BAST_CELL_START_OPEN; /* a tag closes it once */
        }
    } else {
        d->state = DOOR_OPENING;
    }
}

static int player_in(const game_state *gs, const door_t *d)
{
    const player_t *p = &gs->player;
    return game_circle_overlaps_tile(p->x, p->y, PLAYER_RADIUS, d->tx, d->ty);
}

void doors_tic(game_state *gs)
{
    for (int i = 0; i < gs->door_count; i++) {
        door_t *d = &gs->doors[i];
        switch (d->state) {
        case DOOR_OPENING:
            d->pos += DOOR_SPEED;
            if (d->pos >= FIX_ONE) {
                d->pos = FIX_ONE;
                d->state = DOOR_OPEN;
                d->timer = DOOR_WAIT_TICS;
            }
            break;
        case DOOR_OPEN:
            /* START_OPEN doors stay open until a tag closes them. */
            if (DOOR_MODE(d->param) != DOOR_MODE_AUTO || (d->flags & BAST_CELL_START_OPEN))
                break;
            if (d->timer > 0)
                d->timer--;
            /* Never start closing on the player: wait until the cell is clear. */
            if (d->timer == 0 && !player_in(gs, d)) {
                d->state = DOOR_CLOSING; /* and moves this tic: blocks at once */
                d->pos -= DOOR_SPEED;
            }
            break;
        case DOOR_CLOSING:
            if (player_in(gs, d)) {
                d->state = DOOR_OPENING; /* re-open on the player */
                break;
            }
            d->pos -= DOOR_SPEED;
            if (d->pos <= 0) {
                d->pos = 0;
                d->state = DOOR_CLOSED;
            }
            break;
        default:
            break;
        }
    }
}

/* Distance along the ray (unit direction component `dir`) from `pos` to the
 * next grid line, and between grid lines. Near-zero components never cross. */
#define NO_CROSS INT32_MAX
static void dda_axis(fix_t pos, fix_t dir, int *step, fix_t *tmax, fix_t *tdelta)
{
    const fix_t cell = fix_from_int(fix_to_int(pos));
    if (fix_abs(dir) < 256) { /* < 1/256: more than 256 units per cell */
        *step = 0;
        *tmax = *tdelta = NO_CROSS;
        return;
    }
    *tdelta = fix_div(FIX_ONE, fix_abs(dir));
    if (dir > 0) {
        *step = 1;
        *tmax = fix_div(cell + FIX_ONE - pos, dir);
    } else {
        *step = -1;
        *tmax = fix_div(pos - cell, -dir);
    }
}

int door_use_target(const game_state *gs)
{
    const player_t *p = &gs->player;
    int tx = fix_to_int(p->x), ty = fix_to_int(p->y);
    int sx, sy;
    fix_t tmx, tmy, tdx, tdy;
    dda_axis(p->x, fine_cos(p->angle), &sx, &tmx, &tdx);
    dda_axis(p->y, fine_sin(p->angle), &sy, &tmy, &tdy);
    for (;;) {
        fix_t t;
        if (tmx <= tmy) {
            t = tmx;
            tx += sx;
            tmx = tmx > NO_CROSS - tdx ? NO_CROSS : tmx + tdx;
        } else {
            t = tmy;
            ty += sy;
            tmy = tmy > NO_CROSS - tdy ? NO_CROSS : tmy + tdy;
        }
        if (t > DOOR_USE_RANGE)
            return -1;
        int d = door_at(gs, tx, ty);
        if (d >= 0)
            return d;
        if (game_tile_blocks(gs, tx, ty))
            return -1;
    }
}
