/* DynaHack 0.6.0  web/webwin.c  browser client (RVIP) */
/* Takes the place of the curses client (nitrohack/): implements
 * struct nh_window_procs for libnitrohack and runs the command loop.
 * C decides everything, web/dynahack.js only draws it and rvip-wm.js
 * places the windows (RVIP.md Part W, W0):
 *   js_map:  ROWNO*COLNO tile indexes (-1 = none yet, stage 4) and the
 *            same cells as text (char | colour << 8, the game's own
 *            symbols from nh_get_drawing_info), cursor x/y, level
 *   js_text: 0 prompt, 1 status lines, 2 inventory, 3 pop-up, 4 new
 *            message, 5 messages so far are old, 6 replace last message.
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
#define idle() ((void) 0)
#define web_delay() ((void) 0)
#define yield_sometimes() ((void) 0)
#endif

struct row { char ch; int sel, clr; char *s; }; /* sel: 0/1, 2 = heading */
struct pop { const char *prompt; struct row *rows; int n, top, cur, any; };

static struct nh_drawing_info *di;
static struct nh_dbuf_entry dbuf[ROWNO][COLNO];
static int cells[ROWNO * COLNO], chars[ROWNO * COLNO];
static int curx, cury, have_map, level_z;
static struct pop *popup;
static char promptbuf[BUFSZ * 2], statbuf[BUFSZ * 3];
static char hist_prev[BUFSZ];
static int hist_reps, nhist, at_cmd;
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

static void redraw(void)
{
    int x, y, i;

    if (have_map) {
        for (y = 0; y < ROWNO; y++)
            for (x = 0; x < COLNO; x++) {
                struct nh_symdef s = cell_sym(&dbuf[y][x]);

                cells[y * COLNO + x] = -1; /* ponytail: tiles come in stage 4 */
                chars[y * COLNO + x] = (unsigned char) s.ch | pal(s.color) << 8;
            }
        js_map(cells, chars, curx, cury, level_z);
    }
    js_text(0, promptbuf);
    js_text(1, statbuf);
    js_text(2, invtext ? invtext : "");

    tlen = 0, tadd("%s", "");
    if (popup) { /* first row: top, cursor, prompt */
        tadd("%d\t%d\t%s\n", popup->top, popup->cur, popup->prompt ? popup->prompt : "");
        for (i = 0; i < popup->n; i++) {
            struct row *r = &popup->rows[i];

            tadd("-1\t%c\t%d\t%d\t%s\n", r->ch ? r->ch : ' ', r->sel, r->clr, r->s);
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
        case 0x101: return 'k';
        case 0x102: return 'j';
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

static void add_msg(const char *s)
{
    if (!*s)
        return;
    if (nhist && !strcmp(s, hist_prev)) { /* repeat -> "message (xN)" */
        char fold[BUFSZ + 16];

        snprintf(fold, sizeof fold, "%s (x%d)", s, ++hist_reps);
        js_text(6, fold);
        return;
    }
    snprintf(hist_prev, sizeof hist_prev, "%s", s);
    hist_reps = 1;
    nhist++;
    js_text(4, s);
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
            if ((k == ' ' || k == '>' || k == 'j') && p->top + rows < p->n)
                p->top += k == 'j' ? 1 : rows;
            else if ((k == '<' || k == 'k') && p->top > 0)
                p->top -= k == 'k' ? 1 : (p->top < rows ? p->top : rows);
            else
                break;
            continue;
        }
        for (i = 0; i < p->n && !(p->rows[i].sel != 2 && p->rows[i].ch == k); i++)
            ;
        if (i < p->n) { /* accelerator */
            if (how == PICK_ONE) {
                p->cur = i;
                break;
                break;
            }
            p->rows[i].sel ^= 1, p->cur = i;
            continue;
        }
        if (k == '\n' || k == '\r' || (k == ' ' && how == PICK_ONE))
            break;
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

static int web_display_menu(struct nh_menuitem *items, int icount, const char *title,
                            int how, int *results)
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
        r->ch = items[i].accel;
        if (pick && !r->ch && how != PICK_NONE && acc)
            r->ch = acc, acc = next_accel(acc);
    }
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

static int obj_color(struct nh_objitem *it)
{
    if (it->otype <= 0 || it->otype > di->num_objects)
        return CLR_GRAY;
    if (it->omonnum > 0 && !strcmp(di->objects[it->otype - 1].symname, "corpse"))
        return pal(di->monsters[it->omonnum - 1].color);
    return pal(di->objects[it->otype - 1].color);
}

static int web_display_objects(struct nh_objitem *items, int icount, const char *title,
                               int how, struct nh_objresult *results)
{
    struct pop p = { title, calloc(icount + 1, sizeof(struct row)), icount };
    int i, n;

    if (how == PICK_INVACTION) /* stage 3 adds the item action menus */
        how = PICK_NONE;
    for (i = 0; i < icount; i++) {
        struct row *r = &p.rows[i];

        r->s = strdup(items[i].caption);
        r->sel = items[i].role == MI_NORMAL && items[i].id ? 0 : 2;
        r->clr = r->sel == 2 ? CLR_YELLOW : obj_color(&items[i]);
        r->ch = items[i].accel;
    }
    n = run_pop(&p, how);
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

        tadd("-1\t%c\t%d\t%d\t%s\n", items[i].accel ? items[i].accel : ' ', head ? 2 : 0,
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
        p.rows[p.n].ch = 0, p.rows[p.n].sel = 2, p.rows[p.n].clr = CLR_GRAY;
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

    for (;;) {
        if (has)
            snprintf(promptbuf, sizeof promptbuf, "%s  Count: %d", query, cnt);
        else
            snprintf(promptbuf, sizeof promptbuf, "%s", query);
        k = getkey();
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

/* '?' and '#' with an empty line: every command, pick one to run it */
static struct nh_cmd_desc *cmd_menu(const char *title)
{
    struct nh_menuitem *items = calloc(ncmds, sizeof *items);
    int i, n = 0, pick[1];
    char k[16];

    for (i = 0; i < ncmds; i++) {
        unsigned char c = cmds[i].defkey ? cmds[i].defkey : cmds[i].altkey;

        if (cmds[i].flags & CMD_DEBUG)
            continue;
        if (!c)
            strcpy(k, "#");
        else if (c < 32)
            snprintf(k, sizeof k, "^%c", c + 64);
        else if (c >= 128)
            snprintf(k, sizeof k, "M-%c", c - 128);
        else
            snprintf(k, sizeof k, "%c", c);
        items[n].id = i + 1, items[n].role = MI_NORMAL;
        snprintf(items[n].caption, BUFSZ, "%-4s %-14s %s", k, cmds[i].name, cmds[i].desc);
        n++;
    }
    n = web_display_menu(items, n, title, PICK_ONE, pick);
    free(items);
    return n > 0 ? &cmds[pick[0] - 1] : NULL;
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
        else if (k == '?')
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
