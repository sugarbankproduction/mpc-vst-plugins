/* ==== GAME OF LIFE, GRAVITY ORBITS, PENDULUMS (2026-10-30) ============================================================
 * Included by engine.c after the bouncing balls (it is part of engine.c and plays its notes the way they do:
 * play_step, so through chord memory and VELOCITY RANDOM, each note's end in the step clock's list). The worlds
 * themselves are src/gens_a.h.
 *
 * What plays, each in the Synth's KEY and SCALE (key_root, key_scale), from LOW OCTAVE (or the lowest held key):
 *   LIFE: each step of RATE (a tempo division, or FREE at its own SPEED) the grid moves on. BIRTHS: every cell just
 *   born plays, its row the degree (bottom low, over OCTAVES), its column when within the step (a strum over the first
 *   quarter) and how loud (the beats' columns louder). SCAN COLUMN: a playhead steps across the columns and the live
 *   cells in its column play (row the degree, the beats louder); the grid moves on each time it comes round. SCAN ROW:
 *   the playhead steps up the rows and the live cells in its row play, their column the degree. NOTES PER STEP caps
 *   the notes of a step (spread over the rows or columns, not the lowest first). A grid that dies or loops (comes back
 *   the same for a while) is seeded again; turning SEED seeds it again at once (DENSITY: how full).
 *   ORBITS: a planet crossing spoke k plays degree k of SPOKES spread over an octave; PLANETS AS: Octave puts planet n
 *   n octaves up (over OCTAVES, velocity from its speed), Velocity plays every planet in one octave, the first loudest.
 *   SYNC puts each note on the next tempo grid line.
 *   PENDULUMS: pendulum i swings SWINGS + i x SPREAD times each CYCLE of bars at the host's tempo, so they drift apart
 *   and meet again at the end of each cycle; each plays degree i passing the centre, at its ends, or both. DAMPING
 *   makes each swing smaller (and softer) until the next cycle pushes them again; RESTART ON KEY starts the swing at the
 *   key. Velocity from the swing's size (and its speed), DYNAMICS.
 * Shared: KEYS (Free plays whenever it is on and the Synth is ON; Held Keys takes the keys the arpeggiator's way and
 * plays only while some are held, from the lowest; (2026-10-30) Key Launch: each key starts it from that key, see
 * ga_key_on; FOLLOW KEYS: engine.c gen_root_of), MAX NOTES/S (a bucket of two, refilled at that rate), NOTE LENGTH.
 * Deterministic: each Synth's worlds have their own seed. The worlds move every GA_STEP samples, only while one is on
 * (off: no work); positions are never saved. The window's views ("life_view", "orbit_view", "pend_view") are written by
 * the audio thread into small fixed copies guarded by counters (seqlocks) and read without any lock. Each generator
 * also feeds the matrix (LIFE POPULATION, ORBIT 1 X / Y, PENDULUM 1, PENDULUM SYNC: 0 .. 1). Nothing here allocates,
 * locks or touches a file. */

#define GA_BURST 2.0f      /* MAX NOTES/S: up to two notes at once, then that many a second */
#define GA_CHG_RESEED (CHORD_MAX + 3)   /* RESEED's bit in s->changed (engine.c's CHANGED_SLOT) */
static const float GA_SYNC_BEATS[] = {0.0f, 1.0f, 0.5f, 0.25f, 0.125f, 1.0f / 3, 1.0f / 6};   /* "orb_quant" */
static const float LF_RATE_BEATS[] = {1.0f, 0.5f, 0.25f, 0.125f, 1.0f / 3, 1.0f / 6};       /* "life_rate" (then Free) */
#define LF_FREE 6
static const uint8_t LF_ROW_ORDER[LF_ROWS] = {0, 4, 2, 6, 1, 5, 3, 7};   /* NOTES PER STEP: spread over the rows ... */
static const uint8_t LF_COL_ORDER[LF_COLS] = {0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15};   /* ... columns */
static const float PD_BARS[] = {1, 2, 4, 8, 16, 32};                     /* "pend_cycle" */

static float lf_hz(const float *p) { return 0.5f * powf(32.0f, p[P_LIFE_SPEED] * 0.01f); }        /* 0.5 .. 16 a second */
static float ga_len_ms(float knob) { return 20.0f * powf(100.0f, knob * 0.01f); }                  /* 20 ms .. 2 s */
static float ob_time(const float *p) { return 0.26f * powf(4.0f, (p[P_ORB_SPEED] - 50.0f) * 0.02f); }   /* its time a second */

static uint32_t ga_seed(const one_t *s, uint32_t salt, int v) {
    return ((0x2545f491u * (uint32_t)(s->idx + 1)) ^ (0x9e3779b1u * (uint32_t)(v + 1)) ^ salt) | 1u;
}

