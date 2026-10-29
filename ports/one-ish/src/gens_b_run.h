/* ==== RAIN AND RIPPLES, MARKOV MELODY (2026-10-30) ====================================================================
 * Included by engine.c after the bouncing balls (src/physgen_run.h); part of engine.c. Both play their notes the way
 * the balls do (play_step: through chord memory and VELOCITY RANDOM, each note's end in the step clock's list). The pond
 * and the learned table themselves are src/gens_b.h.
 *
 * RAIN: each drop's landing plays a degree of the Synth's KEY and SCALE by where across the pond it falls (left low,
 * right high, over OCTAVES octaves from LOW OCTAVE), its size its velocity (SIZE SPREAD 0: every drop the same); a
 * ring then spreads from it, and where two rings first meet, or a ring first reaches a stone, an echo plays (softer,
 * ECHO times as likely; ECHO 0: none). MAX NOTES/S caps the notes a second, SYNC puts each note on the next tempo grid
 * line, NOTE LENGTH sets how long each lasts, FREE or HELD KEYS as the balls (HELD: from the lowest key held).
 *
 * MARKOV: it learns from the keys played into this Synth (LEARN on; whether it plays or not, and not while HELD KEYS
 * gives it the keys): each note's degree in the KEY and SCALE (IN KEY off: in semitones), the step from the note before
 * (and the one before that) and the gap since it in steps of RATE; chords (notes within 30 ms) and pauses (over 4 s)
 * are left out. ON, it plays on the RATE grid (the host's tempo, its song position while it plays): the next step by
 * the table (ORDER 1: after the last degree; 2: after the last two, the first-order row when the second-order one is
 * still empty), CREATIVITY as gens_b.h says, between OCTAVES octaves around where the player played (HELD KEYS: up from
 * the lowest key held); its gaps learned, STRAIGHT (every step) or SPARSE (gaps of 2-4 steps); NOTE LENGTH a share of
 * its gap; its velocity the player's, smoothed. FORGET (each press) goes back to the built-in table.
 * The learned table is runtime state, not saved with the sound: it is about 4.7 KB a Synth (14 KB for the three) in
 * counts, more than the rest of a sound's state (the MPC's chunk is 8 KB), and a sound should play the same whoever
 * played into it last. A sound starts from the built-in table.
 *
 * Deterministic: each Synth has its own seeds (the pond's, the melody's), so the same settings and keys give the same
 * notes. Off (both): no work at all. The windows' views ("rain_view", "markov_view") are written every few steps into a
 * small fixed copy guarded by a counter (a seqlock, as the balls' view), read by the window without any lock. Nothing
 * here allocates, locks or touches a file. */

#define GB_BURST 2.0f      /* MAX NOTES/S: up to two notes at once, then that many a second */
static const float GB_SYNC_BEATS[] = {0.0f, 1.0f, 0.5f, 0.25f, 0.125f, 1.0f / 3, 1.0f / 6};   /* "rain_sync" */
static const float MK_RATE_BEATS[] = {1.0f, 0.5f, 0.25f, 0.125f, 1.0f / 3, 1.0f / 6};          /* "mkv_rate" */
#define MK_CHORD_S 0.03f   /* notes this close are a chord: not learned */
#define MK_PAUSE_S 4.0f    /* a gap this long starts a new phrase */
#define GB_FALL 0.99037f   /* the RAIN DROP and MARKOV STEP pulses, each step: exp(-64 / 44100 / 0.15), a third in 0.15 s */

static float rn_rate(const float *p) { return 0.2f * powf(50.0f, p[P_RAIN_RATE] * 0.01f); }      /* 0.2 .. 10 /s */
static float rn_life(const float *p) { return 0.5f * powf(16.0f, p[P_RAIN_LIFE] * 0.01f); }      /* 0.5 .. 8 s */
static float rn_len_ms(const float *p) { return 20.0f * powf(100.0f, p[P_RAIN_LEN] * 0.01f); }   /* 20 ms .. 2 s */
static int mk_scale(const one_t *s) {   /* IN KEY: the Synth's scale, else every semitone */
    return s->val[P_MKV_QUANT] > 0.5f ? ks_clamp_scale((int)s->val[P_KEY_SCALE]) : KS_CHROMATIC;
}
static int gb_root(const one_t *s) { return (int)clampf(s->val[P_KEY_ROOT], 0, 11); }
static float mk_beats(const one_t *s) { return MK_RATE_BEATS[(int)clampf(s->val[P_MKV_RATE], 0, 5)]; }

