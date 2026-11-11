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

/* Breadth-first distances (in tiles) from (tx, ty) over tiles passable in
 * the live state (game_tile_blocks: a closed or moving door blocks, an open
 * one does not). Recomputed every tic, so goto waits for nothing: it reports
 * NAV_NO_PATH while a door on the only route is shut. */
static void bfs(const game_state *gs, int tx, int ty, int16_t *dist)
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
            if (game_tile_blocks(gs, nx, ny) || dist[ny * w + nx] != NAV_UNREACHED)
                continue;
            dist[ny * w + nx] = (int16_t)(dist[c] + 1);
            queue[tail++] = (int16_t)(ny * w + nx);
        }
    }
}

int nav_goto(const game_state *gs, int tx, int ty, ticcmd *out)
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

    int16_t dist[NAV_MAX_CELLS];
    bfs(gs, tx, ty, dist);
    const int w = map->width;
    const int px = fix_to_int(p->x), py = fix_to_int(p->y);
    if (game_tile_blocks(gs, px, py) || dist[py * w + px] == NAV_UNREACHED)
        return NAV_NO_PATH;

    /* Next tile on the path: the first neighbour (E, N, W, S) one step
     * closer; the target itself once the player stands in it. */
    int nx = px, ny = py;
    const int here = dist[py * w + px];
    for (int k = 0; k < 4 && here > 0; k++) {
        int cx = px + dx[k], cy = py + dy[k];
        if (!game_tile_blocks(gs, cx, cy) && dist[cy * w + cx] == here - 1) {
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
    return NAV_MOVING;
}
