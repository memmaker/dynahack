/* DynaHack 0.6.0  web/webwin.c  browser client (RVIP) */
/* Takes the place of the curses client (nitrohack/): implements
 * struct nh_window_procs for libnitrohack and runs the command loop.
 * C decides everything, web/dynahack.js only draws it and rvip-wm.js
 * places the windows (RVIP.md Part W, W0):
 *   js_map:  ROWNO*COLNO tiles, (top tile + 1) | (floor tile + 1) << 16
 *            (0 = nothing; tile_* tables from web/mktiles.py), and the
 *            same cells as text (char | colour << 8, the game's own
 *            symbols from nh_get_drawing_info), cursor x/y, level
 *   js_text: 0 prompt, 1 status lines, 2 inventory, 3 pop-up, 4 new
 *            message, 5 messages so far are old, 6 replace last message,
 *            7 visible monsters, 8 prompt line over the map (question or
 *            the newest message of this action).
 *            Rows are tab-separated (tile, letter, sel, colour, text).
 * Keys come from Module.nh.key(); Asyncify lets the game wait for them.
 * Without __EMSCRIPTEN__ the JS calls are stubs that feed random keys:
 * a headless driver for the ASan run (web/asan.sh). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fnmatch.h>
#include "nitrohack.h"

#ifdef __EMSCRIPTEN__
#define GAMEDIR "/dynahack/"            /* IndexedDB (IDBFS) mount */
#define DATADIR "/dynahack-data/"   /* preloaded nhdat, read-only */
#else
#define GAMEDIR "./"
#define DATADIR "./"
#endif
#define SAVEDIR GAMEDIR "save/"
#define LOGDIR GAMEDIR "log/"
#define POP_ROWS 40
#define Ctrl(c) (0x1f & (c))

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
EM_JS(void, js_map, (int *c, int *t, int hx, int hy, int lev),
      { Module.nh.map(c, t, hx, hy, lev); });
EM_JS(void, js_text, (int id, const char *s),
      { Module.nh.text(id, UTF8ToString(s)); });
EM_JS(int, js_key, (int peek, int at_cmd), { return Module.nh.key(peek, at_cmd); });
EM_ASYNC_JS(void, js_end, (void), { await Module.nh.end(); });
EM_JS(void, js_sound, (const char *name), { Module.nh.sound(UTF8ToString(name)); });
EM_JS(void, js_music, (int on), { Module.nh.music(on); });
/* Run report (roguelikes-index/server/CONTRACT.md), called from
   libnitrohack/src/end.c done(): fire-and-forget GET, never throws,
   offline just fails silently. Negative ints are omitted. */
EM_JS(void, js_beacon, (const char *g, const char *ev, const char *name, const char *killer, int depth, int score, int turns, int lvl), {
    try {
        var p = [['g', UTF8ToString(g)], ['ev', UTF8ToString(ev)], ['name', name ? UTF8ToString(name) : ''],
                 ['killer', killer ? UTF8ToString(killer) : ''], ['depth', depth], ['score', score], ['turns', turns], ['lvl', lvl]];
        var q = p.filter(function (a) { return a[1] !== '' && !(a[1] < 0); })
                 .map(function (a) { return a[0] + '=' + encodeURIComponent(a[1]); }).join('&');
        fetch('/roguelikes/beacon?' + q, { keepalive: true, mode: 'no-cors' }).catch(function () {});
    } catch (e) {}
});
#define idle() emscripten_sleep(15)
#define web_delay() emscripten_sleep(50)
/* let the browser run (keys, drawing) during multi-turn actions */
static void yield_sometimes(void)
{
    static double last;

    if (emscripten_get_now() - last > 50) {
        emscripten_sleep(0);
        last = emscripten_get_now();
    }
}
#else /* headless random-key driver (ASan) */
static long keys_left = 2000;
static void js_map(int *c, int *t, int hx, int hy, int lev) {}
static void js_text(int id, const char *s) { if (id == 4) puts(s); }
static int js_key(int peek, int at_cmd)
{
    static const char pool[] = "hjklyubnHJKLYUBN.s,:<>iewWrqPRTaAdDft"
                               "zZxX0123456789 \r\033\033\033?#@v_";
    if (peek)
        return 0;
    if (--keys_left < 0) /* then leave any prompt, save: S, y to confirm */
        return keys_left > -9 ? '\033' : keys_left == -9 ? 'S'
             : keys_left < -60 ? (exit(1), 0) : 'y';
    return (unsigned char) pool[rand() % (sizeof pool - 1)];
}
static void js_end(void) {}
static void js_sound(const char *name) {}
static void js_music(int on) {}
#define idle() ((void) 0)
#define web_delay() ((void) 0)
#define yield_sometimes() ((void) 0)
#endif

struct row { char ch; int sel, clr, tile; char *s; }; /* sel: 0/1, 2 = heading */
/* inv: 1 = inventory list (letter/+ main action, Ctrl+letter or * examine,
 * - drop, Enter/Space/5/click item menu, 0/. close, other keys = commands),
 * 2 = item prompt / item menu (5/6 choose, 4/0/. close), 3 = 2 with the
 * keys shown in the text (command menu); key = the key
 * that ended it ('*' for Ctrl+letter) */
struct pop { const char *prompt; struct row *rows; int n, top, cur, any, inv, key; };

/* web/gen/src/tiletab.c (web/mktiles.py): display symbol -> tiles.png index */
extern const short tile_bg[], tile_trap[], tile_obj[], tile_mon[], tile_warn[], tile_expl[],
                   tile_zap[], tile_effect[], tile_invis[], tile_swallow[];

static struct nh_drawing_info *di;
static struct nh_dbuf_entry dbuf[ROWNO][COLNO];
static int cells[ROWNO * COLNO], chars[ROWNO * COLNO];
static int curx, cury, have_map, level_z;
static struct pop *popup;
static char promptbuf[BUFSZ * 2], statbuf[BUFSZ * 3];
static char hist_prev[BUFSZ], toplast[BUFSZ + 16]; /* toplast: newest message of this action, for the prompt line */
static int hist_reps, nhist, at_cmd;
static int qkey; /* one queued key, read before the browser's */
static char next_cmd[32], objprompt[BUFSZ], objcur; /* menu choice; item prompt title + first candidate */
static struct nh_cmd_arg next_arg;
static int reopen_inv, auto_objlist;
static char *invtext;
static char *tbuf;
static size_t tlen, tcap;