/* a note as a degree of the scale (counted from the root below C-1's octave; never negative) */
static int mk_deg(int note, int root, int sc) {
    int q = ks_quantize(note, root, sc), rel = q - root, size = KS_SIZE[sc];
    if (rel < 0) return 0;
    int oct = rel / 12, pc = rel - 12 * oct, idx = 0;
    for (int i = 0; i < size; i++) if (KS_STEPS[sc][i] == pc) idx = i;
    return oct * size + idx;
}
static int mk_note(const one_t *s, int d) {
    int root = gb_root(s), sc = mk_scale(s);
    return ks_quantize(ks_degree(root, sc, 0, d < 0 ? 0 : d), root, sc);
}

/* ---- the beat clock (SYNC), as the balls' ---- */
static void gb_beat(one_t *s, int64_t t) {
    gb_run_t *g = &s->gb;
    double bpm = s->sp_bpm > 0.0 ? s->sp_bpm : (double)s->bpm, bps = bpm / 60.0 / SR;
    if (s->sp_playing && s->sp_ppq >= 0.0) {
        g->b0 = s->sp_ppq;
        g->t0 = g->sp_at;
        g->bps0 = bps;
        g->follow = 1;
    } else if (g->follow || bps != g->bps0) {
        g->b0 += (double)(t - g->t0) * g->bps0;
        g->t0 = t;
        g->bps0 = bps;
        g->follow = 0;
    }
}
static int64_t gb_on_grid(const one_t *s, int64_t at, double grid) {   /* the first grid line at or after `at` */
    const gb_run_t *g = &s->gb;
    if (grid <= 0.0 || g->bps0 <= 0.0) return at;
    double b = g->b0 + (double)(at - g->t0) * g->bps0, bq = ceil(b / grid - 1e-9) * grid;
    int64_t q = g->t0 + (int64_t)ceil((bq - g->b0) / g->bps0 - 1e-6);
    return q < at ? at : q;
}

static void gb_queue(one_t *s, int64_t at, int note, int vel, int32_t len, int dedupe) {
    gb_run_t *g = &s->gb;
    if (dedupe)   /* the same note on the same grid line plays once (the louder) */
        for (int i = 0; i < g->nq; i++)
            if (g->q[i].at == at && g->q[i].note == note) {
                if (vel > g->q[i].vel) g->q[i].vel = (uint8_t)vel;
                return;
            }
    if (g->nq == GB_Q) return;   /* full: dropped */
    g->q[g->nq].at = at;
    g->q[g->nq].len = len > 1 ? len : 1;
    g->q[g->nq].note = (uint8_t)note;
    g->q[g->nq++].vel = (uint8_t)vel;
}

static int gb_lowest_key(const one_t *s) {
    int low = s->keys.note[0];
    for (int i = 1; i < s->keys.n; i++) if (s->keys.note[i] < low) low = s->keys.note[i];
    return low;
}
static int gb_keys_ok(const one_t *s, int held) { return !held || (s->running && s->keys.n); }

/* ---- RAIN ---- */
static uint32_t rn_seed(const one_t *s) { return (0x6c8e9cf5u * (uint32_t)(s->idx + 1)) ^ 0x85ebca6bu; }