static double ga_beat_at(const ga_run_t *r, int64_t t) { return r->b0 + (double)(t - r->t0) * r->bps0; }
/* the beat clock (the host's song position while it plays, else free at its tempo): src/physgen_run.h's ph_beat */
static void ga_beat(one_t *s, int64_t t) {
    ga_run_t *r = &s->ga;
    double bpm = gen_bpm(s), bps = bpm / 60.0 / SR;   /* (2026-10-30: GEN TEMPO) */
    if (!gen_own(s) && s->sp_playing && s->sp_ppq >= 0.0) {   /* (OWN: free from where it is) */
        r->b0 = s->sp_ppq;
        r->t0 = r->sp_at;
        r->bps0 = bps;
        r->follow = 1;
    } else if (r->follow || bps != r->bps0) {
        r->b0 += (double)(t - r->t0) * r->bps0;
        r->t0 = t;
        r->bps0 = bps;
        r->follow = 0;
    }
}
static int64_t ga_on_grid(const ga_run_t *r, int64_t at, double g) {   /* the first grid line at or after `at` */
    if (g <= 0.0 || r->bps0 <= 0.0) return at;
    double b = ga_beat_at(r, at), bq = ceil(b / g - 1e-9) * g;
    int64_t q = r->t0 + (int64_t)ceil((bq - r->b0) / r->bps0 - 1e-6);
    return q < at ? at : q;
}

static void ga_out_reset(ga_out_t *o) { o->nq = 0; o->tokens = GA_BURST; }
/* a note for later (the same note at the same sample once: the louder), if MAX NOTES/S allows; 1 if it was taken */
static int ga_queue(ga_out_t *o, int64_t at, int note, int vel) {
    for (int i = 0; i < o->nq; i++)
        if (o->q[i].at == at && o->q[i].note == note) {
            if (vel > o->q[i].vel) o->q[i].vel = (uint8_t)vel;
            return 1;
        }
    if (o->tokens < 1.0f || o->nq == GA_Q) return 0;   /* over MAX NOTES/S (or full): not played */
    o->tokens -= 1.0f;
    o->q[o->nq].at = at;
    o->q[o->nq].note = (uint8_t)note;
    o->q[o->nq++].vel = (uint8_t)vel;
    return 1;
}

/* scale degree d (0: the root at LOW OCTAVE, or at the lowest held key) as a note in the key */
static int ga_note(const one_t *s, int bit, int oct_slot, int d) {
    const float *p = s->val;
    int root = (int)clampf(p[P_KEY_ROOT], 0, 11), sc = ks_clamp_scale((int)p[P_KEY_SCALE]), note, key = -1;
    if ((s->ga.held & bit) && s->keys.n) {
        key = s->keys.note[0];
        for (int i = 1; i < s->keys.n; i++) if (s->keys.note[i] < key) key = s->keys.note[i];
    } else if (s->ga.launch & bit) {   /* (2026-10-30) KEY LAUNCH: from the key that launched it */
        key = s->ga.lkey[bit >> 1];
    }
    if (key >= 0) {   /* (2026-10-30) FOLLOW KEYS: in the key played */
        root = gen_root_of(s, key);
        return gen_follow_snap(s, ks_quantize(key + ks_degree(root, sc, 0, d) - root, root, sc));
    } else {
        note = ks_degree(root, sc, (int)clampf(p[oct_slot], 0, 5) + 2, d);   /* C1 = 24 (middle C is C4) */
    }
    return ks_quantize(note, root, sc);
}
static int ga_scale_size(const one_t *s) { return KS_SIZE[ks_clamp_scale((int)s->val[P_KEY_SCALE])]; }
static int ga_accent(int col) { return col % 4 == 0 ? 110 : col % 2 == 0 ? 90 : 76; }   /* the beats' columns louder */

