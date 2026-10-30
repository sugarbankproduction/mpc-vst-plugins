/* QUICK ASSIGN on the desktop editor (see editor.c): modulation made from the front panel, as the instrument's DEST and
 * expression assign buttons do, without a trip to the MATRIX page.
 *
 *   Pick a source: the DEST button in the title strip of LFO 1-4, the three envelopes and NOISE (Noise EG); or Alt-click
 *   a source's part of the panel (an LFO, an envelope, NOISE: Noise EG or the per-note random values, the pitch and mod
 *   wheels, the X/Y pad: X, Y or its gate, the ribbon, the keyboard: velocity, aftertouch, the key, its gate, the pedals,
 *   a constant; GENERATIVE: any source added to the matrix after these), or right-click it where no control is. A part
 *   with several sources asks which. The source's part lights up while it is picked; the help line says what to do.
 *   Connect: click a knob (or a switch or LED list the matrix can move, or an item on a MORE page). It takes the first
 *   free row (no destination) of the focused Synth's matrix, or the row that already has this source and destination,
 *   in every Synth in focus (Ctrl-picked Synths each their own row); the drag sets the amount, up positive, down
 *   negative (1% a pixel, Shift 0.2%), the knob's own value untouched. Several knobs one after the other; Esc, or the
 *   DEST button again, ends it. Later, Ctrl-drag a modulated knob to change its amount.
 *   (2026-10-30, every gesture a plain click) DEST buttons on every source: LFO 1-4, the envelopes, NOISE, GENERATIVE
 *   (its title strip: the generators' sources) and the controller panel's top left (the wheels, the X/Y pad, the
 *   ribbon, the keyboard and pedals: a list). While a source is picked the centre screen shows MOD FROM <source>: each
 *   of its connections in the Synth in focus with - / + (5% a click), REMOVE and MATRIX (that row, marked), CHANGE
 *   SOURCE (a list of every source), EDIT MODULATIONS (the MATRIX page with only its rows) and DONE (as Esc). Dragging a knob it already moves changes that
 *   connection's amount. Alt-click, right-click, Ctrl-drag and Esc stay as shortcuts.
 *   See it: a thin arc around each modulated knob from its value by the amount (both ways for a source that swings both
 *   ways), coloured by the source's family (envelopes amber, LFOs blue, the hands' controllers green, random violet,
 *   others pink); a dot on a modulated switch. Hovering says what modulates it; a right-click lists the connections
 *   with REMOVE and OPEN IN MATRIX (the row marked there).
 *
 * Every change goes through the matrix's own params (one truth with the MATRIX page), each one told to the host as a
 * knob move is (begin edit, automate, end edit); a whole connect-and-drag or remove is one undo step (library.c). UI
 * thread only; the audio thread sees only setParameter, as from a host. Sources added at the end of the matrix's list
 * later need nothing here (the GENERATIVE part offers them); a destination is found by its param key. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "library.h"
#pragma GCC diagnostic push   /* (only some of gfx.h's shapes) */
#pragma GCC diagnostic ignored "-Wunused-function"
#include "gfx.h"
#pragma GCC diagnostic pop
#include "skin.h"
#include "../src/modmatrix.h"

#define SRC_KEY "mod1_src"
enum { MSG_MS = 5000, MAX_LINKS = MOD_SLOTS };
/* (2026-10-30) the MOD FROM page: PAGE_LINKS connections a sub-page (the screen's rows 0..6), each with - + REMOVE
 * MATRIX (Z_QP_ROW + line * 4 + k), then CHANGE SOURCE, EDIT MODULATIONS and DONE on the last row */
enum { PAGE_LINKS = ROWS - 1, QP_MINUS = 0, QP_PLUS, QP_REMOVE, QP_MATRIX, QP_PER, AMT_STEP = 5 };
#define Z_QP_ROW (Z_QM + 100)
#define Z_QP_PICK (Z_QM + 200)
#define Z_QP_ALL (Z_QM + 201)
#define Z_QP_DONE (Z_QM + 202)
typedef struct { int row, src, on; float amt; } link_t;
static int src_links(ed_t *e, int c, int s, link_t *out);
static int page_index_of(ed_t *e, int row);
static void page_press(ed_t *e, int z);
static const char *page_help(ed_t *e, int z);
static int page_hit(ed_t *e, POINT pt);
static RECT page_rect(ed_t *e, int z);

/* ---- the parts of the panel a source is picked from ---- */
enum { QA_LFO1, QA_LFO2, QA_LFO3, QA_LFO4, QA_FENV, QA_AENV, QA_MENV, QA_NOISE, QA_PITCH, QA_MODW, QA_PAD, QA_RIBBON,
       QA_KEYS, QA_GEN, QA_HANDS, QA_COUNT };
enum { SRC_MAX = 16 };
static const struct {
    const char *panel;        /* the panel's title (PANELS), or NULL: a zone of the controller */
    int dest;                 /* a DEST button in its title strip (QA_HANDS: the controller panel's top left) */
    signed char src[SRC_MAX]; /* its sources (0 ends); QA_GEN: the generators' (is_gen_src) */
} AREA[QA_COUNT] = {
    {"LFO 1", 1, {MS_LFO1}}, {"LFO 2", 1, {MS_LFO2}}, {"LFO 3", 1, {MS_LFO3}}, {"LFO 4", 1, {MS_LFO4}},
    {"FILTER ENVELOPE", 1, {MS_FENV}}, {"AMPLIFIER ENVELOPE", 1, {MS_AENV}}, {"MODULATION ENVELOPE", 1, {MS_MENV}},
    {"NOISE", 1, {MS_NOISE_EG, MS_RAND, MS_RAND_UNI}},
    {NULL, 0, {MS_BEND}}, {NULL, 0, {MS_WHEEL}}, {NULL, 0, {MS_PAD_X, MS_PAD_Y, MS_PAD_GATE}}, {NULL, 0, {MS_RIBBON}},
    {NULL, 0, {MS_VEL, MS_AT, MS_KEY, MS_GATE, MS_SUSTAIN, MS_SOSTENUTO, MS_EXP1, MS_EXP2, MS_CONST}},
    {"GENERATIVE", 1, {0}},
    /* (2026-10-30) the controller panel's DEST: everything the hands play, in one list */
    {NULL, 1, {MS_BEND, MS_WHEEL, MS_PAD_X, MS_PAD_Y, MS_PAD_GATE, MS_RIBBON, MS_VEL, MS_AT, MS_KEY, MS_GATE, MS_SUSTAIN,
               MS_SOSTENUTO, MS_EXP1, MS_EXP2, MS_CONST}}};
/* the generators' sources (the matrix's list from RAIN DROP on, and any added after): the GENERATIVE part's */
static int is_gen_src(int s) { return s >= MS_RAIN_DROP; }

static int key_of(const char *k) {   /* a param's index by key, or -1 */
    for (int i = 0; i < NPARAMS; i++)
        if (!strcmp(PARAMS[i].key, k)) return i;
    return -1;
}
static int src_param(void) { static int k = -2; if (k == -2) k = key_of(SRC_KEY); return k; }
static int nsources(void) { return src_param() >= 0 ? PARAMS[src_param()].nopts : 0; }
static const char *src_name(int s) { return s > 0 && s < nsources() ? PARAMS[src_param()].opts[s] : "?"; }

