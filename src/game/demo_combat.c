#include "game/demo_combat.h"

#include <string.h>

#include "game/ai.h"

static const thinker_t *get(const game_state *gs, uint16_t id)
{
    if (id == 0 || id > gs->things.count)
        return NULL;
    return &gs->things.list[id - 1];
}

static fix_t dist_to(const game_state *gs, const thinker_t *t)
{
    return fix_dist(t->x - gs->player.x, t->y - gs->player.y);
}

int combat_visible(const game_state *gs, uint16_t id, fix_t radius)
{
    const thinker_t *t = get(gs, id);
    if (!t || !thinker_alive(t) || !(t->flags & TH_FLAG_SHOOTABLE))
        return 0;
    if (radius > 0 && dist_to(gs, t) > radius)
        return 0;
    return ai_line_of_sight(gs, t);
}

uint16_t combat_nearest(const game_state *gs, fix_t radius)
{
    uint16_t best = 0;
    fix_t best_d = 0;
    /* Ascending ids with a strict `<`: the lower id wins a tie. */
    for (int i = 0; i < gs->things.count; i++) {
        const uint16_t id = (uint16_t)(i + 1);
        if (!combat_visible(gs, id, radius))
            continue;
        const fix_t d = dist_to(gs, &gs->things.list[i]);
        if (!best || d < best_d) {
            best = id;
            best_d = d;
        }
    }
    return best;
}

int32_t combat_aim(const game_state *gs, uint16_t id, ticcmd *out)
{
    memset(out, 0, sizeof *out);
    const thinker_t *t = get(gs, id);
    if (!t)
        return 0;
    const angle_t want = angle_of(t->x - gs->player.x, t->y - gs->player.y);
    const int32_t err = (int16_t)(uint16_t)(want - gs->player.angle);
    int32_t turn = err;
    if (turn > PLAYER_TURN_SPEED)
        turn = PLAYER_TURN_SPEED;
    if (turn < -PLAYER_TURN_SPEED)
        turn = -PLAYER_TURN_SPEED;
    out->turn = (int16_t)turn;
    const int32_t left = err - turn;
    return left < 0 ? -left : left;
}

int8_t combat_strafe(int32_t t)
{
    return (int8_t)(t % COMBAT_STRAFE_PERIOD < COMBAT_STRAFE_LEFT_TICS ? -COMBAT_STRAFE_SPEED
                                                                       : COMBAT_STRAFE_SPEED);
}