/* ---- the settings (after any param change): on and off, each world's start ---- */
static void ga_life_seed(one_t *s) {
    ga_run_t *r = &s->ga;
    r->lseed = (int)s->val[P_LIFE_SEED];
    lf_seed(&r->lf, ga_seed(s, 0x4c494645u + 0x01000193u * (uint32_t)r->lpress, r->lseed),
            clampf(s->val[P_LIFE_DENSITY] * 0.01f, 0.05f, 0.8f));
    memcpy(r->lf.born, r->lf.c, sizeof r->lf.c);   /* (the new grid sounds as if just born) */
    r->lhead = 0;
    r->lpop = lf_pop(&r->lf) / (float)(LF_ROWS * LF_COLS);
}
static void ga_pend_start(one_t *s, double beat) {   /* (the note bucket full again too: the same start, the same notes) */
    s->ga.pstart = beat;
    s->ga.pamp = 1.0f;
    s->ga.po.tokens = GA_BURST;
}
static void ga_apply(one_t *s) {
    ga_run_t *r = &s->ga;
    const float *p = s->val;
    int on = p[P_SYN_ON] > 0.5f ? (p[P_LIFE_ON] > 0.5f) * GA_LIFE | (p[P_ORB_ON] > 0.5f) * GA_ORB |
                                       (p[P_PEND_ON] > 0.5f) * GA_PEND : 0;
    int fresh = on & ~r->on;
    static const short KEYS_OF[3] = {P_LIFE_KEYS, P_ORB_KEYS, P_PEND_KEYS};   /* (2026-10-30) FREE, HELD, LAUNCH */
    int held = 0, launch = 0;
    for (int g = 0; g < 3; g++) {
        int m = (int)clampf(p[KEYS_OF[g]], GK_FREE, GK_LAUNCH);
        held |= (m == GK_HELD) << g;
        launch |= (m == GK_LAUNCH) << g;
    }
    r->held = on & held;
    r->lgo &= on & launch & r->launch;   /* (switched off, or another way: waits for a key again) */
    r->launch = on & launch;
    r->takes = r->held | r->launch;
    if (on && !r->on) { r->t_next = s->now; r->ticks = 0; }
    /* LIFE: seeded when switched on, when SEED turns and at RESEED (a new grid each press; the button goes back off) */
    int press = p[P_LIFE_RESEED] > 0.5f;
    if (press) {
        s->val[P_LIFE_RESEED] = 0.0f;
        s->changed |= 1u << GA_CHG_RESEED;
        if (on & GA_LIFE) r->lpress++;
    }
    if (fresh & GA_LIFE) r->lpress = 0;   /* (switched on: the SEED's own grid) */
    if ((fresh & GA_LIFE) || ((on & GA_LIFE) && ((int)p[P_LIFE_SEED] != r->lseed || press))) {
        ga_life_seed(s);
        r->lgrid = INT64_MIN;
        r->lacc = 0.0f;
        r->lflash = 0;
    }
    if (fresh & GA_LIFE) ga_out_reset(&r->lo);
    /* ORBITS: launched again when switched on, or when ECCENTRICITY or SPREAD changes the orbits */
    ob_cfg_t c = {(int)clampf(roundf(p[P_ORB_PLANETS]), 1, OB_PLANETS), (int)clampf(roundf(p[P_ORB_SPOKES]), 1, OB_SPOKES),
                  0.03f * clampf(p[P_ORB_GRAVITY] * 0.01f, 0.0f, 1.0f), clampf(p[P_ORB_ECC] * 0.01f, 0.0f, 0.8f),
                  clampf(p[P_ORB_SPREAD] * 0.01f, 0.0f, 1.0f)};
    int relaunch = (fresh & GA_ORB) || c.ecc != r->oc.ecc || c.spread != r->oc.spread;
    if (c.spokes != r->oc.spokes)   /* new spokes: each planet's sector again (no hit for the change) */
        for (int i = 0; i < OB_PLANETS; i++) r->ob.p[i].sector = ob_sector(r->ob.p[i].x, r->ob.p[i].y, c.spokes);
    r->oc = c;
    if ((on & GA_ORB) && relaunch) ob_restart(&r->ob, &r->oc, ga_seed(s, 0x4f524254u, 0));
    if (fresh & GA_ORB) { ga_out_reset(&r->oo); memset(r->oflash, 0, sizeof r->oflash); memset(r->sflash, 0, sizeof r->sflash); }
    /* PENDULUMS: from the start of a bar with the host playing, else from now */
    if (fresh & GA_PEND) {
        ga_pend_start(s, r->follow ? 0.0 : ga_beat_at(r, s->now));
        ga_out_reset(&r->po);
        memset(r->pflash, 0, sizeof r->pflash);
    }
    /* off: nothing more plays, the sources rest at 0 */
    if (!(on & GA_LIFE)) { r->lo.nq = 0; r->lpop = 0.0f; }
    if (!(on & GA_ORB)) { r->oo.nq = 0; r->ox = r->oy = 0.0f; }
    if (!(on & GA_PEND)) { r->po.nq = 0; r->p1 = r->psync = 0.0f; }
    r->on = on;
}

/* RESTART ON KEY */
/* (2026-10-30) KEY LAUNCH: LIFE seeded again from the key (the same key, the same grid; it plays until the grid dies
 * out or settles into a loop), the orbits launched again in step from their start, the pendulums started together (a
 * push that DAMPING lets die away; no new push each cycle); each plays from that key */