static void web_getlin(const char *, char *);

static void tadd(const char *fmt, ...)
{
    va_list ap;
    int n;

    for (;;) {
        va_start(ap, fmt);
        n = vsnprintf(tbuf ? tbuf + tlen : 0, tbuf ? tcap - tlen : 0, fmt, ap);
        va_end(ap);
        if (tbuf && tlen + n < tcap) {
            tlen += n;
            return;
        }
        tcap = (tlen + n + 1) * 2;
        tbuf = realloc(tbuf, tcap);
    }
}

static int pal(int c)
{
    c &= CLR_MASK;
    return (c >= CLR_MAX || c == NO_COLOR) ? CLR_GRAY : c;
}

/* ---------- map: the top layer of each cell, like nitrohack's mapglyph ---------- */

static struct nh_symdef cell_sym(struct nh_dbuf_entry *e)
{
    struct nh_symdef s = { ' ', "", NO_COLOR };
    int id;

    if (e->effect) {
        id = NH_EFFECT_ID(e->effect);
        switch (NH_EFFECT_TYPE(e->effect)) {
        case E_EXPLOSION:
            s = di->explsyms[id % NUMEXPCHARS];
            s.color = di->expltypes[id / NUMEXPCHARS].color;
            break;
        case E_SWALLOW:
            s = di->swallowsyms[id & 7];
            s.color = di->monsters[id >> 3].color;
            break;
        case E_ZAP:
            s = di->zapsyms[id & 3];
            s.color = di->zaptypes[id >> 2].color;
            break;
        case E_BREATH:
            s = di->breathsyms[id & 3];
            s.color = di->zaptypes[id >> 2].color;
            break;
        case E_MISC:
            s = di->effects[id];
            break;
        }
        return s;
    }
    if (e->invis)
        return di->invis[0];
    if (e->mon) {
        if (e->mon > di->num_monsters && (e->monflags & MON_WARNING))
            return di->warnings[e->mon - 1 - di->num_monsters];
        return di->monsters[e->mon - 1];
    }
    if (e->obj) {
        s = di->objects[e->obj - 1];
        if (e->obj_mn && !strcmp(s.symname, "corpse"))
            s.color = di->monsters[e->obj_mn - 1].color;
        if (e->objflags & DOBJ_PRIZE)
            s.color = CLR_BRIGHT_MAGENTA;
        return s;
    }
    if (e->trap)
        return di->traps[e->trap - 1];
    return di->bgelements[e->bg];
}

/* the tile of the top layer, as cell_sym() picks the symbol */
static int cell_tile(struct nh_dbuf_entry *e)
{
    if (e->effect) {
        int id = NH_EFFECT_ID(e->effect);

        switch (NH_EFFECT_TYPE(e->effect)) {
        case E_EXPLOSION: return tile_expl[id];
        case E_SWALLOW: return tile_swallow[id & 7];
        case E_ZAP: case E_BREATH: return tile_zap[id];
        case E_MISC: return tile_effect[id];
        }
    }
    if (e->invis)
        return tile_invis[0];
    if (e->mon)
        return e->mon > di->num_monsters && (e->monflags & MON_WARNING)
               ? tile_warn[e->mon - 1 - di->num_monsters] : tile_mon[e->mon - 1];
    if (e->obj)
        return tile_obj[e->obj - 1];
    if (e->trap)
        return tile_trap[e->trap - 1];
    return tile_bg[e->bg];
}

static int obj_tile(struct nh_objitem *it)
{
    return it->otype > 0 && it->otype <= di->num_objects ? tile_obj[it->otype - 1] : -1;
}

static void redraw(void)
{
    int x, y, i;

    if (have_map) {
        for (y = 0; y < ROWNO; y++)
            for (x = 0; x < COLNO; x++) {
                struct nh_symdef s = cell_sym(&dbuf[y][x]);

                int t = cell_tile(&dbuf[y][x]), under = tile_bg[dbuf[y][x].bg];

                cells[y * COLNO + x] = (t + 1) | (t == under ? 0 : (under + 1) << 16);
                chars[y * COLNO + x] = (unsigned char) s.ch | pal(s.color) << 8;
            }
        js_map(cells, chars, curx, cury, level_z);
        /* Visible window: monsters on the map but the hero (names, colours and
         * tiles from the game's drawing info; objects skipped: the API names
         * them only through nh_describe_pos(), whose mksobj() would change the
         * game state outside the log) */
        tlen = 0, tadd("%s", "");
        for (y = 0; y < ROWNO; y++)
            for (x = 1; x < COLNO; x++) {
                struct nh_dbuf_entry *e = &dbuf[y][x];

                if (!e->mon || e->mon > di->num_monsters || (x == curx && y == cury))
                    continue;
                tadd("%d\t \t0\t%d\t%s%s%s\n", tile_mon[e->mon - 1], pal(di->monsters[e->mon - 1].color),
                     e->monflags & MON_TAME ? "tame " : e->monflags & MON_PEACEFUL ? "peaceful " : "",
                     di->monsters[e->mon - 1].symname, e->monflags & MON_DETECTED ? " (sensed)" : "");
            }
        js_text(7, tbuf);
    }
    js_text(0, promptbuf);
    js_text(8, *promptbuf ? promptbuf : toplast);
    js_text(1, statbuf);
    js_text(2, invtext ? invtext : "");

    tlen = 0, tadd("%s", "");
    if (popup) { /* first row: top, cursor, prompt */
        tadd("%d\t%d\t%s\n", popup->top, popup->cur, popup->prompt ? popup->prompt : "");
        for (i = 0; i < popup->n; i++) {
            struct row *r = &popup->rows[i];

            tadd("%d\t%c\t%d\t%d\t%s\n", r->sel == 2 ? -1 : r->tile, r->ch && popup->inv != 3 ? r->ch : ' ', r->sel, r->clr, r->s);
        }
    }
    js_text(3, tbuf);
}

/* the next key: ASCII, 0x101.. arrows etc. as hjklyubn, 0x20000|row = a
 * click on a pop-up row (returned as '\n' after moving the cursor) */
