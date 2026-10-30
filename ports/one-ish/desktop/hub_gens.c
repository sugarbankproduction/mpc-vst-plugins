/* The GENERATIVE page's own buttons and the filters' SYNC (2026-10-30), the window's side only (no engine change):
 *
 * RANDOMISE | ALL and MUTATE | ALL (the right column's last row): the randomiser on the GENERATIVE
 * section alone (src/randomize.h rnd_gen_gen and rnd_gen_gens): every generator's settings in musical ranges, one
 * generator switched on (or two), KEY and SCALE; MUTATE nudges the settings of those that are on. The big part acts on
 * the Synth in focus, ALL on each of the three (each its own). The sound itself never changes. One undo step each.
 *
 * RESET | ALL (beside ALL OFF, the page's bottom row): every generator's settings back to their defaults, and so off (Euclid, Turing,
 * balls, particles, LIFE, orbits, pendulums, rain, Markov), in the Synth in focus or (ALL) all three. KEY and SCALE
 * stay (the arpeggiator and sequencer play in them too), and FOLLOW TEMPO (shared). As it undoes work, it asks once:
 * the first click turns it red ("CLICK AGAIN"), a second click within 4 s resets; anything else leaves it. One undo
 * step (the console's UNDO brings everything back).
 *
 * SYNC (over the filters' ALL 3): once, both cutoffs (SVF and ladder) of all three Synths to the Synth in focus's.
 * LINK CUTOFF in a Synth would drag one cutoff with the other while they land, so it is off meanwhile and goes back
 * on. One undo step; the host is told of each change, as for a knob. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "editor.h"
#include "library.h"

enum { HUB_ROWS = 6, GENS_ARM_MS = 4000 };
_Static_assert(NHUB <= HUB_ROWS + 3, "the hub page's RANDOMISE row sits under GEN TEMPO and FOLLOW KEYS (hub.c)");

static int key_of(const char *k) {
    for (int i = 0; i < NPARAMS; i++)
        if (!strcmp(PARAMS[i].key, k)) return i;
    return -1;
}

/* a generator's own setting: Synth 1's index, not shared, not KEY / SCALE (the arp and sequencer use them too) */
int gen_param(int i) {
    if (i < 0 || i >= NPARAMS || synth_of(i) || shared_param(i)) return 0;
    const char *k = PARAMS[i].key;
    static const char *const PRE[] = {"gen_", "phys_", "rain_", "mkv_", "life_", "orb_", "pend_"};
    for (unsigned p = 0; p < sizeof PRE / sizeof PRE[0]; p++)
        if (!strncmp(k, PRE[p], strlen(PRE[p]))) return 1;
    return 0;
}

/* ---- where they sit (2026-10-30, with the generators' GEN TEMPO and FOLLOW KEYS rows): RANDOMISE | ALL and
 * MUTATE | ALL the right column's last row (under FOLLOW KEYS | FOLLOW TEMPO), RESET | ALL beside ALL OFF ---- */
static RECT row_rect(int n) {   /* n: 0 the RANDOMISE / MUTATE row, 1 the bottom row's left half (ALL OFF, RESET) */
    RECT nm, c;
    if (n == 0) {
        slot_rects(ROWS + HUB_ROWS - 1, &nm, &c);
        RECT r = {nm.left, nm.top + sc(2), c.right, nm.bottom - sc(2)};
        return r;
    }
    int half = (rw(&L.row[0]) - sc(8)) / 2;
    RECT r = {L.row[0].left, L.row[ROWS - 1].top + sc(2), L.row[0].left + half, L.row[ROWS - 1].bottom - sc(2)};
    return r;
}
/* half 0 / 1 of the row (-1: the whole row); all: its ALL part, else the big part */
static RECT split(RECT r, int half, int all) {
    int w = half < 0 ? rw(&r) : (rw(&r) - sc(6)) / 2, x0 = r.left + (half > 0 ? w + sc(6) : 0), x1 = x0 + w;
    RECT o = {all ? x1 - sc(30) : x0, r.top, all ? x1 : x1 - sc(33), r.bottom};
    return o;
}
RECT gens_rect(int z) {   /* RANDOMISE | ALL and MUTATE | ALL one row; RESET | ALL beside ALL OFF */
    RECT r = {0, 0, 0, 0};
    switch (z) {
    case Z_GEN_RND: return split(row_rect(0), 0, 0);
    case Z_GEN_RND_ALL: return split(row_rect(0), 0, 1);
    case Z_GEN_MUT: return split(row_rect(0), 1, 0);
    case Z_GEN_MUT_ALL: return split(row_rect(0), 1, 1);
    case Z_GEN_RESET: return split(row_rect(1), 1, 0);
    case Z_GEN_RESET_ALL: return split(row_rect(1), 1, 1);
    case Z_SYNC_CUT: return L.sync_cut;
    }
    return r;
}
int gens_hit(ed_t *e, POINT pt) {
    (void)e;
    for (int z = Z_GEN_RND; z <= Z_GEN_RESET_ALL; z++) {
        RECT r = gens_rect(z);
        if (PtInRect(&r, pt)) return z;
    }
    return -1;
}

/* ---- RESET ---- */
void gens_reset(ed_t *e, int all) {
    lib_hold(e, 1);   /* one undo step */
    lib_touch(e, all ? "Reset the generators (all 3)" : "Reset the generators");
    for (int c = 0; c < NSYNTHS; c++) {
        if (!all && c != e->focus) continue;
        for (int i = 0; i < NPARAMS; i++) {
            if (!gen_param(i)) continue;
            int k = synth_param(i, c);
            if (fabsf(e->fx->getParameter(e->fx, k) - PARAMS[k].def) > 1e-6f) ed_edit_abs(e, k, PARAMS[k].def);
        }
    }
    lib_hold(e, -1);
}

