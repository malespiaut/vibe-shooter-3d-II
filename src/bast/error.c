#include "bast_internal.h"

#include <stdarg.h>
#include <stdio.h>

static char err_buf[256] = "no error";

const char *bast_error(void)
{
    return err_buf;
}

void bast_set_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err_buf, sizeof err_buf, fmt, ap);
    va_end(ap);
}
