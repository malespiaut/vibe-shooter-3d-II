#ifndef ENGINE_SPRITE_H
#define ENGINE_SPRITE_H

/* Sprite frame selection (GAME.md 3, docs/design/tech.md World: Sprites,
 * Sprite naming). Integer only: gameplay code may call it. */

#include "engine/angle.h"

enum { SPRITE_DIRS = 8 };

/* Direction frame 0..7 for a thing facing `thing_angle`, seen by a viewer
 * that lies in direction `viewer_angle` from the thing (the angle of the
 * vector thing -> viewer). Naming contract: d0 = the thing faces the camera,
 * then the camera moves counter-clockwise around it in 45 degree steps
 * (d2 = camera on the thing's left, d4 = its back, d6 = its right).
 * Each direction covers [k*45 - 22.5, k*45 + 22.5) degrees of
 * viewer_angle - thing_angle. */
int sprite_dir(angle_t viewer_angle, angle_t thing_angle);

/* Sprite base name (`<sprite>` in `<sprite>_<anim>_d<dir>_f<frame>.tex`)
 * drawn for map entity `type`, or NULL if that type has no sprite yet. */
const char *sprite_name_for_entity(int type);

#endif