/* a hit's note (its x across the pond as a degree) and velocity (its size or strength) */
static void rn_hit_note(const one_t *s, const rn_hit_t *h, int *note, int *vel) {
    const float *p = s->val;
    int root = gb_root(s), sc = ks_clamp_scale((int)p[P_KEY_SCALE]), size = KS_SIZE[sc];
    int steps = size * ((int)clampf(p[P_RAIN_RANGE], 0, 3) + 1) + 1, d = (int)(h->x * steps);
    d = d < 0 ? 0 : d >= steps ? steps - 1 : d;
    int n;
    if (s->gb.rn_held && s->keys.n) n = gb_lowest_key(s) + ks_degree(root, sc, 0, d) - root;
    else n = ks_degree(root, sc, (int)clampf(p[P_RAIN_OCT], 0, 5) + 2, d);   /* C1 = 24 */
    *note = ks_quantize(n, root, sc);
    float a = clampf(h->amp, 0.0f, 1.0f);
    *vel = (int)clampf(roundf(h->kind == RN_DROP ? 40.0f + 72.0f * a : 20.0f + 70.0f * a), 1.0f, 127.0f);
}

static void rn_tick(one_t *s, int64_t t, float dt) {
    gb_run_t *g = &s->gb;
    const float *p = s->val;
    int sync = (int)p[P_RAIN_SYNC] > 0;
    for (int i = 0; i < RN_RIPS; i++) if (g->rflash[i]) g->rflash[i]--;
    for (int k = 0; k < RN_STONES; k++) if (g->sflash[k]) g->sflash[k]--;
    if (!gb_keys_ok(s, g->rn_held)) return;   /* HELD KEYS with none held: it waits where it is */
    g->rn_tokens = fminf(g->rn_tokens + clampf(p[P_RAIN_MAX], 1.0f, 30.0f) * dt, GB_BURST);
    rn_hit_t h[RN_HITS];
    int nh = rn_step(&g->rw, &g->rc, dt, h);
    int32_t len = (int32_t)(rn_len_ms(p) * 0.001f * SR);
    for (int i = 0; i < nh; i++) {
        if (h[i].kind == RN_DROP) g->src[GBS_DROP] = fmaxf(g->src[GBS_DROP], h[i].amp);
        if (g->rn_tokens < 1.0f) continue;   /* over MAX NOTES/S: not played */
        g->rn_tokens -= 1.0f;
        int note, vel;
        rn_hit_note(s, &h[i], &note, &vel);
        int64_t at = t + (int64_t)(h[i].frac * GB_STEP);
        gb_queue(s, sync ? gb_on_grid(s, at, GB_SYNC_BEATS[(int)clampf(p[P_RAIN_SYNC], 0, 6)]) : at, note, vel, len, sync);
        if (h[i].kind == RN_STONE) g->sflash[h[i].who] = GB_FLASH;
    }
    for (int i = 0; i < RN_RIPS; i++)
        if (g->rw.r[i].lit) { g->rw.r[i].lit = 0; g->rflash[i] = GB_FLASH; }
    g->src[GBS_RIPPLE] = g->rw.energy;
}

/* ---- MARKOV ---- */
static void mk_forget_all(one_t *s) {
    gb_run_t *g = &s->gb;
    mk_builtin(&g->mt, KS_SIZE[mk_scale(s)]);
    g->l1 = g->l2 = g->l_gap = -1;
    g->vavg = 90.0f;
    g->centre = (float)mk_deg(60 + gb_root(s), gb_root(s), mk_scale(s));   /* around the root above middle C */
}

/* the degrees it plays between: OCTAVES octaves around the centre (HELD KEYS: up from the lowest key) */
static void mk_window(one_t *s) {
    gb_run_t *g = &s->gb;
    int sc = mk_scale(s), span = KS_SIZE[sc] * ((int)clampf(s->val[P_MKV_RANGE], 0, 3) + 1);
    int lo = g->mk_held && s->keys.n ? mk_deg(gb_lowest_key(s), gb_root(s), sc) : (int)lroundf(g->centre) - span / 2;
    int top = mk_deg(127, gb_root(s), sc);
    if (lo + span > top) lo = top - span;
    if (lo < 0) lo = 0;
    g->lo = lo;
    g->hi = lo + span;
}

static void mk_start(one_t *s) {
    gb_run_t *g = &s->gb;
    g->mk_rng = (0x27d4eb2fu * (uint32_t)(s->idx + 1)) ^ 0x165667b1u;
    g->mk_wait = 0;
    g->mk_gap = 0;
    g->mk_tokens = GB_BURST;
    g->mk_line = -1;
    g->hist_n = g->hist_head = 0;
    mk_window(s);
    int c = (int)lroundf(g->centre);
    g->g1 = c < g->lo ? g->lo : c > g->hi ? g->hi : c;
    g->g2 = -1;
}