static int getkey(void)
{
    int k;

    redraw();
    if (qkey) {
        k = qkey, qkey = 0;
        return k;
    }
    for (;;) {
        if ((k = js_key(0, at_cmd && !popup)) < 0) {
            idle();
            continue;
        }
        if (k & 0x20000) {
            int i = k & 0xffff;

            if (popup && i < popup->n && popup->rows[i].sel != 2) {
                popup->cur = i;
                return popup->any ? ' ' : '\n';
            }
            continue;
        }
        if (k & 0x10000) /* map click: not used yet */
            continue;
        switch (k) {
        case 0x101: return popup ? '8' : 'k'; /* 8/2 never item letters */
        case 0x102: return popup ? '2' : 'j';
        case 0x103: return 'h';
        case 0x104: return 'l';
        case 0x105: return 'y';
        case 0x106: return 'u';
        case 0x107: return 'b';
        case 0x108: return 'n';
        }
        return k & 0xff;
    }
}

/* ---------- messages ---------- */

/* ---------- sound (RVIP 6b): messages pick the effect (the USER_SOUNDS
 * idea, built in); web/dynahack.js plays sound/<name>.wav (web/mksounds.py)
 * only if the Sound button is on ---------- */
static const struct { const char *pat, *name; } msgsnd[] = {
    { "You die*", "die" },
    { "You kill *", "kill" }, { "You destroy *", "kill" },
    { "You hit *", "hit" }, { "You smite *", "hit" },
    { "You miss *", "miss" }, { "* misses*", "miss" },
    { "* hits!*", "hurt" }, { "* bites!*", "hurt" }, { "* stings!*", "hurt" },
    { "* butts!*", "hurt" }, { "* kicks!*", "hurt" }, { "* touches you!*", "hurt" },
    { "* claws you!*", "hurt" }, { "* suck you!*", "hurt" },
    { "Welcome to experience level *", "levelup" },
    { "You hear *", "hear" },
    { "*door opens.*", "door" }, { "*door closes.*", "door" },
    { "*gold piece*", "gold" },
};

static void sound_msg(const char *msg)
{
    size_t i;

    for (i = 0; i < sizeof msgsnd / sizeof *msgsnd; i++)
        if (!fnmatch(msgsnd[i].pat, msg, 0)) {
            js_sound(msgsnd[i].name);
            return;
        }
}

/* each status update: stairs on a level change; town music on Mine Town,
 * the Town and the Black Market (the API has no shop flag, so no shops) */
static void sound_level(const struct nh_player_info *pi)
{
    static char last[COLNO + 16];
    char now[COLNO + 16];

    snprintf(now, sizeof now, "%d %s", pi->z, pi->levdesc_short);
    if (!strcmp(now, last))
        return;
    if (*last)
        js_sound("stairs");
    strcpy(last, now);
    js_music(!strncmp(pi->levdesc_short, "Mine Town", 9) || !strncmp(pi->levdesc_short, "Town", 4) ||
             !strncmp(pi->levdesc_short, "BlackMrkt", 9));
}

static void add_msg(const char *s)
{
    if (!*s)
        return;
    sound_msg(s);
    if (nhist && !strcmp(s, hist_prev)) { /* repeat -> "message (xN)" */
        char fold[BUFSZ + 16];

        snprintf(fold, sizeof fold, "%s (x%d)", s, ++hist_reps);
        js_text(6, fold);
        strcpy(toplast, fold);
        return;
    }
    snprintf(hist_prev, sizeof hist_prev, "%s", s);
    hist_reps = 1;
    nhist++;
    js_text(4, s);
    snprintf(toplast, sizeof toplast, "%s", s);
}

static void web_print_message(int turn, const char *msg) { add_msg(msg); }
static void web_raw_print(const char *str) { add_msg(str); }

/* auto_more (RVIP 3d): no --More--; P_MAP waits so detection can be seen */
static void web_pause(enum nh_pause_reason reason)
{
    if (reason != P_MAP)
        return;
    snprintf(promptbuf, sizeof promptbuf, "(press any key)");
    getkey();
    *promptbuf = 0;
}

static void web_delay_output(void)
{
    redraw();
    web_delay();
}

/* ---------- pop-ups ---------- */

/* generic list loop: returns -1 cancelled, else 0 (selection in rows) */
static int run_pop(struct pop *p, int how)
{
    int i, k, rows = POP_ROWS - (p->prompt ? 1 : 0);

    p->top = 0, p->cur = -1, p->any = how == PICK_ANY;
    if (how != PICK_NONE)
        for (i = 0; i < p->n; i++)
            if (p->rows[i].sel != 2) {
                p->cur = i;
                break;
            }
    for (i = 0; p->inv == 2 && objcur && i < p->n; i++)
        if (p->rows[i].sel != 2 && p->rows[i].ch == objcur)
            p->cur = i; /* item prompt: start on the first candidate */
    popup = p;
    for (;;) {
        if (p->cur >= 0) {
            if (p->cur < p->top)
                p->top = p->cur;
            if (p->cur >= p->top + rows)
                p->top = p->cur - rows + 1;
        }
        k = getkey();
        if (k == '\033') {
            popup = NULL;
            return -1;
        }
        if (how == PICK_NONE) {
            if ((k == ' ' || k == '>' || k == 'j' || k == '2') && p->top + rows < p->n)
                p->top += k == 'j' || k == '2' ? 1 : rows;
            else if ((k == '<' || k == 'k' || k == '8') && p->top > 0)
                p->top -= k == 'k' || k == '8' ? 1 : (p->top < rows ? p->top : rows);
            else
                break;
            continue;
        }
        p->key = k;
        for (i = 0; i < p->n && !(p->rows[i].sel != 2 && p->rows[i].ch == k); i++)
            ;
        if (i < p->n) { /* accelerator */
            if (how == PICK_ONE) {
                p->cur = i;
                break;
            }
            p->rows[i].sel ^= 1, p->cur = i;
            continue;
        }
        if (p->inv && (k == '0' || k == '.' || (p->inv >= 2 && k == '4'))) {
            popup = NULL;
            return -1;
        }
        if (p->inv && (k == '5' || (p->inv >= 2 && k == '6')))
            break;
        if (p->inv == 1 && (k == '+' || k == '-' || k == '*'))
            break;
        if (p->inv == 1 && k > 0 && k < 27 && k != '\n' && k != '\r') { /* Ctrl+letter */
            for (i = 0; i < p->n && !(p->rows[i].sel != 2 && p->rows[i].ch == k + 96); i++)
                ;
            if (i < p->n) {
                p->cur = i, p->key = '*';
                break;
            }
        }
        if (k == '\n' || k == '\r' || (k == ' ' && how == PICK_ONE))
            break;
        if (p->inv == 1 && !strchr("28jk", k)) { /* any other key: a command */
            qkey = k;
            popup = NULL;
            return -1;
        }
        if (k == 'j' || k == 'k' || k == '2' || k == '8') {
            int d = (k == 'j' || k == '2') ? 1 : -1;

            for (i = p->cur + d; i >= 0 && i < p->n; i += d)
                if (p->rows[i].sel != 2) {
                    p->cur = i;
                    break;
                }
            continue;
        }
        if (k == ' ' && p->cur >= 0) {
            p->rows[p->cur].sel ^= 1;
            continue;
        }
        if (how == PICK_ANY && (k == ',' || k == '-'))
            for (i = 0; i < p->n; i++)
                if (p->rows[i].sel != 2)
                    p->rows[i].sel = k == ',';
    }
    popup = NULL;
    if (how == PICK_ONE) /* exactly the row under the cursor */
        for (i = 0; i < p->n; i++)
            if (p->rows[i].sel != 2)
                p->rows[i].sel = i == p->cur;
    return 0;
}

