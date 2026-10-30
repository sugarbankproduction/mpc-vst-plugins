/* Front-panel conveniences (2026-10-30), the window's side only (the engine is unchanged by them):
 *
 * GENERATIVE (a console button on the third row): the generators' hub page on the centre screen. One row per
 * generator (HUB[] from gen_layout.py: EUCLID, TURING, BOUNCING BALLS, PARTICLES, and any page another branch adds
 * under the ids LIFE, ORBITS, PEND, RAIN, MARKOV), its ON for the Synth in focus (a click switches it), the Synths it
 * is on in (1 2 3), and a click on its name opens its MORE page. Under them KEY and SCALE (what they all play in) with
 * their LINKs, and ALL OFF. (2026-10-30) FOLLOW TEMPO (the right column's last row): the shared gen_follow_tempo,
 * a plain click toggles it; lit, the balls, particles, rain, orbits and LIFE's FREE rate run faster or slower with the
 * tempo (tempo / 120 times their own speed); the text beside it says how much.
 *
 * GEN OFF (beside GENERATIVE) and ALL OFF (on the page) switch every generator's ON off in all three Synths: one undo
 * step, the host told of each. Pressed again while they are all still off (and the same sound loaded), it brings back
 * the ones it switched off. That memory is the window's, never saved.
 *
 * TEMPO (the third row): the tempo everything synced follows, with HOST / OWN. It is the SETTINGS page's RECEIVE MIDI
 * CLOCK (HOST = on) and TEMPO, the same two settings (settings.c keeps them, in its file, for every ONE-ish), so the
 * two places always agree. HOST shows the host's tempo; OWN shows its own, which a drag (up / down) or the wheel sets
 * (Shift: 0.1 BPM). A drag or a scroll on it while it is HOST takes over: OWN, from the host's tempo.
 *
 * MASTER (in SYNTH OUTPUT): lit, that section's VOLUME knob shows and moves master_level, the whole instrument's level
 * (shared by the three Synths) and is labelled MASTER; off, the Synth in focus's own VOLUME. A view, never saved.
 *
 * LINK: a shared param per group of settings (gen_layout.py LINK_GROUPS: KEY, SCALE, the arp's and the sequencer's
 * rate, each LFO's rate, the master effect sends, the balls and particles, and ALL 3 over LINK CUTOFF: the two
 * cutoffs, the one relative LINK, the other Synths moving by as much as the knob did). While it is lit, an edit of one of them from the window goes to all
 * three Synths at once (editor.c edit_targets): the same host automation and the same undo step as the edit itself.
 * Being params, the LINKs are kept with the sound (projects, patches, presets, undo).
 *
 * (2026-10-30) TOGETHER (the third row, under SYNTH 1): lit, a click on a SYNTH button adds that Synth to the ones
 * edited together, or takes it out (what Ctrl-click does); off again, the panel goes back to just the Synth it shows.
 * Ctrl-click, Shift+1-3 and Ctrl+Shift+F5-F7 light it too. UNDO / REDO (the third row's right end): the library's undo
 * of any edit from the window (Ctrl+Z / Ctrl+Y); hovering one names the step it would take back or do again. Both are
 * the window's own state, never saved. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "editor.h"
#include "library.h"
#include "hostrate.h"

enum { HUB_ROWS = 6 };   /* generator rows per column (two columns) */
_Static_assert(NHUB < 2 * HUB_ROWS, "FOLLOW TEMPO takes the hub page's last generator row");

/* ---- LINK ---- */
static int key_ix(const char *k);
static short LINKMAP[NPARAMS];
static int linkmap_ready;
int link_param(int i) {
    if (!linkmap_ready) {
        for (int k = 0; k < NPARAMS; k++) LINKMAP[k] = -1;
        for (int k = 0; k < NLINKOF; k++)
            if (LINK_OF[k][0] >= 0) LINKMAP[LINK_OF[k][0]] = LINK_OF[k][1];
        linkmap_ready = 1;
    }
    i = base_of(i);
    return i >= 0 && i < NPARAMS ? LINKMAP[i] : -1;
}
int link_lit(ed_t *e, int i) {
    int l = link_param(i);
    return l >= 0 && e->fx->getParameter(e->fx, l) > 0.5f;
}
int link_relative(ed_t *e, int i) {   /* lit, and a relative one (ALL 3 over LINK CUTOFF: the cutoffs) */
    static int cut = -2;
    if (cut == -2) cut = key_ix("link_cut");
    return cut >= 0 && link_param(i) == cut && link_lit(e, i);
}

