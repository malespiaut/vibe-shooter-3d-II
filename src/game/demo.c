#include "game/demo.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TOKENS 8

typedef struct {
    int lineno;
    int ntok;
    char *tok[MAX_TOKENS];
    char *err;
    size_t errlen;
} line_ctx;

static int fail(line_ctx *c, const char *fmt, ...)
{
    int k = snprintf(c->err, c->errlen, "line %d: ", c->lineno);
    if (k < 0 || (size_t)k >= c->errlen)
        return 0;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(c->err + k, c->errlen - (size_t)k, fmt, ap);
    va_end(ap);
    return 0;
}

/* Strict decimal integer in [lo, hi]. */
static int parse_int(line_ctx *c, const char *s, long lo, long hi, const char *what, long *out)
{
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (*s == '\0' || *end != '\0' || errno)
        return fail(c, "%s: '%s' is not an integer", what, s);
    if (v < lo || v > hi)
        return fail(c, "%s: %ld out of range %ld..%ld", what, v, lo, hi);
    *out = v;
    return 1;
}

static int want_args(line_ctx *c, int n)
{
    if (c->ntok - 1 != n)
        return fail(c, "'%s' takes %d argument%s, got %d", c->tok[0], n, n == 1 ? "" : "s",
                    c->ntok - 1);
    return 1;
}

static demo_cmd *push(demo_t *d, line_ctx *c, int kind)
{
    if (d->count == d->cap) {
        int cap = d->cap ? d->cap * 2 : 64;
        demo_cmd *n = realloc(d->cmds, (size_t)cap * sizeof *n);
        if (!n) {
            fail(c, "out of memory");
            return NULL;
        }
        d->cmds = n;
        d->cap = cap;
    }
    demo_cmd *cmd = &d->cmds[d->count++];
    memset(cmd, 0, sizeof *cmd);
    cmd->kind = kind;
    cmd->line = c->lineno;
    return cmd;
}

static int parse_buttons(line_ctx *c, const char *s, uint8_t *out)
{
    uint8_t b = 0;
    if (strcmp(s, "-") == 0) {
        *out = 0;
        return 1;
    }
    for (const char *p = s; *p; p++) {
        uint8_t bit;
        if (*p == 'F')
            bit = BT_FIRE;
        else if (*p == 'U')
            bit = BT_USE;
        else if (*p == 'R')
            bit = BT_RUN;
        else if (*p >= '1' && *p <= '7') {
            if (b & BT_WEAPON_MASK)
                return fail(c, "buttons '%s': more than one weapon", s);
            bit = (uint8_t)((*p - '0') << BT_WEAPON_SHIFT);
        } else
            return fail(c, "buttons '%s': unknown button '%c' (F U R 1-7 or -)", s, *p);
        if (b & bit && !(bit & BT_WEAPON_MASK))
            return fail(c, "buttons '%s': '%c' repeated", s, *p);
        b |= bit;
    }
    *out = b;
    return 1;
}

/* in <n> <fwd> <side> <turn> <buttons> */
static int cmd_in(demo_t *d, line_ctx *c)
{
    long n, fwd, side, turn;
    uint8_t buttons;
    if (!want_args(c, 5) || !parse_int(c, c->tok[1], 1, 1000000, "tics", &n) ||
        !parse_int(c, c->tok[2], -TICCMD_MAX, TICCMD_MAX, "fwd", &fwd) ||
        !parse_int(c, c->tok[3], -TICCMD_MAX, TICCMD_MAX, "side", &side) ||
        !parse_int(c, c->tok[4], INT16_MIN, INT16_MAX, "turn", &turn) ||
        !parse_buttons(c, c->tok[5], &buttons))
        return 0;
    demo_cmd *cmd = push(d, c, DEMO_CMD_IN);
    if (!cmd)
        return 0;
    cmd->n = (int32_t)n;
    cmd->cmd.fwd = (int8_t)fwd;
    cmd->cmd.side = (int8_t)side;
    cmd->cmd.turn = (int16_t)turn;
    cmd->cmd.buttons = buttons;
    return 1;
}

