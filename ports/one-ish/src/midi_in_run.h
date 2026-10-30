/* The instance's MIDI input settings at work (types: src/midi_in.h). Included by src/synths.h after the instance
 * (inst_t) and before midi(). The window sends them as "midicfg\n<key>=<value>;..." (any subset):
 *     rx_clock=0|1   RECEIVE MIDI CLOCK: off, the Synths ignore the host's tempo and song position and run at tempo=
 *     tempo=<bpm>    10-280
 *     sustain=<cc>, exp1=<cc>, exp2=<cc>   the pedals' CC numbers (0-119): the sustain pedal, the matrix sources
 *                    Exp 1 and Exp 2. A pedal moved to another number no longer answers to its old one.
 *     map=<cc>:<key>:<7|14>,...   MIDI LEARN: the whole CC map (replaces the one before; "map=" clears it). A CC sets
 *                    the param <key> (any Synth's, "s2_flt_cut"), 7-bit (0-127 over its range) or 14-bit (this CC the
 *                    MSB, CC + 32 the LSB). CCs the engine reads for other things (mi_fixed, the pedals) are refused.
 * NRPN input after the manual's table (below) always works, on any channel. A param set from MIDI lands as the host's
 * automation would (set_param) and the host is told (mpc_engine_changed). Nothing here allocates or touches a file:
 * midi() works on the fixed tables only. */

static void synth_bpm(one_t *s, float bpm);
static void set_param(void *inst, const char *key, const char *val);

static void mi_init(midi_in_t *m) {
    m->rx_clock = 1;
    m->tempo = 120.0f;
    m->host_bpm = 0.0f;
    for (int p = 0; p < PEDALS; p++) m->pedal[p] = PEDAL_ROLE[p];
    for (int c = 0; c < 128; c++) m->map[c] = -1;
    for (int ch = 0; ch < 16; ch++) m->nrpn[ch] = -1;
}

/* the params.json index of a key ("flt_cut", "s2_flt_cut"), or -1 */
static int mi_pidx(const inst_t *in, const char *key) {
    const pkey_t *e = key_find(in, key);
    return e && e->slot >= 0 ? in->syn[(int)e->syn]->pidx[e->slot] : -1;
}

/* "map=..." */
static void mi_map(inst_t *in, char *list) {
    midi_in_t *m = &in->mi;
    for (int c = 0; c < 128; c++) { m->map[c] = -1; m->map14[c] = 0; }
    char *save = NULL;
    for (char *t = strtok_r(list, ",", &save); t; t = strtok_r(NULL, ",", &save)) {
        char *k = strchr(t, ':'), *b = k ? strchr(k + 1, ':') : NULL;
        if (!b) continue;
        *k++ = 0;
        *b++ = 0;
        char *end;
        long cc = strtol(t, &end, 10), bits = atol(b);
        int p = mi_pidx(in, k);
        if (end == t || mi_fixed((int)cc) || p < 0 || PARAMS[p].string_display || PARAMS[p].nopts == 1) continue;
        int taken = m->map[cc] >= 0 || (cc >= 32 && cc < 64 && m->map14[cc - 32]);
        for (int q = 0; q < PEDALS; q++) taken |= cc == m->pedal[q];
        int wide = bits == 14 && cc < 32 && !mi_fixed((int)cc + 32) && m->map[cc + 32] < 0;
        for (int q = 0; q < PEDALS && wide; q++) wide = cc + 32 != m->pedal[q];
        if (taken) continue;
        m->map[cc] = (short)p;
        m->map14[cc] = (uint8_t)wide;
    }
}

/* the map as text, CC by CC (get_param "midimap", for the tests) */
static int mi_map_text(const inst_t *in, char *buf, int n) {
    int used = 0;
    buf[0] = 0;
    for (int c = 0; c < 128; c++) {
        if (in->mi.map[c] < 0) continue;
        used += snprintf(used < n ? buf + used : NULL, used < n ? (size_t)(n - used) : 0, "%s%d:%s:%d", used ? "," : "",
                         c, PARAMS[in->mi.map[c]].key, in->mi.map14[c] ? 14 : 7);
    }
    return used < n ? used : n - 1;
}