/* a key played into this Synth (the MIDI note-on path): learned */
static void mk_hear(one_t *s, int note, int vel) {
    gb_run_t *g = &s->gb;
    if (s->val[P_MKV_LEARN] < 0.5f || g->mk_held || !g->mk_init) return;
    int64_t dt = s->now - g->l_t;
    if (g->l1 >= 0 && dt < (int64_t)(MK_CHORD_S * SR)) return;   /* a chord's other notes */
    int sc = mk_scale(s), size = KS_SIZE[sc], d = mk_deg(note, gb_root(s), sc);
    if (g->l1 < 0 || dt > (int64_t)(MK_PAUSE_S * SR)) {   /* a new phrase */
        g->l2 = g->l_gap = -1;
    } else {
        mk_learn_step(&g->mt, size, g->l2, g->l1, d);
        double bps = g->bps0 > 0.0 ? g->bps0 : (s->sp_bpm > 0.0 ? s->sp_bpm : (double)s->bpm) / 60.0 / SR;
        int gc = mk_gap_class((float)(dt * bps / mk_beats(s)));
        if (gc >= 0) mk_learn_gap(&g->mt, g->l_gap, gc);
        g->l_gap = gc;
        g->l2 = g->l1;
    }
    g->l1 = d;
    g->l_t = s->now;
    g->vavg += 0.25f * ((float)vel - g->vavg);
    g->centre += (g->mt.learned ? 0.2f : 1.0f) * ((float)d - g->centre);
}

/* one grid line: the next note, or a step of its gap */
static void mk_step(one_t *s, int64_t line) {
    gb_run_t *g = &s->gb;
    const float *p = s->val;
    if (!gb_keys_ok(s, g->mk_held)) { g->mk_wait = 0; return; }
    if (g->mk_wait > 0) { g->mk_wait--; return; }
    if (g->mk_tokens < 1.0f) return;   /* over MAX NOTES/S: this line rests */
    g->mk_tokens -= 1.0f;
    mk_window(s);
    int sc = mk_scale(s), size = KS_SIZE[sc];
    float c = clampf(p[P_MKV_CREAT] * 0.01f, 0.0f, 1.0f);
    if (g->g1 < g->lo || g->g1 > g->hi) { g->g1 = g->g1 < g->lo ? g->lo : g->hi; g->g2 = -1; }
    const uint16_t *row = g->mt.t1[mk_mod(g->g1, size)];
    if (p[P_MKV_ORDER] > 0.5f && g->g2 >= 0) {   /* second order, when that row has anything */
        const uint16_t *r2 = g->mt.t2[mk_mod(g->g2, size)][mk_mod(g->g1, size)];
        unsigned sum = 0;
        for (int i = 0; i < MK_IV; i++) sum += r2[i];
        if (sum) row = r2;
    }
    uint32_t near = 0;   /* it may wander to steps of up to 4 degrees */
    for (int i = MK_SPAN - 4; i <= MK_SPAN + 4; i++) near |= 1u << i;
    int k = mk_pick(row, MK_IV, c, near, &g->mk_rng, g->bars), iv = k - MK_SPAN, d = g->g1 + iv;
    if (d > g->hi) d = g->g1 - (iv < 0 ? -iv : iv);   /* past the range: the same step the other way */
    if (d < g->lo) d = g->g1 + (iv < 0 ? -iv : iv);
    d = d < g->lo ? g->lo : d > g->hi ? g->hi : d;
    g->choice = k;
    g->g2 = g->g1;
    g->g1 = d;
    /* its gap to the next note */
    int r = (int)clampf(p[P_MKV_RHYTHM], 0, 2), gap = 0;
    if (r == 0) {
        gap = mk_pick(g->mt.r1[g->mk_gap], MK_RC, c, 0x3Fu, &g->mk_rng, NULL);
    } else if (r == 2) {
        static const uint16_t SPARSE[MK_RC] = {0, 4, 3, 3, 1, 0};
        gap = mk_pick(SPARSE, MK_RC, fmaxf(c, 0.5f), 0, &g->mk_rng, NULL);
    }
    g->mk_gap = gap;
    g->mk_wait = MK_GAP[gap] - 1;
    float steps = MK_GAP[gap] * mk_beats(s) / (float)(g->bps0 > 0.0 ? g->bps0 : 2.0 / SR);
    int32_t len = (int32_t)(steps * clampf(p[P_MKV_GATE] * 0.01f, 0.05f, 1.0f));
    int vel = (int)clampf(roundf(g->vavg + (seq_rand01(&g->mk_rng) - 0.5f) * 16.0f * c), 1.0f, 127.0f);
    int n = mk_note(s, d);
    if (g->mk_held && s->keys.n) {   /* from the lowest key held (in the key) */
        int low = gb_lowest_key(s);
        n = ks_quantize(low + mk_note(s, d) - mk_note(s, g->lo), gb_root(s), sc);
    }
    gb_queue(s, line, n, vel, len, 0);
    g->hist[g->hist_head].t = line;
    g->hist[g->hist_head].d = (int16_t)(d - g->lo);
    g->hist_head = (g->hist_head + 1) % MK_HIST;
    if (g->hist_n < MK_HIST) g->hist_n++;
    g->src[GBS_MK_STEP] = 1.0f;
    g->src[GBS_MK_HEIGHT] = g->hi > g->lo ? (float)(d - g->lo) / (float)(g->hi - g->lo) : 0.0f;
}