static int area_sources(int a, int *out) {   /* the sources of part a that the matrix has */
    int n = 0;
    if (a == QA_GEN) {
        for (int s = 1; s < nsources() && n < 32; s++)
            if (is_gen_src(s)) out[n++] = s;
        return n;
    }
    for (int k = 0; k < SRC_MAX && AREA[a].src[k]; k++)
        if (AREA[a].src[k] < nsources()) out[n++] = AREA[a].src[k];
    return n;
}
static int area_of_source(int s) {
    if (is_gen_src(s)) return QA_GEN;
    for (int a = 0; a < QA_COUNT; a++)
        for (int k = 0; k < SRC_MAX && AREA[a].src[k]; k++)
            if (AREA[a].src[k] == s) return a;
    return -1;
}
static int panel_rect(const char *title, RECT *r) {
    for (int p = 0; p < NPANELS; p++)
        if (PANELS[p].style == 1 && !strcmp(PANELS[p].title, title)) {
            SetRect(r, sc(PANELS[p].x), sc(PANELS[p].y), sc(PANELS[p].x + PANELS[p].w), sc(PANELS[p].y + PANELS[p].h));
            return 1;
        }
    return 0;
}
static int area_rect(int a, RECT *r) {   /* where part a is on the panel (0: not there) */
    if (AREA[a].panel) return panel_rect(AREA[a].panel, r);
    if (a == QA_HANDS) {   /* the controller panel: from its DEST to the pad's right */
        SetRect(r, L.qm_hands.left - sc(6), L.qm_hands.top - sc(6), L.pad.right + sc(8), L.pad.bottom + sc(18));
        return !IsRectEmpty(&L.qm_hands);
    }
    const RECT *z = a == QA_PITCH ? &L.pitch : a == QA_MODW ? &L.modw : a == QA_PAD ? &L.pad : a == QA_RIBBON ? &L.ribbon
                                                                                                         : &L.keys;
    *r = *z;
    if (a != QA_KEYS && a != QA_RIBBON) InflateRect(r, sc(4), sc(4));
    return !IsRectEmpty(r);
}
static int dest_rect(int a, RECT *r) {   /* its DEST button, at the left of the title strip */
    RECT p;
    if (a == QA_HANDS) { *r = L.qm_hands; return !IsRectEmpty(r); }
    if (!AREA[a].dest || !area_rect(a, &p)) return 0;
    SetRect(r, p.left + sc(3), p.top + sc(2), p.left + sc(29), p.top + sc(11));
    return 1;
}
static int area_at(POINT pt) {
    for (int a = 0; a < QA_COUNT; a++) {
        RECT r;
        if (area_rect(a, &r) && PtInRect(&r, pt)) {
            int s[32];
            if (area_sources(a, s)) return a;
        }
    }
    return -1;
}

/* ---- the destinations: a panel key -> its matrix destination (MD_*) ---- */
static const char *const FX_KEY[] = {   /* modmatrix.h's MDF_* order (src/engine.c FXMD_SLOT) */
    "modfx_mix", "modfx_gain", "modfx_ctrl1", "modfx_ctrl2", "modfx_rate", "modfx_rate_r", "modfx_depth",
    "modfx_fb", "modfx_fb_r", "modfx_time", "modfx_time_r", "modfx_dtime", "modfx_dfb", "modfx_tone",
    "modfx_spread", "modfx_sweep", "modfx_noise", "modfx_vrate", "modfx_vdepth", "modfx_flutter", "modfx_wow",
    "modfx_bias", "modfx_room", "modfx_decay", "modfx_bits", "modfx_srate", "modfx_reso", "modfx_cut1",
    "modfx_cut2", "modfx_cut3", "modfx_lvl1", "modfx_lvl2", "modfx_lvl3", "mfx_send1", "mfx_send2", "mfx_series",
    "mfx_width", "dly_time", "dly_time_r", "dly_fb", "dly_tone", "dly_offset", "rev_decay", "rev_size", "rev_pre",
    "rev_damp", "rev_lowdamp", "rev_diff", "rev_tone", "rev_mod", "rev_early", "modfx_pitch", "modfx_struct",
    "modfx_pos", "modfx_size", "modfx_density", "modfx_texture", "modfx_drive", "modfx_bump", "modfx_lowcut",
    "modfx_dtime_r", "modfx_depth_r", "modfx_offset", "modfx_fbl", "modfx_fbr", "modfx_xover", "dly_mix",
    "dly_gain", "dly_fb_r", "dly_fbl", "dly_fbr", "dly_rate", "dly_rate_r", "dly_depth", "dly_depth_r",
    "dly_spread", "dly_noise", "dly_sweep", "dly_dfb", "dly_vrate", "dly_vdepth", "dly_flutter", "dly_wow",
    "dly_bias", "dly_room", "dly_decay", "dly_size", "dly_pre", "dly_damp", "dly_lowdamp", "dly_diff", "dly_mod",
    "dly_early", "dly_distance", "dly_hifreq", "dly_hlevel", "dly_mlevel", "dly_llevel", "dly_ldecay", "dly_hdecay",
    "dly_pitch", "dly_pitch_b", "dly_pdecay", "dly_gravity", "dly_xover", "dly_bits", "dly_srate", "dly_reso",
    "dly_cut1", "dly_cut2", "dly_cut3", "dly_lvl1", "dly_lvl2", "dly_lvl3", "rev_mix", "rev_gain", "rev_time",
    "rev_time_r", "rev_fb", "rev_fb_r", "rev_fbl", "rev_fbr", "rev_rate", "rev_rate_r", "rev_depth", "rev_depth_r",
    "rev_offset", "rev_spread", "rev_noise", "rev_sweep", "rev_dfb", "rev_vrate", "rev_vdepth", "rev_flutter",
    "rev_wow", "rev_bias", "rev_room", "rev_distance", "rev_hifreq", "rev_hlevel", "rev_mlevel", "rev_llevel",
    "rev_ldecay", "rev_hdecay", "rev_pitch", "rev_pitch_b", "rev_pdecay", "rev_gravity", "rev_xover", "rev_bits",
    "rev_srate", "rev_reso", "rev_cut1", "rev_cut2", "rev_cut3", "rev_lvl1", "rev_lvl2", "rev_lvl3",
    "modfx_spray", "modfx_prand", "modfx_reverse",   /* (2026-10-30) GRAINS' */
    "dly_gpos", "dly_gdens", "dly_genv", "dly_gspray", "dly_gprand", "dly_greverse",
    "rev_gpos", "rev_gdens", "rev_genv", "rev_gspray", "rev_gprand", "rev_greverse",
    "modfx_sibil", "modfx_sens", "dly_sibil", "dly_sens", "rev_sibil", "rev_sens"};   /* (2026-10-30) the vocoders' */
_Static_assert(sizeof FX_KEY / sizeof FX_KEY[0] == MDF_COUNT, "a panel key per effect destination");
static const struct { const char *key; short dst; } DST[] = {
    {"o1_freq", MD_O1_PITCH}, {"o2_freq", MD_O2_PITCH}, {"o3_freq", MD_O3_PITCH},
    {"o1_level", MD_O1_LEVEL}, {"o2_level", MD_O2_LEVEL}, {"o3_level", MD_O3_LEVEL}, {"noise_level", MD_NOISE_LEVEL},
    {"ring_level", MD_RING_LEVEL}, {"o1_angle", MD_O1_ANGLE}, {"o2_angle", MD_O2_ANGLE}, {"o3_angle", MD_O3_ANGLE},
    {"o1_pw", MD_O1_PW}, {"o2_pw", MD_O2_PW}, {"o3_pw", MD_O3_PW}, {"o1_mix", MD_O1_MIX}, {"o2_mix", MD_O2_MIX},
    {"o3_mix", MD_O3_MIX}, {"fm_amt", MD_FM_AMT}, {"flt_cut", MD_LAD_CUT}, {"flt_res", MD_LAD_RES},
    {"flt_drive", MD_LAD_DRIVE}, {"svf_cut", MD_SVF_CUT}, {"svf_res", MD_SVF_RES}, {"flt_mix", MD_FLT_MIX},
    {"volume", MD_AMP}, {"pitch_lfo", MD_PITCH_LFO1}, {"pitch_env", MD_PITCH_ENV}, {"angle_lfo3", MD_ANGLE_LFO3},
    {"pw_lfo3", MD_PW_LFO3}, {"o1_beat", MD_O1_BEAT}, {"o2_beat", MD_O2_BEAT}, {"o3_beat", MD_O3_BEAT},
    {"svf_space", MD_SVF_SPACE}, {"svf_env", MD_SVF_ENV}, {"svf_lfo", MD_SVF_LFO2}, {"svf_fm", MD_SVF_FM},
    {"lad_env", MD_LAD_ENV}, {"lad_lfo", MD_LAD_LFO2}, {"lad_fm", MD_LAD_FM}, {"pan", MD_PAN},
    {"noise_color_mix", MD_NOISE_MIX}, {"drift", MD_DRIFT}, {"spread", MD_SPREAD}, {"mac_harm", MD_MAC_HARM},
    {"mac_timbre", MD_MAC_TIMBRE}, {"mac_morph", MD_MAC_MORPH}, {"mac_level", MD_MAC_LEVEL}, {"ext_level", MD_EXT_LEVEL},
    {"o1_oct", MD_O1_OCT}, {"o2_oct", MD_O2_OCT}, {"o3_oct", MD_O3_OCT}, {"noise_a", MD_NOISE_A},
    {"noise_sus", MD_NOISE_SUS}, {"noise_r", MD_NOISE_R}};