/* param p (params.json index) to v in its own units, as the host's automation would set it; the host is told */
static void mi_put(inst_t *in, int p, double v) {
    const param_t *q = &PARAMS[p];
    if (q->nopts > 1) v = round(v < 0 ? 0 : v > q->nopts - 1 ? q->nopts - 1 : v);
    else if (q->int_display) v = round(v);
    char b[32];
    snprintf(b, sizeof b, "%.9g", v);
    set_param(in, q->key, b);
    in->mi.chg[p >> 6] |= 1ull << (p & 63);
}
static void mi_put_norm(inst_t *in, int p, double norm) {   /* 0..1 across its range (or its options) */
    const param_t *q = &PARAMS[p];
    mi_put(in, p, q->nopts > 1 ? norm * (q->nopts - 1) : q->min + norm * (q->max - q->min));
}

/* ---- NRPN input (manual p149-158): every parameter in the table, for each Synth ----
 * A controller selects a number with CC 99 (MSB) and CC 98 (LSB), then sends the value with CC 6 (MSB) and CC 38 (LSB):
 * 14 bits, 0-16383, set on each (the MSB alone, then refined by the LSB). Synth 2's numbers are Synth 1's + 3840,
 * Synth 3's + 7680; MASTER VOLUME (1) is the shared master level. An RPN (CC 101 / 100) deselects. Options: the
 * manual's bands (TRI 0-4095, PULSE 4096-8191 ...), onto ONE-ish's own lists. */
enum { NK_CONT,      /* continuous over the param's whole range */
       NK_OPT,       /* the manual's n bands, onto the options listed */
       NK_LFO_SYNC,  /* RATE SYNC'D: 13 values, 64 .. 1/64 beats, onto the rate knob (only while that LFO is synced) */
       NK_ENV_SYNC,  /* an envelope's synced time: 13 values, 1/64 .. 64 beats, the nearest ONE-ish division */
       NK_MONO };    /* POLYPHONY MONO: on = 1 voice; off = the default voices again (when it was mono) */