static void free_pop(struct pop *p)
{
    int i;

    for (i = 0; i < p->n; i++)
        free(p->rows[i].s);
    free(p->rows);
}

static char next_accel(char a) { return a == 'z' ? 'A' : a == 'Z' ? 0 : a + 1; }

static int display_menu(struct nh_menuitem *items, int icount, const char *title,
                        int how, int *results, int inv)
{
    struct pop p = { title, calloc(icount + 1, sizeof(struct row)), icount };
    int i, n;
    char acc = 'a';

    for (i = 0; i < icount; i++) {
        struct row *r = &p.rows[i];
        int pick = items[i].role == MI_NORMAL && items[i].id;

        r->s = strdup(items[i].caption);
        r->sel = pick ? (items[i].selected ? 1 : 0) : 2;
        r->clr = items[i].role == MI_HEADING ? CLR_YELLOW : CLR_GRAY;
        r->tile = -1;
        r->ch = items[i].accel;
        if (pick && !r->ch && how != PICK_NONE && acc && inv != 3)
            r->ch = acc, acc = next_accel(acc);
    }
    p.inv = inv;
    n = run_pop(&p, how);
    if (n == 0 && how != PICK_NONE)
        for (i = 0; i < icount; i++)
            if (p.rows[i].sel == 1) {
                if (results)
                    results[n] = items[i].id;
                n++;
            }
    free_pop(&p);
    return n;
}

static int web_display_menu(struct nh_menuitem *items, int icount, const char *title,
                            int how, int *results)
{
    return display_menu(items, icount, title, how, results, 0);
}

static int obj_color(struct nh_objitem *it)
{
    if (it->otype <= 0 || it->otype > di->num_objects)
        return CLR_GRAY;
    if (it->omonnum > 0 && !strcmp(di->objects[it->otype - 1].symname, "corpse"))
        return pal(di->monsters[it->omonnum - 1].color);
    return pal(di->objects[it->otype - 1].color);
}

static void set_next(const char *name, char invlet)
{
    snprintf(next_cmd, sizeof next_cmd, "%s", name);
    next_arg.argtype = invlet ? CMD_ARG_OBJ : CMD_ARG_NONE;
    next_arg.invlet = invlet;
}

/* inventory list result (RVIP 3c): letter/+ main action, - drop,
 * * or Ctrl+letter examine (whatisinv), Enter/Space/5/click the item's
 * menu = the library's nh_get_object_commands() (describe = examine) */
static void item_action(struct nh_objitem *it, int key)
{
    static const char *const mains[] = { "takeoff", "remove", "eat", "drink", "read", "zap",
                                         "apply", "put on", "wear", "wield" };
    int i, j, n = 0, pick[1];
    struct nh_cmd_desc *oc = nh_get_object_commands(&n, it->accel);
    struct nh_menuitem *items;
    char title[BUFSZ];

    if (key == '-' || key == '*') {
        set_next(key == '-' ? "drop" : "whatisinv", it->accel);
        return;
    }
    if (key != '\n' && key != '\r' && key != ' ' && key != '5') { /* main action */
        set_next("whatisinv", it->accel);
        for (j = 0; j < 10; j++)
            for (i = 0; i < n; i++)
                if (!strcmp(oc[i].name, mains[j]) && oc[i].altkey == it->accel) {
                    set_next(oc[i].name, it->accel);
                    return;
                }
        return;
    }
    if (!n)
        return;
    items = calloc(n, sizeof *items);
    for (i = 0; i < n; i++) {
        items[i].id = i + 1, items[i].role = MI_NORMAL, items[i].accel = oc[i].defkey;
        snprintf(items[i].caption, BUFSZ, "%s", oc[i].desc);
    }
    oc = memcpy(malloc(n * sizeof *oc + 1), oc, n * sizeof *oc); /* freed by the next API call */
    snprintf(title, sizeof title, "%c - %s", it->accel, it->caption);
    if (display_menu(items, n, title, PICK_ONE, pick, 2) > 0) /* altkey = the object */
        set_next(oc[pick[0] - 1].name, oc[pick[0] - 1].altkey);
    free(oc);
    free(items);
}

static int web_display_objects(struct nh_objitem *items, int icount, const char *title,
                               int how, struct nh_objresult *results)
{
    struct pop p = { title ? title : objprompt, calloc(icount + 1, sizeof(struct row)), icount };
    int i, n, inv = how == PICK_INVACTION;

    p.inv = inv ? 1 : how == PICK_ONE ? 2 : 0;
    if (inv)
        how = PICK_ONE;
    for (i = 0; i < icount; i++) {
        struct row *r = &p.rows[i];

        r->s = strdup(items[i].caption);
        r->sel = items[i].role == MI_NORMAL && items[i].id ? 0 : 2;
        r->clr = r->sel == 2 ? CLR_YELLOW : obj_color(&items[i]);
        r->tile = obj_tile(&items[i]);
        r->ch = items[i].accel;
    }
    n = run_pop(&p, how);
    if (inv) { /* the item actions run as the next command */
        if (n == 0 && p.cur >= 0)
            item_action(&items[p.cur], p.key);
        free_pop(&p);
        return 0;
    }
    if (n == 0 && how != PICK_NONE)
        for (i = 0; i < icount; i++)
            if (p.rows[i].sel == 1) {
                if (results) {
                    results[n].id = items[i].id;
                    results[n].count = -1;
                }
                n++;
            }
    free_pop(&p);
    return n;
}

