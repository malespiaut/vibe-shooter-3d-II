#ifndef ENGINE_FILES_H
#define ENGINE_FILES_H

/* Asset file access (docs/design/tech.md, "Archives"): `<dir>/game.pak`
 * first, loose files under `<dir>` when there is no archive or the name is
 * not in it. Names are relative to assets/gen with '/' ("sfx/door.snd"). */

#include <stdint.h>

/* Use `dir` (the assets/gen directory) and open `<dir>/game.pak` if it
 * exists. A no-op when `dir` is already the current one. Returns 0 when
 * game.pak exists but is not a valid archive (see files_error()); loose
 * files are then still served. */
int files_init(const char *dir);
void files_shutdown(void);
/* 1 when game.pak is open. */
int files_have_pak(void);
/* Whole file as a malloc'd buffer (free() it), size in *size. NULL when the
 * file is in neither place (see files_error()). */
uint8_t *files_read(const char *name, int64_t *size);
const char *files_error(void);

#endif
