#ifndef GAME_ITEMS_H
#define GAME_ITEMS_H

/* Pickups, inventory and player damage (items.md, weapons.md Ammo types,
 * tech.md Skill). Simulation only: the sprite renderer draws pickups. */

#include "engine/render.h"
#include "game/state.h"

#define HEALTH_CAP 100         /* Fish Snack, Tuna Tin */
#define HEALTH_SUPER_CAP 200   /* Golden Sardine */
#define ARMOUR_CAP 200
#define CATNIP_TICS 1050
#define CATSEYE_TICS 2100
#define NINTH_LIFE_HEALTH 50
#define NINTH_LIFE_INVULN_TICS 70

/* True for entity types that are pickups (weapons 32..47, items 48..95). */
int item_is_pickup(int type);

/* Starting inventory beyond weapons and ammo (weapons_init owns those):
 * health 100, armour 0, no Satchel or powerups. Keys stay with the door
 * state (game_init zeroes them, game_give_keys and key pickups set them). */
void player_start_inventory(player_t *p);

/* Spawns a pickup_t for every non-weapon pickup entity of the map whose
 * SKILLn flag matches gs->skill, in entity order (weapon pickups are
 * thinkers, spawned by thinkers_spawn_map, which must run first). Sets
 * items_total to both, and resets the items counter. */
void items_spawn(game_state *gs);

/* Applies pickup `type` to the player. Returns 1 when it was used (and must
 * disappear), 0 when the player cannot use it (it stays). */
int item_apply(game_state *gs, int type);

/* Takes every pickup within PICKUP_RADIUS of the player centre that the
 * player can use, weapon-pickup thinkers included (weapon_give_ex);
 * PICKUP_TRIGGER pickups fire their tag. Called by game_tic after the move,
 * before weapon_tic. */
void items_touch(game_state *gs);

/* Fills `out` (up to `max`) with every pickup still lying in the world:
 * untaken pickup_t in spawn order, then, in creation order, live
 * weapon-pickup thinkers and every decoration thinker (a broken pot or an
 * exploded barrel as its gib frame, thinker_gib_frame()). Read only (the renderer's view of live state). Returns
 * the count. */
int items_render_things(const game_state *gs, render_thing *out, int max);

/* Counts down powerup and invulnerability timers. Called by game_tic. */
void items_tic(game_state *gs);

/* Damages the player by `amount` (> 0): Kitten halves it (min 1), armour
 * absorbs 1/3 (Linen) or 1/2 (Plate) rounded down, capped by the armour
 * left, and drains by what it absorbed. A lethal hit with Ninth Life
 * carried leaves 50 health, armour as it was before the hit, and 70 tics of
 * invulnerability; otherwise health 0 and dead. Ignored while invulnerable
 * or dead. */
void player_damage(game_state *gs, int32_t amount);

/* player_damage from a source at (sx, sy), for the HUD's hurt direction.
 * Both post a GEV_DAMAGE event for every hit that is not ignored. */
void player_damage_from(game_state *gs, int32_t amount, fix_t sx, fix_t sy);

#endif
