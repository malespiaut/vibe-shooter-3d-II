#include "game/demo_nav.h"

#include <string.h>

#define NAV_MAX_CELLS (64 * 64) /* bast maps are at most 64x64 */
#define NAV_UNREACHED (-1)

/* Fine angle error at which goto stops walking and only turns. */
#define NAV_WALK_CONE ANG45
/* Final approach: wanted speed is (distance left) / NAV_BRAKE_DIV, so the
 * friction step (run speed / 4 per tic) can always stop the player short of
 * the centre. Tuned on m1test; see tests/test_demo_cmds.py. */
#define NAV_BRAKE_DIV 4

static fix_t tile_centre(int t)
{
    return fix_from_int(t) + FIX_ONE / 2;
}

angle_t nav_deg_to_angle(int32_t deg)
{
    int32_t d = deg % 360;
    if (d < 0)
        d += 360;
    return (angle_t)((d * 65536 + 180) / 360);
}

angle_t nav_angle_to_tile(const game_state *gs, int tx, int ty)
{
    return angle_of(tile_centre(tx) - gs->player.x, tile_centre(ty) - gs->player.y);
}

/* Turn toward `target`, at most PLAYER_TURN_SPEED, landing exactly on it
 * when it is closer than that. Returns the signed error before the turn. */
static int32_t turn_toward(const game_state *gs, angle_t target, ticcmd *out)
{
    int32_t err = (int16_t)(uint16_t)(target - gs->player.angle);
    int32_t t = err;
    if (t > PLAYER_TURN_SPEED)
        t = PLAYER_TURN_SPEED;
    if (t < -PLAYER_TURN_SPEED)
        t = -PLAYER_TURN_SPEED;
    out->turn = (int16_t)t;
    return err;
}

int nav_face(const game_state *gs, angle_t target, ticcmd *out)
{
    memset(out, 0, sizeof *out);
    int32_t err = (int16_t)(uint16_t)(target - gs->player.angle);
    if (err >= -NAV_FACE_TOL && err <= NAV_FACE_TOL)
        return 1;
    turn_toward(gs, target, out);
    return 0;
}

void nav_reset(nav_state *ns)
{
    ns->tile = -1;
    ns->stall = 0;
    ns->avoid = -1;
}

/* Marks the tile holding the centre of every live thing (enemy, barrel:
 * thinker_alive, the same things the player collides with), and `avoid`. */
static void mark_things(const game_state *gs, int avoid, uint8_t *thing)
{
    const int w = gs->map->width, h = gs->map->height;
    memset(thing, 0, (size_t)(w * h));
    for (int i = 0; i < gs->things.count; i++) {
        const thinker_t *t = &gs->things.list[i];
        if (!thinker_alive(t))
            continue;
        int x = fix_to_int(t->x), y = fix_to_int(t->y);
        if (x >= 0 && y >= 0 && x < w && y < h)
            thing[y * w + x] = 1;
    }
    if (avoid >= 0 && avoid < w * h)
        thing[avoid] = 1;
}

/* Breadth-first distances (in tiles) from (tx, ty) over tiles passable in
 * the live state (game_tile_blocks: a closed or moving door blocks, an open
 * one does not) and not marked in `thing` (NULL: none). Recomputed every
 * tic, so goto waits for nothing: it reports NAV_NO_PATH while a door on
 * the only route is shut. */
static void bfs(const game_state *gs, int tx, int ty, const uint8_t *thing, int16_t *dist)
{
    const int w = gs->map->width, h = gs->map->height;
    static const int dx[4] = { 1, 0, -1, 0 }, dy[4] = { 0, 1, 0, -1 };
    int16_t queue[NAV_MAX_CELLS];
    int head = 0, tail = 0;
    for (int i = 0; i < w * h; i++)
        dist[i] = NAV_UNREACHED;
    dist[ty * w + tx] = 0;
    queue[tail++] = (int16_t)(ty * w + tx);
    while (head < tail) {
        int c = queue[head++];
        int cx = c % w, cy = c / w;
        for (int k = 0; k < 4; k++) {
            int nx = cx + dx[k], ny = cy + dy[k];
            if (game_tile_blocks(gs, nx, ny) || dist[ny * w + nx] != NAV_UNREACHED ||
                (thing && thing[ny * w + nx]))
                continue;
            dist[ny * w + nx] = (int16_t)(dist[c] + 1);
            queue[tail++] = (int16_t)(ny * w + nx);
        }
    }
}