static short DST_OF[NPARAMS];   /* panel index -> MD_* (-1: none) */
static unsigned char IS_MATRIX[NPARAMS];
static void tables(void) {
    static int ready;
    if (ready) return;
    for (int i = 0; i < NPARAMS; i++) DST_OF[i] = -1;
    for (unsigned k = 0; k < sizeof DST / sizeof DST[0]; k++)
        if (key_of(DST[k].key) >= 0) DST_OF[key_of(DST[k].key)] = DST[k].dst;
    static const char *const ENV[3] = {"fenv", "amp", "menv"};
    static const char *const STAGE[MDE_COUNT] = {"delay", "a", "hold", "d", "s", "r", "scale"};
    static const char *const SW[MDS_COUNT] = {"multi", "sync", "loop", "latch"};
    static const char *const LFO[MDL_COUNT] = {"rate", NULL, "var", "delay", "fade_in", "fade_out"};
    char k[32];
    for (int e = 0; e < 3; e++) {
        for (int s = 0; s < MDE_COUNT; s++) {
            snprintf(k, sizeof k, "%s_%s", ENV[e], STAGE[s]);
            if (key_of(k) >= 0) DST_OF[key_of(k)] = (short)(MD_FENV_DELAY + e * MDE_COUNT + s);
        }
        for (int s = 0; s < MDS_COUNT; s++) {
            snprintf(k, sizeof k, "%s_%s", ENV[e], SW[s]);
            if (key_of(k) >= 0) DST_OF[key_of(k)] = (short)(MD_FENV_MULTI + e * MDS_COUNT + s);
        }
    }
    for (int n = 0; n < 4; n++) {
        for (int s = 0; s < MDL_COUNT; s++) {
            if (!LFO[s]) continue;   /* (the rate in semitones has no knob of its own) */
            snprintf(k, sizeof k, "lfo%d_%s", n + 1, LFO[s]);
            if (key_of(k) >= 0) DST_OF[key_of(k)] = (short)(MD_LFO1_RATE + n * MDL_COUNT + s);
        }
        snprintf(k, sizeof k, "lfo%d_wave", n + 1);
        if (key_of(k) >= 0) DST_OF[key_of(k)] = (short)(MD_LFO1_WAVE + n);
    }
    for (int f = 0; f < MDF_COUNT; f++)
        if (key_of(FX_KEY[f]) >= 0) DST_OF[key_of(FX_KEY[f])] = (short)md_of_fxk(f);
    for (int m = 0; m < NMATRIX; m++) {
        for (int c = 0; c < MX_COLS; c++)
            if (MATRIX[m][c] >= 0) IS_MATRIX[MATRIX[m][c]] = 1;
        if (MATRIX_ON[m] >= 0) IS_MATRIX[MATRIX_ON[m]] = 1;
    }
    ready = 1;
}
static int dst_of(int i) {   /* panel index i's matrix destination, or -1 (a destination the matrix's list lacks: -1) */
    tables();
    int d = i >= 0 && i < NPARAMS ? DST_OF[i] : -1, nd = NMATRIX ? PARAMS[MATRIX[0][MX_DST]].nopts : 0;
    return d > MD_OFF && d < nd ? d : -1;
}
static const char *dst_name(int d) { return NMATRIX && d >= 0 ? PARAMS[MATRIX[0][MX_DST]].opts[d] : "?"; }

/* ---- the matrix rows, Synth c's own params ---- */
static int cell(int m, int col, int c) { return synth_param(MATRIX[m][col], c); }
static int opt_at(ed_t *e, int i) {
    return i >= 0 && PARAMS[i].nopts > 1 ? (int)lroundf(e->fx->getParameter(e->fx, i) * (PARAMS[i].nopts - 1)) : 0;
}
static float real_at(ed_t *e, int i) { return PARAMS[i].min + e->fx->getParameter(e->fx, i) * (PARAMS[i].max - PARAMS[i].min); }
static float amt_norm(int i, float pct) {
    float v = (pct - PARAMS[i].min) / (PARAMS[i].max - PARAMS[i].min);
    return v < 0 ? 0 : v > 1 ? 1 : v;
}
static int row_on(ed_t *e, int m, int c) { return MATRIX_ON[m] < 0 || opt_at(e, synth_param(MATRIX_ON[m], c)) > 0; }
static int links(ed_t *e, int c, int d, link_t *out) {   /* Synth c's rows that reach destination d */
    int n = 0;
    for (int m = 0; m < NMATRIX && n < MAX_LINKS; m++) {
        if (opt_at(e, cell(m, MX_DST, c)) != d) continue;
        int s = opt_at(e, cell(m, MX_SRC, c));
        if (s <= 0 && (MATRIX[m][MX_VIA] < 0 || opt_at(e, cell(m, MX_VIA, c)) <= 0)) continue;   /* (no source at all) */
        link_t l = {m, s, row_on(e, m, c), real_at(e, cell(m, MX_AMT, c))};
        out[n++] = l;
    }
    return n;
}
static int find_row(ed_t *e, int c, int s, int d, int *fresh) {   /* this source and destination's row, else a free one */
    *fresh = 0;
    for (int m = 0; m < NMATRIX; m++)
        if (opt_at(e, cell(m, MX_SRC, c)) == s && opt_at(e, cell(m, MX_DST, c)) == d) return m;
    for (int m = 0; m < NMATRIX; m++)
        if (opt_at(e, cell(m, MX_DST, c)) == 0) { *fresh = 1; return m; }
    return -1;
}

/* a value into a param, as a knob move tells the host: begin edit, the value, automate, end edit */
static void edit_begin(ed_t *e, int i, int end) {
    if (e->host) e->host(e->fx, end ? audioMasterEndEdit : audioMasterBeginEdit, i, 0, NULL, 0);
}
static void put(ed_t *e, int i, float v) {
    if (i < 0) return;
    lib_touch(e, NULL);
    e->fx->setParameter(e->fx, i, v);
    if (e->host) e->host(e->fx, audioMasterAutomate, i, 0, NULL, v);
}
static void put_once(ed_t *e, int i, float v) {
    if (i < 0 || fabsf(e->fx->getParameter(e->fx, i) - v) < 1e-7f) return;
    edit_begin(e, i, 0);
    put(e, i, v);
    edit_begin(e, i, 1);
}
static float opt_norm(int i, int o) { return PARAMS[i].nopts > 1 ? (float)o / (PARAMS[i].nopts - 1) : 0.0f; }
static void row_make(ed_t *e, int m, int c, int s, int d) {   /* a free row: s -> d at 0%, nothing else in it */
    put_once(e, cell(m, MX_AMT, c), amt_norm(cell(m, MX_AMT, c), 0.0f));
    put_once(e, cell(m, MX_VIA, c), 0.0f);
    if (MATRIX[m][MX_VIA_AMT] >= 0) put_once(e, cell(m, MX_VIA_AMT, c), PARAMS[cell(m, MX_VIA_AMT, c)].def);
    if (MATRIX[m][MX_XF] >= 0) put_once(e, cell(m, MX_XF, c), 0.0f);
    if (MATRIX[m][MX_XF_AMT] >= 0) put_once(e, cell(m, MX_XF_AMT, c), PARAMS[cell(m, MX_XF_AMT, c)].def);
    put_once(e, cell(m, MX_SRC, c), opt_norm(cell(m, MX_SRC, c), s));
    put_once(e, cell(m, MX_DST, c), opt_norm(cell(m, MX_DST, c), d));   /* (last: until now the row reached nothing) */
}
static void row_clear(ed_t *e, int m, int c) {   /* the row empty again (its ENABLE as it was) */
    put_once(e, cell(m, MX_DST, c), 0.0f);
    put_once(e, cell(m, MX_SRC, c), 0.0f);
    put_once(e, cell(m, MX_AMT, c), amt_norm(cell(m, MX_AMT, c), 0.0f));
    put_once(e, cell(m, MX_VIA, c), 0.0f);
    if (MATRIX[m][MX_VIA_AMT] >= 0) put_once(e, cell(m, MX_VIA_AMT, c), PARAMS[cell(m, MX_VIA_AMT, c)].def);
    if (MATRIX[m][MX_XF] >= 0) put_once(e, cell(m, MX_XF, c), 0.0f);
    if (MATRIX[m][MX_XF_AMT] >= 0) put_once(e, cell(m, MX_XF_AMT, c), PARAMS[cell(m, MX_XF_AMT, c)].def);
}

