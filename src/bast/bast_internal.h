#ifndef BAST_INTERNAL_H
#define BAST_INTERNAL_H

#include "bast.h"

void bast_set_error(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Read a whole file. Returns malloc'd buffer (caller frees) or NULL. */
uint8_t *bast_read_file(const char *path, int64_t *size);

#endif
