#include "game/thinker.h"

#include <string.h>

#include "game/enemy_info.h"

int thinker_alive(const thinker_t *t)
{
    return !(t->flags & TH_FLAG_PICKUP) && t->state != TH_STATE_DEAD &&
           t->state != TH_STATE_TAKEN;
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
    t->state = TH_STATE_DEAD;
    t->tics = 0;
    if (t->health <= -(t->max_health / 2))
        t->flags |= TH_FLAG_GIB;
    if (t->flags & TH_FLAG_COUNTKILL)
        th->kills++;
    return 1;
}
