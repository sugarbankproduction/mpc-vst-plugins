/* The MATRIX page's own tools and the SETUP page's key range by a key (2026-10-30; the desktop editor, see editor.c).
 *
 *   DELETE: an X at the right end of every row on the MATRIX page. A click takes that row out; the rows below it move
 *   up one (their numbers with them) and the last row is empty. In every Synth in focus; one undo step. (Nothing else
 *   points at a matrix row by its number: the sequencer's lanes and STEP MOD name destinations, not rows. A MIDI learn
 *   or host automation of a row's own param stays on that row number.)
 *   FILTERS: SOURCE and DEST on the MATRIX title line. A click lists the sources (or destinations) the rows of the Synth
 *   in focus use, with how many rows each; one picked, the page shows only those rows (a source also matches a row
 *   where it is the VIA), CLEAR (or "Show every row") shows all 16 again. EDIT MODULATIONS on quick assign's MOD FROM
 *   page (a DEST button, then that button) opens the page filtered by that source.
 *   DESTINATION in two steps: a click on a row's DESTINATION (or the ribbon's) lists the sections (oscillators, mixer
 *   and noise, filters, envelopes, LFOs, amp and output, the synth effect, master sends, master FX 1, master FX 2);
 *   each opens its parameters. The old one-list menu stays on a right-click.
 *   KEY RANGE: LOW and HIGH in each Synth's LOWEST KEY / HIGHEST KEY cell on the SETUP page. Click one, then press a key
 *   on the window's keyboard, the computer keyboard or a MIDI keyboard: that key becomes the end of the range (the other
 *   end moves along if it would cross). The button reads PRESS and the help line says so while it waits; Esc, the
 *   button again or leaving the page cancels.
 *
 * Window state only (never saved); every change goes through the params as a click would (the host told, undo). */
#include <stdio.h>
#include <string.h>
#include "library.h"
#include "../src/modmatrix.h"

enum { MSG_MS = 5000 };
enum { SEC_OSC, SEC_MIX, SEC_FLT, SEC_ENV, SEC_LFO, SEC_OUT, SEC_SFX, SEC_SEND, SEC_FX1, SEC_FX2, SEC_OTHER, SEC_COUNT };
static const char *const SEC_NAME[SEC_COUNT] = {"Oscillators", "Mixer and noise", "Filters", "Envelopes", "LFOs",
                                                "Amp and output", "Synth effect", "Master sends", "Master FX 1",
                                                "Master FX 2", "Other"};

static int key_of(const char *k) {
    for (int i = 0; i < NPARAMS; i++)
        if (!strcmp(PARAMS[i].key, k)) return i;
    return -1;
}
static int cellp(int m, int col, int c) { return MATRIX[m][col] < 0 ? -1 : synth_param(MATRIX[m][col], c); }
static int opt_at(ed_t *e, int i) {
    return i >= 0 && PARAMS[i].nopts > 1 ? (int)lroundf(e->fx->getParameter(e->fx, i) * (PARAMS[i].nopts - 1)) : 0;
}
static float norm_of(int i, float real) {   /* a value in the param's own units, normalised */
    if (PARAMS[i].nopts > 1) return real / (PARAMS[i].nopts - 1);
    float v = (real - PARAMS[i].min) / (PARAMS[i].max - PARAMS[i].min);
    return v < 0 ? 0 : v > 1 ? 1 : v;
}
static void say(ed_t *e, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(e->mx.msg, sizeof e->mx.msg, fmt, ap);
    va_end(ap);
    e->mx.msg_at = GetTickCount();
}
static void repaint(ed_t *e) { if (e->wnd) InvalidateRect(e->wnd, &L.screen, FALSE); }
static int src_list(void) { return NMATRIX ? MATRIX[0][MX_SRC] : -1; }
static int dst_list(void) { return NMATRIX ? MATRIX[0][MX_DST] : -1; }
static const char *src_name(int s) { int k = src_list(); return k >= 0 && s > 0 && s < PARAMS[k].nopts ? PARAMS[k].opts[s] : "?"; }