/* ---- SYNTH OUTPUT's MASTER: while it is lit the section's VOLUME knob is master_level (the whole instrument) ---- */
int master_param(void) {
    static int m = -2;
    if (m == -2) m = key_ix("master_level");
    return m;
}
int panel_source(ed_t *e, int i) {
    static int vol = -2;
    if (vol == -2) vol = key_ix("volume");
    return e->vol_master && i == vol && vol >= 0 && master_param() >= 0 ? master_param() : i;
}

/* ---- the page's and the console's places ---- */
static RECT hub_entry(int k) {   /* generator k's row */
    RECT nm, c;
    slot_rects(k % HUB_ROWS + (k / HUB_ROWS) * ROWS, &nm, &c);
    RECT r = {nm.left, nm.top + sc(2), c.right, nm.bottom - sc(2)};
    return r;
}
static RECT half_row(int row, int right) {
    int half = (rw(&L.row[0]) - sc(8)) / 2, x = L.row[0].left + (right ? half + sc(8) : 0);
    RECT r = {x, L.row[row].top + sc(2), x + half, L.row[row].bottom - sc(2)};
    return r;
}
RECT hub_value_rect(int row) {   /* KEY (0) or SCALE (1): its value, between the label and the LINK */
    RECT r = half_row(ROWS - 2, row);
    r.left += sc(52);
    r.right -= sc(44);
    return r;
}
RECT hub_link_rect(int row) {
    RECT r = half_row(ROWS - 2, row);
    r.left = r.right - sc(40);
    return r;
}
RECT hub_follow_rect(void) {   /* (2026-10-30) FOLLOW TEMPO's button: the right column's last row */
    RECT r = hub_entry(2 * HUB_ROWS - 1);
    r.right = r.left + sc(104);
    return r;
}
RECT hub_rect(int z) {
    RECT r = {0, 0, 0, 0};
    if (z >= Z_HUB_ON && z < Z_HUB_ON + NHUB) {
        r = hub_entry(z - Z_HUB_ON);
        r.right = r.left + sc(34);
    } else if (z >= Z_HUB_OPEN && z < Z_HUB_OPEN + NHUB) {
        r = hub_entry(z - Z_HUB_OPEN);
        r.left += sc(38);
    } else if (z == Z_HUB_ALLOFF) {
        r = half_row(ROWS - 1, 0);
    } else if (z == Z_TAB_GEN) r = L.tab_gen;
    else if (z == Z_GEN_OFF) r = L.gen_off;
    else if (z == Z_TEMPO) r = L.tempo;
    else if (z == Z_TEMPO_HOST) r = L.tempo_host;
    else if (z == Z_TEMPO_OWN) r = L.tempo_own;
    else if (z == Z_VOL_MASTER) r = L.vol_master;
    else if (z == Z_TOGETHER) r = L.together;
    else if (z == Z_UNDO) r = L.undo;
    else if (z == Z_REDO) r = L.redo;
    else if (z == Z_TEMPO_DOWN) { r = L.tempo; r.right = r.left + sc(14); }
    else if (z == Z_TEMPO_UP) { r = L.tempo; r.left = r.right - sc(14); }
    return r;
}

static const char *const KS_KEY[2] = {"key_root", "key_scale"}, *const KS_LINK[2] = {"link_key", "link_scale"};
static int key_ix(const char *k) {
    for (int i = 0; i < NPARAMS; i++)
        if (!strcmp(PARAMS[i].key, k)) return i;
    return -1;
}

int hub_hit(ed_t *e, POINT pt) {   /* the hub page (the screen shows it) */
    for (int k = 0; k < NHUB; k++) {
        RECT on = hub_rect(Z_HUB_ON + k), op = hub_rect(Z_HUB_OPEN + k);
        if (HUB[k].on >= 0 && PtInRect(&on, pt)) return Z_HUB_ON + k;
        if (HUB[k].page >= 0 && PtInRect(&op, pt)) return Z_HUB_OPEN + k;
    }
    for (int row = 0; row < 2; row++) {   /* KEY and SCALE, their LINKs: the params themselves (a list, a toggle) */
        RECT v = hub_value_rect(row), l = hub_link_rect(row);
        int iv = key_ix(KS_KEY[row]), il = key_ix(KS_LINK[row]);
        if (iv >= 0 && PtInRect(&v, pt)) { e->hit_screen = 1; e->hit_rect = v; return iv; }
        if (il >= 0 && PtInRect(&l, pt)) { e->hit_screen = 1; e->hit_rect = l; return il; }
    }
    int ft = key_ix("gen_follow_tempo");   /* (2026-10-30) FOLLOW TEMPO: the param itself (a click toggles it) */
    RECT f = hub_follow_rect();
    if (ft >= 0 && PtInRect(&f, pt)) { e->hit_screen = 1; e->hit_rect = f; return ft; }
    RECT a = hub_rect(Z_HUB_ALLOFF);
    return PtInRect(&a, pt) ? Z_HUB_ALLOFF : -1;
}
int hub_hit_console(POINT pt) {
    static const int Z[] = {Z_TAB_GEN, Z_GEN_OFF, Z_TEMPO_DOWN, Z_TEMPO_UP, Z_TEMPO, Z_TEMPO_HOST, Z_TEMPO_OWN,
                            Z_VOL_MASTER, Z_TOGETHER, Z_UNDO, Z_REDO};
    for (unsigned k = 0; k < sizeof Z / sizeof Z[0]; k++) {
        RECT r = hub_rect(Z[k]);
        if (PtInRect(&r, pt)) return Z[k];
    }
    return -1;
}

