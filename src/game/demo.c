#include "game/demo.h"

#include "game/demo_combat.h"
#include "game/demo_nav.h"
#include "game/flow.h"

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

/* Map coordinates: maps are at most 64x64. */
static int parse_tile(line_ctx *c, int first, int32_t *tx, int32_t *ty)
{
    long x, y;
    if (!parse_int(c, c->tok[first], 0, 63, "tile x", &x) ||
        !parse_int(c, c->tok[first + 1], 0, 63, "tile y", &y))
        return 0;
    *tx = (int32_t)x;
    *ty = (int32_t)y;
    return 1;
}

/* goto <tx> <ty> [max] | goto @<name> [max] */
static int cmd_goto(demo_t *d, line_ctx *c)
{
    int nargs = c->ntok - 1;
    int named = nargs >= 1 && c->tok[1][0] == '@';
    int need = named ? 1 : 2;
    if (nargs != need && nargs != need + 1)
        return fail(c, "'goto' takes <tx> <ty> [max] or @<name> [max]");
    demo_cmd tmp = { 0 };
    if (named) {
        const char *nm = c->tok[1] + 1;
        if (!*nm || strlen(nm) >= DEMO_NAME_MAX)
            return fail(c, "goto: waypoint name must be 1-%d characters", DEMO_NAME_MAX - 1);
        strcpy(tmp.name, nm);
    } else if (!parse_tile(c, 1, &tmp.tx, &tmp.ty)) {
        return 0;
    }
    long max = DEMO_GOTO_DEFAULT_MAX;
    if (nargs == need + 1 && !parse_int(c, c->tok[need + 1], 1, 1000000, "max", &max))
        return 0;
    demo_cmd *cmd = push(d, c, DEMO_CMD_GOTO);
    if (!cmd)
        return 0;
    tmp.kind = cmd->kind;
    tmp.line = cmd->line;
    tmp.n = (int32_t)max;
    *cmd = tmp;
    return 1;
}

/* face <angle_deg> | face <tx> <ty> | face nearest */
static int cmd_face(demo_t *d, line_ctx *c)
{
    demo_cmd tmp = { 0 };
    if (c->ntok == 2 && strcmp(c->tok[1], "nearest") == 0) {
        return push(d, c, DEMO_CMD_FACE_NEAREST) != NULL;
    } else if (c->ntok == 2) {
        long deg;
        if (!parse_int(c, c->tok[1], -360, 360, "angle", &deg))
            return 0;
        tmp.angle = nav_deg_to_angle((int32_t)deg);
    } else if (c->ntok == 3) {
        if (!parse_tile(c, 1, &tmp.tx, &tmp.ty))
            return 0;
        tmp.has_tile = 1;
    } else {
        return fail(c, "'face' takes <angle_deg>, <tx> <ty> or nearest");
    }
    demo_cmd *cmd = push(d, c, DEMO_CMD_FACE);
    if (!cmd)
        return 0;
    tmp.kind = cmd->kind;
    tmp.line = cmd->line;
    *cmd = tmp;
    return 1;
}

/* use: one tic of BT_USE. */
static int cmd_use(demo_t *d, line_ctx *c)
{
    if (!want_args(c, 0))
        return 0;
    demo_cmd *cmd = push(d, c, DEMO_CMD_IN);
    if (!cmd)
        return 0;
    cmd->n = 1;
    cmd->cmd.buttons = BT_USE;
    return 1;
}

/* weapon <n>: one tic selecting slot n. */
static int cmd_weapon(demo_t *d, line_ctx *c)
{
    long n;
    if (!want_args(c, 1) || !parse_int(c, c->tok[1], 1, 7, "weapon slot", &n))
        return 0;
    demo_cmd *cmd = push(d, c, DEMO_CMD_IN);
    if (!cmd)
        return 0;
    cmd->n = 1;
    cmd->cmd.buttons = (uint8_t)(n << BT_WEAPON_SHIFT);
    return 1;
}