/* the Inventory window (invent) — floor lists go to the messages */
static nh_bool web_list_items(struct nh_objitem *items, int icount, nh_bool invent)
{
    int i;

    if (!invent)
        return FALSE;
    tlen = 0, tadd("%s", "");
    for (i = 0; i < icount; i++) {
        int head = items[i].role != MI_NORMAL;

        tadd("%d\t%c\t%d\t%d\t%s\n", head ? -1 : obj_tile(&items[i]),
             items[i].accel ? items[i].accel : ' ', head ? 2 : 0,
             head ? CLR_YELLOW : obj_color(&items[i]), items[i].caption);
    }
    free(invtext);
    invtext = strdup(tbuf);
    return TRUE;
}

static void show_lines(const char *title, const char *buf)
{
    struct pop p = { title, NULL, 0 };
    const char *s = buf, *e;
    int cap = 0;

    while (*s) {
        e = strchr(s, '\n');
        if (!e)
            e = s + strlen(s);
        if (p.n == cap)
            p.rows = realloc(p.rows, (cap = cap * 2 + 16) * sizeof(struct row));
        p.rows[p.n].s = strndup(s, e - s);
        p.rows[p.n].ch = 0, p.rows[p.n].sel = 2, p.rows[p.n].clr = CLR_GRAY, p.rows[p.n].tile = -1;
        p.n++;
        s = *e ? e + 1 : e;
    }
    run_pop(&p, PICK_NONE);
    free_pop(&p);
}

static void web_display_buffer(const char *buf, nh_bool trymove) { show_lines(NULL, buf); }

static void web_outrip(struct nh_menuitem *items, int icount, nh_bool tombstone,
                       const char *name, int gold, const char *killbuf, int end_how, int year)
{
    int i;

    tlen = 0, tadd("Rest in peace, %s.\n%d gold, %s, %d\n\n", name, gold, killbuf, year);
    for (i = 0; i < icount; i++)
        tadd("%s\n", items[i].caption);
    {
        char *t = strdup(tbuf);

        show_lines("Game over", t);
        free(t);
    }
}

/* ---------- status ---------- */

static void web_update_status(struct nh_player_info *pi)
{
    char st[16];
    int i;

    if (pi->st == 18 && pi->st_extra)
        snprintf(st, sizeof st, pi->st_extra == 100 ? "18/**" : "18/%02d", pi->st_extra);
    else
        snprintf(st, sizeof st, "%d", pi->st);
    level_z = pi->z;
    sound_level(pi);
    snprintf(statbuf, sizeof statbuf,
             "%s the %s  St:%s Dx:%d Co:%d In:%d Wi:%d Ch:%d  %s\n"
             "%s  $%d  HP:%d(%d) Pw:%d(%d) AC:%d Xp:%d/%d T:%d",
             pi->plname, pi->rank, st, pi->dx, pi->co, pi->in, pi->wi, pi->ch,
             pi->align == A_CHAOTIC ? "Chaotic" : pi->align == A_LAWFUL ? "Lawful" : "Neutral",
             pi->levdesc_dlvl, pi->gold, pi->hp, pi->hpmax, pi->en, pi->enmax, pi->ac,
             pi->level, pi->xp, pi->moves);
    for (i = 0; i < pi->nr_items && i < STATUSITEMS_MAX; i++)
        snprintf(statbuf + strlen(statbuf), sizeof statbuf - strlen(statbuf), " %s",
                 pi->statusitems[i]);
}

static void web_update_screen(struct nh_dbuf_entry d[ROWNO][COLNO], int ux, int uy)
{
    memcpy(dbuf, d, sizeof dbuf);
    curx = ux, cury = uy, have_map = 1;
    redraw();
}

static void web_level_changed(int displaymode) {}

/* ---------- questions ---------- */

static enum nh_direction key_dir(int k)
{
    static const char keys[] = "hykulnjb", num[] = "47896321"; /* DIR_W..DIR_SW */
    const char *p;

    if (k && (p = strchr(keys, k)))
        return (enum nh_direction)(p - keys);
    if (k && (p = strchr(num, k)))
        return (enum nh_direction)(p - num);
    return k == '<' ? DIR_UP : k == '>' ? DIR_DOWN : DIR_NONE;
}

static enum nh_direction web_getdir(const char *query, nh_bool restricted)
{
    enum nh_direction d;
    int k;

    snprintf(promptbuf, sizeof promptbuf, "%s", query ? query : "In what direction?");
    k = getkey();
    *promptbuf = 0;
    if (k == '.' || k == 's' || k == '5')
        return DIR_SELF;
    if ((d = key_dir(k)) == DIR_NONE && k != '\033')
        add_msg("What a strange direction!");
    return d;
}

static char web_yn_function(const char *query, const char *rset, char def)
{
    char resp[QBUFSZ], *e;
    int k;

    snprintf(resp, sizeof resp, "%s", rset);
    if ((e = strchr(resp, '\033')))
        *e = 0;
    snprintf(promptbuf, sizeof promptbuf, "%s [%s] ", query, resp);
    if (def)
        snprintf(promptbuf + strlen(promptbuf), sizeof promptbuf - strlen(promptbuf), "(%c) ", def);
    for (;;) {
        k = tolower(getkey());
        if (k == '\033') {
            k = strchr(rset, 'q') ? 'q' : strchr(rset, 'n') ? 'n' : def;
            break;
        }
        if ((k == ' ' || k == '\n' || k == '\r') && def) {
            k = def;
            break;
        }
        if (k && strchr(rset, k))
            break;
    }
    *promptbuf = 0; /* the library logs the answer itself */
    return k;
}

static void web_getlin(const char *query, char *buf)
{
    int n = 0, k;

    buf[0] = 0;
    for (;;) {
        snprintf(promptbuf, sizeof promptbuf, "%s %s_", query, buf);
        k = getkey();
        if (k == '\033') {
            strcpy(buf, "\033");
            break;
        }
        if (k == '\n' || k == '\r')
            break;
        if ((k == '\b' || k == 127) && n > 0)
            buf[--n] = 0;
        else if (k >= ' ' && k < 127 && n < BUFSZ - 1)
            buf[n++] = k, buf[n] = 0;
    }
    *promptbuf = 0;
}