static void mk_tick(one_t *s, int64_t t, float dt) {
    gb_run_t *g = &s->gb;
    g->mk_tokens = fminf(g->mk_tokens + clampf(s->val[P_MKV_MAX], 1.0f, 30.0f) * dt, GB_BURST);
    int64_t line = gb_on_grid(s, t, mk_beats(s));
    if (line < t + GB_STEP && line != g->mk_line) {
        g->mk_line = line;
        mk_step(s, line);
    }
}

/* ---- the settings (after any param change): on and off ---- */
static void gb_apply(one_t *s) {
    gb_run_t *g = &s->gb;
    const float *p = s->val;
    if (!g->mk_init) {   /* (the first time, at create: the built-in table) */
        g->mk_init = 1;
        g->mk_forget = p[P_MKV_FORGET];
        mk_forget_all(s);
    }
    if (p[P_MKV_FORGET] != g->mk_forget) {   /* FORGET pressed */
        g->mk_forget = p[P_MKV_FORGET];
        mk_forget_all(s);
    }
    rn_cfg_t *c = &g->rc;
    c->rate = rn_rate(p);
    c->spread = clampf(p[P_RAIN_SPREAD] * 0.01f, 0.0f, 1.0f);
    c->speed = 0.05f * powf(20.0f, p[P_RAIN_SPEED] * 0.01f);   /* 0.05 .. 1 pond widths a second */
    c->life = rn_life(p);
    c->echo = clampf(p[P_RAIN_ECHO] * 0.01f, 0.0f, 1.0f);
    c->stones = (int)clampf(roundf(p[P_RAIN_STONES]), 0, RN_STONES);
    int syn = p[P_SYN_ON] > 0.5f, rn = syn && p[P_RAIN_ON] > 0.5f, mk = syn && p[P_MKV_ON] > 0.5f;
    if ((rn && !g->rn_on) || (mk && !g->mk_on)) {
        if (!g->rn_on && !g->mk_on) { g->t_next = s->now; g->ticks = 0; g->nq = 0; }
    }
    if (rn && !g->rn_on) {   /* switched on: the pond from its start */
        rn_restart(&g->rw, rn_seed(s));
        memset(g->rflash, 0, sizeof g->rflash);
        memset(g->sflash, 0, sizeof g->sflash);
        g->rn_tokens = GB_BURST;
    }
    if (!rn) g->src[GBS_DROP] = g->src[GBS_RIPPLE] = 0.0f;
    g->rn_on = rn;
    g->rn_held = rn && p[P_RAIN_KEYS] > 0.5f;
    g->mk_held = mk && p[P_MKV_KEYS] > 0.5f;
    if (mk && !g->mk_on) mk_start(s);
    if (!mk) g->src[GBS_MK_STEP] = g->src[GBS_MK_HEIGHT] = 0.0f;
    g->mk_on = mk;
    if (!rn && !mk) g->nq = 0;   /* both off: nothing more plays */
}