/* ---- the generators: on, all off, back on ---- */
int hub_on(ed_t *e, int row, int c) {
    return row >= 0 && row < NHUB && HUB[row].on >= 0 && e->fx->getParameter(e->fx, synth_param(HUB[row].on, c)) > 0.5f;
}
static int any_on(ed_t *e) {
    for (int k = 0; k < NHUB; k++)
        for (int c = 0; c < NSYNTHS; c++)
            if (hub_on(e, k, c)) return 1;
    return 0;
}
static int program(ed_t *e) { return (int)e->fx->dispatcher(e->fx, effGetProgram, 0, 0, NULL, 0); }
int hub_restore_n(ed_t *e) {   /* what a press would bring back: only while all are off, with the same sound */
    if (!e->gen_was_n || e->gen_was_prog != program(e) || any_on(e)) return 0;
    return e->gen_was_n;
}
void hub_all_off(ed_t *e) {
    int back = hub_restore_n(e), n = 0;
    lib_hold(e, 1);   /* one undo step */
    lib_touch(e, back ? "Generators back on" : "Generators off");
    if (back) {
        for (int k = 0; k < e->gen_was_n; k++) ed_edit_abs(e, e->gen_was[k], 1.0f);
        e->gen_was_n = 0;
    } else {
        for (int k = 0; k < NHUB; k++) {
            if (HUB[k].on < 0) continue;
            for (int c = 0; c < NSYNTHS; c++) {
                int i = synth_param(HUB[k].on, c), dup = 0;
                for (int j = 0; j < n; j++) dup |= e->gen_was[j] == i;
                if (dup || e->fx->getParameter(e->fx, i) < 0.5f) continue;
                if (n < (int)(sizeof e->gen_was / sizeof e->gen_was[0])) e->gen_was[n++] = (short)i;
                ed_edit_abs(e, i, 0.0f);
            }
        }
        if (n) { e->gen_was_n = n; e->gen_was_prog = program(e); }   /* (none on: what it kept stays) */
    }
    lib_hold(e, -1);
    if (e->wnd) InvalidateRect(e->wnd, NULL, FALSE);
}

/* ---- TEMPO ---- */
int tempo_host_bpm(ed_t *e, double *bpm) {
    double t = e->lib ? e->lib->tempo : 0.0;   /* (library.c asks the host twice a second) */
    if (bpm) *bpm = t;
    return t > 0.0;
}
static float tempo_now(ed_t *e) {   /* what the readout shows: OWN's, or the host's (unknown: OWN's setting) */
    float own;
    double host;
    if (settings_tempo(&own) || !tempo_host_bpm(e, &host)) return own;
    return (float)host;
}
void tempo_text(ed_t *e, char *out, int n) {
    float own;
    double host;
    if (!settings_tempo(&own) && !tempo_host_bpm(e, &host)) snprintf(out, n, "---");
    else snprintf(out, n, "%.1f", tempo_now(e));
}
/* (2026-10-30) FOLLOW TEMPO's text beside its button: how much faster the free-running generators go now */
void hub_follow_text(ed_t *e, char *out, int n) {
    int ft = key_ix("gen_follow_tempo");
    if (ft < 0 || e->fx->getParameter(e->fx, ft) < 0.5f) { snprintf(out, n, "off"); return; }
    float own, bpm;
    double host;
    if (!settings_tempo(&own) && !tempo_host_bpm(e, &host)) bpm = 120.0f;   /* (no tempo yet: the engine's 120) */
    else bpm = tempo_now(e);
    float f = bpm / 120.0f;
    snprintf(out, n, "speed x %.2f", f < 0.25f ? 0.25f : f > 4.0f ? 4.0f : f);   /* (the tempo is on the console) */
}
static float clamp_bpm(float v) { return v < 10.0f ? 10.0f : v > 280.0f ? 280.0f : v; }
void tempo_press(ed_t *e, int y) {   /* a drag starts (editor.c press: M_TEMPO) */
    e->mode = M_TEMPO;
    e->drag_y = y;
    e->enc_moved = 0;   /* (moved yet: a click alone changes nothing) */
    e->tempo_from = tempo_now(e);
}
void tempo_drag(ed_t *e, int y, int shift) {   /* 1 BPM per 2 pixels up (Shift: 0.1 per 2) */
    int d = (e->drag_y - y) / 2;
    if (!d && !e->enc_moved) return;
    e->enc_moved = 1;
    settings_tempo_set(e, 1, clamp_bpm(roundf((e->tempo_from + d * (shift ? 0.1f : 1.0f)) * 10.0f) / 10.0f), 0);
}
void tempo_release(ed_t *e) {
    float bpm;
    settings_tempo(&bpm);
    if (e->enc_moved) settings_tempo_set(e, 1, bpm, 1);   /* (saved now, once) */
    e->enc_moved = 0;
}
int tempo_wheel(ed_t *e, int z, int dir, int fine) {
    if (z != Z_TEMPO) return 0;
    settings_tempo_set(e, 1, clamp_bpm(roundf((tempo_now(e) + dir * (fine ? 0.1f : 1.0f)) * 10.0f) / 10.0f), 1);
    return 1;
}