static void ga_key_on(one_t *s, int note) {
    ga_run_t *r = &s->ga;
    if ((r->on & GA_PEND) && s->val[P_PEND_RESTART] > 0.5f) ga_pend_start(s, ga_beat_at(r, s->now));
    if (r->launch & GA_LIFE) {
        r->lkey[0] = note;
        lf_seed(&r->lf, ga_seed(s, 0x4c494645u ^ 0x4b455953u, note), clampf(s->val[P_LIFE_DENSITY] * 0.01f, 0.05f, 0.8f));
        memcpy(r->lf.born, r->lf.c, sizeof r->lf.c);
        r->lhead = 0;
        r->lgrid = INT64_MIN;
        r->lacc = 0.0f;
        r->lo.tokens = GA_BURST;
        r->lgo |= GA_LIFE;
    }
    if (r->launch & GA_ORB) {
        r->lkey[1] = note;
        ob_restart(&r->ob, &r->oc, ga_seed(s, 0x4f524254u, 0));
        r->oo.tokens = GA_BURST;
        r->lgo |= GA_ORB;
    }
    if (r->launch & GA_PEND) {
        r->lkey[2] = note;
        ga_pend_start(s, ga_beat_at(r, s->now));
        r->lgo |= GA_PEND;
    }
}

/* the matrix sources, 0 .. 1 (each control step, per voice and for the effect pass) */
static void ga_src(const one_t *s, float *src) {
    src[MS_LIFE_POP] = s->ga.lpop;
    src[MS_ORB1_X] = s->ga.ox;
    src[MS_ORB1_Y] = s->ga.oy;
    src[MS_PEND1] = s->ga.p1;
    src[MS_PEND_SYNC] = s->ga.psync;
}

/* ---- GAME OF LIFE: a step (at sample at) ---- */
static void lf_play(one_t *s, int64_t at, int64_t step_len) {
    ga_run_t *r = &s->ga;
    const float *p = s->val;
    int mode = (int)clampf(p[P_LIFE_MODE], 0, 2), rule = (int)clampf(p[P_LIFE_RULE], 0, 2), wrap = p[P_LIFE_WRAP] > 0.5f;
    int max = (int)clampf(roundf(p[P_LIFE_NOTES]), 1, 8), n = 0;
    int size = ga_scale_size(s), top = size * ((int)clampf(p[P_LIFE_RANGE], 0, 3) + 1);   /* the highest degree */
    float dens = clampf(p[P_LIFE_DENSITY] * 0.01f, 0.05f, 0.8f);
    memset(r->llit, 0, sizeof r->llit);
    if (mode == LFM_BIRTHS) {   /* the grid moves on, then its births play */
        lf_step(&r->lf, rule, wrap);
        if ((r->launch & GA_LIFE) && (!lf_pop(&r->lf) || r->lf.loops >= LF_LOOP_GENS)) {   /* (2026-10-30) KEY */
            r->lgo &= ~GA_LIFE;                                                             /* LAUNCH: it is over */
            r->lpop = lf_pop(&r->lf) / (float)(LF_ROWS * LF_COLS);
            return;
        }
        if (!lf_pop(&r->lf) || r->lf.loops >= LF_LOOP_GENS) {
            lf_fill(&r->lf, dens);
            memcpy(r->lf.born, r->lf.c, sizeof r->lf.c);
        }
        int rot = (r->lf.hpos * 5) % LF_COLS;   /* (which birth of a row, turning from step to step) */
        for (int k = 0; k < LF_ROWS && n < max; k++) {
            int y = LF_ROW_ORDER[k];
            uint16_t b = r->lf.born[y];
            if (!b) continue;
            uint16_t rb = (uint16_t)((uint32_t)b >> rot | (uint32_t)b << (LF_COLS - rot));
            int x = (__builtin_ctz(rb) + rot) % LF_COLS;
            int64_t t = at + (int64_t)(step_len * 0.25 * x / LF_COLS);
            int note = ga_note(s, GA_LIFE, P_LIFE_OCT, (y * top + (LF_ROWS - 1) / 2) / (LF_ROWS - 1));
            if (ga_queue(&r->lo, t, note, ga_accent(x))) { r->llit[y] |= (uint16_t)(1u << x); n++; }
        }
    } else if (mode == LFM_COLUMN) {   /* the live cells of the playhead's column */
        int x = r->lhead;
        for (int k = 0; k < LF_ROWS && n < max; k++) {
            int y = LF_ROW_ORDER[k];
            if (!(r->lf.c[y] >> x & 1)) continue;
            int note = ga_note(s, GA_LIFE, P_LIFE_OCT, (y * top + (LF_ROWS - 1) / 2) / (LF_ROWS - 1));
            if (ga_queue(&r->lo, at, note, ga_accent(x))) { r->llit[y] |= (uint16_t)(1u << x); n++; }
        }
    } else {   /* SCAN ROW: the live cells of the playhead's row, their column the degree */
        int y = r->lhead;
        for (int k = 0; k < LF_COLS && n < max; k++) {
            int x = LF_COL_ORDER[k];
            if (!(r->lf.c[y] >> x & 1)) continue;
            int note = ga_note(s, GA_LIFE, P_LIFE_OCT, (x * top + (LF_COLS - 1) / 2) / (LF_COLS - 1));
            if (ga_queue(&r->lo, at, note, ga_accent(x))) { r->llit[y] |= (uint16_t)(1u << x); n++; }
        }
    }
    r->lflash = n ? GA_FLASH : r->lflash;
    if (mode != LFM_BIRTHS && ++r->lhead >= (mode == LFM_COLUMN ? LF_COLS : LF_ROWS)) {   /* came round: moves on */
        r->lhead = 0;
        lf_step(&r->lf, rule, wrap);
        if ((r->launch & GA_LIFE) && (!lf_pop(&r->lf) || r->lf.loops >= LF_LOOP_GENS)) r->lgo &= ~GA_LIFE;   /* (over) */
        else if (!lf_pop(&r->lf) || r->lf.loops >= LF_LOOP_GENS) lf_fill(&r->lf, dens);
    }
    r->lpop = lf_pop(&r->lf) / (float)(LF_ROWS * LF_COLS);
}
static void lf_tick(one_t *s, int64_t t, int go, float tf) {   /* tf: FOLLOW TEMPO's factor (FREE) */
    ga_run_t *r = &s->ga;
    const float *p = s->val;
    const float dt = (float)GA_STEP / SR;
    int rate = (int)clampf(p[P_LIFE_RATE], 0, LF_FREE);
    double frac = -1.0;
    int64_t len;
    if (rate == LF_FREE) {
        float hz = lf_hz(p) * tf;
        len = (int64_t)(SR / hz);
        r->lacc += hz * dt;
        if (r->lacc >= 1.0f) { r->lacc -= 1.0f; frac = 1.0 - r->lacc / (hz * dt); }
    } else {
        double g = LF_RATE_BEATS[rate], b0 = ga_beat_at(r, t), b1 = ga_beat_at(r, t + GA_STEP);
        int64_t k = (int64_t)ceil(b0 / g);
        len = r->bps0 > 0.0 ? (int64_t)(g / r->bps0) : (int64_t)SR;
        if (k * g < b1 && k != r->lgrid) { frac = (k * g - b0) / (b1 - b0); r->lgrid = k; }
    }
    if (frac < 0.0 || !go) return;
    lf_play(s, t + (int64_t)(clampf((float)frac, 0.0f, 0.999f) * GA_STEP), len);
}