static void gb_key_on(one_t *s, int note, int vel) { mk_hear(s, note, vel); }

/* ---- the windows' views (seqlock: the counter odd while they change) ---- */
static void gb_view_write(one_t *s) {
    gb_run_t *g = &s->gb;
    uint32_t v = g->vseq;
    __atomic_store_n(&g->vseq, v + 1, __ATOMIC_RELAXED);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    int nd = 0, nr = 0, ns = g->rn_on ? g->rc.stones : 0;
    for (int i = 0; g->rn_on && i < RN_DROPS; i++) {
        const rn_drop_t *d = &g->rw.d[i];
        if (!d->live) continue;
        __atomic_store_n(&g->vd[nd][0], (int16_t)(d->x * 1000.0f), __ATOMIC_RELAXED);
        __atomic_store_n(&g->vd[nd][1], (int16_t)(d->y * 1000.0f), __ATOMIC_RELAXED);
        __atomic_store_n(&g->vd[nd][2], (int16_t)(clampf(1.0f - d->t / RN_FALL, 0.0f, 1.0f) * 1000.0f), __ATOMIC_RELAXED);
        nd++;
    }
    for (int i = 0; g->rn_on && i < RN_RIPS; i++) {
        const rn_rip_t *r = &g->rw.r[i];
        if (!r->live) continue;
        __atomic_store_n(&g->vr[nr][0], (int16_t)(r->x * 1000.0f), __ATOMIC_RELAXED);
        __atomic_store_n(&g->vr[nr][1], (int16_t)(r->y * 1000.0f), __ATOMIC_RELAXED);
        __atomic_store_n(&g->vr[nr][2], (int16_t)(fminf(r->r, 2.0f) * 1000.0f), __ATOMIC_RELAXED);
        __atomic_store_n(&g->vr[nr][3], (int16_t)(clampf(rn_left(r), 0.0f, 1.0f) * 1000.0f), __ATOMIC_RELAXED);
        __atomic_store_n(&g->vr[nr][4], (int16_t)(g->rflash[i] > 0), __ATOMIC_RELAXED);
        nr++;
    }
    for (int k = 0; k < ns; k++) {
        __atomic_store_n(&g->vs[k][0], (int16_t)(g->rw.sx[k] * 1000.0f), __ATOMIC_RELAXED);
        __atomic_store_n(&g->vs[k][1], (int16_t)(g->rw.sy[k] * 1000.0f), __ATOMIC_RELAXED);
        __atomic_store_n(&g->vs[k][2], (int16_t)(g->sflash[k] > 0), __ATOMIC_RELAXED);
    }
    int nm = g->mk_on ? g->hist_n : 0;
    for (int i = 0; i < nm; i++) {   /* newest first */
        int j = (g->hist_head - 1 - i + 2 * MK_HIST) % MK_HIST;
        int64_t age = (s->now - g->hist[j].t) * 1000 / (int64_t)SR;
        __atomic_store_n(&g->vmh[i], g->hist[j].d, __ATOMIC_RELAXED);
        __atomic_store_n(&g->vmage[i], (int32_t)(age < 0 ? 0 : age > 99999 ? 99999 : age), __ATOMIC_RELAXED);
    }
    for (int i = 0; i < MK_IV; i++) __atomic_store_n(&g->vmbar[i], (int16_t)(g->bars[i] * 1000.0f), __ATOMIC_RELAXED);
    __atomic_store_n(&g->vmspan, (int16_t)(g->hi - g->lo), __ATOMIC_RELAXED);
    __atomic_store_n(&g->vmchoice, (int16_t)g->choice, __ATOMIC_RELAXED);
    __atomic_store_n(&g->vmlearned, (int32_t)g->mt.learned, __ATOMIC_RELAXED);
    __atomic_store_n(&g->vmorder, (uint8_t)(s->val[P_MKV_ORDER] > 0.5f ? 2 : 1), __ATOMIC_RELAXED);
    __atomic_store_n(&g->vnd, (uint8_t)nd, __ATOMIC_RELAXED);
    __atomic_store_n(&g->vnr, (uint8_t)nr, __ATOMIC_RELAXED);
    __atomic_store_n(&g->vns, (uint8_t)ns, __ATOMIC_RELAXED);
    __atomic_store_n(&g->vmn, (uint8_t)nm, __ATOMIC_RELAXED);
    __atomic_store_n(&g->vseq, v + 2, __ATOMIC_RELEASE);
}