/* ---- SYNC: both cutoffs of all three Synths to the focus's ---- */
void sync_cutoffs(ed_t *e) {
    static const char *const CUT[2] = {"svf_cut", "flt_cut"};
    int link = key_of("flt_link");
    lib_hold(e, 1);
    lib_touch(e, "Sync the cutoffs");
    for (int c = 0; c < NSYNTHS; c++) {
        if (c == e->focus) continue;
        int lk = link >= 0 ? synth_param(link, c) : -1, was = lk >= 0 && e->fx->getParameter(e->fx, lk) > 0.5f;
        if (was) ed_edit_abs(e, lk, 0.0f);
        for (int k = 0; k < 2; k++) {
            int i = key_of(CUT[k]);
            if (i < 0) continue;
            float v = e->fx->getParameter(e->fx, synth_param(i, e->focus));
            if (fabsf(e->fx->getParameter(e->fx, synth_param(i, c)) - v) > 1e-7f) ed_edit_abs(e, synth_param(i, c), v);
        }
        if (was) ed_edit_abs(e, lk, 1.0f);
    }
    lib_hold(e, -1);
}

int gens_press(ed_t *e, int z) {
    if (z == Z_SYNC_CUT) {
        sync_cutoffs(e);
    } else if (z == Z_GEN_RND || z == Z_GEN_RND_ALL || z == Z_GEN_MUT || z == Z_GEN_MUT_ALL) {
        ed_rnd_gens(e, z == Z_GEN_MUT || z == Z_GEN_MUT_ALL, z == Z_GEN_RND_ALL || z == Z_GEN_MUT_ALL);
    } else if (z == Z_GEN_RESET || z == Z_GEN_RESET_ALL) {   /* the first click asks, the second resets */
        unsigned now = (unsigned)GetTickCount();
        if (e->gens_arm == z && now - e->gens_arm_t < GENS_ARM_MS) {
            e->gens_arm = 0;
            gens_reset(e, z == Z_GEN_RESET_ALL);
        } else {
            e->gens_arm = z;
            e->gens_arm_t = now;
        }
    } else {
        return 0;
    }
    if (z != Z_GEN_RESET && z != Z_GEN_RESET_ALL) e->gens_arm = 0;
    if (e->wnd) InvalidateRect(e->wnd, NULL, FALSE);
    return 1;
}

static int armed(ed_t *e, int z) {   /* RESET waiting for its second click (and not yet run out) */
    if (e->gens_arm != z) return 0;
    if ((unsigned)GetTickCount() - e->gens_arm_t < GENS_ARM_MS) return 1;
    e->gens_arm = 0;
    return 0;
}
void gens_draw(HDC dc, ed_t *e) {
    screen_button(dc, gens_rect(Z_GEN_RND), "RANDOMISE", 0, e->hover == Z_GEN_RND, font_cell);
    screen_button(dc, gens_rect(Z_GEN_RND_ALL), "ALL", 0, e->hover == Z_GEN_RND_ALL, font_cell);
    screen_button(dc, gens_rect(Z_GEN_MUT), "MUTATE", 0, e->hover == Z_GEN_MUT, font_cell);
    screen_button(dc, gens_rect(Z_GEN_MUT_ALL), "ALL", 0, e->hover == Z_GEN_MUT_ALL, font_cell);
    int a1 = armed(e, Z_GEN_RESET), a3 = armed(e, Z_GEN_RESET_ALL);   /* (lit: waiting for the second click) */
    screen_button(dc, gens_rect(Z_GEN_RESET), a1 ? "SURE?" : "RESET", a1, e->hover == Z_GEN_RESET, font_cell);
    screen_button(dc, gens_rect(Z_GEN_RESET_ALL), a3 ? "SURE?" : "ALL", a3, e->hover == Z_GEN_RESET_ALL,
                  a3 ? font_tiny : font_cell);
}

const char *gens_help(ed_t *e, int z) {
    if ((z == Z_GEN_RESET || z == Z_GEN_RESET_ALL) && armed(e, z))
        return z == Z_GEN_RESET ? "SURE? Click RESET again (within 4 s) to put every generator of the Synth in focus back to "
                                  "its defaults, so all off. Anything else: nothing happens."
                                : "SURE? Click ALL again (within 4 s) to put every generator of all three Synths back to "
                                  "its defaults, so all off. Anything else: nothing happens.";
    switch (z) {
    case Z_GEN_RND: return "RANDOMISE (generators): new settings for every generator of the Synth in focus, in musical ranges, "
                           "with one generator switched on (sometimes two), and a new KEY and SCALE. The sound stays. One "
                           "undo step. ALL beside it: each of the three Synths its own.";
    case Z_GEN_RND_ALL: return "ALL: RANDOMISE the generators of all three Synths (each its own). One undo step.";
    case Z_GEN_MUT: return "MUTATE (generators): nudges the settings of the generators that are on (40%, or "
                           "the RANDOM page's AMOUNT if higher); which ones are on, KEY and SCALE stay. One undo step. ALL beside it: all three Synths.";
    case Z_GEN_MUT_ALL: return "ALL: MUTATE the generators of all three Synths. One undo step.";
    case Z_GEN_RESET: return "RESET: every generator of the Synth in focus back to its default settings, so all off. KEY, "
                             "SCALE and FOLLOW TEMPO stay. Click twice (it asks first). One undo step.";
    case Z_GEN_RESET_ALL: return "ALL: RESET the generators of all three Synths. Click twice (it asks first). One undo step.";
    case Z_SYNC_CUT: return "SYNC: once, both cutoffs (SVF and ladder) of all three Synths to this Synth's. One undo step. "
                            "(ALL 3 under it keeps them moving together from now on.)";
    }
    return NULL;
}
