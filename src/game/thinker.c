#include "game/thinker.h"

#include <string.h>

/* enemies.md: HP and size (w units); radius = w / 2 in 16.16. */
static const struct {
    uint16_t type;
    int32_t hp;
    fix_t radius;
} ENEMIES[] = {
    { BAST_ENT_MUTT_TROOPER, 20, 16384 },              /* 0.5 wide */
    { BAST_ENT_CHIHUAHUA_CHOMPER, 15, 12288 },         /* 0.375 */
    { BAST_ENT_POMERANIAN_PLASMAGUNNER, 45, 20480 },   /* 0.625 */
    { BAST_ENT_DACHSHUND_HOWITZER, 70, 32768 },        /* 1.0 */
    { BAST_ENT_MASTIFF_JUGGERNAUT, 400, 29491 },       /* 0.9 */
    { BAST_ENT_SHEPHERD_SHIELDBEARER, 90, 20480 },     /* 0.625 */
    { BAST_ENT_WOOFU, 2400, 49152 },                   /* 1.5 */
};

int thinker_enemy_info(uint16_t type, int32_t *hp, fix_t *radius)
{
    for (size_t i = 0; i < sizeof ENEMIES / sizeof ENEMIES[0]; i++) {
        if (ENEMIES[i].type == type) {
            *hp = ENEMIES[i].hp;
            *radius = ENEMIES[i].radius;
            return 1;
        }
    }
    return 0;
}

uint16_t thinker_spawn(thinkers_t *th, uint16_t type, fix_t x, fix_t y, angle_t angle)
{
    int32_t hp;
    fix_t radius;
    if (th->count >= MAX_THINKERS || !thinker_enemy_info(type, &hp, &radius))
        return 0;
    thinker_t *t = &th->list[th->count++];
    memset(t, 0, sizeof *t);
    t->type = type;
    t->state = TH_STATE_STAND;
    t->flags = TH_FLAG_COUNTKILL;
    t->x = x;
    t->y = y;
    t->angle = angle;
    t->health = hp;
    t->max_health = hp;
    t->radius = radius;
    th->kill_total++;
    return (uint16_t)th->count;
}

void thinkers_spawn_map(thinkers_t *th, const bast_map *map, int skill)
{
    memset(th, 0, sizeof *th);
    const uint16_t skill_bit = (uint16_t)(BAST_ENT_SKILL1 << (skill - 1));
    for (int i = 0; i < map->entity_count; i++) {
        const bast_map_entity *e = &map->entities[i];
        if (e->flags & skill_bit)
            thinker_spawn(th, e->type, e->x, e->y, e->angle);
    }
}

thinker_t *thinker_get(thinkers_t *th, uint16_t id)
{
    return id >= 1 && id <= th->count ? &th->list[id - 1] : NULL;
}

int thinker_damage(thinkers_t *th, thinker_t *t, int32_t damage)
{
    if (t->state == TH_STATE_DEAD)
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