/* ---- the rows shown ---- */
static int row_matches(ed_t *e, int m) {   /* the Synth in focus's row m passes the filters */
    int c = e->focus;
    if (e->mx.fsrc > 0 && opt_at(e, cellp(m, MX_SRC, c)) != e->mx.fsrc && opt_at(e, cellp(m, MX_VIA, c)) != e->mx.fsrc)
        return 0;
    if (e->mx.fdst > 0 && opt_at(e, cellp(m, MX_DST, c)) != e->mx.fdst) return 0;
    return 1;
}
int mx_filtering(ed_t *e) { return e->mx.fsrc > 0 || e->mx.fdst > 0; }
int mx_view(ed_t *e, int *rows) {
    int n = 0;
    for (int m = 0; m < NMATRIX; m++)
        if (!mx_filtering(e) || row_matches(e, m)) rows[n++] = m;
    return n;
}
static void filters_clear(ed_t *e) { e->mx.fsrc = e->mx.fdst = 0; }
int mx_sub_of(ed_t *e, int row) {
    int rows[NMATRIX], n = mx_view(e, rows);
    for (int k = 0; k < n; k++)
        if (rows[k] == row) return k / ROWS;
    filters_clear(e);   /* (a filter hides it: everything shown again) */
    return row >= 0 ? row / ROWS : 0;
}
void mx_edit_mods(ed_t *e, int src) {
    e->mx.fsrc = src;
    e->mx.fdst = 0;
    ed_show(e, SCR_MATRIX, 0);
    int rows[NMATRIX], n = mx_view(e, rows);
    if (n) say(e, "EDIT MODULATIONS: the %d row%s using %s (CLEAR shows all 16).", n, n == 1 ? "" : "s", src_name(src));
    else say(e, "EDIT MODULATIONS: no row uses %s yet (CLEAR shows all 16).", src_name(src));
    repaint(e);
}

/* ---- where things are ---- */
RECT mx_title_rect(void) { RECT r = L.tabline; r.right = r.left + sc(96); return r; }
static RECT setup_btn(int row, int c) {
    RECT cell = setup_cell(row, c), r = {cell.right - sc(32), cell.top + sc(1), cell.right, cell.bottom - sc(1)};
    return r;
}
RECT mx_setup_value(int row, int c) { RECT r = setup_cell(row, c); r.right -= sc(35); return r; }
int mx_setup_key(int key) {
    static int lo = -2, hi = -2;
    if (lo == -2) { lo = key_of("key_lo"); hi = key_of("key_hi"); }
    return key >= 0 && key == lo ? 0 : key >= 0 && key == hi ? 1 : -1;
}
static int setup_row_of(ed_t *e, int hi) {   /* the SETUP page's row (this sub-page) with key_lo / key_hi, or -1 */
    int keys[ROWS];
    setup_rows(e->sub, keys);
    for (int r = 0; r < ROWS; r++)
        if (keys[r] >= 0 && mx_setup_key(keys[r]) == hi) return r;
    return -1;
}
RECT mx_rect(ed_t *e, int z) {
    RECT r = {0, 0, 0, 0}, t = L.tabline;
    if (e->screen == SCR_MATRIX) {
        if (z >= Z_MX_FSRC && z <= Z_MX_CLEAR) {
            static const short X[3][2] = {{98, 82}, {184, 90}, {278, 36}};
            int k = z - Z_MX_FSRC;
            if (z == Z_MX_CLEAR && !mx_filtering(e)) return r;
            SetRect(&r, t.left + sc(X[k][0]), t.top + sc(1), t.left + sc(X[k][0] + X[k][1]), t.bottom - sc(1));
        } else if (z >= Z_MX_DEL && z < Z_MX_DEL + ROWS) {
            int rows[NMATRIX], n = mx_view(e, rows), k = z - Z_MX_DEL;
            if (e->sub * ROWS + k >= n) return r;
            RECT row = L.row[k];
            SetRect(&r, row.left + sc(MXC[MX_COLS + 1] + 2), row.top + sc(3), row.right, row.bottom - sc(3));
        }
    } else if (e->screen == SCR_SETUP && z >= Z_MX_KEY && z < Z_MX_KEY + 2 * NSYNTHS) {
        int k = z - Z_MX_KEY, row = setup_row_of(e, k % 2);
        if (row >= 0) r = setup_btn(row, k / 2);
    }
    return r;
}
int mx_hit(ed_t *e, POINT pt) {
    static const int Z[] = {Z_MX_FSRC, Z_MX_FDST, Z_MX_CLEAR};
    for (unsigned k = 0; k < 3 && e->screen == SCR_MATRIX; k++) {
        RECT r = mx_rect(e, Z[k]);
        if (PtInRect(&r, pt)) return Z[k];
    }
    for (int z = Z_MX_DEL; z < Z_MX_DEL + ROWS && e->screen == SCR_MATRIX; z++) {
        RECT r = mx_rect(e, z);
        if (PtInRect(&r, pt)) return z;
    }
    for (int z = Z_MX_KEY; z < Z_MX_KEY + 2 * NSYNTHS && e->screen == SCR_SETUP; z++) {
        RECT r = mx_rect(e, z);
        if (PtInRect(&r, pt)) return z;
    }
    return -1;
}

