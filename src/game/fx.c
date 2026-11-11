#include "game/fx.h"

#include "engine/sprite.h"
#include "game/projectile.h"
#include "game/state.h"

int32_t fx_lifetime(int kind)
{
    /* Frame counts of assets/src/fx/puff.json and blood_hit.json. */
    switch (kind) {
    case FX_PUFF:
        return FX_PUFF_FRAMES * FX_TICS_PER_FRAME;
    case FX_BLOOD:
        return FX_BLOOD_FRAMES * FX_TICS_PER_FRAME;
    default:
        return 0;
    }
}

void fx_spawn(game_state *gs, int kind, fix_t x, fix_t y)
{
    const int32_t life = fx_lifetime(kind);
    if (!life)
        return;
    fx_list_t *fl = &gs->fx;
    fx_t *f = &fl->list[fl->spawned % MAX_FX];
    fl->spawned++;
    *f = (fx_t){ (uint8_t)kind, life, x, y };
}

void fx_tic(game_state *gs)
{
    for (int i = 0; i < MAX_FX; i++) {
        fx_t *f = &gs->fx.list[i];
        if (f->kind != FX_NONE && --f->tics <= 0)
            *f = (fx_t){ 0 };
    }
}

int fx_count(const game_state *gs)
{
    int n = 0;
    for (int i = 0; i < MAX_FX; i++)
        n += gs->fx.list[i].kind != FX_NONE;
    return n;
}

/* Frame of a one-shot effect with `life` tics in total and `left` to go. */
static int oneshot_frame(int32_t life, int32_t left)
{
    const int32_t f = (life - left) / FX_TICS_PER_FRAME;
    return f < 0 ? 0 : (int)f;
}

int fx_render_things(const game_state *gs, render_thing *out, int n, int max)
{
    for (int i = 0; i < gs->things.count && n < max; i++) {
        const thinker_t *t = &gs->things.list[i];
        if (t->state != TH_STATE_STAND)
            continue;
        int spr, frame;
        fix_t z = t->z;
        switch (t->type) {
        case TH_TYPE_SCARAB_PROJ: /* looping flight, animated by tic */
            spr = FXS_SCARAB_PROJ;
            frame = (int)(gs->tic / FX_TICS_PER_FRAME) % FX_SCARAB_FRAMES;
            break;
        case TH_TYPE_EXPLOSION_SMALL:
        case TH_TYPE_EXPLOSION_BIG:
            spr = t->type == TH_TYPE_EXPLOSION_SMALL ? FXS_EXPLOSION_SMALL : FXS_EXPLOSION_BIG;
            frame = oneshot_frame(fx_tics(t->type), t->tics);
            /* A barrel blast starts on the floor (z 0): centre it on the
             * barrel's height instead so it does not sink into the floor. */
            if (z < FX_EXPLOSION_MIN_Z)
                z = FX_EXPLOSION_MIN_Z;
            break;
        default:
            continue;
        }
        out[n++] = (render_thing){ .type = (uint16_t)(SPRITE_FX_BASE + spr), .x = t->x, .y = t->y,
                                   .z = z, .frame = (uint8_t)frame };
    }
    /* Oldest first, so ties in depth draw the newest on top. */
    const fx_list_t *fl = &gs->fx;
    for (uint32_t k = 0; k < MAX_FX && n < max; k++) {
        const fx_t *f = &fl->list[(fl->spawned + k) % MAX_FX];
        if (f->kind == FX_NONE)
            continue;
        const int spr = f->kind == FX_PUFF ? FXS_PUFF : FXS_BLOOD;
        out[n++] = (render_thing){ .type = (uint16_t)(SPRITE_FX_BASE + spr), .x = f->x, .y = f->y,
                                   .z = FX_HIT_Z,
                                   .frame = (uint8_t)oneshot_frame(fx_lifetime(f->kind), f->tics) };
    }
    return n;
}