static char web_query_key(const char *query, int *count)
{
    int k, cnt = 0, has = 0;
    const char *b = strrchr(query, '[');
    int objq = !strncmp(query, "What do you want to ", 20) && b &&
               (!strcmp(b, "[*]") || strstr(b, " or ?*]"));
    const char *l = b ? b + 1 + 2 * (b[1] == '-' && b[2] == ' ') : ""; /* letters */
    int list = *l == '*' || l[1] == ' ' ? '*' : '?'; /* '?' with one item only plines */

    /* getobj (RVIP 3c): open the item list at once, unless '-' (bare hands,
     * fingers) is an answer: then Enter opens it. Once per command, so
     * leaving the list returns to the letter prompt. */
    if (objq) {
        snprintf(objprompt, sizeof objprompt, "%.*s", (int)(b - query - 1), query);
        objcur = *l != '*' ? *l : 0;
        if (auto_objlist && l == b + 1) {
            auto_objlist = 0;
            if (count)
                *count = -1;
            return list;
        }
    }

    for (;;) {
        if (has)
            snprintf(promptbuf, sizeof promptbuf, "%s  Count: %d", query, cnt);
        else
            snprintf(promptbuf, sizeof promptbuf, "%s", query);
        k = getkey();
        if (objq && (k == '\r' || k == '\n'))
            k = list;
        if (!count || !isdigit(k))
            break;
        has = 1;
        if (cnt < 100000)
            cnt = cnt * 10 + k - '0';
    }
    *promptbuf = 0;
    if (count)
        *count = has ? cnt : -1;
    return k;
}

static int web_getpos(int *x, int *y, nh_bool force, const char *goal)
{
    static const char pick[] = " \r\n.,;:";
    static const int vals[] = { 1, 1, 1, 1, 2, 3, 4 };
    static const int dx[] = { -1, -1, 0, 1, 1, 1, 0, -1 }, dy[] = { 0, -1, -1, -1, 0, 1, 1, 1 };
    int cx = *x >= 1 ? *x : curx, cy = *y >= 0 ? *y : cury, ox = curx, oy = cury;
    int k, res;
    const char *p;
    enum nh_direction d;

    for (;;) {
        struct nh_desc_buf db;

        nh_describe_pos(cx, cy, &db);
        snprintf(promptbuf, sizeof promptbuf, "Pick %s (move, . to pick%s): %s",
                 goal ? goal : "a location", force ? "" : ", Esc cancels",
                 *db.mondesc ? db.mondesc : *db.objdesc ? db.objdesc : *db.trapdesc ? db.trapdesc : db.bgdesc);
        curx = cx, cury = cy; /* the page frames the cursor cell */
        k = getkey();
        if (k == '\033' && !force) {
            res = -1, cx = cy = -10;
            break;
        }
        if (k && (p = strchr(pick, k))) {
            res = vals[p - pick];
            break;
        }
        if (k == '@')
            cx = ox, cy = oy;
        else if ((d = key_dir(k)) >= DIR_W && d <= DIR_SW)
            cx += dx[d], cy += dy[d];
        else if ((d = key_dir(tolower(k))) >= DIR_W && d <= DIR_SW)
            cx += 8 * dx[d], cy += 8 * dy[d];
        cx = cx < 1 ? 1 : cx > COLNO - 1 ? COLNO - 1 : cx;
        cy = cy < 0 ? 0 : cy > ROWNO - 1 ? ROWNO - 1 : cy;
    }
    curx = ox, cury = oy;
    *promptbuf = 0;
    *x = cx, *y = cy;
    return res;
}

/* own copy: the library frees xmalloc()ed data (objects, monsters,
 * their names) at the end of the next API call */
static struct nh_symdef *copy_syms(const struct nh_symdef *s, int n)
{
    struct nh_symdef *c = malloc(n * sizeof *c);
    int i;

    for (i = 0; i < n; i++)
        c[i] = s[i], c[i].symname = strdup(s[i].symname ? s[i].symname : "");
    return c;
}

static struct nh_drawing_info *copy_drawing(const struct nh_drawing_info *d)
{
    struct nh_drawing_info *c = malloc(sizeof *c);

    *c = *d;
    c->objects = copy_syms(d->objects, d->num_objects);
    c->monsters = copy_syms(d->monsters, d->num_monsters);
    c->invis = copy_syms(d->invis, 1);
    return c;
}

static struct nh_window_procs web_procs = {
    web_pause, web_display_buffer, web_update_status, web_print_message,
    web_display_menu, web_display_objects, web_list_items, web_update_screen,
    web_raw_print, web_query_key, web_getpos, web_getdir, web_yn_function,
    web_getlin, web_delay_output, web_level_changed, web_outrip, web_print_message,
};

/* ---------- commands ---------- */

static struct nh_cmd_desc *cmds, *keymap[256];
static int ncmds;

static struct nh_cmd_desc *find_cmd(const char *name)
{
    int i;

    for (i = 0; i < ncmds; i++)
        if (!strcmp(cmds[i].name, name))
            return &cmds[i];
    return NULL;
}

static void init_keymap(void)
{
    int i;
    struct nh_cmd_desc *c = nh_get_commands(&ncmds);

    /* the library frees its xmalloc()ed results after the next API call */
    cmds = malloc(ncmds * sizeof *cmds);
    memcpy(cmds, c, ncmds * sizeof *cmds);
    for (i = ncmds - 1; i >= 0; i--) {
        if (cmds[i].altkey)
            keymap[cmds[i].altkey] = &cmds[i];
        if (cmds[i].defkey)
            keymap[cmds[i].defkey] = &cmds[i];
    }
}

/* Enter, '?' and '#' with an empty line: every command, grouped by the
 * library's cmdlist[] flags; picking one runs it. '<' '>' '#' are queued
 * as keys (qkey). */
static int cmd_group(const struct nh_cmd_desc *c)
{
    if ((c->flags & CMD_MOVE) || !strcmp(c->name, "autoexplore") || !strcmp(c->name, "travel"))
        return 0;
    if (c->flags & CMD_ARG_OBJ)
        return 1;
    if (c->flags & (CMD_NOTIME | CMD_HELP))
        return 3;
    return 2;
}

static struct nh_cmd_desc *cmd_menu(const char *title)
{
    static const char *const heads[] = { "Moving", "Items", "Actions", "Information (no game time)" };
    static const struct { int key; const char *desc; } extra[] = {
        { '<', "go up the stairs (off them: walk to the nearest known)" },
        { '>', "go down the stairs (off them: walk to the nearest known)" },
        { '#', "type an extended command" },
    };
    struct nh_menuitem *items = calloc(ncmds + 8, sizeof *items);
    int g, i, n = 0, pick[1];
    char k[16];