/* ---- DELETE: the rows below move up ---- */
static void put(ed_t *e, int i, float v) {   /* a param's own index, as a click (host told), only if it changes */
    if (i >= 0 && fabsf(e->fx->getParameter(e->fx, i) - v) > 1e-7f) ed_edit_abs(e, i, v);
}
static void delete_row(ed_t *e, int m) {
    char what[48];
    snprintf(what, sizeof what, "Delete matrix row %d", m + 1);
    lib_hold(e, 1);
    lib_touch(e, what);
    for (int c = 0; c < NSYNTHS; c++) {
        if (c != e->focus && !(e->focus_mask >> c & 1u)) continue;
        for (int j = m; j < NMATRIX - 1; j++) {
            for (int k = 0; k < MX_COLS; k++)
                if (MATRIX[j][k] >= 0 && MATRIX[j + 1][k] >= 0) put(e, cellp(j, k, c), e->fx->getParameter(e->fx, cellp(j + 1, k, c)));
            if (MATRIX_ON[j] >= 0 && MATRIX_ON[j + 1] >= 0)
                put(e, synth_param(MATRIX_ON[j], c), e->fx->getParameter(e->fx, synth_param(MATRIX_ON[j + 1], c)));
        }
        int last = NMATRIX - 1;   /* an empty row: Off to Off, amount 0, no VIA (+100%), no XFORM, enabled */
        static const float EMPTY[MX_COLS] = {0, 0, 0, 0, 100, 0, 0};
        for (int k = 0; k < MX_COLS; k++)
            if (MATRIX[last][k] >= 0) put(e, cellp(last, k, c), norm_of(cellp(last, k, c), EMPTY[k]));
        if (MATRIX_ON[last] >= 0) put(e, synth_param(MATRIX_ON[last], c), 1.0f);
    }
    lib_hold(e, -1);
    if (e->qm.mark >= m) e->qm.mark = e->qm.mark == m ? -1 : e->qm.mark - 1;   /* (quick assign's marked row moves too) */
    e->qm.last_row = -1;
    say(e, "Row %d deleted: the rows below moved up one (UNDO brings it back).", m + 1);
    if (e->wnd) InvalidateRect(e->wnd, NULL, FALSE);   /* (the knobs' modulation rings too) */
}

/* ---- the filters' lists ---- */
static int menu_at(ed_t *e, HMENU m, RECT r) {
    POINT pt = {r.left, r.bottom};
    ClientToScreen(e->wnd, &pt);
    int c = TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, e->wnd, NULL);
    DestroyMenu(m);
    return c;
}
static void pick_filter(ed_t *e, int dst) {
    int list = dst ? dst_list() : src_list(), cur = dst ? e->mx.fdst : e->mx.fsrc, any = 0;
    if (list < 0) return;
    HMENU m = CreatePopupMenu();
    AppendMenuA(m, MF_STRING | (cur ? 0 : MF_CHECKED), 1, "Show every row");
    char t[96];
    for (int o = 1; o < PARAMS[list].nopts; o++) {
        int n = 0;
        for (int r = 0; r < NMATRIX; r++)
            n += dst ? opt_at(e, cellp(r, MX_DST, e->focus)) == o
                     : opt_at(e, cellp(r, MX_SRC, e->focus)) == o || opt_at(e, cellp(r, MX_VIA, e->focus)) == o;
        if (!n) continue;
        snprintf(t, sizeof t, "%s  (%d row%s)", PARAMS[list].opts[o], n, n == 1 ? "" : "s");
        AppendMenuA(m, MF_STRING | (o == cur ? MF_CHECKED : 0) | (any && any % 24 == 0 ? MF_MENUBARBREAK : 0),
                    (UINT_PTR)(100 + o), t);
        any++;
    }
    if (!any) AppendMenuA(m, MF_STRING | MF_GRAYED, 0, dst ? "(no row has a destination yet)" : "(no row has a source yet)");
    int c = menu_at(e, m, mx_rect(e, dst ? Z_MX_FDST : Z_MX_FSRC));
    if (c <= 0) return;
    int v = c == 1 ? 0 : c - 100;
    if (dst) e->mx.fdst = v; else e->mx.fsrc = v;
    e->sub = 0;
    int rows[NMATRIX], n = mx_view(e, rows);
    if (mx_filtering(e)) say(e, "Showing %d of %d rows. CLEAR shows them all.", n, NMATRIX);
    else say(e, "Showing every row.");
    repaint(e);
}

