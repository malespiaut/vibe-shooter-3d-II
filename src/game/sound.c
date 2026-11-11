#include "game/sound.h"

#include "game/state.h"

static const char *const names[SFX_COUNT] = {
#define SFX(name) #name,
#include "game/sfx_names.h"
#undef SFX
};

const char *sfx_name(int id)
{
    return id >= 0 && id < SFX_COUNT ? names[id] : NULL;
}

static void push(game_state *gs, int sfx_id, int local, fix_t x, fix_t y)
{
    sound_queue *q = &gs->sounds;
    if (sfx_id < 0 || sfx_id >= SFX_COUNT || q->count >= SOUND_QUEUE_MAX)
        return;
    q->ev[q->count++] = (sound_event){ (uint16_t)sfx_id, (uint8_t)local, x, y };
}

void game_sound(game_state *gs, int sfx_id, fix_t x, fix_t y)
{
    push(gs, sfx_id, 0, x, y);
}

void game_sound_local(game_state *gs, int sfx_id)
{
    push(gs, sfx_id, 1, gs->player.x, gs->player.y);
}