int hub_press(ed_t *e, int z) {
    if (z < Z_HUB_ON || z >= Z_HUB_END) return 0;
    float own;
    if (z == Z_TAB_GEN) {   /* the hub page (Shift: GEN OFF) */
        if (GetKeyState(VK_SHIFT) < 0) hub_all_off(e);
        else ed_show(e, SCR_GEN, 0);
    } else if (z == Z_GEN_OFF || z == Z_HUB_ALLOFF) {
        hub_all_off(e);
    } else if (z == Z_TEMPO_DOWN || z == Z_TEMPO_UP) {   /* the readout's < >: a plain click steps it (and OWN) */
        settings_tempo_set(e, 1, clamp_bpm(roundf((tempo_now(e) + (z == Z_TEMPO_UP ? 1 : -1) *
                                                   (GetKeyState(VK_SHIFT) < 0 ? 0.1f : 1.0f)) * 10.0f) / 10.0f), 1);
    } else if (z == Z_TOGETHER) {   /* lit: SYNTH buttons add / remove; off: just the Synth shown again */
        if (e->together) { e->together = 0; ed_focus(e, e->focus, 0); }
        else e->together = 1;
    } else if (z == Z_UNDO || z == Z_REDO) {   /* (its message says what it did, on the help line) */
        if (z == Z_UNDO) lib_undo(e);
        else lib_redo(e);
    } else if (z == Z_VOL_MASTER) {   /* (a view: nothing changes in the sound) */
        e->vol_master = !e->vol_master && master_param() >= 0;
    } else if (z == Z_TEMPO_HOST || z == Z_TEMPO_OWN) {
        settings_tempo(&own);
        settings_tempo_set(e, z == Z_TEMPO_OWN, own, 1);
    } else if (z >= Z_HUB_ON && z < Z_HUB_ON + NHUB && HUB[z - Z_HUB_ON].on >= 0) {
        int i = HUB[z - Z_HUB_ON].on;   /* the Synth in focus (and those edited with it, or LINKed) */
        ed_edit(e, i, hub_on(e, z - Z_HUB_ON, e->focus) ? 0.0f : 1.0f);
    } else if (z >= Z_HUB_OPEN && z < Z_HUB_OPEN + NHUB && HUB[z - Z_HUB_OPEN].page >= 0) {
        ed_show(e, HUB[z - Z_HUB_OPEN].page, 0);
    }
    if (e->wnd) InvalidateRect(e->wnd, NULL, FALSE);
    return 1;
}