typedef struct {
    short num;               /* Synth 1's NRPN */
    const char *key;         /* Synth 1's key (Synths 2 and 3: "s2_" / "s3_" in front) */
    signed char kind, n;     /* NK_; NK_OPT: how many bands */
    signed char opt[5];      /* NK_OPT: the option each band picks */
    const char *only;        /* set only while this switch (the LFO's or envelope's SYNC) is ... */
    signed char only_on;     /* ... on (1) or off (0) */
} nrpn_t;
#define OPTS(...) {__VA_ARGS__}
#define C(num, key) {num, key, NK_CONT, 0, {0}, NULL, 0}
#define SW(num, key) {num, key, NK_OPT, 2, OPTS(0, 1), NULL, 0}
static const nrpn_t NRPN[] = {
    C(1, "master_level"),
    /* LFOs: waveform TRI / PULSE / SAW / S&H; rate free (while not synced) and synced */
    {451, "lfo1_wave", NK_OPT, 4, OPTS(0, 2, 3, 5), NULL, 0},
    {463, "lfo1_rate", NK_CONT, 0, {0}, "lfo1_sync", 0},
    {464, "lfo1_rate", NK_LFO_SYNC, 0, {0}, "lfo1_sync", 1},
    {501, "lfo2_wave", NK_OPT, 4, OPTS(0, 2, 3, 5), NULL, 0},
    {513, "lfo2_rate", NK_CONT, 0, {0}, "lfo2_sync", 0},
    {514, "lfo2_rate", NK_LFO_SYNC, 0, {0}, "lfo2_sync", 1},
    {551, "lfo3_wave", NK_OPT, 4, OPTS(0, 2, 3, 5), NULL, 0},
    {563, "lfo3_rate", NK_CONT, 0, {0}, "lfo3_sync", 0},
    {564, "lfo3_rate", NK_LFO_SYNC, 0, {0}, "lfo3_sync", 1},
    {601, "lfo4_wave", NK_OPT, 4, OPTS(0, 2, 3, 5), NULL, 0},
    {613, "lfo4_rate", NK_CONT, 0, {0}, "lfo4_sync", 0},
    {614, "lfo4_rate", NK_LFO_SYNC, 0, {0}, "lfo4_sync", 1},
    /* oscillators: octave 32'-2', frequency, beat, SAW and TRIANGLE (both ONE-ish's one wave angle), pulse width, mix,
     * wave TRI / SAW, hard sync */
#define OSC_NRPN(f, o) {f + 2, o "_oct", NK_OPT, 5, OPTS(0, 1, 2, 3, 4), NULL, 0}, C(f + 1, o "_freq"), C(f + 6, o "_beat"), \
    C(f + 8, o "_angle"), C(f + 7, o "_angle"), {f, o "_wave", NK_OPT, 2, OPTS(0, 1), NULL, 0}
    OSC_NRPN(150, "o1"), C(300, "o1_pw"), C(900, "o1_mix"),
    OSC_NRPN(200, "o2"), C(350, "o2_pw"), C(904, "o2_mix"), SW(217, "o2_sync"),
    OSC_NRPN(250, "o3"), C(400, "o3_pw"), C(908, "o3_mix"), SW(267, "o3_sync"),
#undef OSC_NRPN
    /* pitch modulation, ring mod, wave modulation (LFO 3), FM: route 1-2 / 1-3 / 3-1 */
    C(850, "pitch_lfo"), C(851, "pitch_env"), SW(52, "ring_src"), C(852, "angle_lfo3"), C(853, "pw_lfo3"),
    C(66, "fm_amt"), {65, "fm_route", NK_OPT, 3, OPTS(0, 1, 2), NULL, 0},
    /* noise: its envelope's attack, release and sustain; colour mix; the colour pair */
    C(802, "noise_a"), C(805, "noise_r"), SW(804, "noise_sus"), C(801, "noise_color_mix"),
    {800, "noise_color_pair", NK_OPT, 3, OPTS(0, 1, 2), NULL, 0},
    /* polyphony: unison detune, mono, unison */
    C(1211, "uni_detune"), {1203, "voices", NK_MONO, 0, {0}, NULL, 0}, SW(1209, "unison"),
    /* mixer: each source's level and its SVF and ladder buttons */
#define MIX_NRPN(b, s) C(b, s "_level"), SW(b + 1, s "_svf"), SW(b + 2, s "_lad")
    MIX_NRPN(901, "o1"), MIX_NRPN(905, "o2"), MIX_NRPN(909, "o3"), MIX_NRPN(912, "ring"), MIX_NRPN(915, "noise"),
#undef MIX_NRPN
    /* filters: SVF mode LP / HP / BP / NOTCH, route SER / PAR, ladder LP / HP, slope 6 / 12 / 18 / 24 dB */
    C(950, "svf_cut"), C(952, "svf_res"), C(1150, "flt_cut"), C(1152, "flt_res"), C(1103, "flt_mix"),
    {958, "svf_mode", NK_OPT, 4, OPTS(0, 2, 1, 3), NULL, 0}, {1102, "flt_route", NK_OPT, 2, OPTS(1, 0), NULL, 0},
    SW(1158, "flt_mode"), {1159, "flt_slope", NK_OPT, 4, OPTS(3, 2, 1, 0), NULL, 0},
    /* cutoff modulation */
    C(955, "svf_lfo"), C(954, "svf_env"), C(956, "svf_fm"), C(1155, "lad_lfo"), C(1154, "lad_env"), C(1156, "lad_fm"),
    /* the three envelopes: attack, decay, release free (while not synced) and synced; sustain; multi trig, sync, loop,
     * latch */
#define ENV_NRPN(b, e) \
    {b + 17, e "_a", NK_CONT, 0, {0}, e "_sync", 0}, {b + 19, e "_d", NK_CONT, 0, {0}, e "_sync", 0}, \
    {b + 20, e "_r", NK_CONT, 0, {0}, e "_sync", 0}, {b + 22, e "_a", NK_ENV_SYNC, 0, {0}, e "_sync", 1}, \
    {b + 24, e "_d", NK_ENV_SYNC, 0, {0}, e "_sync", 1}, {b + 25, e "_r", NK_ENV_SYNC, 0, {0}, e "_sync", 1}, \
    C(b + 2, e "_s"), SW(b + 9, e "_multi"), SW(b + 10, e "_sync"), SW(b + 11, e "_loop"), SW(b + 12, e "_latch")
    ENV_NRPN(650, "fenv"), ENV_NRPN(700, "amp"), ENV_NRPN(750, "menv"),
#undef ENV_NRPN
    /* VCA */
    C(50, "volume"), C(51, "pan"),
};
#undef C
#undef SW
#undef OPTS
#define NNRPN ((int)(sizeof NRPN / sizeof NRPN[0]))
#define NRPN_SYNTH 3840   /* Synth n's numbers: Synth 1's + (n - 1) x 3840 */
/* the 13 synced values (64 .. 1/64 beats) as 13 equal bands of 0..16383 (2026-10-30: was v x 12 / 16383, where only
 * 16383 itself reached the last, so a controller sending the data MSB alone never got 64 beats) */