/* ---- DESTINATION in two steps ---- */
static int starts(const char *s, const char *p) { return !strncmp(s, p, strlen(p)); }
static int section_of(const char *n) {
    if (starts(n, "Synth FX")) return SEC_SFX;
    if (starts(n, "Send ") || starts(n, "FX1 > FX2") || starts(n, "Master FX")) return SEC_SEND;
    if (starts(n, "FX1")) return SEC_FX1;
    if (starts(n, "FX2")) return SEC_FX2;
    if (strstr(n, " Env ")) return SEC_ENV;
    if (starts(n, "LFO")) return SEC_LFO;
    if (starts(n, "Ladder") || starts(n, "SVF") || starts(n, "Filter Mix")) return SEC_FLT;
    if (starts(n, "Noise") || starts(n, "Ring") || starts(n, "Ext ") || starts(n, "Macro Level") ||
        (starts(n, "Osc") && strstr(n, " Level")))
        return SEC_MIX;
    if (starts(n, "Pitch") || starts(n, "Osc") || starts(n, "FM") || starts(n, "Angle") || starts(n, "PW") ||
        starts(n, "Macro"))
        return SEC_OSC;
    if (starts(n, "Amp Level") || starts(n, "Pan") || starts(n, "Spread") || starts(n, "Analog Drift")) return SEC_OUT;
    return SEC_OTHER;
}
int mx_is_dst_list(int i) {   /* the same list as a matrix row's DESTINATION (its length, first and last names) */
    int k = dst_list();
    if (k < 0 || i < 0 || i >= NPARAMS || PARAMS[i].nopts != PARAMS[k].nopts || PARAMS[i].nopts < 2) return 0;
    return !strcmp(PARAMS[i].opts[1], PARAMS[k].opts[1]) &&
           !strcmp(PARAMS[i].opts[PARAMS[i].nopts - 1], PARAMS[k].opts[PARAMS[k].nopts - 1]);
}
void mx_pick_dst(ed_t *e, int i, RECT r) {
    int n = PARAMS[i].nopts, cur = option_of(e, i);
    HMENU top = CreatePopupMenu(), sub[SEC_COUNT];
    int count[SEC_COUNT] = {0};
    for (int s = 0; s < SEC_COUNT; s++) sub[s] = CreatePopupMenu();
    for (int d = 1; d < n; d++) {
        int s = section_of(PARAMS[i].opts[d]);
        AppendMenuA(sub[s], MF_STRING | (d == cur ? MF_CHECKED : 0) | (count[s] && count[s] % 24 == 0 ? MF_MENUBARBREAK : 0),
                    (UINT_PTR)(d + 1), PARAMS[i].opts[d]);
        count[s]++;
    }
    AppendMenuA(top, MF_STRING | (cur == 0 ? MF_CHECKED : 0), 1, PARAMS[i].opts[0]);   /* Off */
    for (int s = 0; s < SEC_COUNT; s++) {
        if (!count[s]) { DestroyMenu(sub[s]); continue; }
        int here = cur > 0 && section_of(PARAMS[i].opts[cur]) == s;
        AppendMenuA(top, MF_POPUP | (here ? MF_CHECKED : 0), (UINT_PTR)sub[s], SEC_NAME[s]);
    }
    int c = menu_at(e, top, r);   /* (destroying the top menu destroys its sub-menus) */
    if (c > 0 && c <= n) ed_edit(e, i, n > 1 ? (float)(c - 1) / (n - 1) : 0.0f);
}