/* ---- messages and repaints ---- */
static void say(ed_t *e, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(e->qm.msg, sizeof e->qm.msg, fmt, ap);
    va_end(ap);
    e->qm.msg_at = GetTickCount();
    if (e->wnd) InvalidateRect(e->wnd, &L.screen, FALSE);
}
static void repaint_all(ed_t *e) { if (e->wnd) InvalidateRect(e->wnd, NULL, FALSE); }
static void repaint_ctl(ed_t *e, int i) {
    if (!e->wnd || i < 0 || i >= NPARAMS || IsRectEmpty(&L.ctl[i])) return;
    RECT r = L.ctl[i];
    InflateRect(&r, sc(18), sc(8));
    InvalidateRect(e->wnd, &r, FALSE);
}
static void focus_names(ed_t *e, char *out, int n) {   /* "Synth 1", "Synths 1+3" */
    int k = 0, many = (e->focus_mask & (e->focus_mask - 1)) != 0;
    out[0] = 0;
    for (int c = 0; c < NSYNTHS; c++)
        if (e->focus_mask >> c & 1u) k += snprintf(out + k, n - k, k ? "+%d" : many ? "Synths %d" : "Synth %d", c + 1);
}

/* ---- picking a source: the screen shows MOD FROM <source> while it is picked, the page it showed before after ---- */
static void arm(ed_t *e, int s) {
    if (s && !e->qm.src && e->screen != SCR_QMOD) { e->qm.back_screen = e->screen; e->qm.back_sub = e->sub; }
    if (s && e->screen != SCR_QMOD) ed_show(e, SCR_QMOD, 0);
    if (s && s != e->qm.src) e->sub = 0;
    if (!s && e->screen == SCR_QMOD) ed_show(e, e->qm.back_screen, e->qm.back_sub);
    e->qm.src = s;
    e->qm.msg[0] = 0;
    repaint_all(e);
}
static int menu(ed_t *e, HMENU m, int x, int y) {   /* a popup's choice (load_test answers it instead) */
    int choice;
    if (e->qm.answer) {
        choice = e->qm.answer;
        e->qm.answer = 0;
    } else {
        POINT pt = {x, y};
        ClientToScreen(e->wnd, &pt);
        choice = TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, e->wnd, NULL);
    }
    DestroyMenu(m);
    return choice;
}
static void pick_source(ed_t *e, int a, int x, int y, int ask) {   /* part a: its one source, or a choice of them */
    int s[32], n = area_sources(a, s);
    if (!n) return;
    if (n == 1 && !ask) { arm(e, e->qm.src == s[0] ? 0 : s[0]); return; }
    HMENU m = CreatePopupMenu();
    AppendMenuA(m, MF_STRING | MF_GRAYED, 0, "Modulate from:");
    for (int k = 0; k < n; k++) AppendMenuA(m, MF_STRING | (s[k] == e->qm.src ? MF_CHECKED : 0), (UINT_PTR)s[k], src_name(s[k]));
    int c = menu(e, m, x, y);
    if (c > 0) arm(e, c);
}

/* ---- connecting and turning ---- */
static void drag_start(ed_t *e, int i, int d, int y, const char *what) {
    char label[48];
    snprintf(label, sizeof label, "%s", what);
    lib_hold(e, 1);   /* the connection and its amount: one undo step */
    lib_touch(e, label);
    for (int c = 0; c < NSYNTHS; c++)
        if (e->qm.row[c] >= 0) edit_begin(e, cell(e->qm.row[c], MX_AMT, c), 0);
    e->qm.dst = d;
    e->qm.ctl = i;
    e->qm.drag_y = y;
    e->qm.from = real_at(e, cell(e->qm.row[e->focus], MX_AMT, e->focus));
    e->qm.last_row = e->qm.row[e->focus];
    e->qm.last_dst = d;
    e->mode = M_QMOD;
    SetCapture(e->wnd);
    repaint_ctl(e, i);
    InvalidateRect(e->wnd, &L.screen, FALSE);
}
static int connect(ed_t *e, int i, int y) {   /* the source picked -> control i: a row in each Synth in focus */
    int d = dst_of(base_of(i)), s = e->qm.src, full = 0, made = 0;
    char who[24];
    if (d < 0) return 0;
    for (int c = 0; c < NSYNTHS; c++) e->qm.row[c] = -1;
    int fresh[NSYNTHS] = {0};
    for (int c = 0; c < NSYNTHS; c++) {
        if (!(e->focus_mask >> c & 1u) && c != e->focus) continue;
        e->qm.row[c] = find_row(e, c, s, d, &fresh[c]);
        if (e->qm.row[c] < 0) full |= 1 << c;
    }
    if (e->qm.row[e->focus] < 0) {
        say(e, "All %d matrix rows of Synth %d are in use: %s can't be connected to %s. Free one first (REMOVE in a "
            "source's MOD FROM list, or a row's DESTINATION to Off on the MATRIX page).", NMATRIX, e->focus + 1,
            src_name(s), dst_name(d));
        return 1;
    }
    char what[64];
    snprintf(what, sizeof what, "Quick assign %s > %s", src_name(s), dst_name(d));
    lib_hold(e, 1);
    lib_touch(e, what);
    for (int c = 0; c < NSYNTHS; c++) {
        int m = e->qm.row[c];
        if (m < 0) continue;
        if (fresh[c]) { row_make(e, m, c, s, d); made++; }
        if (MATRIX_ON[m] >= 0) put_once(e, synth_param(MATRIX_ON[m], c), 1.0f);   /* (a row switched off: on again) */
    }
    drag_start(e, i, d, y, what);
    lib_hold(e, -1);   /* (drag_start holds it until the mouse lets go) */
    if (e->screen == SCR_MATRIX) e->sub = mx_sub_of(e, e->qm.row[e->focus]);   /* (the MATRIX page shows the row) */
    if (e->screen == SCR_QMOD) e->sub = page_index_of(e, e->qm.row[e->focus]) / PAGE_LINKS;   /* (and MOD FROM its line) */
    focus_names(e, who, sizeof who);
    if (full) {
        char f[24] = "";
        int k = 0;
        for (int c = 0; c < NSYNTHS; c++)
            if (full >> c & 1) k += snprintf(f + k, sizeof f - k, k ? "+%d" : "%d", c + 1);
        say(e, "%s > %s connected; Synth %s has all %d rows in use: not there.", src_name(s), dst_name(d), f, NMATRIX);
    } else {
        say(e, "%s > %s %s (row %d, %s).", src_name(s), dst_name(d), made ? "connected" : "found", e->qm.row[e->focus] + 1,
            who);
    }
    return 1;
}
static int turn_existing(ed_t *e, int i, int y) {   /* Ctrl-drag a modulated control: its connection's amount */
    int d = dst_of(base_of(i));
    link_t l[MAX_LINKS];
    int n = d < 0 ? 0 : links(e, e->focus, d, l);
    if (!n) return 0;
    int pick = 0;
    for (int k = 0; k < n; k++)
        if (l[k].row == e->qm.last_row && d == e->qm.last_dst) pick = k;
    int s = l[pick].src;
    for (int c = 0; c < NSYNTHS; c++) {
        e->qm.row[c] = -1;
        if (c == e->focus) { e->qm.row[c] = l[pick].row; continue; }
        if (!(e->focus_mask >> c & 1u)) continue;
        for (int m = 0; m < NMATRIX && e->qm.row[c] < 0; m++)   /* the other Synths' row of the same connection */
            if (opt_at(e, cell(m, MX_SRC, c)) == s && opt_at(e, cell(m, MX_DST, c)) == d) e->qm.row[c] = m;
    }
    char what[64];
    snprintf(what, sizeof what, "Mod amount %s > %s", src_name(s), dst_name(d));
    drag_start(e, i, d, y, what);
    return 1;
}

