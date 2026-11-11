#include "game/thinker.h"

#include <string.h>

#include "engine/sprite.h"
#include "game/ai.h"
#include "game/enemy_info.h"

int thinker_alive(const thinker_t *t)
{
    return !(t->flags & TH_FLAG_PICKUP) && t->type < TH_TYPE_SCARAB_PROJ &&
           t->state != TH_STATE_DEAD && t->state != TH_STATE_TAKEN &&
           t->state != TH_STATE_DYING && t->state != TH_STATE_FREE;
}

uint16_t thinker_spawn(thinkers_t *th, uint16_t type, fix_t x, fix_t y, angle_t angle)
{
    const enemy_info_t *info = enemy_info(type);
    if (th->count >= MAX_THINKERS || !info)
        return 0;
    thinker_t *t = &th->list[th->count++];
    memset(t, 0, sizeof *t);
    t->type = type;
    t->state = TH_STATE_STAND;
    t->flags = TH_FLAG_COUNTKILL | TH_FLAG_SHOOTABLE;
    t->x = x;
    t->y = y;
    t->angle = angle;
    t->health = info->hp;
    t->max_health = info->hp;
    t->radius = info->radius;
    t->movedir = DI_NONE;
    th->kill_total++;
    return (uint16_t)th->count;
}

uint16_t thinker_spawn_pickup(thinkers_t *th, uint16_t type, fix_t x, fix_t y)
{
    if (th->count >= MAX_THINKERS || type < BAST_ENT_ANKH_PISTOL || type > BAST_ENT_SPHINX_HOWLER)
        return 0;
    thinker_t *t = &th->list[th->count++];
    memset(t, 0, sizeof *t);
    t->type = type;
    t->state = TH_STATE_STAND;
    t->flags = TH_FLAG_PICKUP;
    t->x = x;
    t->y = y;
    return (uint16_t)th->count;
}

uint16_t thinker_spawn_barrel(thinkers_t *th, fix_t x, fix_t y)
{
    if (th->count >= MAX_THINKERS)
        return 0;
    thinker_t *t = &th->list[th->count++];
    memset(t, 0, sizeof *t);
    t->type = BAST_ENT_KIBBLE_BARREL;
    t->state = TH_STATE_STAND;
    t->flags = TH_FLAG_SHOOTABLE;
    t->x = x;
    t->y = y;
    t->health = BARREL_HP;
    t->max_health = BARREL_HP;
    t->radius = BARREL_RADIUS;
    return (uint16_t)th->count;
}

uint16_t thinker_alloc(thinkers_t *th)
{
    for (int i = 0; i < th->count; i++) {
        if (th->list[i].state == TH_STATE_FREE) {
            memset(&th->list[i], 0, sizeof th->list[i]);
            return (uint16_t)(i + 1);
        }
    }
    if (th->count >= MAX_THINKERS)
        return 0;
    memset(&th->list[th->count], 0, sizeof th->list[0]);
    return (uint16_t)++th->count;
}

int entity_on_skill(const bast_map_entity *e, int skill)
{
    return (e->flags & (uint16_t)(BAST_ENT_SKILL1 << (skill - 1))) != 0;
}

void thinkers_spawn_map(thinkers_t *th, const bast_map *map, int skill)
{
    memset(th, 0, sizeof *th);
    for (int i = 0; i < map->entity_count; i++) {
        const bast_map_entity *e = &map->entities[i];
        if (!entity_on_skill(e, skill))
            continue;
        thinker_t *en = thinker_get(th, thinker_spawn(th, e->type, e->x, e->y, e->angle));
        if (en) {
            if (e->flags & BAST_ENT_AMBUSH)
                en->flags |= TH_FLAG_AMBUSH;
            en->tag = e->tag; /* for KILL_TRIGGER (game/tags.c) */
            continue;
        }
        if (e->type == BAST_ENT_KIBBLE_BARREL) {
            thinker_spawn_barrel(th, e->x, e->y);
            continue;
        }
        thinker_t *t = thinker_get(th, thinker_spawn_pickup(th, e->type, e->x, e->y));
        if (t) {
            t->tag = e->tag;
            if (e->flags & BAST_ENT_PICKUP_TRIGGER)
                t->flags |= TH_FLAG_TRIGGER;
        }
    }
}

thinker_t *thinker_get(thinkers_t *th, uint16_t id)
{
    return id >= 1 && id <= th->count ? &th->list[id - 1] : NULL;
}

int thinker_damage(thinkers_t *th, thinker_t *t, int32_t damage)
{
    if (!thinker_alive(t) || !(t->flags & TH_FLAG_SHOOTABLE))
        return 0;
    t->health -= damage;
    if (t->health > 0)
        return 0;
    if (t->type == BAST_ENT_KIBBLE_BARREL) {
        t->state = TH_STATE_DYING;
        t->tics = BARREL_FUSE_TICS;
        return 1;
    }
    t->state = TH_STATE_DEAD;
    t->tics = 0;
    /* enemies.md Animation list: the boss has no `gib`, its death plays. */
    if (t->health <= -(t->max_health / 2) && t->type != BAST_ENT_WOOFU)
        t->flags |= TH_FLAG_GIB;
    if (t->flags & TH_FLAG_COUNTKILL)
        th->kills++;
    return 1;
}

static int clamp_frame(uint32_t since, int tics_per_frame, int frames)
{
    const uint32_t f = since / (uint32_t)tics_per_frame;
    return f >= (uint32_t)frames ? frames - 1 : (int)f;
}

void thinker_anim(const thinker_t *t, uint32_t tic, int *anim, int *frame)
{
    const enemy_info_t *info = enemy_info(t->type);
    int a = SPR_IDLE, f = 0;
    switch (t->state) {
    case TH_STATE_CHASE:
        a = SPR_WALK;
        if (t->movedir != DI_NONE)
            f = (int)((tic / TH_ANIM_WALK_TICS) % (uint32_t)sprite_enemy_frames(SPR_WALK));
        break;
    case TH_STATE_WINDUP:
        a = SPR_WINDUP;
        f = info && t->tics <= info->windup / 2 ? 1 : 0;
        break;
    case TH_STATE_ATTACK:
        a = SPR_ATTACK;
        f = t->tics > AI_ATTACK_TICS / 2 ? 0 : 1;
        break;
    case TH_STATE_PAIN:
        a = SPR_PAIN;
        break;
    case TH_STATE_DEAD: {
        const uint32_t since = tic >= t->died_tic ? tic - t->died_tic : 0;
        if (t->flags & TH_FLAG_GIB) {
            a = SPR_GIB;
            f = clamp_frame(since, TH_ANIM_GIB_TICS, sprite_enemy_frames(SPR_GIB));
        } else {
            a = SPR_DEATH;
            f = clamp_frame(since, TH_ANIM_DEATH_TICS, sprite_enemy_frames(SPR_DEATH));
        }
        break;
    }
    default:
        f = (int)((tic / TH_ANIM_IDLE_TICS) % (uint32_t)sprite_enemy_frames(SPR_IDLE));
        break;
    }
    *anim = a;
    *frame = f;
}