/* ---- GRAVITY ORBITS: a step ---- */
static void ob_tick(one_t *s, int64_t t, int go, float tf) {   /* tf: FOLLOW TEMPO's factor */
    ga_run_t *r = &s->ga;
    const float *p = s->val;
    const float dt = (float)GA_STEP / SR;
    for (int i = 0; i < OB_PLANETS; i++) if (r->oflash[i]) r->oflash[i]--;
    for (int k = 0; k < OB_SPOKES; k++) if (r->sflash[k]) r->sflash[k]--;
    if (!go) return;
    ob_hit_t h[OB_HITS];
    int nh = ob_step(&r->ob, &r->oc, dt * ob_time(p) * tf, h), size = ga_scale_size(s);
    int range = (int)clampf(p[P_ORB_RANGE], 0, 3) + 1, as_vel = p[P_ORB_PLANET_TO] > 0.5f;
    double g = GA_SYNC_BEATS[(int)clampf(p[P_ORB_QUANT], 0, 6)];
    for (int i = 0; i < nh; i++) {
        int d = h[i].spoke * size / r->oc.spokes + (as_vel ? 0 : size * (h[i].planet % range)), vel;
        if (as_vel) vel = 115 - 14 * h[i].planet;
        else vel = 60 + (int)(60.0f * fminf(1.0f, h[i].speed / 2.7f));   /* (a circular orbit's speed is about 1.35) */
        int64_t at = t + (int64_t)(h[i].frac * GA_STEP);
        if (ga_queue(&r->oo, ga_on_grid(r, at, g), ga_note(s, GA_ORB, P_ORB_OCT, d), vel)) {
            r->oflash[h[i].planet] = GA_FLASH;
            r->sflash[h[i].spoke] = GA_FLASH;
        }
    }
    r->ox = clampf(0.5f + 0.5f * r->ob.p[0].x, 0.0f, 1.0f);
    r->oy = clampf(0.5f + 0.5f * r->ob.p[0].y, 0.0f, 1.0f);
}