int qm_press(ed_t *e, int i, int x, int y) {
    POINT pt = {x, y};
    if (e->qm.mark >= 0) { e->qm.mark = -1; InvalidateRect(e->wnd, &L.screen, FALSE); }
    if (i >= Z_QM && i < Z_QM + QA_COUNT) {   /* a DEST button: its source, or done */
        pick_source(e, i - Z_QM, x, y, 0);
        return 1;
    }
    if (i >= Z_QM + QA_COUNT && i < Z_QM_END) { page_press(e, i); return 1; }   /* the MOD FROM page */
    if (GetKeyState(VK_MENU) < 0) {   /* Alt-click: the source of the part clicked */
        int a = area_at(pt);
        if (a >= 0) { pick_source(e, a, x, y, 0); return 1; }
    }
    if (e->qm.src) {
        if (i >= 0 && i < NPARAMS) {
            if (connect(e, i, y)) return 1;
            if (e->hit_screen) return 0;   /* (the matrix's own cells, a page's other items: as always) */
            say(e, "%s has no matrix destination: %s can't move it. Pick another knob, or DONE when finished.",
                PARAMS[fp(e, i)].name, src_name(e->qm.src));
            return 1;
        }
        if (i < 0 && area_at(pt) >= 0) { pick_source(e, area_at(pt), x, y, 0); return 1; }   /* another source */
        return 0;
    }
    if (GetKeyState(VK_CONTROL) < 0 && i >= 0 && i < NPARAMS) return turn_existing(e, i, y);
    return 0;
}

void qm_drag(ed_t *e, int x, int y, int fine) {
    (void)x;
    float a = e->qm.from + (e->qm.drag_y - y) * (fine ? 0.2f : 1.0f);
    if (!fine) a = roundf(a);
    for (int c = 0; c < NSYNTHS; c++) {
        int m = e->qm.row[c];
        if (m < 0) continue;
        int k = cell(m, MX_AMT, c);
        float v = amt_norm(k, a);
        if (fabsf(e->fx->getParameter(e->fx, k) - v) > 1e-7f) put(e, k, v);
    }
    repaint_ctl(e, e->qm.ctl);
    InvalidateRect(e->wnd, &L.screen, FALSE);
}

void qm_release(ed_t *e) {
    for (int c = 0; c < NSYNTHS; c++)
        if (e->qm.row[c] >= 0) edit_begin(e, cell(e->qm.row[c], MX_AMT, c), 1);
    int m = e->qm.row[e->focus];
    if (m >= 0 && e->qm.src)
        say(e, "%s > %s: %+.0f%% (row %d). Click the next knob, or DONE when finished.", src_name(opt_at(e, cell(m, MX_SRC, e->focus))),
            dst_name(e->qm.dst), real_at(e, cell(m, MX_AMT, e->focus)), m + 1);
    else if (m >= 0)
        say(e, "%s > %s: %+.0f%% (row %d).", src_name(opt_at(e, cell(m, MX_SRC, e->focus))), dst_name(e->qm.dst),
            real_at(e, cell(m, MX_AMT, e->focus)), m + 1);
    e->mode = M_NONE;
    e->qm.dst = -1;
    repaint_ctl(e, e->qm.ctl);
    e->qm.ctl = -1;
    lib_hold(e, -1);   /* the undo step */
}