/* wait <n> */
static int cmd_wait(demo_t *d, line_ctx *c)
{
    long n;
    if (!want_args(c, 1) || !parse_int(c, c->tok[1], 1, 1000000, "tics", &n))
        return 0;
    demo_cmd *cmd = push(d, c, DEMO_CMD_WAIT);
    if (!cmd)
        return 0;
    cmd->n = (int32_t)n;
    return 1;
}

/* Header lines, allowed only before the first body command. */
static int hdr_level(demo_t *d, line_ctx *c)
{
    long v;
    if (!want_args(c, 1) || !parse_int(c, c->tok[1], 1, 99, "level", &v))
        return 0;
    d->level = (int)v;
    return 1;
}

static int hdr_skill(demo_t *d, line_ctx *c)
{
    long v;
    if (!want_args(c, 1) || !parse_int(c, c->tok[1], 1, 3, "skill", &v))
        return 0;
    d->skill = (int)v;
    return 1;
}

static int hdr_seed(demo_t *d, line_ctx *c)
{
    char *end;
    if (!want_args(c, 1))
        return 0;
    errno = 0;
    unsigned long long v = strtoull(c->tok[1], &end, 10);
    if (*end || errno || v > UINT32_MAX || c->tok[1][0] == '-')
        return fail(c, "seed: '%s' is not a 32-bit unsigned integer", c->tok[1]);
    d->seed = (uint32_t)v;
    return 1;
}

/* map <name>: plays maps/<name>.vmap instead of maps/level<N>.vmap (test
 * maps such as m1test that are not numbered levels). */
static int hdr_map(demo_t *d, line_ctx *c)
{
    if (!want_args(c, 1))
        return 0;
    const char *s = c->tok[1];
    if (strlen(s) >= DEMO_MAP_NAME_MAX)
        return fail(c, "map name too long");
    for (const char *p = s; *p; p++)
        if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-')
            return fail(c, "map name '%s': only letters, digits, '_' and '-'", s);
    strcpy(d->map, s);
    return 1;
}

typedef struct {
    const char *name;
    int (*parse)(demo_t *d, line_ctx *c);
} cmd_entry;

static const cmd_entry header_cmds[] = {
    { "level", hdr_level },
    { "skill", hdr_skill },
    { "seed", hdr_seed },
    { "map", hdr_map },
};

/* The body dispatch table. */
static const cmd_entry body_cmds[] = {
    { "in", cmd_in },
    { "wait", cmd_wait },
};

#define COUNT(a) (int)(sizeof(a) / sizeof((a)[0]))

static const cmd_entry *find(const cmd_entry *t, int n, const char *name)
{
    for (int i = 0; i < n; i++)
        if (strcmp(t[i].name, name) == 0)
            return &t[i];
    return NULL;
}

demo_t *demo_parse(const char *text, char *err, size_t errlen)
{
    demo_t *d = calloc(1, sizeof *d);
    char *buf = malloc(strlen(text) + 1);
    if (!d || !buf) {
        snprintf(err, errlen, "out of memory");
        free(d);
        free(buf);
        return NULL;
    }
    strcpy(buf, text);
    d->level = 1;
    d->skill = 2;

    line_ctx c = { 0 };
    c.err = err;
    c.errlen = errlen;
    int seen_version = 0, in_body = 0, ok = 1;
    char *line = buf;
    while (ok && line) {
        char *next = strchr(line, '\n');
        if (next)
            *next++ = '\0';
        c.lineno++;
        char *hash = strchr(line, '#');
        if (hash)
            *hash = '\0';
        c.ntok = 0;
        for (char *t = strtok(line, " \t\r"); t; t = strtok(NULL, " \t\r")) {
            if (c.ntok == MAX_TOKENS) {
                ok = fail(&c, "too many fields");
                break;
            }
            c.tok[c.ntok++] = t;
        }
        line = next;
        if (!ok || c.ntok == 0)
            continue;

        const char *name = c.tok[0];
        if (!seen_version) {
            if (strcmp(name, "vsdemo") != 0)
                ok = fail(&c, "expected 'vsdemo 1' as the first line, got '%s'", name);
            else if (c.ntok != 2 || strcmp(c.tok[1], "1") != 0)
                ok = fail(&c, "unsupported demo version (want 'vsdemo 1')");
            seen_version = 1;
            continue;
        }
        const cmd_entry *e = find(header_cmds, COUNT(header_cmds), name);
        if (e) {
            if (in_body)
                ok = fail(&c, "header line '%s' after the first command", name);
            else
                ok = e->parse(d, &c);
            continue;
        }
        e = find(body_cmds, COUNT(body_cmds), name);
        if (!e) {
            ok = fail(&c, "unknown command '%s'", name);
            continue;
        }
        in_body = 1;
        ok = e->parse(d, &c);
    }
    if (ok && !seen_version)
        ok = fail(&c, "empty demo (missing 'vsdemo 1')");
    free(buf);
    if (!ok) {
        demo_free(d);
        return NULL;
    }
    return d;
}