/* ---- PENDULUMS: a step ---- */
static void pd_tick(one_t *s, int64_t t, int go) {
    ga_run_t *r = &s->ga;
    const float *p = s->val;
    const float dt = (float)GA_STEP / SR;
    for (int i = 0; i < PD_MAX; i++) if (r->pflash[i]) r->pflash[i]--;
    int n = (int)clampf(roundf(p[P_PEND_COUNT]), 2, PD_MAX), sw = (int)clampf(roundf(p[P_PEND_SPEED]), 1, 32);
    int spread = (int)clampf(roundf(p[P_PEND_SPREAD]), 1, 8), trig = (int)clampf(p[P_PEND_TRIGGER], 0, 2);
    double cyc = 4.0 * PD_BARS[(int)clampf(p[P_PEND_CYCLE], 0, 5)];
    double c0 = (ga_beat_at(r, t) - r->pstart) / cyc, c1 = (ga_beat_at(r, t + GA_STEP) - r->pstart) / cyc;
    float damp = clampf(p[P_PEND_DAMP] * 0.01f, 0.0f, 1.0f);
    if (floor(c1) > floor(c0) && !(r->launch & GA_PEND)) r->pamp = 1.0f;   /* a new cycle: pushed again (LAUNCH: the key) */
    else if (damp > 0.0f) r->pamp *= expf(-1.5f * damp * damp * dt);
    float dyn = clampf(p[P_PEND_DYN] * 0.01f, 0.0f, 1.0f), re = 0.0f, im = 0.0f;
    for (int i = 0; i < n; i++) {
        double u0 = pd_half(i, sw, spread, c0), u1 = pd_half(i, sw, spread, c1);
        float ph = (float)fmod(u1, 2.0) * 3.1415927f, x = r->pamp * cosf(ph);
        r->px[i] = (int16_t)(x * 1000.0f);
        re += cosf(ph);
        im += sinf(ph);
        if (!go || r->pamp < 0.03f) continue;
        pd_hit_t h[8];
        int nh = pd_hits(i, u0, u1, trig, h, 0, 8);
        float fast = (float)(sw + i * spread) / (float)(sw + (n - 1) * spread);
        for (int k = 0; k < nh; k++) {
            float tt = r->pamp * (h[k].end ? 1.0f : 0.6f + 0.4f * fast);
            int vel = (int)clampf(roundf(100.0f * (1.0f - dyn) + dyn * (25.0f + 102.0f * tt)), 1.0f, 127.0f);
            if (ga_queue(&r->po, t + (int64_t)(h[k].frac * GA_STEP), ga_note(s, GA_PEND, P_PEND_OCT, i), vel))
                r->pflash[i] = GA_FLASH;
        }
    }
    r->p1 = clampf(0.5f + 0.5f * r->px[0] * 0.001f, 0.0f, 1.0f);
    r->psync = clampf(sqrtf(re * re + im * im) / n, 0.0f, 1.0f);
}

/* ---- the window's views (seqlocks: each counter odd while it changes) ---- */
static void ga_view_write(ga_run_t *r, int n_pend) {
    uint32_t v;
    if (r->on & GA_LIFE) {
        v = r->lvseq;
        __atomic_store_n(&r->lvseq, v + 1, __ATOMIC_RELAXED);
        __atomic_thread_fence(__ATOMIC_RELEASE);
        for (int y = 0; y < LF_ROWS; y++) {
            __atomic_store_n(&r->vrows[y], r->lf.c[y], __ATOMIC_RELAXED);
            __atomic_store_n(&r->vlit[y], r->lflash > 0 ? r->llit[y] : (uint16_t)0, __ATOMIC_RELAXED);
        }
        __atomic_store_n(&r->vhead, (uint8_t)r->lhead, __ATOMIC_RELAXED);
        __atomic_store_n(&r->vpop, (uint8_t)lf_pop(&r->lf), __ATOMIC_RELAXED);
        __atomic_store_n(&r->lvseq, v + 2, __ATOMIC_RELEASE);
    }
    if (r->on & GA_ORB) {
        v = r->ovseq;
        __atomic_store_n(&r->ovseq, v + 1, __ATOMIC_RELAXED);
        __atomic_thread_fence(__ATOMIC_RELEASE);
        int lit = 0;
        for (int i = 0; i < r->oc.n; i++) {
            __atomic_store_n(&r->vox[i], (int16_t)(clampf(r->ob.p[i].x, -1.0f, 1.0f) * 1000.0f), __ATOMIC_RELAXED);
            __atomic_store_n(&r->voy[i], (int16_t)(clampf(r->ob.p[i].y, -1.0f, 1.0f) * 1000.0f), __ATOMIC_RELAXED);
            __atomic_store_n(&r->vof[i], (uint8_t)(r->oflash[i] > 0), __ATOMIC_RELAXED);
        }
        for (int k = 0; k < r->oc.spokes; k++) lit |= (r->sflash[k] > 0) << k;
        __atomic_store_n(&r->von, (uint8_t)r->oc.n, __ATOMIC_RELAXED);
        __atomic_store_n(&r->vspokes, (uint8_t)r->oc.spokes, __ATOMIC_RELAXED);
        __atomic_store_n(&r->vslit, (uint8_t)lit, __ATOMIC_RELAXED);
        __atomic_store_n(&r->ovseq, v + 2, __ATOMIC_RELEASE);
    }
    if (r->on & GA_PEND) {
        v = r->pvseq;
        __atomic_store_n(&r->pvseq, v + 1, __ATOMIC_RELAXED);
        __atomic_thread_fence(__ATOMIC_RELEASE);
        for (int i = 0; i < n_pend; i++) {
            __atomic_store_n(&r->vpx[i], r->px[i], __ATOMIC_RELAXED);
            __atomic_store_n(&r->vpf[i], (uint8_t)(r->pflash[i] > 0), __ATOMIC_RELAXED);
        }
        __atomic_store_n(&r->vpn, (uint8_t)n_pend, __ATOMIC_RELAXED);
        __atomic_store_n(&r->pvseq, v + 2, __ATOMIC_RELEASE);
    }
}