/* "rain_view": "<drops> <rings> <stones>;" then "x,y,fall;" for each drop, "x,y,r,e,lit;" for each ring, "x,y,lit;"
 * for each stone (x, y, r: 0 .. 1000 of the pond's width; fall and e 0 .. 1000). "markov_view": "<notes> <learned>
 * <span> <order> <choice>;" then "height,age;" for each of its last notes, newest first (height 0 .. span degrees, age
 * in ms), then the MK_IV bars "b," (each step's share of the last choice, 0 .. 1000, from -7 to +7). Safe from any
 * thread at any time (it never waits: after a few tries it gives what it has) */
static int gb_view_text(one_t *s, int which, char *buf, int n) {
    gb_run_t *g = &s->gb;
    int16_t a[RN_RIPS][5], b[MK_IV];
    int32_t age[MK_HIST], learned = 0;
    int c0 = 0, c1 = 0, c2 = 0, span = 0, order = 1, choice = MK_SPAN;
    int16_t sd[RN_DROPS][3], st[RN_STONES][3], mh[MK_HIST];
    for (int tries = 0; tries < 8; tries++) {
        uint32_t q = __atomic_load_n(&g->vseq, __ATOMIC_ACQUIRE);
        if (q & 1u) continue;
        if (which == 0) {
            c0 = __atomic_load_n(&g->vnd, __ATOMIC_RELAXED);
            c1 = __atomic_load_n(&g->vnr, __ATOMIC_RELAXED);
            c2 = __atomic_load_n(&g->vns, __ATOMIC_RELAXED);
            if (c0 > RN_DROPS) c0 = RN_DROPS;
            if (c1 > RN_RIPS) c1 = RN_RIPS;
            if (c2 > RN_STONES) c2 = RN_STONES;
            for (int i = 0; i < c0; i++) for (int k = 0; k < 3; k++) sd[i][k] = __atomic_load_n(&g->vd[i][k], __ATOMIC_RELAXED);
            for (int i = 0; i < c1; i++) for (int k = 0; k < 5; k++) a[i][k] = __atomic_load_n(&g->vr[i][k], __ATOMIC_RELAXED);
            for (int i = 0; i < c2; i++) for (int k = 0; k < 3; k++) st[i][k] = __atomic_load_n(&g->vs[i][k], __ATOMIC_RELAXED);
        } else {
            c0 = __atomic_load_n(&g->vmn, __ATOMIC_RELAXED);
            if (c0 > MK_HIST) c0 = MK_HIST;
            for (int i = 0; i < c0; i++) {
                mh[i] = __atomic_load_n(&g->vmh[i], __ATOMIC_RELAXED);
                age[i] = __atomic_load_n(&g->vmage[i], __ATOMIC_RELAXED);
            }
            for (int i = 0; i < MK_IV; i++) b[i] = __atomic_load_n(&g->vmbar[i], __ATOMIC_RELAXED);
            span = __atomic_load_n(&g->vmspan, __ATOMIC_RELAXED);
            learned = __atomic_load_n(&g->vmlearned, __ATOMIC_RELAXED);
            order = __atomic_load_n(&g->vmorder, __ATOMIC_RELAXED);
            choice = __atomic_load_n(&g->vmchoice, __ATOMIC_RELAXED);
        }
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if (__atomic_load_n(&g->vseq, __ATOMIC_RELAXED) == q) break;
    }
    int used;
    if (which == 0) {
        if (!g->rn_on) c0 = c1 = c2 = 0;
        used = snprintf(buf, n, "%d %d %d;", c0, c1, c2);
        for (int i = 0; i < c0 && used < n - 24; i++) used += snprintf(buf + used, n - used, "%d,%d,%d;", sd[i][0], sd[i][1], sd[i][2]);
        for (int i = 0; i < c1 && used < n - 32; i++)
            used += snprintf(buf + used, n - used, "%d,%d,%d,%d,%d;", a[i][0], a[i][1], a[i][2], a[i][3], a[i][4]);
        for (int i = 0; i < c2 && used < n - 24; i++) used += snprintf(buf + used, n - used, "%d,%d,%d;", st[i][0], st[i][1], st[i][2]);
    } else {
        if (!g->mk_on) c0 = 0;
        used = snprintf(buf, n, "%d %d %d %d %d;", c0, (int)learned, span, order, choice);
        for (int i = 0; i < c0 && used < n - 24; i++) used += snprintf(buf + used, n - used, "%d,%d;", mh[i], (int)age[i]);
        for (int i = 0; i < MK_IV && used < n - 8; i++) used += snprintf(buf + used, n - used, "%d,", b[i]);
    }
    return used < n ? used : n - 1;
}