#define NRPN_BAND13(v) ((v) * 13 / 16384)

/* NRPN num to v (0..16383) */
static void mi_nrpn(inst_t *in, int num, int v) {
    int c = num / NRPN_SYNTH, base = num % NRPN_SYNTH;
    if (c >= NSYN) return;
    for (int k = 0; k < NNRPN; k++) {
        const nrpn_t *r = &NRPN[k];
        if (r->num != base) continue;   /* (a shared param has no Synth 2 / 3 key: those numbers find nothing) */
        char key[64];
        if (c) snprintf(key, sizeof key, "s%d_%s", c + 1, r->key);
        else snprintf(key, sizeof key, "%s", r->key);
        int p = mi_pidx(in, key);
        if (p < 0) return;
        if (r->only) {   /* a free time while synced, or a synced one while free: not this one's to set */
            char ok[64];
            if (c) snprintf(ok, sizeof ok, "s%d_%s", c + 1, r->only);
            else snprintf(ok, sizeof ok, "%s", r->only);
            const pkey_t *e = key_find(in, ok);
            if (e && e->slot >= 0 && (in->syn[(int)e->syn]->val[e->slot] > 0.5f) != r->only_on) continue;
        }
        switch (r->kind) {
        case NK_CONT: mi_put_norm(in, p, v / 16383.0); break;
        case NK_OPT: { int b = v * r->n / 16383; mi_put(in, p, r->opt[b < r->n ? b : r->n - 1]); break; }
        case NK_LFO_SYNC: mi_put(in, p, (NRPN_BAND13(v)) * 100.0 / 12.0); break;   /* LFO_DIV: the same 13, in order */
        case NK_ENV_SYNC: {   /* 1/64 .. 64 beats: the nearest of the envelopes' divisions (engine.c env_div) */
            if (eg13(in->syn[c])) {   /* (2026-10-30) EG TIMES "MIN, 13 Syncs": the same 13 (env_div13), exactly */
                mi_put(in, p, (NRPN_BAND13(v)) * 100.0 / 12.0);
                break;
            }
            double beats = exp2((NRPN_BAND13(v)) - 6), best = 1e9;
            int div = 0;
            for (int d = 0; d < NDIV; d++) {
                double far = fabs(log2(beats / SYNC_DIV[d].beats));
                if (far < best - 1e-9) { best = far; div = d; }
            }
            mi_put(in, p, 100.0 * (NDIV - 1 - div) / (NDIV - 1));
            break;
        }
        case NK_MONO: {
            one_t *s = in->syn[c];
            if (v >= 8192) mi_put(in, p, 0.0);   /* "Mono" */
            else if (s->val[P_VOICES] < 0.5f) mi_put(in, p, s->def[P_VOICES]);
            break;
        }
        }
        return;
    }
}

/* the table's numbers and how many of their keys don't exist (get_param "nrpn_table", for the tests: "<n> <missing>") */
static int mi_nrpn_check(const inst_t *in, char *buf, int n) {
    int found = 0, missing = 0;
    for (int c = 0; c < NSYN; c++)
        for (int k = 0; k < NNRPN; k++) {
            char key[64];
            if (c) snprintf(key, sizeof key, "s%d_%s", c + 1, NRPN[k].key);
            else snprintf(key, sizeof key, "%s", NRPN[k].key);
            int shared = !strcmp(NRPN[k].key, "master_level");
            if (c && shared) continue;
            if (mi_pidx(in, key) >= 0) found++;
            else missing++;
        }
    return snprintf(buf, (size_t)n, "%d %d", found, missing);
}

/* a controller message for the MIDI settings: NRPN, then the learned map. 1: used here (it goes no further) */
static int mi_cc(inst_t *in, int ch, int cc, int v) {
    midi_in_t *m = &in->mi;
    if (cc == 99) { m->nrpn[ch] = (short)(v << 7 | (m->nrpn[ch] >= 0 ? m->nrpn[ch] & 127 : 0)); return 1; }
    if (cc == 98) { m->nrpn[ch] = (short)((m->nrpn[ch] >= 0 ? m->nrpn[ch] & ~127 : 0) | v); return 1; }
    if (cc == 101 || cc == 100) { m->nrpn[ch] = -1; return 0; }   /* an RPN: its data isn't ours */
    if ((cc == 6 || cc == 38) && m->nrpn[ch] >= 0) {
        if (cc == 6) m->nmsb[ch] = (uint8_t)v;
        mi_nrpn(in, m->nrpn[ch], cc == 6 ? v << 7 : m->nmsb[ch] << 7 | v);
        return 1;
    }
    if (m->map[cc] >= 0) {
        if (!m->map14[cc]) { mi_put_norm(in, m->map[cc], v / 127.0); return 1; }
        m->msb[ch][cc] = (uint8_t)v;   /* the MSB alone: coarse, refined when the LSB comes */
        mi_put_norm(in, m->map[cc], (v << 7) / 16383.0);
        return 1;
    }
    if (cc >= 32 && cc < 64 && m->map14[cc - 32]) {
        mi_put_norm(in, m->map[cc - 32], (m->msb[ch][cc - 32] << 7 | v) / 16383.0);
        return 1;
    }
    return 0;
}