/* the views as text, safe from any thread at any time (it never waits: after a few tries it gives what it has):
 *   "life_view":  "<mode> <playhead> <live cells>;" then each row's live cells, then each row's lit ones (hex
 *                 16-bit masks, bottom row first, "," after each), all 0 while it is off;
 *   "orbit_view": "<planets> <spokes> <lit spokes>;" then "x,y,lit;" for each planet (-1000 .. 1000 from the sun);
 *   "pend_view":  "<pendulums>;" then "x,lit;" for each (-1000 .. 1000 from the centre). */
static int ga_view_text(one_t *s, int which, char *buf, int n) {
    ga_run_t *r = &s->ga;
    int used = 0;
    if (which == GA_LIFE) {
        uint16_t c[LF_ROWS] = {0}, l[LF_ROWS] = {0};
        int head = 0, pop = 0;
        for (int tries = 0; tries < 8; tries++) {
            uint32_t a = __atomic_load_n(&r->lvseq, __ATOMIC_ACQUIRE);
            if (a & 1u) continue;
            for (int y = 0; y < LF_ROWS; y++) {
                c[y] = __atomic_load_n(&r->vrows[y], __ATOMIC_RELAXED);
                l[y] = __atomic_load_n(&r->vlit[y], __ATOMIC_RELAXED);
            }
            head = __atomic_load_n(&r->vhead, __ATOMIC_RELAXED);
            pop = __atomic_load_n(&r->vpop, __ATOMIC_RELAXED);
            __atomic_thread_fence(__ATOMIC_ACQUIRE);
            if (__atomic_load_n(&r->lvseq, __ATOMIC_RELAXED) == a) break;
        }
        int on = (r->on & GA_LIFE) != 0;
        used = snprintf(buf, n, "%d %d %d;", (int)clampf(s->val[P_LIFE_MODE], 0, 2), on ? head : 0, on ? pop : 0);
        for (int k = 0; k < 2 * LF_ROWS && used < n - 8; k++)
            used += snprintf(buf + used, n - used, "%x,", on ? (k < LF_ROWS ? c[k] : l[k - LF_ROWS]) : 0);
    } else if (which == GA_ORB) {
        int16_t x[OB_PLANETS], y[OB_PLANETS];
        uint8_t f[OB_PLANETS];
        int np = 0, sp = 0, lit = 0;
        for (int tries = 0; tries < 8; tries++) {
            uint32_t a = __atomic_load_n(&r->ovseq, __ATOMIC_ACQUIRE);
            if (a & 1u) continue;
            np = __atomic_load_n(&r->von, __ATOMIC_RELAXED);
            sp = __atomic_load_n(&r->vspokes, __ATOMIC_RELAXED);
            lit = __atomic_load_n(&r->vslit, __ATOMIC_RELAXED);
            if (np > OB_PLANETS) np = OB_PLANETS;
            for (int i = 0; i < np; i++) {
                x[i] = __atomic_load_n(&r->vox[i], __ATOMIC_RELAXED);
                y[i] = __atomic_load_n(&r->voy[i], __ATOMIC_RELAXED);
                f[i] = __atomic_load_n(&r->vof[i], __ATOMIC_RELAXED);
            }
            __atomic_thread_fence(__ATOMIC_ACQUIRE);
            if (__atomic_load_n(&r->ovseq, __ATOMIC_RELAXED) == a) break;
        }
        if (!(r->on & GA_ORB)) np = lit = 0;
        used = snprintf(buf, n, "%d %d %d;", np, sp, lit);
        for (int i = 0; i < np && used < n - 24; i++) used += snprintf(buf + used, n - used, "%d,%d,%d;", x[i], y[i], f[i]);
    } else {
        int16_t x[PD_MAX];
        uint8_t f[PD_MAX];
        int np = 0;
        for (int tries = 0; tries < 8; tries++) {
            uint32_t a = __atomic_load_n(&r->pvseq, __ATOMIC_ACQUIRE);
            if (a & 1u) continue;
            np = __atomic_load_n(&r->vpn, __ATOMIC_RELAXED);
            if (np > PD_MAX) np = PD_MAX;
            for (int i = 0; i < np; i++) {
                x[i] = __atomic_load_n(&r->vpx[i], __ATOMIC_RELAXED);
                f[i] = __atomic_load_n(&r->vpf[i], __ATOMIC_RELAXED);
            }
            __atomic_thread_fence(__ATOMIC_ACQUIRE);
            if (__atomic_load_n(&r->pvseq, __ATOMIC_RELAXED) == a) break;
        }
        if (!(r->on & GA_PEND)) np = 0;
        used = snprintf(buf, n, "%d;", np);
        for (int i = 0; i < np && used < n - 16; i++) used += snprintf(buf + used, n - used, "%d,%d;", x[i], f[i]);
    }
    return used < n ? used : n - 1;
}

