#include "game/thinker.h"

#include <string.h>

#include "game/enemy_info.h"

int thinker_alive(const thinker_t *t)
{
    return !(t->flags & TH_FLAG_PICKUP) && t->type < TH_TYPE_SCARAB_PROJ &&
           t->state != TH_STATE_DEAD && t->state != TH_STATE_TAKEN &&
           t->state != TH_STATE_DYING && t->state != TH_STATE_FREE &&
           t->state != TH_STATE_PROP;
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

int thinker_is_decor(int type)
{
    return type >= BAST_ENT_KIBBLE_BARREL && type <= BAST_ENT_SERVER_RACK;
}

uint16_t thinker_spawn_decor(thinkers_t *th, uint16_t type, fix_t x, fix_t y)
{
    if (type == BAST_ENT_KIBBLE_BARREL)
        return thinker_spawn_barrel(th, x, y);
    if (th->count >= MAX_THINKERS || !thinker_is_decor(type))
        return 0;
    thinker_t *t = &th->list[th->count++];
    memset(t, 0, sizeof *t);
    t->type = type;
    t->state = TH_STATE_STAND;
    t->x = x;
    t->y = y;
    t->radius = PROP_RADIUS;
    if (type == BAST_ENT_DOG_BANNER) {
        t->state = TH_STATE_PROP;
        t->radius = 0;
    } else if (type == BAST_ENT_PAPYRUS_POT) {
        t->flags = TH_FLAG_SHOOTABLE;
        t->health = POT_HP;
        t->max_health = POT_HP;
    }
    return (uint16_t)th->count;
}

int thinker_gib_frame(const thinker_t *t)
{
    if (t->state != TH_STATE_DEAD ||
        (t->type != BAST_ENT_PAPYRUS_POT && t->type != BAST_ENT_KIBBLE_BARREL))
        return -1;
    const int f = (PROP_GIB_TICS - t->tics) / PROP_GIB_FRAME_TICS;
    return f < 0 ? 0 : f > PROP_GIB_FRAMES - 1 ? PROP_GIB_FRAMES - 1 : f;
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
            continue;
        }
        if (thinker_is_decor(e->type)) {
            thinker_spawn_decor(th, e->type, e->x, e->y);
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
    if (t->type == BAST_ENT_PAPYRUS_POT) {
        t->state = TH_STATE_DEAD;
        t->tics = PROP_GIB_TICS;
        t->flags |= TH_FLAG_GIB;
        return 1;
    }
    t->state = TH_STATE_DEAD;
    t->tics = 0;
    if (t->health <= -(t->max_health / 2))
        t->flags |= TH_FLAG_GIB;
    if (t->flags & TH_FLAG_COUNTKILL)
        th->kills++;
    return 1;
}