/* fire <n>: hold fire for n tics. fire nearest [max]: see demo_next. */
static int cmd_fire(demo_t *d, line_ctx *c)
{
    long n;
    if (c->ntok >= 2 && strcmp(c->tok[1], "nearest") == 0) {
        long max = COMBAT_FIRE_DEFAULT_MAX;
        if (c->ntok > 3)
            return fail(c, "'fire nearest' takes [max]");
        if (c->ntok == 3 && !parse_int(c, c->tok[2], 1, 1000000, "max", &max))
            return 0;
        demo_cmd *cmd = push(d, c, DEMO_CMD_FIRE_NEAREST);
        if (!cmd)
            return 0;
        cmd->n = (int32_t)max;
        return 1;
    }
    if (!want_args(c, 1) || !parse_int(c, c->tok[1], 1, 1000000, "tics", &n))
        return 0;
    demo_cmd *cmd = push(d, c, DEMO_CMD_IN);
    if (!cmd)
        return 0;
    cmd->n = (int32_t)n;
    cmd->cmd.buttons = BT_FIRE;
    return 1;
}

/* clear [radius] [max] */
static int cmd_clear(demo_t *d, line_ctx *c)
{
    long radius = COMBAT_CLEAR_DEFAULT_RADIUS, max = COMBAT_CLEAR_DEFAULT_MAX;
    if (c->ntok > 3)
        return fail(c, "'clear' takes [radius] [max]");
    if (c->ntok >= 2 && !parse_int(c, c->tok[1], 1, 64, "radius", &radius))
        return 0;
    if (c->ntok == 3 && !parse_int(c, c->tok[2], 1, 1000000, "max", &max))
        return 0;
    demo_cmd *cmd = push(d, c, DEMO_CMD_CLEAR);
    if (!cmd)
        return 0;
    cmd->value = (int32_t)radius;
    cmd->n = (int32_t)max;
    return 1;
}

/* key <up|down|enter|esc|any>: one tic pressing a menu key (game/flow.h). */
static int cmd_key(demo_t *d, line_ctx *c)
{
    static const char *const NAMES[] = { "up", "down", "enter", "esc", "any" };
    static const uint8_t KEYS[] = { FKEY_UP, FKEY_DOWN, FKEY_ENTER, FKEY_ESC, FKEY_ANY };
    if (!want_args(c, 1))
        return 0;
    for (int i = 0; i < 5; i++) {
        if (strcmp(c->tok[1], NAMES[i]) == 0) {
            demo_cmd *cmd = push(d, c, DEMO_CMD_IN);
            if (!cmd)
                return 0;
            cmd->n = 1;
            cmd->key = KEYS[i];
            return 1;
        }
    }
    return fail(c, "key: '%s' is not one of up down enter esc any", c->tok[1]);
}

/* Stats `expect` can read: the STATS line fields that live in game state.
 * kills/items/secrets compare the first number of the X/Y pair. items reads
 * gs->items_got (issue #64). Counters
 * whose systems do not exist yet read 0, exactly as the STATS line prints
 * them (main.c print_stats); they move here when their systems land.
 * level and god are run options, not game state, and are not offered. */
static const char *const STATS[] = { "tic", "health", "armor", "kills",
                                     "items", "secrets", "exit", "tile", "screen" };

static int32_t stat_value(const game_state *gs, const char *name)
{
    if (strcmp(name, "tic") == 0)
        return (int32_t)gs->tic;
    if (strcmp(name, "health") == 0)
        return gs->player.health;
    if (strcmp(name, "armor") == 0)
        return gs->player.armour;
    if (strcmp(name, "kills") == 0)
        return gs->things.kills;
    if (strcmp(name, "items") == 0)
        return gs->items_got;
    if (strcmp(name, "secrets") == 0)
        return gs->secrets;
    if (strcmp(name, "exit") == 0)
        return gs->exit;
    return 0;
}