/* ---- KEY RANGE by a key ---- */
static void note_name(int n, char *out, int len) {
    static const char *const NM[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    snprintf(out, len, "%s%d", NM[n % 12], n / 12 - 1);
}
static void wait_key(ed_t *e, int kr) {
    e->mx.kr = kr;
    e->mx.heard0 = __atomic_load_n(&e->mx.heard, __ATOMIC_ACQUIRE);
    e->mx.msg[0] = 0;
    repaint(e);
}
void mx_key_heard(ed_t *e, int note) {
    if (!e->mx.kr || note < 0 || note > 127) return;
    int c = (e->mx.kr - 1) / 2, hi = (e->mx.kr - 1) % 2, lo_k = key_of("key_lo"), hi_k = key_of("key_hi");
    if (lo_k < 0 || hi_k < 0) return;
    int mine = synth_param(hi ? hi_k : lo_k, c), other = synth_param(hi ? lo_k : hi_k, c);
    e->mx.kr = 0;
    char nm[8], what[48];
    note_name(note, nm, sizeof nm);
    snprintf(what, sizeof what, "Synth %d %s key %s", c + 1, hi ? "highest" : "lowest", nm);
    lib_hold(e, 1);
    lib_touch(e, what);
    put(e, mine, norm_of(mine, (float)note));
    float o = PARAMS[other].min + e->fx->getParameter(e->fx, other) * (PARAMS[other].max - PARAMS[other].min);
    int moved = hi ? o > note : o < note;   /* the other end would cross: it comes along */
    if (moved) put(e, other, norm_of(other, (float)note));
    lib_hold(e, -1);
    say(e, "Synth %d's %s KEY is now %s (%d)%s.", c + 1, hi ? "HIGHEST" : "LOWEST", nm, note,
        moved ? (hi ? "; its LOWEST KEY came down to it" : "; its HIGHEST KEY came up to it") : "");
    if (e->wnd) InvalidateRect(e->wnd, NULL, FALSE);
}
void mx_midi_seen(ed_t *e, const void *events) {   /* the host's thread */
    const struct { int32_t n; intptr_t reserved; VstMidiEvent *ev[1]; } *list = events;
    for (int k = 0; k < list->n; k++) {
        const VstMidiEvent *m = list->ev[k];
        if (!m || m->type != 1 || (m->midiData[0] & 0xF0) != 0x90 || !m->midiData[2]) continue;
        unsigned w = __atomic_load_n(&e->mx.heard, __ATOMIC_RELAXED);
        __atomic_store_n(&e->mx.heard, (((w >> 8) + 1) << 8) | (m->midiData[1] & 127u), __ATOMIC_RELEASE);
    }
}
int mx_key(ed_t *e, int vk) {
    if (vk != VK_ESCAPE || !e->mx.kr) return 0;
    e->mx.kr = 0;
    say(e, "Setting the key range by a key: cancelled.");
    repaint(e);
    return 1;
}
void mx_tick(ed_t *e) {
    if (e->mx.kr && e->screen != SCR_SETUP) e->mx.kr = 0;   /* (left the page: no longer waiting) */
    unsigned h = __atomic_load_n(&e->mx.heard, __ATOMIC_ACQUIRE);
    if (e->mx.kr && h != e->mx.heard0) mx_key_heard(e, (int)(h & 127));
    if (e->mx.msg[0] && GetTickCount() - e->mx.msg_at > MSG_MS) { e->mx.msg[0] = 0; repaint(e); }
}
void mx_setup_draw(HDC dc, ed_t *e, int row, int key) {
    int hi = mx_setup_key(key);
    if (hi < 0) return;
    for (int c = 0; c < NSYNTHS; c++) {
        int z = Z_MX_KEY + 2 * c + hi, wait = e->mx.kr == 1 + 2 * c + hi;
        screen_button(dc, setup_btn(row, c), wait ? "PRESS" : hi ? "HIGH" : "LOW", wait, e->hover == z, font_tiny);
    }
}

/* ---- clicks and help ---- */
int mx_press(ed_t *e, int z) {
    if (z < Z_MX || z >= Z_MX_END) return 0;
    if (z == Z_MX_FSRC || z == Z_MX_FDST) pick_filter(e, z == Z_MX_FDST);
    else if (z == Z_MX_CLEAR) { filters_clear(e); e->sub = 0; say(e, "Showing every row."); repaint(e); }
    else if (z >= Z_MX_DEL && z < Z_MX_DEL + ROWS) {
        int rows[NMATRIX], n = mx_view(e, rows), k = e->sub * ROWS + z - Z_MX_DEL;
        if (k < n) delete_row(e, rows[k]);
    } else if (z >= Z_MX_KEY && z < Z_MX_KEY + 2 * NSYNTHS) {
        int kr = 1 + z - Z_MX_KEY;
        if (e->mx.kr == kr) { e->mx.kr = 0; say(e, "Setting the key range by a key: cancelled."); repaint(e); }
        else wait_key(e, kr);
    }
    return 1;
}
const char *mx_zone_help(ed_t *e, int z) {
    static char t[300];
    if (z == Z_MX_FSRC)
        return "SOURCE: show only the rows that use one source (as SOURCE or as VIA). Click for the list of the sources "
               "in use, with how many rows each. CLEAR shows every row again.";
    if (z == Z_MX_FDST)
        return "DEST: show only the rows that move one destination. Click for the list of the destinations in use. "
               "CLEAR shows every row again.";
    if (z == Z_MX_CLEAR) return "CLEAR: the filters off: all 16 rows again.";
    if (z >= Z_MX_DEL && z < Z_MX_DEL + ROWS) {
        int rows[NMATRIX], n = mx_view(e, rows), k = e->sub * ROWS + z - Z_MX_DEL;
        snprintf(t, sizeof t, "DELETE row %d: it goes and the rows below move up one (the last row is empty). UNDO "
                 "brings it back.", k < n ? rows[k] + 1 : 0);
        return t;
    }
    if (z >= Z_MX_KEY && z < Z_MX_KEY + 2 * NSYNTHS) {
        int c = (z - Z_MX_KEY) / 2, hi = (z - Z_MX_KEY) % 2;
        snprintf(t, sizeof t, "%s: click, then press a key (on the keyboard below, the computer keys or a MIDI "
                 "keyboard): it becomes Synth %d's %s KEY. Click again or Esc to cancel.", hi ? "HIGH" : "LOW", c + 1,
                 hi ? "HIGHEST" : "LOWEST");
        return t;
    }
    return NULL;
}
int mx_help(ed_t *e, char *out, int n) {
    if (e->mx.kr) {
        int c = (e->mx.kr - 1) / 2, hi = (e->mx.kr - 1) % 2;
        snprintf(out, n, "PRESS A KEY for Synth %d's %s KEY: the window's keyboard, the computer keys (click the panel "
                 "first) or a MIDI keyboard.\nEsc or %s again: cancel.", c + 1, hi ? "HIGHEST" : "LOWEST", hi ? "HIGH" : "LOW");
        return 1;
    }
    if (e->mx.msg[0] && GetTickCount() - e->mx.msg_at <= MSG_MS && (e->screen == SCR_MATRIX || e->screen == SCR_SETUP) &&
        (e->hover < 0 || (e->hover >= Z_MX && e->hover < Z_MX_END))) {
        snprintf(out, n, "%s", e->mx.msg);
        return 1;
    }
    return 0;
}
intptr_t mx_test(ed_t *e, intptr_t op, char *buf, float opt) {
    switch (op) {
    case MXT_XY: {
        RECT r = mx_rect(e, (int)lroundf(opt));
        return IsRectEmpty(&r) ? -1 : (r.left + r.right) / 2 | ((r.top + r.bottom) / 2) << 16;
    }
    case MXT_VIEW: {
        int rows[NMATRIX], n = mx_view(e, rows), w = 0;
        if (buf) {
            buf[0] = 0;
            for (int k = 0; k < n; k++) w += snprintf(buf + w, 1024 - w, k ? " %d" : "%d", rows[k] + 1);
        }
        return n;
    }
    case MXT_FSRC: return e->mx.fsrc;
    case MXT_FDST: return e->mx.fdst;
    case MXT_WAIT: return e->mx.kr;
    case MXT_CELL_XY: {
        int k = (int)lroundf(opt), r = k / 16, col = k % 16;
        RECT c = matrix_cell(r, col + 1);
        return (c.left + c.right) / 2 | ((c.top + c.bottom) / 2) << 16;
    }
    case MXT_SECTION: {
        int d = (int)lroundf(opt), k = dst_list();
        return k >= 0 && d > 0 && d < PARAMS[k].nopts ? section_of(PARAMS[k].opts[d]) : -1;
    }
    }
    return 0;
}