    for (g = 0; g < 4; g++) {
        items[n].role = MI_HEADING;
        strcpy(items[n++].caption, heads[g]);
        for (i = 0; g == 0 && i < 3; i++) {
            items[n].id = -1 - i, items[n].role = MI_NORMAL, items[n].accel = extra[i].key;
            snprintf(items[n++].caption, BUFSZ, "%-12c %s", extra[i].key, extra[i].desc);
        }
        for (i = 0; i < ncmds; i++) {
            unsigned char c = cmds[i].defkey ? cmds[i].defkey : cmds[i].altkey;

            if ((cmds[i].flags & CMD_DEBUG) || cmd_group(&cmds[i]) != g)
                continue;
            if (!strcmp(cmds[i].name, "move"))
                strcpy(k, "hjklyubn");
            else if (!strcmp(cmds[i].name, "run"))
                strcpy(k, "HJKLYUBN");
            else if (!c)
                snprintf(k, sizeof k, "#%s", cmds[i].name);
            else if (c < 32)
                snprintf(k, sizeof k, "^%c", c + 64);
            else if (c >= 128)
                snprintf(k, sizeof k, "M-%c", c - 128);
            else
                snprintf(k, sizeof k, "%c", c);
            items[n].id = i + 1, items[n].role = MI_NORMAL;
            items[n].accel = c > 32 && c < 127 && !isdigit(c) ? c : 0;
            snprintf(items[n++].caption, BUFSZ, "%-12s %s", k, cmds[i].desc);
        }
    }
    i = display_menu(items, n, title, PICK_ONE, pick, 3);
    free(items);
    if (i <= 0)
        return NULL;
    if (pick[0] < 0) {
        qkey = extra[-1 - pick[0]].key;
        return NULL;
    }
    return &cmds[pick[0] - 1];
}

static struct nh_cmd_desc *ext_cmd(void)
{
    char buf[BUFSZ];
    int i, hit = -1, len;

    web_getlin("#", buf);
    if (*buf == '\033')
        return NULL;
    if (!*buf)
        return cmd_menu("Extended commands");
    len = strlen(buf);
    for (i = 0; i < ncmds; i++)
        if (!strncmp(cmds[i].name, buf, len)) {
            if (!cmds[i].name[len]) {
                hit = i;
                break;
            }
            hit = hit == -1 ? i : -2;
        }
    if (hit < 0) {
        add_msg(hit == -1 ? "Unknown extended command." : "Ambiguous command.");
        return NULL;
    }
    return &cmds[hit];
}

static const char *get_command(int *count, struct nh_cmd_arg *arg)
{
    static const char dirs[] = "hyku lnjb";
    struct nh_cmd_desc *cmd;
    enum nh_direction d;
    int k;

    auto_objlist = 1, objcur = 0;
    if (*next_cmd) { /* chosen in the inventory: runs now, then reopens it */
        static char name[32];

        reopen_inv = strcmp(next_cmd, "inventory") != 0;
        strcpy(name, next_cmd), *next_cmd = 0;
        *count = 0, *arg = next_arg;
        return name;
    }
    for (;;) {
        *count = 0;
        arg->argtype = CMD_ARG_NONE;
        at_cmd = 1;
        k = getkey();
        at_cmd = 0;
        while (isdigit(k)) { /* count prefix */
            *count = *count * 10 + k - '0';
            if (*count > 0xffff)
                *count /= 10;
            snprintf(promptbuf, sizeof promptbuf, "Count: %d", *count);
            k = getkey();
            *promptbuf = 0;
        }
        if (k == '\033')
            continue;
        js_text(5, ""); /* a new action: older messages dim */
        *toplast = 0;
        /* movement: hjklyubn move, shifted run, Ctrl go2, < > up/down */
        if ((d = key_dir(k)) != DIR_NONE && !isdigit(k)) {
            arg->argtype = CMD_ARG_DIR, arg->d = d;
            return "move";
        }
        if (k < 128 && isupper(k) && strchr(dirs, tolower(k)) && tolower(k) != ' ') {
            arg->argtype = CMD_ARG_DIR, arg->d = key_dir(tolower(k));
            return "run";
        }
        if (k > 0 && k < 27 && !keymap[k] && strchr(dirs, k + 96) && k != Ctrl('j')) {
            arg->argtype = CMD_ARG_DIR, arg->d = key_dir(k + 96);
            return "go2";
        }
        if (k == '#')
            cmd = ext_cmd();
        else if (k == '?' || k == '\r' || k == '\n')
            cmd = cmd_menu("Commands (pick one to run it)");
        else
            cmd = keymap[k & 0xff];
        if (!cmd) {
            if (k != '#' && k != '?' && k != '\r' && k != '\n') {
                char line[64];

                snprintf(line, sizeof line, "Unknown command '%s%c'.", k < 32 ? "^" : "",
                         k < 32 ? k + 64 : k & 0x7f);
                add_msg(line);
            }
            continue;
        }
        if (!(cmd->flags & CMD_ARG_NONE) && (cmd->flags & CMD_ARG_DIR)) {
            d = web_getdir(NULL, FALSE);
            if (d == DIR_NONE)
                continue;
            arg->argtype = CMD_ARG_DIR, arg->d = d;
        }
        return cmd->name;
    }
}

/* a monster on the map that is not tame, peaceful or a warning */
static int hostile_in_view(void)
{
    int x, y;

    for (y = 0; y < ROWNO; y++)
        for (x = 1; x < COLNO; x++)
            if (dbuf[y][x].mon && !(x == curx && y == cury) &&
                !(dbuf[y][x].monflags & (MON_TAME | MON_PEACEFUL | MON_WARNING)))
                return 1;
    return 0;
}

static int commandloop(void)
{
    int state = READY_FOR_INPUT, count;
    const char *cmd;
    struct nh_cmd_arg arg;

    while (state < GAME_OVER) {
        count = 0;
        cmd = NULL;
        arg.argtype = CMD_ARG_NONE;
        if (state == READY_FOR_INPUT)
            cmd = get_command(&count, &arg);
        else {
            yield_sometimes();
            if (state == MULTI_IN_PROGRESS && js_key(1, 0) > 0)
                count = -1; /* a key interrupts a multi-turn action */
        }
        state = nh_command(cmd, count, &arg);
        if (reopen_inv && state == READY_FOR_INPUT) {
            reopen_inv = 0;
            if (!*next_cmd && !hostile_in_view())
                set_next("inventory", 0);
        }
    }
    return state;
}