/* expect <stat><op><value>, or expect tile=X,Y / tile!=X,Y */
static int cmd_expect(demo_t *d, line_ctx *c)
{
    if (c->ntok != 2)
        return fail(c, "'expect' takes one <stat><op><value> field, e.g. health>=20");
    const char *s = c->tok[1];
    size_t k = strcspn(s, "=!<>");
    if (k == 0 || s[k] == '\0')
        return fail(c, "expect '%s': want <stat><op><value>", s);
    char stat[DEMO_NAME_MAX];
    if (k >= sizeof stat)
        return fail(c, "expect: unknown stat '%.*s'", (int)k, s);
    memcpy(stat, s, k);
    stat[k] = '\0';
    int known = 0;
    for (int i = 0; i < (int)(sizeof STATS / sizeof STATS[0]); i++)
        known |= strcmp(stat, STATS[i]) == 0;
    if (!known)
        return fail(c, "expect: unknown stat '%s' (tic health armor kills items secrets exit "
                       "tile screen)", stat);

    static const struct {
        const char *text;
        int op;
    } ops[] = { { "<=", DEMO_OP_LE }, { ">=", DEMO_OP_GE }, { "!=", DEMO_OP_NE },
                { "=", DEMO_OP_EQ },  { "<", DEMO_OP_LT },  { ">", DEMO_OP_GT } };
    const char *p = s + k;
    int op = -1;
    for (int i = 0; i < 6; i++) {
        size_t n = strlen(ops[i].text);
        if (strncmp(p, ops[i].text, n) == 0) {
            op = ops[i].op;
            p += n;
            break;
        }
    }
    if (op < 0)
        return fail(c, "expect '%s': unknown operator (= != < <= > >=)", s);

    demo_cmd tmp = { 0 };
    tmp.op = op;
    strcpy(tmp.name, stat);
    if (strcmp(stat, "screen") == 0) {
        /* expect screen=title: the flow screen the runner reports. */
        if (op != DEMO_OP_EQ && op != DEMO_OP_NE)
            return fail(c, "expect screen: only = and != apply");
        int sc = flow_screen_from_name(p);
        if (sc < 0)
            return fail(c, "expect screen: unknown screen '%s' (title menu skill controls play "
                           "pause)", p);
        tmp.value = sc;
    } else if (strcmp(stat, "tile") == 0) {
        if (op != DEMO_OP_EQ && op != DEMO_OP_NE)
            return fail(c, "expect tile: only = and != apply");
        char buf[32];
        if (strlen(p) >= sizeof buf || !strchr(p, ','))
            return fail(c, "expect tile: want X,Y, got '%s'", p);
        strcpy(buf, p);
        char *comma = strchr(buf, ',');
        *comma = '\0';
        char *xy[2] = { buf, comma + 1 };
        long v[2];
        for (int i = 0; i < 2; i++)
            if (!parse_int(c, xy[i], 0, 63, i ? "tile y" : "tile x", &v[i]))
                return 0;
        tmp.tx = (int32_t)v[0];
        tmp.ty = (int32_t)v[1];
        tmp.has_tile = 1;
    } else {
        long v;
        if (!parse_int(c, p, -1000000000L, 1000000000L, "value", &v))
            return 0;
        tmp.value = (int32_t)v;
    }
    demo_cmd *cmd = push(d, c, DEMO_CMD_EXPECT);
    if (!cmd)
        return 0;
    tmp.kind = cmd->kind;
    tmp.line = cmd->line;
    *cmd = tmp;
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

/* start <level|title>: where the run begins. `start level` (the default)
 * plays the level from tic 0; `start title` begins on the title screen. */
static int hdr_start(demo_t *d, line_ctx *c)
{
    if (!want_args(c, 1))
        return 0;
    if (strcmp(c->tok[1], "level") == 0)
        d->start = FLOW_PLAY;
    else if (strcmp(c->tok[1], "title") == 0)
        d->start = FLOW_TITLE;
    else
        return fail(c, "start: want 'level' or 'title', got '%s'", c->tok[1]);
    return 1;
}

typedef struct {
    const char *name;
    int (*parse)(demo_t *d, line_ctx *c);
} cmd_entry;

/* on_death end|restart: what a death does to the run. end (default) stops
 * it (completion demos fail); restart lets the demo press use after the
 * death view and play on in the restarted level. */
static int hdr_on_death(demo_t *d, line_ctx *c)
{
    if (!want_args(c, 1))
        return 0;
    if (strcmp(c->tok[1], "end") == 0)
        d->on_death_restart = 0;
    else if (strcmp(c->tok[1], "restart") == 0)
        d->on_death_restart = 1;
    else
        return fail(c, "on_death: want 'end' or 'restart', got '%s'", c->tok[1]);
    return 1;
}

static const cmd_entry header_cmds[] = {
    { "on_death", hdr_on_death },
    { "level", hdr_level },
    { "skill", hdr_skill },
    { "seed", hdr_seed },
    { "map", hdr_map },
    { "start", hdr_start },
};

/* The body dispatch table. */
static const cmd_entry body_cmds[] = {
    { "in", cmd_in },
    { "wait", cmd_wait },
    { "goto", cmd_goto },
    { "face", cmd_face },
    { "use", cmd_use },
    { "weapon", cmd_weapon },
    { "fire", cmd_fire },
    { "clear", cmd_clear },
    { "expect", cmd_expect },
    { "key", cmd_key },
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
    d->start = FLOW_PLAY;
    d->screen = FLOW_PLAY;

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

static const char *const OP_TEXT[] = { "=", "!=", "<", "<=", ">", ">=" };

static int compare(int32_t a, int op, int32_t b)
{
    switch (op) {
    case DEMO_OP_EQ: return a == b;
    case DEMO_OP_NE: return a != b;
    case DEMO_OP_LT: return a < b;
    case DEMO_OP_LE: return a <= b;
    case DEMO_OP_GT: return a > b;
    default: return a >= b;
    }
}

static int run_fail(char *err, size_t errlen, const demo_cmd *c, const char *fmt, ...)
{
    int k = snprintf(err, errlen, "line %d: ", c->line);
    if (k >= 0 && (size_t)k < errlen) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err + k, errlen - (size_t)k, fmt, ap);
        va_end(ap);
    }
    return DEMO_FAIL;
}