/* ---- right-click: a control's connections, or a part's sources ---- */
static void remove_link(ed_t *e, int row) {
    int s = opt_at(e, cell(row, MX_SRC, e->focus)), d = opt_at(e, cell(row, MX_DST, e->focus));
    char what[64];
    snprintf(what, sizeof what, "Remove %s > %s", src_name(s), dst_name(d));
    lib_hold(e, 1);
    lib_touch(e, what);
    row_clear(e, row, e->focus);
    for (int c = 0; c < NSYNTHS; c++) {   /* the same connection in the other Synths in focus */
        if (c == e->focus || !(e->focus_mask >> c & 1u)) continue;
        for (int m = 0; m < NMATRIX; m++)
            if (opt_at(e, cell(m, MX_SRC, c)) == s && opt_at(e, cell(m, MX_DST, c)) == d) { row_clear(e, m, c); break; }
    }
    lib_hold(e, -1);
    say(e, "%s > %s removed (row %d is free again).", src_name(s), dst_name(d), row + 1);
    repaint_all(e);
}
/* ---- (2026-10-30) the MOD FROM page: the picked source's connections in the Synth in focus ---- */
static int src_links(ed_t *e, int c, int s, link_t *out) {   /* Synth c's rows from source s that reach somewhere */
    int n = 0;
    for (int m = 0; m < NMATRIX && n < MAX_LINKS; m++) {
        if (opt_at(e, cell(m, MX_SRC, c)) != s || opt_at(e, cell(m, MX_DST, c)) <= 0) continue;
        link_t l = {m, s, row_on(e, m, c), real_at(e, cell(m, MX_AMT, c))};
        out[n++] = l;
    }
    return n;
}
static int page_index_of(ed_t *e, int row) {   /* where matrix row `row` is in the list (0 if not there) */
    link_t l[MAX_LINKS];
    int n = e->qm.src ? src_links(e, e->focus, e->qm.src, l) : 0;
    for (int k = 0; k < n; k++)
        if (l[k].row == row) return k;
    return 0;
}
int qm_subs(ed_t *e) {
    link_t l[MAX_LINKS];
    int n = e->qm.src ? src_links(e, e->focus, e->qm.src, l) : 0;
    return n > PAGE_LINKS ? (n + PAGE_LINKS - 1) / PAGE_LINKS : 1;
}
static RECT page_rect(ed_t *e, int z) {   /* where button z is (empty: not shown now) */
    RECT r = {0, 0, 0, 0};
    if (z >= Z_QP_ROW && z < Z_QP_ROW + PAGE_LINKS * QP_PER) {
        int line = (z - Z_QP_ROW) / QP_PER, k = (z - Z_QP_ROW) % QP_PER;
        link_t l[MAX_LINKS];
        int n = e->qm.src ? src_links(e, e->focus, e->qm.src, l) : 0;
        if (e->sub * PAGE_LINKS + line >= n) return r;
        static const short X[QP_PER + 1] = {-150, -128, -106, -54, 0};   /* from the row's right: - + REMOVE MATRIX */
        RECT row = L.row[line];
        SetRect(&r, row.right + sc(X[k]), row.top + sc(3), row.right + sc(X[k + 1]) - sc(4), row.bottom - sc(3));
    } else if (z >= Z_QP_PICK && z <= Z_QP_DONE) {
        int w = (rw(&L.row[ROWS - 1]) - 2 * sc(8)) / 3, x = L.row[ROWS - 1].left + (z - Z_QP_PICK) * (w + sc(8));
        SetRect(&r, x, L.row[ROWS - 1].top + sc(2), x + w, L.row[ROWS - 1].bottom - sc(2));
    }
    return r;
}
static int page_hit(ed_t *e, POINT pt) {
    for (int z = Z_QP_ROW; z < Z_QP_ROW + PAGE_LINKS * QP_PER; z++) {
        RECT r = page_rect(e, z);
        if (PtInRect(&r, pt)) return z;
    }
    for (int z = Z_QP_PICK; z <= Z_QP_DONE; z++) {
        RECT r = page_rect(e, z);
        if (PtInRect(&r, pt)) return z;
    }
    return -1;
}
static void amount_step(ed_t *e, int row, int d) {   /* a connection's amount by d % (and the same in Synths edited with it) */
    int s = opt_at(e, cell(row, MX_SRC, e->focus)), dst = opt_at(e, cell(row, MX_DST, e->focus));
    float a = real_at(e, cell(row, MX_AMT, e->focus)) + d;
    char what[64];
    snprintf(what, sizeof what, "Mod amount %s > %s", src_name(s), dst_name(dst));
    lib_hold(e, 1);   /* one undo step */
    lib_touch(e, what);
    for (int c = 0; c < NSYNTHS; c++) {
        if (c != e->focus && !(e->focus_mask >> c & 1u)) continue;
        for (int m = 0; m < NMATRIX; m++)
            if (c == e->focus ? m == row : opt_at(e, cell(m, MX_SRC, c)) == s && opt_at(e, cell(m, MX_DST, c)) == dst) {
                int k = cell(m, MX_AMT, c);
                put_once(e, k, amt_norm(k, a));
                break;
            }
    }
    lib_hold(e, -1);
    say(e, "%s > %s: %+.0f%% (row %d).", src_name(s), dst_name(dst), real_at(e, cell(row, MX_AMT, e->focus)), row + 1);
    repaint_all(e);
}
static void open_matrix(ed_t *e, int row) {   /* quick assign done; the MATRIX page at that row (marked), or row 1 */
    e->qm.src = 0;
    e->qm.mark = row;
    ed_show(e, SCR_MATRIX, row >= 0 ? mx_sub_of(e, row) : 0);
    repaint_all(e);
}
static void pick_any(ed_t *e) {   /* CHANGE SOURCE: every source of the matrix, in columns */
    HMENU m = CreatePopupMenu();
    for (int s = 1; s < nsources(); s++)
        AppendMenuA(m, MF_STRING | (s == e->qm.src ? MF_CHECKED : 0) | (s > 1 && (s - 1) % 18 == 0 ? MF_MENUBARBREAK : 0),
                    (UINT_PTR)s, src_name(s));
    RECT b = page_rect(e, Z_QP_PICK);
    int c = menu(e, m, b.left, b.bottom);
    if (c > 0 && c < nsources()) arm(e, c);
}
static void page_press(ed_t *e, int z) {
    if (z == Z_QP_PICK) { pick_any(e); return; }
    if (z == Z_QP_ALL) {   /* (2026-10-30) EDIT MODULATIONS: the MATRIX page filtered by this source (matrix_page.c) */
        int s = e->qm.src;
        e->qm.src = 0;
        e->qm.mark = -1;
        mx_edit_mods(e, s);
        repaint_all(e);
        return;
    }
    if (z == Z_QP_DONE) {
        char t[48];
        snprintf(t, sizeof t, "%s", src_name(e->qm.src));
        arm(e, 0);
        say(e, "Quick assign from %s done.", t);
        return;
    }
    if (z < Z_QP_ROW || z >= Z_QP_ROW + PAGE_LINKS * QP_PER || !e->qm.src) return;
    link_t l[MAX_LINKS];
    int n = src_links(e, e->focus, e->qm.src, l), k = e->sub * PAGE_LINKS + (z - Z_QP_ROW) / QP_PER;
    if (k >= n) return;
    switch ((z - Z_QP_ROW) % QP_PER) {
    case QP_MINUS: amount_step(e, l[k].row, -AMT_STEP); break;
    case QP_PLUS: amount_step(e, l[k].row, AMT_STEP); break;
    case QP_REMOVE: remove_link(e, l[k].row); if (e->sub >= qm_subs(e)) e->sub = qm_subs(e) - 1; break;
    case QP_MATRIX: open_matrix(e, l[k].row); break;
    }
}
static const char *page_help(ed_t *e, int z) {
    (void)e;
    if (z == Z_QP_PICK) return "CHANGE SOURCE: a list of every modulation source (LFOs, envelopes, noise, the wheels, the pad, "
                               "the ribbon, the keyboard, the pedals, the generators): the next knobs you click connect to it.";
    if (z == Z_QP_ALL) return "EDIT MODULATIONS: the MATRIX page with only this source's rows, where each one's amount, VIA "
                              "and XFORM can be changed (quick assign ends; CLEAR there shows every row).";
    if (z == Z_QP_DONE) return "DONE: quick assign ends; the screen goes back to the page it showed (as Esc, or the DEST "
                               "button again).";
    switch ((z - Z_QP_ROW) % QP_PER) {
    case QP_MINUS: return "-: this connection's amount 5% lower (below 0 the source works the other way round).";
    case QP_PLUS: return "+: this connection's amount 5% higher. (Dragging its knob while the source is picked sets it too.)";
    case QP_REMOVE: return "REMOVE: this connection goes (its matrix row is free again; in the Synths edited together too).";
    case QP_MATRIX: return "MATRIX: this connection's row on the MATRIX page, marked (VIA, XFORM and the rest are there).";
    }
    return "";
}
static void ptext(HDC dc, RECT r, const char *s, HFONT f, COLORREF c, UINT fmt) {
    if (skin_text(&r, s, f, c, fmt)) return;
    SelectObject(dc, f);
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    DrawTextA(dc, s, -1, &r, fmt | DT_NOPREFIX);
}
static void pbutton(HDC dc, RECT r, const char *label, int hot) {   /* as the screen's other buttons */
    gfx_rect((float)r.left, (float)r.top, (float)rw(&r), (float)rh(&r), 0xFF0F2230);
    gfx_frame((float)r.left, (float)r.top, (float)rw(&r), (float)rh(&r), 1.0f, hot ? 0xFFFFFFFF : 0xFF3E6273);
    ptext(dc, r, label, font_cell, hot ? RGB(255, 255, 255) : RGB(150, 215, 232), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}
void qm_page_title(ed_t *e, char *out, int n) { snprintf(out, n, "MOD FROM %s:  click knobs to connect", src_name(e->qm.src)); }
void qm_page_draw(HDC dc, ed_t *e) {   /* under the title line (editor_draw.c draws it) */
    gfx_begin(dc);
    skin_bind(gfx_g);
    char who[24], t[160];
    focus_names(e, who, sizeof who);
    snprintf(t, sizeof t, "Click a knob, drag up / down for the amount. %s's connections:", who);
    ptext(dc, L.head, t, font_tiny, RGB(90, 130, 145), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    link_t l[MAX_LINKS];
    int n = e->qm.src ? src_links(e, e->focus, e->qm.src, l) : 0;
    if (!n) {
        RECT r = {L.row[0].left, L.row[0].top, L.row[0].right, L.row[1].bottom};
        ptext(dc, r, "Nothing connected yet: click a knob on the panel (a ringed one is modulated already).", font_cell,
              RGB(150, 215, 232), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    for (int line = 0; line < PAGE_LINKS && e->sub * PAGE_LINKS + line < n; line++) {
        const link_t *k = &l[e->sub * PAGE_LINKS + line];
        RECT row = L.row[line], mi = page_rect(e, Z_QP_ROW + line * QP_PER);
        int live = e->mode == M_QMOD && e->qm.row[e->focus] == k->row;   /* (its knob being dragged now) */
        COLORREF c = k->on ? RGB(150, 215, 232) : RGB(90, 130, 145);
        char a[24], d[96];
        snprintf(d, sizeof d, "%d  %s%s", k->row + 1, dst_name(opt_at(e, cell(k->row, MX_DST, e->focus))), k->on ? "" : " (off)");
        snprintf(a, sizeof a, "%+.0f%%", k->amt);
        RECT dr = {row.left + sc(2), row.top, mi.left - sc(48), row.bottom}, ar = {mi.left - sc(46), row.top, mi.left - sc(6), row.bottom};
        ptext(dc, dr, d, font_cell, live ? RGB(255, 255, 255) : c, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        ptext(dc, ar, a, font_cell, live ? RGB(255, 255, 255) : c, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        static const char *const B[QP_PER] = {"-", "+", "REMOVE", "MATRIX"};
        for (int b = 0; b < QP_PER; b++) {
            int z = Z_QP_ROW + line * QP_PER + b;
            pbutton(dc, page_rect(e, z), B[b], e->hover == z);
        }
    }
    static const char *const NB[3] = {"CHANGE SOURCE", "EDIT MODULATIONS", "DONE"};
    for (int b = 0; b < 3; b++) pbutton(dc, page_rect(e, Z_QP_PICK + b), NB[b], e->hover == Z_QP_PICK + b);
    gfx_end();
    skin_bind(NULL);
}

int qm_right(ed_t *e, int i, int x, int y) {
    POINT pt = {x, y};
    if (i >= 0 && i < NPARAMS) {
        int d = dst_of(base_of(i));
        link_t l[MAX_LINKS];
        int n = d < 0 ? 0 : links(e, e->focus, d, l);
        if (!n) return 0;
        HMENU m = CreatePopupMenu();
        char t[96];
        for (int k = 0; k < n; k++) {
            if (k) AppendMenuA(m, MF_SEPARATOR, 0, NULL);
            snprintf(t, sizeof t, "%s > %s  %+.0f%%  (row %d%s)", l[k].src ? src_name(l[k].src) : "(via only)", dst_name(d),
                     l[k].amt, l[k].row + 1, l[k].on ? "" : ", off");
            AppendMenuA(m, MF_STRING | MF_GRAYED, 0, t);
            AppendMenuA(m, MF_STRING, (UINT_PTR)(100 + l[k].row), "    Remove");
            AppendMenuA(m, MF_STRING, (UINT_PTR)(200 + l[k].row), "    Open in MATRIX");
        }
        if (PARAMS[i].nopts > 1) {
            AppendMenuA(m, MF_SEPARATOR, 0, NULL);
            AppendMenuA(m, MF_STRING, 300, "Choose a value...");
        }
        int c = menu(e, m, x, y);
        if (c >= 100 && c < 100 + NMATRIX) remove_link(e, c - 100);
        else if (c >= 200 && c < 200 + NMATRIX) {   /* the MATRIX page at that row, marked */
            e->screen = SCR_MATRIX;
            e->sub = mx_sub_of(e, c - 200);
            e->qm.mark = c - 200;
            repaint_all(e);
        } else if (c == 300) return 2;
        return 1;
    }
    if (i >= Z_QM + QA_COUNT && i < Z_QM_END) return 1;   /* (the MOD FROM page's buttons: a plain click) */
    int a = i >= Z_QM && i < Z_QM_END ? i - Z_QM : area_at(pt);
    if (a < 0) return 0;
    pick_source(e, a, x, y, 1);
    return 1;
}

int qm_key(ed_t *e, int vk) {
    if (vk != VK_ESCAPE || !e->qm.src || e->mode == M_QMOD) return 0;
    char t[48];
    snprintf(t, sizeof t, "%s", src_name(e->qm.src));
    arm(e, 0);
    say(e, "Quick assign from %s done.", t);
    return 1;
}
void qm_tick(ed_t *e) {
    if (e->qm.msg[0] && GetTickCount() - e->qm.msg_at > MSG_MS && e->mode != M_QMOD) {
        e->qm.msg[0] = 0;
        if (e->wnd) InvalidateRect(e->wnd, &L.screen, FALSE);
    }
}

/* ---- what the help line says ---- */
int qm_help(ed_t *e, char *out, int n) {
    if (e->mode == M_QMOD && e->qm.row[e->focus] >= 0) {
        int m = e->qm.row[e->focus], k = 0;
        char rows[64] = "";
        for (int c = 0; c < NSYNTHS; c++)
            if (e->qm.row[c] >= 0 && c != e->focus) k += snprintf(rows + k, sizeof rows - k, "; Synth %d row %d", c + 1, e->qm.row[c] + 1);
        snprintf(out, n, "%s > %s:  %+.1f%%   (Synth %d row %d%s)\nDrag up for a positive amount, down for a negative "
                 "one (Shift: fine). The knob's own setting stays as it is.",
                 src_name(opt_at(e, cell(m, MX_SRC, e->focus))), dst_name(e->qm.dst), real_at(e, cell(m, MX_AMT, e->focus)),
                 e->focus + 1, m + 1, rows);
        return 1;
    }
    int recent = e->qm.msg[0] && GetTickCount() - e->qm.msg_at <= MSG_MS;
    if (e->qm.src) {
        int h = e->hover >= 0 && e->hover < NPARAMS ? e->hover : -1, d = h >= 0 ? dst_of(base_of(h)) : -1;
        char line[160];
        if (h >= 0 && d >= 0) snprintf(line, sizeof line, "Click %s to connect it to %s, and drag for the amount.", dst_name(d), src_name(e->qm.src));
        else if (h >= 0) snprintf(line, sizeof line, "%s has no matrix destination.", PARAMS[fp(e, h)].name);
        else snprintf(line, sizeof line, "Click a knob for %s to move, drag it up or down for the amount.", src_name(e->qm.src));
        if (e->hover >= Z_QM + QA_COUNT && e->hover < Z_QM_END) {   /* a button of the MOD FROM page: what it does */
            snprintf(out, n, "%s%s%s", recent ? e->qm.msg : "", recent ? "\n" : "", page_help(e, e->hover));
            return 1;
        }
        snprintf(out, n, "QUICK ASSIGN from %s: %s\n%s", src_name(e->qm.src), recent ? e->qm.msg : line,
                 recent ? line : "Connect as many as you like (drag a knob it moves to change its amount); DONE when finished.");
        return 1;
    }
    if (recent && (e->hover < 0 || e->hover >= NPARAMS || e->hover == e->qm.ctl)) {   /* (a control's own help first) */
        snprintf(out, n, "%s", e->qm.msg);
        return 1;
    }
    return 0;
}
void qm_links_text(ed_t *e, int i, char *out, int n) {
    int d = dst_of(base_of(i)), len = (int)strlen(out);
    link_t l[MAX_LINKS];
    int k = d < 0 ? 0 : links(e, e->focus, d, l);
    if (!k || len >= n - 1) return;
    int w = snprintf(out + len, n - len, "   [modulated by");
    for (int j = 0; j < k && len + w < n; j++)
        w += snprintf(out + len + w, n - len - w, "%s %s %+.0f%%%s", j ? "," : "", l[j].src ? src_name(l[j].src) : "via",
                      l[j].amt, l[j].on ? "" : " (off)");
    if (len + w < n) snprintf(out + len + w, n - len - w, "; to change or remove: that source's DEST, then the list on the "
                              "screen, or the MATRIX page; shortcuts: Ctrl-drag, right-click]");
}
const char *qm_zone_help(ed_t *e, int z) {
    static char t[300];
    int a = z - Z_QM, s[32];
    if (a < 0 || a >= QA_COUNT || !area_sources(a, s)) return NULL;
    if (a == QA_HANDS)
        snprintf(t, sizeof t, "DEST: modulate from what you play: the pitch and mod wheels, the X/Y pad, the ribbon, the "
                 "keyboard (velocity, aftertouch, the key) and the pedals. Click it for the list, then click each knob it "
                 "should move and drag up or down for the amount.");
    else if (a == QA_GEN)
        snprintf(t, sizeof t, "DEST: modulate from the generators (the rain, the balls, the particles, Turing, Euclid, Life, "
                 "the orbits, the pendulums, the Markov melody). Click it for the list, then click knobs and drag.");
    else
        snprintf(t, sizeof t, "DEST: modulate from %s. Click it, then click each knob %s should move and drag it up or "
                 "down for the amount; the screen lists the connections (- / +, REMOVE). DEST again or DONE: finished.",
                 src_name(s[0]), src_name(s[0]));
    (void)e;
    return t;
}
int qm_value_text(ed_t *e, int i, char *out, int n) {
    if (e->mode != M_QMOD || i != e->qm.ctl || e->qm.row[e->focus] < 0) return 0;
    snprintf(out, n, "mod %+.0f%%", real_at(e, cell(e->qm.row[e->focus], MX_AMT, e->focus)));
    return 1;
}
int qm_marked(ed_t *e) { return e->screen == SCR_MATRIX ? e->qm.mark : -1; }

void qm_matrix_changed(ed_t *e, int i) {   /* a matrix param moved: the controls with rings now or before repaint */
    tables();
    if (i < 0 || i >= NPARAMS || !IS_MATRIX[i] || !e->wnd) return;
    int ds[MOD_SLOTS], n = 0;
    for (int m = 0; m < NMATRIX && n < MOD_SLOTS; m++) ds[n++] = opt_at(e, cell(m, MX_DST, e->focus));
    for (int c = 0; c < NCTLS; c++) {
        int k = CTLS[c].param, d = DST_OF[k], hit = e->qm.drawn[k];
        for (int j = 0; j < n && !hit && d > 0; j++) hit = ds[j] == d;
        if (hit) repaint_ctl(e, k);
    }
}

/* ---- drawing: over the panel's controls ---- */
static DWORD family(int s) {   /* the colour of a source's family */
    switch (s) {
    case MS_FENV: case MS_AENV: case MS_MENV: case MS_NOISE_EG: return 0xFFF2AA4C;   /* envelopes: amber */
    case MS_LFO1: case MS_LFO2: case MS_LFO3: case MS_LFO4: return 0xFF5EC4F2;      /* LFOs: blue */
    case MS_RAND: case MS_RAND_UNI: return 0xFFBC94F2;                              /* random: violet */
    case MS_CONST: return 0xFFD4D4D8;
    }
    return is_gen_src(s) ? 0xFFF28CBE : 0xFF86D886;   /* the generators: pink; the hands' controllers: green */
}
static int swings_both(ed_t *e, int s) {   /* a source that moves both ways from the knob's setting */
    if (s == MS_LFO1 || s == MS_LFO2 || s == MS_LFO3 || s == MS_LFO4) {
        char k[24];
        snprintf(k, sizeof k, "lfo%d_polarity", s == MS_LFO1 ? 1 : s == MS_LFO2 ? 2 : s == MS_LFO3 ? 3 : 4);
        int p = key_of(k);
        return p < 0 || opt_at(e, synth_param(p, e->focus)) == 0;
    }
    return s == MS_KEY || s == MS_RAND || s == MS_BEND || s == MS_RIBBON;
}
static void label(HDC dc, const RECT *r, const char *s, COLORREF c) {
    if (skin_text(r, s, font_tiny, c, DT_CENTER | DT_VCENTER | DT_SINGLELINE)) return;
    SelectObject(dc, font_tiny);
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    RECT rr = *r;
    DrawTextA(dc, s, -1, &rr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}
static int seen(const RECT *r, int pad, const RECT *clip) {
    RECT a = *r, o;
    InflateRect(&a, pad, pad);
    return IntersectRect(&o, &a, clip);
}
static void rings(ed_t *e, int i, const link_t *l, int n) {
    float cx, cy, r;
    knob_geom(i, &cx, &cy, &r, NULL);
    float v = e->fx->getParameter(e->fx, fp(e, i)), a = -135.0f + 270.0f * v, s = L.s;
    float base = r + (r > 10.0f * s ? 7.5f : 4.0f) * s;   /* (just outside the printed scale) */
    for (int k = 0; k < n && k < 3; k++) {
        float rr = base + k * 2.4f * s, sweep = l[k].amt * 2.7f, to = a + sweep;
        DWORD c = family(l[k].src), dim = (c & 0x00FFFFFF) | (l[k].on ? 0x70000000u : 0x38000000u);
        if (!l[k].on) c = (c & 0x00FFFFFF) | 0x60000000u;
        to = to < -135.0f ? -135.0f : to > 135.0f ? 135.0f : to;
        if (swings_both(e, l[k].src)) {
            float back = a - sweep;
            back = back < -135.0f ? -135.0f : back > 135.0f ? 135.0f : back;
            gfx_arc(cx, cy, rr, back, a, 1.6f * s, dim);
        }
        gfx_arc(cx, cy, rr, a, to, 1.6f * s, c);
        gpt_t p = gfx_polar(cx, cy, rr, fabsf(to - a) < 0.5f ? a : to);
        gfx_disc(p.x, p.y, 1.5f * s, c);   /* its end (just a dot at 0%) */
    }
    if (n > 3) {
        gpt_t p = gfx_polar(cx, cy, base + 3 * 2.4f * s, 135.0f);
        gfx_disc(p.x, p.y, 1.4f * s, 0xC0FFFFFF);   /* more than three */
    }
}
void qm_draw(HDC dc, ed_t *e, const RECT *clip) {
    tables();
    gfx_begin(dc);
    skin_bind(gfx_g);
    for (int c = 0; c < NCTLS; c++) {   /* the rings and dots */
        int i = CTLS[c].param, d = DST_OF[i] > 0 ? dst_of(i) : -1;
        if (d < 0 || !seen(&L.ctl[i], sc(16), clip)) continue;
        link_t l[MAX_LINKS];
        int n = links(e, e->focus, d, l);
        e->qm.drawn[i] = n > 0;
        if (!n) continue;
        if (L.kind[i] == K_KNOB || L.kind[i] == K_SMALL || L.kind[i] == K_BIG) rings(e, i, l, n);
        else {   /* a switch, an LED list, a rotary switch: a dot at its top right per source family */
            for (int k = 0; k < n && k < 3; k++)
                gfx_disc((float)L.ctl[i].right - (3 + 5 * k) * L.s, (float)L.ctl[i].top + 3 * L.s, 1.8f * L.s, family(l[k].src));
        }
    }
    int armed = e->qm.src ? area_of_source(e->qm.src) : -1;
    for (int a = 0; a < QA_COUNT; a++) {   /* the DEST buttons */
        RECT b;
        int s[32];
        if (!dest_rect(a, &b) || !seen(&b, sc(2), clip) || !area_sources(a, s)) continue;
        int on = armed == a || (a == QA_HANDS && armed >= QA_PITCH && armed <= QA_KEYS), hot = e->hover == Z_QM + a;
        gfx_rect((float)b.left, (float)b.top, (float)rw(&b), (float)rh(&b), on ? 0xFFF2AA4C : hot ? 0xFF4A4A50 : 0xFF2A2A2E);
        gfx_frame((float)b.left, (float)b.top, (float)rw(&b), (float)rh(&b), 1.0f, on ? 0xFFFFE0A8 : 0xFF0C0C0E);
        label(dc, &b, "DEST", on ? RGB(30, 20, 8) : hot ? RGB(255, 255, 255) : RGB(214, 214, 216));
    }
    RECT r;
    if (armed >= 0 && area_rect(armed, &r) && seen(&r, sc(8), clip)) {   /* the source picked: its part lit */
        InflateRect(&r, sc(1), sc(1));
        gfx_frame((float)r.left - 2, (float)r.top - 2, (float)rw(&r) + 4, (float)rh(&r) + 4, 5.0f * L.s, 0x30F2AA4C);
        gfx_frame((float)r.left, (float)r.top, (float)rw(&r), (float)rh(&r), 1.6f * L.s, 0xE0F2AA4C);
    }
    gfx_end();
    skin_bind(NULL);
}

/* ---- hit codes and load_test ---- */
int qm_hit(ed_t *e, POINT pt) {
    for (int a = 0; a < QA_COUNT; a++) {
        RECT b;
        if (dest_rect(a, &b) && PtInRect(&b, pt)) return Z_QM + a;
    }
    return e->screen == SCR_QMOD && e->qm.src ? page_hit(e, pt) : -1;
}
intptr_t qm_test(ed_t *e, intptr_t op, char *buf, float opt) {
    switch (op) {
    case QMT_SOURCE: snprintf(buf, 64, "%s", e->qm.src ? src_name(e->qm.src) : ""); return e->qm.src;
    case QMT_ANSWER: e->qm.answer = (int)lroundf(opt); return 1;
    case QMT_DRAWN: { int i = (int)lroundf(opt); return i >= 0 && i < NPARAMS ? e->qm.drawn[i] : -1; }
    case QMT_DEST_XY: {   /* part opt's DEST button, or (opt >= 100) part opt-100's middle, in pixels */
        int a = (int)lroundf(opt);
        RECT b;
        if (a >= 100 ? !area_rect(a - 100, &b) : !dest_rect(a, &b)) return -1;
        return (b.left + b.right) / 2 | ((b.top + b.bottom) / 2) << 16;
    }
    case QMT_HELP: {   /* quick assign's help line now; else what modulates the control under the mouse */
        buf[0] = 0;
        if (!qm_help(e, buf, 1024) && e->hover >= 0 && e->hover < NPARAMS) qm_links_text(e, e->hover, buf, 1024);
        return (intptr_t)strlen(buf);
    }
    case QMT_DST_OF: return dst_of((int)lroundf(opt));
    case QMT_PAGE_XY: {   /* the MOD FROM page's button Z_QM + opt, in pixels (-1: not shown) */
        RECT b = page_rect(e, Z_QM + (int)lroundf(opt));
        if (IsRectEmpty(&b)) return -1;
        return (b.left + b.right) / 2 | ((b.top + b.bottom) / 2) << 16;
    }
    case QMT_LINKS: { link_t l[MAX_LINKS]; return e->qm.src ? src_links(e, e->focus, e->qm.src, l) : -1; }
    }
    return 0;
}

/* ---- (2026-10-30) for the CABLES view (cables.c): the same names, colours, destinations and remove ---- */
const char *qm_src_name(int s) { return src_name(s); }
const char *qm_dst_name(int d) { return dst_name(d); }
int qm_dst_of(int i) { return dst_of(i); }
unsigned qm_family(int s) { return family(s); }
int qm_is_gen_src(int s) { return is_gen_src(s); }
void qm_remove_row(ed_t *e, int row) { remove_link(e, row); }
void qm_pick(ed_t *e, int s) {   /* a source's jack clicked: quick assign from s (the same again: done) */
    if (s <= 0 || s >= nsources()) return;
    if (e->qm.src == s) { arm(e, 0); say(e, "Quick assign from %s done.", src_name(s)); return; }
    arm(e, s);
}