/* ---- help ---- */
const char *hub_zone_help(ed_t *e, int z) {
    static char t[200];
    lib_t *lb = e->lib;
    if (z == Z_TOGETHER)
        return e->together ? "TOGETHER (lit): click SYNTH 1 / 2 / 3 to add a Synth to the ones edited together, or to take "
                             "it out again; every knob then moves all of them. Click TOGETHER to go back to one Synth."
                           : "TOGETHER: light it, then click SYNTH buttons to edit several Synths at once (a knob moves "
                             "all of them; the panel shows the last one clicked). Shortcut: Ctrl-click a SYNTH button.";
    if (z == Z_UNDO || z == Z_REDO) {
        int redo = z == Z_REDO, n = lb ? (redo ? lb->nredo : lb->nundo) : 0;
        const char *what = n ? (redo ? lb->redo[n - 1].what : lb->undo[n - 1].what) : "";
        if (!n) snprintf(t, sizeof t, redo ? "REDO: nothing to redo (it does again what UNDO took back)."
                                           : "UNDO: nothing to undo yet (it takes back your last edit in this window).");
        else snprintf(t, sizeof t, "%s: %s %s (%d step%s). Keys: %s.", redo ? "REDO" : "UNDO", redo ? "do again" : "take back",
                      what[0] ? what : "the last edit", n, n == 1 ? "" : "s", redo ? "Ctrl+Y" : "Ctrl+Z");
        return t;
    }
    if (z == Z_TAB_GEN) return "GENERATIVE: the generators on one page: each one's ON (in the Synth in focus) and which Synths "
                               "it is on in; click a name for its page. KEY, SCALE and their LINKs; FOLLOW TEMPO; ALL OFF.";
    if (z == Z_GEN_OFF || z == Z_HUB_ALLOFF)
        return "GEN OFF / ALL OFF: switches every generator (Euclid, Turing, balls, particles and the rest) off in all "
               "three Synths: one undo step. Press again while they are still off to bring back the ones it switched "
               "off (not saved with the sound).";
    if (z == Z_TEMPO) return "TEMPO: what everything synced follows (arp, sequencer, LFOs, synced envelopes, effects, the "
                             "generators). Drag up / down or scroll to set your own (Shift: 0.1 BPM); that switches to "
                             "OWN. The same as the SETTINGS page's TEMPO, kept for every sound.";
    if (z == Z_TEMPO_DOWN || z == Z_TEMPO_UP) return "< >: the own tempo 1 BPM down / up (Shift: 0.1), and OWN on. "
                                                     "Or drag the number up / down, or scroll over it.";
    if (z == Z_TEMPO_HOST) return "HOST: follow the host's tempo and song position (SETTINGS: RECEIVE MIDI CLOCK on).";
    if (z == Z_TEMPO_OWN) return "OWN: ignore the host's tempo; everything runs at the TEMPO shown (SETTINGS: RECEIVE MIDI "
                                 "CLOCK off). Kept in the settings, not in the sound.";
    if (z == Z_VOL_MASTER) return "MASTER: lit, this section's knob is MASTER, the level of the whole instrument (all "
                                  "three Synths, saved with the sound). Off: VOLUME, the Synth in focus's own level. "
                                  "(Only what the knob shows: the button itself is not saved.)";
    if (z >= Z_HUB_ON && z < Z_HUB_ON + NHUB) return "ON: this generator in the Synth in focus (LINKed Synths, and those "
                                                     "edited TOGETHER, follow). The digits show which Synths have it on.";
    if (z >= Z_HUB_OPEN && z < Z_HUB_OPEN + NHUB) return "Opens this generator's page.";
    return NULL;
}
void hub_help(ed_t *e, char *out, int n) {
    (void)e;
    snprintf(out, n, "The generators play notes by themselves in the Synth's KEY and SCALE. ON switches one in the Synth "
                     "in focus; a name opens its page. LINK lit: KEY or SCALE the same in all three Synths. FOLLOW "
                     "TEMPO lit: balls, particles, rain, orbits and Life's free rate speed up and slow down with the "
                     "BPM. ALL OFF: silence them all (again: back on).");
}

/* ---- load_test (effVendorSpecific TEST_HUB) ---- */
intptr_t hub_test(ed_t *e, intptr_t op, char *buf, float opt) {
    char b[64] = "";
    float bpm;
    switch (op) {
    case HT_ENGINE_BPM: return hr_peek(e->hr, "bpm", b, sizeof b) > 0 ? (intptr_t)llround(atof(b) * 1000.0) : -1;
    case HT_OWN: return settings_tempo(NULL);
    case HT_TEMPO: settings_tempo(&bpm); return (intptr_t)llround(bpm * 1000.0);
    case HT_RESTORE: return hub_restore_n(e);
    case HT_LINKED: return link_lit(e, (int)lroundf(opt));
    case HT_VOL_MASTER: return e->vol_master;
    case HT_SUB: return e->sub;
    case HT_TOGETHER: return e->together;
    case HT_HELP:   /* (opt 1: the last actions' messages go first, so the help of what is under the mouse shows) */
        if (!buf) return -1;
        if (opt > 0.5f) e->pt.msg[0] = e->st.msg[0] = e->qm.msg[0] = e->sq.msg[0] = 0;
        help_line(e, buf, 1024);
        return (intptr_t)strlen(buf);
    }
    return 0;
}