/* 1 when the expect holds; else writes the failure. */
static int check_expect(const demo_t *d, const demo_cmd *c, const game_state *gs, char *err,
                        size_t errlen)
{
    if (strcmp(c->name, "screen") == 0) {
        if ((d->screen == c->value) == (c->op == DEMO_OP_EQ))
            return 1;
        run_fail(err, errlen, c, "expect screen%s%s failed: screen is %s at tic %u",
                 OP_TEXT[c->op], flow_screen_name(c->value),
                 flow_screen_name(d->screen) ? flow_screen_name(d->screen) : "?",
                 (unsigned)gs->tic);
        return 0;
    }
    if (c->has_tile) {
        int32_t px = fix_to_int(gs->player.x), py = fix_to_int(gs->player.y);
        int same = px == c->tx && py == c->ty;
        if (same == (c->op == DEMO_OP_EQ))
            return 1;
        run_fail(err, errlen, c, "expect tile%s%d,%d failed: player tile is %d,%d at tic %u",
                 OP_TEXT[c->op], (int)c->tx, (int)c->ty, (int)px, (int)py, (unsigned)gs->tic);
        return 0;
    }
    int32_t v = stat_value(gs, c->name);
    if (compare(v, c->op, c->value))
        return 1;
    run_fail(err, errlen, c, "expect %s%s%d failed: %s is %d at tic %u", c->name, OP_TEXT[c->op],
             (int)c->value, c->name, (int)v, (unsigned)gs->tic);
    return 0;
}

/* goto @name: the WAYPOINT entity of that name. */
static int find_waypoint(const bast_map *map, const char *name, int32_t *tx, int32_t *ty)
{
    for (int i = 0; i < map->entity_count; i++) {
        const bast_map_entity *e = &map->entities[i];
        if (e->type == BAST_ENT_WAYPOINT && strcmp(e->name, name) == 0) {
            *tx = fix_to_int(e->x);
            *ty = fix_to_int(e->y);
            return 1;
        }
    }
    return 0;
}

static int enemies_left(const game_state *gs, fix_t radius)
{
    int n = 0;
    for (int i = 0; i < gs->things.count; i++)
        n += combat_visible(gs, (uint16_t)(i + 1), radius);
    return n;
}

#define COMBAT_DONE (-1)

/* One tic of face nearest / fire nearest / clear: DEMO_TIC with *out set,
 * DEMO_FAIL, or COMBAT_DONE (no tic used). The target is picked on the
 * command's first tic and kept in d->target. */