/* ---------- game start ---------- */

static int pick_one(const char *title, const char *const *names, int n, int *ok)
{
    struct nh_menuitem *items = calloc(n + 1, sizeof *items);
    int i, m = 0, pick[1], r;

    for (i = 0; i < n; i++)
        if (ok[i]) {
            items[m].id = i + 1, items[m].role = MI_NORMAL;
            snprintf(items[m].caption, BUFSZ, "%s", names[i]);
            m++;
        }
    if (m == 1) { /* only one choice: take it */
        r = items[0].id - 1;
        free(items);
        return r;
    }
    items[m].id = 1000, items[m].role = MI_NORMAL, items[m].accel = '*';
    strcpy(items[m].caption, "Random");
    m++;
    r = web_display_menu(items, m, title, PICK_ONE, pick);
    free(items);
    if (r <= 0)
        return -1;
    if (pick[0] == 1000) {
        int c = 0, j;

        for (i = 0; i < n; i++)
            c += ok[i] != 0;
        j = rand() % c;
        for (i = 0; i < n; i++)
            if (ok[i] && !j--)
                return i;
    }
    return pick[0] - 1;
}

/* role, race, gender, alignment: only combinations the game allows */
static int choose_char(int *role, int *race, int *gend, int *align)
{
    struct nh_roles_info *ri = nh_get_roles();
    int ok[64], i, a, b, c;

#define VALID(r, ra, g, al) ri->matrix[nh_cm_idx(*ri, r, ra, g, al)]
    for (i = 0; i < ri->num_roles; i++)
        ok[i] = 1;
    if ((*role = pick_one("Pick a role", ri->rolenames_m, ri->num_roles, ok)) < 0)
        return 0;
    for (i = 0; i < ri->num_races; i++)
        for (ok[i] = 0, b = 0; b < ri->num_genders; b++)
            for (c = 0; c < ri->num_aligns; c++)
                ok[i] |= VALID(*role, i, b, c);
    if ((*race = pick_one("Pick a race", ri->racenames, ri->num_races, ok)) < 0)
        return 0;
    for (i = 0; i < ri->num_genders; i++)
        for (ok[i] = 0, c = 0; c < ri->num_aligns; c++)
            ok[i] |= VALID(*role, *race, i, c);
    if ((*gend = pick_one("Pick a gender", ri->gendnames, ri->num_genders, ok)) < 0)
        return 0;
    for (a = 0; a < ri->num_aligns; a++)
        ok[a] = VALID(*role, *race, *gend, a);
    if ((*align = pick_one("Pick an alignment", ri->alignnames, ri->num_aligns, ok)) < 0)
        return 0;
    return 1;
}

/* newest game log in save/ that can go on (saved, or crashed = tab closed) */
static int find_save(char *path, size_t len)
{
    DIR *d = opendir(SAVEDIR);
    struct dirent *e;
    time_t best = 0;
    struct stat st;
    char f[512];
    int fd, s;

    *path = 0;
    if (!d)
        return 0;
    while ((e = readdir(d))) {
        size_t n = strlen(e->d_name);

        if (n < 8 || strcmp(e->d_name + n - 7, ".nhgame"))
            continue;
        snprintf(f, sizeof f, SAVEDIR "%s", e->d_name);
        if ((fd = open(f, O_RDWR)) < 0)
            continue;
        s = nh_get_savegame_status(fd, NULL);
        fstat(fd, &st);
        close(fd);
        if ((s == LS_SAVED || s == LS_CRASHED || s == LS_IN_PROGRESS) && st.st_mtime >= best)
            best = st.st_mtime, snprintf(path, len, "%s", f);
    }
    closedir(d);
    return *path != 0;
}

/* finished or unreadable game: move its log out of save/ */
static void retire(const char *path)
{
    char to[512];

    snprintf(to, sizeof to, LOGDIR "%s", strrchr(path, '/') + 1);
    rename(path, to);
}

static int play(void)
{
    char path[512], name[BUFSZ], desc[QBUFSZ], q[QBUFSZ * 2];
    int fd, role, race, gend, align, ret;

    if (find_save(path, sizeof path)) {
        fd = open(path, O_RDWR);
        add_msg("Restoring your game...");
        if (nh_restore_game(fd, NULL, FALSE) == GAME_RESTORED)
            goto run;
        close(fd);
        add_msg("That game could not be restored; it was moved to log/.");
        retire(path);
    }
    do {
        while (!choose_char(&role, &race, &gend, &align))
            ;
        nh_root_plselection_prompt(desc, sizeof desc - 1, role, race, gend, align);
        snprintf(q, sizeof q, "You are a %s.  What is your name?", desc);
        do
            web_getlin(q, name);
        while (!*name || strlen(name) >= PL_NSIZ);
    } while (*name == '\033');
    snprintf(path, sizeof path, SAVEDIR "%ld_%s.nhgame", (long) time(NULL), name);
    if ((fd = open(path, O_TRUNC | O_CREAT | O_RDWR, 0660)) < 0) {
        add_msg("Could not create the game log in " SAVEDIR ".");
        return GAME_PANICKED;
    }
    if (!nh_start_game(fd, name, role, race, gend, align, MODE_NORMAL)) {
        close(fd);
        unlink(path);
        return GAME_PANICKED;
    }
 run:
    ret = commandloop();
    close(fd);
    if (ret == GAME_OVER)
        retire(path);
    return ret;
}

int main(int argc, char *argv[])
{
    char *paths[PREFIX_COUNT];
    int i;

    srand(time(NULL) ^ getpid());
    for (i = 0; i < PREFIX_COUNT; i++)
        paths[i] = GAMEDIR;
    paths[DATAPREFIX] = DATADIR;
    paths[DUMPPREFIX] = GAMEDIR "dumps/";
    mkdir(SAVEDIR, 0755), mkdir(LOGDIR, 0755), mkdir(GAMEDIR "dumps", 0755);
    nh_lib_init(&web_procs, paths);
    di = copy_drawing(nh_get_drawing_info());
    init_keymap();
    play();
    nh_lib_exit();
    js_end();
    return 0;
}