/* the next param set from MIDI the host hasn't been told of (mpc_engine_changed); 0: none */
static int mi_changed(inst_t *in, char *key, int len) {
    for (int w = 0; w < (NPARAMS + 63) / 64; w++) {
        if (!in->mi.chg[w]) continue;
        int b = __builtin_ctzll(in->mi.chg[w]);
        in->mi.chg[w] &= in->mi.chg[w] - 1;
        snprintf(key, (size_t)len, "%s", PARAMS[w * 64 + b].key);
        return 1;
    }
    return 0;
}

/* the tempo the Synths follow now: the host's (when received and known), else the SETTINGS page's own */
static void mi_clock(inst_t *in) {
    float bpm = in->mi.rx_clock ? in->mi.host_bpm : in->mi.tempo;
    if (bpm > 0.0f)
        for (int c = 0; c < NSYN; c++) synth_bpm(in->syn[c], bpm);
}

/* the host's tempo (set_param "lfo_bpm"): kept, and followed while the clock is received */
static void mi_host_bpm(inst_t *in, float bpm) {
    in->mi.host_bpm = bpm;
    if (in->mi.rx_clock)
        for (int c = 0; c < NSYN; c++) synth_bpm(in->syn[c], bpm);
}

/* one "key=value" of the command; 0: not one of these */
static int mi_set(inst_t *in, const char *key, char *val) {
    midi_in_t *m = &in->mi;
    if (!strcmp(key, "map")) { mi_map(in, val); return 1; }
    char *end;
    double v = strtod(val, &end);
    if (end == val || !isfinite(v)) return 0;
    static const char *const PK[PEDALS] = {"sustain", "exp1", "exp2"};
    if (!strcmp(key, "rx_clock")) { m->rx_clock = v >= 0.5; return 1; }
    if (!strcmp(key, "tempo")) { m->tempo = (float)(v < 10 ? 10 : v > 280 ? 280 : v); return 1; }
    for (int p = 0; p < PEDALS; p++)
        if (!strcmp(key, PK[p])) {
            if (v >= 0 && v <= 119) m->pedal[p] = (uint8_t)v;
            return 1;
        }
    return 0;
}

/* "midicfg\n..." from the window (set_param "state"); the text is worked on in the instance's own buffer */
static void mi_command(inst_t *in, const char *text) {
    char *buf = in->state_buf, *save = NULL;
    snprintf(buf, sizeof in->state_buf, "%s", text);
    for (char *kv = strtok_r(buf, ";", &save); kv; kv = strtok_r(NULL, ";", &save)) {
        char *eq = strchr(kv, '=');
        if (!eq) continue;
        *eq = 0;
        mi_set(in, kv, eq + 1);
    }
    mi_clock(in);
}

/* the settings as text (get_param "midicfg", for the tests) */
static int mi_text(const inst_t *in, char *buf, int n) {
    const midi_in_t *m = &in->mi;
    return snprintf(buf, (size_t)n, "rx_clock=%d;tempo=%g;sustain=%d;exp1=%d;exp2=%d", m->rx_clock, m->tempo,
                    m->pedal[PEDAL_SUS], m->pedal[PEDAL_EXP1], m->pedal[PEDAL_EXP2]);
}

/* an incoming controller's number as the Synths read it: a pedal's CC becomes its function's (64, 11, 4); a default
 * pedal number no pedal uses now is dropped (-1); any other passes as it is */
static int mi_pedal(const midi_in_t *m, int cc) {
    for (int p = 0; p < PEDALS; p++)
        if (cc == m->pedal[p]) return PEDAL_ROLE[p];
    for (int p = 0; p < PEDALS; p++)
        if (cc == PEDAL_ROLE[p]) return -1;
    return cc;
}