static int combat_next(demo_t *d, const demo_cmd *c, const game_state *gs, ticcmd *out,
                       char *err, size_t errlen)
{
    const char *name = c->kind == DEMO_CMD_FACE_NEAREST ? "face nearest" : "fire nearest";
    if (c->kind == DEMO_CMD_CLEAR) {
        /* Fight the current target while it stays a valid one, else the
         * nearest; done when no visible live enemy is within the radius. */
        const fix_t radius = fix_from_int(c->value);
        if (!combat_visible(gs, d->target, radius))
            d->target = combat_nearest(gs, radius);
        if (!d->target)
            return COMBAT_DONE;
        if (d->done >= c->n)
            return run_fail(err, errlen, c,
                            "clear: timed out after %d tics with %d visible enemies left at tile "
                            "%d,%d",
                            (int)c->n, enemies_left(gs, radius), (int)fix_to_int(gs->player.x),
                            (int)fix_to_int(gs->player.y));
        int32_t left = combat_aim(gs, d->target, out);
        out->side = combat_strafe(d->done);
        if (left < COMBAT_AIM_TOL)
            out->buttons = BT_FIRE;
        d->done++;
        return DEMO_TIC;
    }

    if (d->done == 0 && d->target == 0) {
        d->target = combat_nearest(gs, 0);
        if (!d->target)
            return run_fail(err, errlen, c, "%s: no visible enemy at tic %u", name,
                            (unsigned)gs->tic);
    }
    const thinker_t *t = &gs->things.list[d->target - 1];
    if (!thinker_alive(t))
        return COMBAT_DONE;
    if (c->kind == DEMO_CMD_FACE_NEAREST) {
        int32_t e = (int16_t)(uint16_t)(angle_of(t->x - gs->player.x, t->y - gs->player.y) -
                                        gs->player.angle);
        if (e >= -NAV_FACE_TOL && e <= NAV_FACE_TOL)
            return COMBAT_DONE;
        combat_aim(gs, d->target, out);
        d->done++;
        return DEMO_TIC;
    }
    if (d->done >= c->n)
        return run_fail(err, errlen, c,
                        "fire nearest: timed out after %d tics, thinker %d still alive (health %d)",
                        (int)c->n, (int)d->target, (int)t->health);
    int32_t left = combat_aim(gs, d->target, out);
    /* Only shoot what can be hit: a target that ducked behind a wall is
     * tracked, not fired at. */
    if (left < COMBAT_AIM_TOL && combat_visible(gs, d->target, 0))
        out->buttons = BT_FIRE;
    d->done++;
    return DEMO_TIC;
}

int demo_next(demo_t *d, const game_state *gs, ticcmd *out, char *err, size_t errlen)
{
    d->key = FKEY_NONE;
    while (d->pc < d->count) {
        const demo_cmd *c = &d->cmds[d->pc];
        memset(out, 0, sizeof *out);
        switch (c->kind) {
        case DEMO_CMD_EXPECT:
            if (!check_expect(d, c, gs, err, errlen))
                return DEMO_FAIL;
            break; /* no tic */
        case DEMO_CMD_GOTO: {
            int32_t tx = c->tx, ty = c->ty;
            if (c->name[0] && !find_waypoint(gs->map, c->name, &tx, &ty))
                return run_fail(err, errlen, c, "goto: no WAYPOINT named '%s' in the map",
                                c->name);
            if (d->done == 0)
                nav_reset(&d->nav);
            int s = nav_goto(gs, tx, ty, &d->nav, out);
            if (s == NAV_ARRIVED)
                break;
            if (s == NAV_NO_PATH)
                return run_fail(err, errlen, c, "goto %d,%d: no path from tile %d,%d at tic %u",
                                (int)tx, (int)ty, (int)fix_to_int(gs->player.x),
                                (int)fix_to_int(gs->player.y), (unsigned)gs->tic);
            if (d->done >= c->n)
                return run_fail(err, errlen, c,
                                "goto %d,%d: timed out after %d tics at tile %d,%d", (int)tx,
                                (int)ty, (int)c->n, (int)fix_to_int(gs->player.x),
                                (int)fix_to_int(gs->player.y));
            d->done++;
            return DEMO_TIC;
        }
        case DEMO_CMD_FACE: {
            angle_t target = c->has_tile ? nav_angle_to_tile(gs, c->tx, c->ty) : c->angle;
            if (nav_face(gs, target, out))
                break;
            d->done++;
            return DEMO_TIC;
        }
        case DEMO_CMD_FACE_NEAREST:
        case DEMO_CMD_FIRE_NEAREST:
        case DEMO_CMD_CLEAR: {
            int s = combat_next(d, c, gs, out, err, errlen);
            if (s == COMBAT_DONE)
                break;
            return s;
        }
        default: /* DEMO_CMD_IN, DEMO_CMD_WAIT */
            if (d->done >= c->n)
                break;
            d->done++;
            if (c->kind == DEMO_CMD_IN) {
                *out = c->cmd;
                d->key = c->key;
            }
            return DEMO_TIC;
        }
        d->pc++;
        d->done = 0;
        d->target = 0;
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