/* one step at sample t */
static void gb_tick(one_t *s, int64_t t) {
    gb_run_t *g = &s->gb;
    const float dt = (float)GB_STEP / SR;
    g->src[GBS_DROP] *= GB_FALL;
    g->src[GBS_MK_STEP] *= GB_FALL;
    gb_beat(s, t);
    if (g->rn_on) rn_tick(s, t, dt);
    if (g->mk_on) mk_tick(s, t, dt);
    if (++g->ticks % GB_VIEW_EVERY == 0) gb_view_write(s);
}

/* at each piece of a render (synth_due): the steps due, the notes due at this sample; *n shortened to the next */
static void gb_due(one_t *s, int *n) {
    gb_run_t *g = &s->gb;
    for (int guard = 0; s->now >= g->t_next; guard++) {
        if (guard == 64) { g->t_next = s->now; break; }
        gb_tick(s, g->t_next);
        g->t_next += GB_STEP;
    }
    int64_t d = g->t_next - s->now;
    for (int i = 0; i < g->nq;) {
        if (g->q[i].at <= s->now) {
            play_step(s, g->q[i].note, g->q[i].vel, s->now + g->q[i].len);
            g->q[i] = g->q[--g->nq];
        } else {
            if (g->q[i].at - s->now < d) d = g->q[i].at - s->now;
            i++;
        }
    }
    if (d < 1) d = 1;
    if (d < *n) *n = (int)d;
}

/* the modulation sources (all 0 .. 1; 0 while their generator is off): RAIN DROP jumps to a landing drop's size and
 * falls to a third in 0.15 s, RIPPLE ENERGY the rings' strength together, MARKOV STEP 1 at each of its notes falling
 * as RAIN DROP, MARKOV HEIGHT where its last note is in its range (0 the bottom, 1 the top) */
static void gb_sources(const one_t *s, float *src) {
    src[MS_RAIN_DROP] = s->gb.src[GBS_DROP];
    src[MS_RIPPLE] = s->gb.src[GBS_RIPPLE];
    src[MS_MK_STEP] = s->gb.src[GBS_MK_STEP];
    src[MS_MK_HEIGHT] = s->gb.src[GBS_MK_HEIGHT];
}

/* the knobs shown in their own units */
static int gb_display(const one_t *s, int k, char *buf, int n) {
    const float *p = s->val;
    if (k == P_RAIN_RATE) return snprintf(buf, n, "%.1f /s", rn_rate(p));
    if (k == P_RAIN_LIFE) return snprintf(buf, n, "%.1f s", rn_life(p));
    if (k == P_RAIN_LEN) {
        float ms = rn_len_ms(p);
        return ms < 1000.0f ? snprintf(buf, n, "%.0f ms", ms) : snprintf(buf, n, "%.2f s", ms * 0.001f);
    }
    return 0;
}