void demo_free(demo_t *d)
{
    if (!d)
        return;
    free(d->cmds);
    free(d);
}

int demo_next(demo_t *d, const game_state *gs, ticcmd *out, char *err, size_t errlen)
{
    (void)gs; /* read by the high-level commands */
    (void)err;
    (void)errlen;
    while (d->pc < d->count) {
        const demo_cmd *c = &d->cmds[d->pc];
        if (d->done >= c->n) {
            d->pc++;
            d->done = 0;
            continue;
        }
        d->done++;
        switch (c->kind) {
        case DEMO_CMD_IN:
            *out = c->cmd;
            break;
        default: /* DEMO_CMD_WAIT */
            memset(out, 0, sizeof *out);
            break;
        }
        return DEMO_TIC;
    }
    return DEMO_END;
}

void demo_format_buttons(uint8_t b, char *buf)
{
    char *p = buf;
    if (b & BT_FIRE)
        *p++ = 'F';
    if (b & BT_USE)
        *p++ = 'U';
    if (b & BT_RUN)
        *p++ = 'R';
    int w = (b & BT_WEAPON_MASK) >> BT_WEAPON_SHIFT;
    if (w)
        *p++ = (char)('0' + w);
    if (p == buf)
        *p++ = '-';
    *p = '\0';
}

void demo_rec_init(demo_rec *r)
{
    memset(r, 0, sizeof *r);
}

static int same_cmd(const ticcmd *a, const ticcmd *b)
{
    return a->fwd == b->fwd && a->side == b->side && a->turn == b->turn &&
           a->buttons == b->buttons;
}

int demo_rec_add(demo_rec *r, const ticcmd *cmd)
{
    if (r->len > 0 && same_cmd(&r->cmds[r->len - 1], cmd) && r->counts[r->len - 1] < 1000000) {
        r->counts[r->len - 1]++;
        return 1;
    }
    if (r->len == r->cap) {
        int cap = r->cap ? r->cap * 2 : 256;
        int32_t *nc = realloc(r->counts, (size_t)cap * sizeof *nc);
        if (!nc)
            return 0;
        r->counts = nc;
        ticcmd *nk = realloc(r->cmds, (size_t)cap * sizeof *nk);
        if (!nk)
            return 0;
        r->cmds = nk;
        r->cap = cap;
    }
    r->counts[r->len] = 1;
    r->cmds[r->len] = *cmd;
    r->len++;
    return 1;
}

int demo_rec_write(const demo_rec *r, const char *path, int level, int skill, uint32_t seed,
                   const char *map)
{
    FILE *f = fopen(path, "w");
    if (!f)
        return 0;
    fprintf(f, "vsdemo 1\nlevel %d\nskill %d\nseed %u\n", level, skill, (unsigned)seed);
    if (map && map[0])
        fprintf(f, "map %s\n", map);
    for (int i = 0; i < r->len; i++) {
        char b[8];
        demo_format_buttons(r->cmds[i].buttons, b);
        fprintf(f, "in %d %d %d %d %s\n", (int)r->counts[i], r->cmds[i].fwd, r->cmds[i].side,
                r->cmds[i].turn, b);
    }
    return fclose(f) == 0;
}

void demo_rec_free(demo_rec *r)
{
    free(r->counts);
    free(r->cmds);
    demo_rec_init(r);
}