int nav_goto(const game_state *gs, int tx, int ty, nav_state *ns, ticcmd *out)
{
    static const int dx[4] = { 1, 0, -1, 0 }, dy[4] = { 0, 1, 0, -1 };
    const bast_map *map = gs->map;
    const player_t *p = &gs->player;
    memset(out, 0, sizeof *out);
    if (game_tile_blocks(gs, tx, ty))
        return NAV_NO_PATH;

    const fix_t ex = tile_centre(tx) - p->x, ey = tile_centre(ty) - p->y;
    const fix_t left = fix_dist(ex, ey);
    if (left <= NAV_ARRIVE_DIST) {
        /* Inside the tolerance: let friction stop the player. */
        return p->momx == 0 && p->momy == 0 ? NAV_ARRIVED : NAV_MOVING;
    }

    const int w = map->width;
    const int px = fix_to_int(p->x), py = fix_to_int(p->y);
    if (game_tile_blocks(gs, px, py))
        return NAV_NO_PATH;
    if (py * w + px != ns->tile) {
        ns->tile = py * w + px;
        ns->stall = 0;
        ns->avoid = -1;
    }

    /* Plan around things first; the goal and the player's tile never
     * count as taken. No such route: the tile-only one, which may clear. */
    int16_t dist[NAV_MAX_CELLS];
    uint8_t thing[NAV_MAX_CELLS];
    mark_things(gs, ns->avoid, thing);
    thing[ty * w + tx] = 0;
    thing[py * w + px] = 0;
    bfs(gs, tx, ty, thing, dist);
    const uint8_t *blocked = thing;
    if (dist[py * w + px] == NAV_UNREACHED) {
        bfs(gs, tx, ty, NULL, dist);
        blocked = NULL;
        if (dist[py * w + px] == NAV_UNREACHED)
            return NAV_NO_PATH;
    }

    /* Next tile on the path: the first neighbour (E, N, W, S) one step
     * closer; the target itself once the player stands in it. */
    int nx = px, ny = py;
    const int here = dist[py * w + px];
    for (int k = 0; k < 4 && here > 0; k++) {
        int cx = px + dx[k], cy = py + dy[k];
        if (!game_tile_blocks(gs, cx, cy) && dist[cy * w + cx] == here - 1 &&
            !(blocked && blocked[cy * w + cx])) {
            nx = cx;
            ny = cy;
            break;
        }
    }

    int32_t err = turn_toward(gs, nav_angle_to_tile(gs, nx, ny), out);
    if (err < 0)
        err = -err;
    if (err >= NAV_WALK_CONE)
        return NAV_MOVING; /* turn in place first */

    /* Path length left, approximated as the straight-line distance on the
     * last tile and whole tiles before it. Brake on the final approach. */
    fix_t want = PLAYER_WALK_SPEED;
    if (here <= 1) {
        fix_t brake = left / NAV_BRAKE_DIV;
        if (brake < want)
            want = brake;
    }
    int32_t fwd = (int32_t)(((int64_t)want * TICCMD_MAX + PLAYER_WALK_SPEED - 1) /
                            PLAYER_WALK_SPEED);
    out->fwd = (int8_t)(fwd < 1 ? 1 : fwd > TICCMD_MAX ? TICCMD_MAX : fwd);

    /* Walking but not leaving this tile: something the plan does not see
     * (a thing whose centre is in a neighbouring tile, or one the tile-only
     * fallback runs into) is in the way. Avoid the next tile from now on. */
    if (here > 0 && ++ns->stall >= NAV_STALL_TICS) {
        ns->avoid = ny * w + nx;
        ns->stall = 0;
    }
    return NAV_MOVING;
}