/* one step of the worlds on at sample t */
static void ga_tick(one_t *s, int64_t t) {
    ga_run_t *r = &s->ga;
    const float *p = s->val;
    const float dt = (float)GA_STEP / SR;
    int keys = s->running && s->keys.n;
    float tf = gen_tempo_factor(s, &r->tf, dt);   /* (2026-10-30) FOLLOW TEMPO: LIFE FREE and the orbits */
    ga_beat(s, t);
    if (r->lflash) r->lflash--;
    /* (HELD KEYS with none held: it waits where it is; KEY LAUNCH: until a key launches it) */
    int go = (~r->held | (keys ? GA_LIFE | GA_ORB | GA_PEND : 0)) & (~r->launch | r->lgo);
    if (r->on & GA_LIFE) {
        r->lo.tokens = fminf(r->lo.tokens + clampf(p[P_LIFE_MAX], 1.0f, 30.0f) * dt, GA_BURST);
        lf_tick(s, t, (go & GA_LIFE) != 0, tf);
    }
    if (r->on & GA_ORB) {
        r->oo.tokens = fminf(r->oo.tokens + clampf(p[P_ORB_MAX], 1.0f, 30.0f) * dt, GA_BURST);
        ob_tick(s, t, (go & GA_ORB) != 0, tf);
    }
    if (r->on & GA_PEND) {
        r->po.tokens = fminf(r->po.tokens + clampf(p[P_PEND_MAX], 1.0f, 30.0f) * dt, GA_BURST);
        pd_tick(s, t, (go & GA_PEND) != 0);
        if ((r->launch & GA_PEND) && r->pamp < 0.03f) r->lgo &= ~GA_PEND;   /* (KEY LAUNCH: swung out) */
    }
    if (++r->ticks % GA_VIEW_EVERY == 0) ga_view_write(r, (int)clampf(roundf(p[P_PEND_COUNT]), 2, PD_MAX));
}

/* a generator's notes due at this sample; returns the samples to its next one (or d) */
static int64_t ga_play_due(one_t *s, ga_out_t *o, float len_knob, int64_t d, int bit) {
    int64_t len = (int64_t)(ga_len_ms(len_knob) * 0.001f * SR);
    int gone = (s->ga.held & bit) && !(s->running && s->keys.n);   /* (2026-10-30) HELD KEYS let go: the waiting go */
    for (int i = 0; i < o->nq;) {
        if (o->q[i].at <= s->now || gone) {
            if (!gone) play_step(s, o->q[i].note, o->q[i].vel, s->now + (len > 1 ? len : 1));
            o->q[i] = o->q[--o->nq];
        } else {
            if (o->q[i].at - s->now < d) d = o->q[i].at - s->now;
            i++;
        }
    }
    return d;
}

/* at each piece of a render (synth_due): the steps due, the notes due at this sample; *n shortened to the next */
static void ga_due(one_t *s, int *n) {
    ga_run_t *r = &s->ga;
    for (int guard = 0; s->now >= r->t_next; guard++) {
        if (guard == 64) { r->t_next = s->now; break; }   /* (never: the pieces end at each step) */
        ga_tick(s, r->t_next);
        r->t_next += GA_STEP;
    }
    int64_t d = r->t_next - s->now;
    d = ga_play_due(s, &r->lo, s->val[P_LIFE_LEN], d, GA_LIFE);
    d = ga_play_due(s, &r->oo, s->val[P_ORB_LEN], d, GA_ORB);
    d = ga_play_due(s, &r->po, s->val[P_PEND_LEN], d, GA_PEND);
    if (d < 1) d = 1;
    if (d < *n) *n = (int)d;
}

/* the knobs shown in their own units */
static int ga_display(const one_t *s, int k, char *buf, int n) {
    const float *p = s->val;
    if (k == P_LIFE_SPEED) return snprintf(buf, n, "%.1f /s", lf_hz(p));
    if (k == P_LIFE_LEN || k == P_ORB_LEN || k == P_PEND_LEN) {
        float ms = ga_len_ms(p[k]);
        return ms < 1000.0f ? snprintf(buf, n, "%.0f ms", ms) : snprintf(buf, n, "%.2f s", ms * 0.001f);
    }
    return 0;
}
