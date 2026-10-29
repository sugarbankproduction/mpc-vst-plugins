/* ==== THE BOUNCING BALLS AND PARTICLES (2026-10-29) ==================================================================
 * Included by engine.c after the step sequencer (it is part of engine.c and plays its notes the way the Euclidean rhythm
 * and the Turing machine do: play_step, so through chord memory and VELOCITY RANDOM, each note's end in the step
 * clock's list). The world itself (where the balls and particles are, what they hit) is src/physgen.h.
 *
 * What plays: BALLS, each wall a ball hits: the FLOOR a degree of the Synth's KEY and SCALE (key_root, key_scale) by
 * where along it the ball lands (left low, right high, over OCTAVES octaves from LOW OCTAVE), the CEILING the same an
 * octave up, the LEFT wall the root and the RIGHT wall the fifth (an octave up in their upper halves). PARTICLES, as
 * PARTICLES PLAY says: crossing the LINE (a quarter of the way up), colliding, or dying, a degree by where across the
 * box. Velocity follows how hard (DYNAMICS: 0 every note at 100, 100 all of it from the impact). Every note is in the
 * key. MAX NOTES/S caps the notes a second (a bucket of two, refilled at that rate; the rest are not played), SYNC puts
 * each note on the next tempo grid line (the host's tempo, its song position while it plays), NOTE LENGTH sets how long
 * each lasts. HELD KEYS: the keys go to the generator, as they go to the arpeggiator (LATCH and HOLD keep them), it
 * plays only while there are some and its notes start from the lowest one (in the key); FREE: it plays whenever it is
 * on and the Synth is ON, and the keys play as usual. KEY RESTARTS: a key starts the balls and particles again from
 * their start (the key picks the start: the same key, the same start).
 * Deterministic: each Synth's world has its own seed, so the same settings and keys give the same notes. The world
 * moves every PH_STEP samples, only while it is on; positions are never saved (a sound starts them from the start).
 * The window's view ("phys_view"): written every few steps by the audio thread into a small fixed copy guarded by a
 * counter (a seqlock: odd while it is being written), read by the window without any lock. Nothing here allocates,
 * locks or touches a file. */

#define PH_BURST 2.0f      /* MAX NOTES/S: up to two notes at once, then that many a second */
static const float PH_SYNC_BEATS[] = {0.0f, 1.0f, 0.5f, 0.25f, 0.125f, 1.0f / 3, 1.0f / 6};   /* "phys_sync" */

static float ph_box(const float *p) { return 0.5f * powf(4.0f, p[P_PH_SIZE] * 0.01f); }         /* 0.5 .. 2 */
static float ph_rate(const float *p) { return 0.2f * powf(100.0f, p[P_PH_RATE] * 0.01f); }      /* 0.2 .. 20 a second */
static float ph_life(const float *p) { return 0.3f * powf(30.0f, p[P_PH_LIFE] * 0.01f); }       /* 0.3 .. 9 s */
static float ph_len_ms(const float *p) { return 20.0f * powf(100.0f, p[P_PH_LEN] * 0.01f); }    /* 20 ms .. 2 s */

static uint32_t ph_seed(const one_t *s, int key) {
    return (0x2545f491u * (uint32_t)(s->idx + 1)) ^ (0x9e3779b1u * (uint32_t)(key + 1));
}
static void ph_restart_run(one_t *s, int key) {
    ph_run_t *r = &s->ph;
    ph_restart(&r->w, ph_seed(s, key), r->cfg.speed);
    memset(r->flash, 0, sizeof r->flash);
    memset(r->wall, 0, sizeof r->wall);
    r->nq = 0;
    r->tokens = PH_BURST;
    r->ticks = 0;
    r->t_next = s->now;
}

/* the settings (after any param change): the world's units, on and off */
static void ph_apply(one_t *s) {
    ph_run_t *r = &s->ph;
    const float *p = s->val;
    float box = ph_box(p), k;
    ph_cfg_t *c = &r->cfg;
    c->balls = p[P_PH_BALLS] > 0.5f;
    c->nballs = (int)clampf(roundf(p[P_PH_NBALLS]), 1, PH_BALLS);
    float was = c->speed;
    c->speed = 0.1f * powf(30.0f, p[P_PH_SPEED] * 0.01f) / box;   /* 0.1 .. 3 box widths a second in a 1 x 1 box */
    if (r->on && was > 0.0f && c->speed != was)   /* SPEED (or the box) turned: the balls in flight follow */
        for (int i = 0; i < PH_BALLS; i++) { r->w.b[i].vx *= c->speed / was; r->w.b[i].vy *= c->speed / was; }
    k = p[P_PH_GRAVITY] * 0.01f;
    c->gravity = 10.0f * k * k / box;
    c->bounce = clampf(p[P_PH_BOUNCE] * 0.01f, 0.5f, 1.0f);
    c->parts = p[P_PH_PARTS] > 0.5f;
    c->trigger = (int)clampf(p[P_PH_TRIGGER], 0, 2);
    c->rate = ph_rate(p);
    c->spread = clampf(p[P_PH_SPREAD] * 0.01f, 0.0f, 1.0f);
    c->life = ph_life(p);
    c->attract = 12.0f * clampf(p[P_PH_ATTRACT] * 0.01f, 0.0f, 1.0f);
    int on = p[P_SYN_ON] > 0.5f ? c->balls | c->parts << 1 : 0;
    r->held = on && p[P_PH_KEYS] > 0.5f;
    if (on && !r->on) ph_restart_run(s, -1);   /* switched on: from the start */
    if (!on) r->nq = 0;                        /* off: nothing more plays */
    r->on = on;
}

/* KEY RESTARTS */
static void ph_key_on(one_t *s, int note) {
    if (s->ph.on && s->val[P_PH_RESTART] > 0.5f) ph_restart_run(s, note);
}

/* scale degree d (0: the root at LOW OCTAVE, or at the lowest held key) as a note in the key */
static int ph_note(const one_t *s, int d) {
    const float *p = s->val;
    int root = (int)clampf(p[P_KEY_ROOT], 0, 11), sc = ks_clamp_scale((int)p[P_KEY_SCALE]), note;
    if (s->ph.held && s->keys.n) {
        int low = s->keys.note[0];
        for (int i = 1; i < s->keys.n; i++) if (s->keys.note[i] < low) low = s->keys.note[i];
        note = low + ks_degree(root, sc, 0, d) - root;
    } else {
        note = ks_degree(root, sc, (int)clampf(p[P_PH_OCT], 0, 5) + 2, d);   /* C1 = 24 (middle C is C4) */
    }
    return ks_quantize(note, root, sc);
}

/* a hit's note and velocity */
static void ph_hit_note(const one_t *s, const ph_hit_t *h, int *note, int *vel) {
    const float *p = s->val;
    int size = KS_SIZE[ks_clamp_scale((int)p[P_KEY_SCALE])], steps = size * ((int)clampf(p[P_PH_RANGE], 0, 3) + 1) + 1;
    int d = (int)(h->x * steps), up = h->y > 0.5f ? size : 0;
    if (d >= steps) d = steps - 1;
    if (d < 0) d = 0;
    if (h->kind == PH_CEIL) d += size;
    else if (h->kind == PH_LEFT) d = up;
    else if (h->kind == PH_RIGHT) {   /* the fifth: the scale's 7 semitones up, else its middle degree */
        const uint8_t *st = KS_STEPS[ks_clamp_scale((int)p[P_KEY_SCALE])];
        int f = size / 2;
        for (int i = 0; i < size; i++) if (st[i] == 7) f = i;
        d = f + up;
    }
    *note = ph_note(s, d);
    const ph_cfg_t *c = &s->ph.cfg;
    float ref = fmaxf(c->speed, sqrtf(2.0f * c->gravity)), t = ref > 0.0f ? fminf(1.0f, h->speed / ref) : 1.0f;
    float dyn = clampf(p[P_PH_DYN] * 0.01f, 0.0f, 1.0f);
    *vel = (int)clampf(roundf(100.0f * (1.0f - dyn) + dyn * (25.0f + 102.0f * t)), 1.0f, 127.0f);
}

/* SYNC: the beat clock (the host's song position while it plays, else free at its tempo) */
static void ph_beat(one_t *s, int64_t t) {
    ph_run_t *r = &s->ph;
    double bpm = s->sp_bpm > 0.0 ? s->sp_bpm : (double)s->bpm, bps = bpm / 60.0 / SR;
    if (s->sp_playing && s->sp_ppq >= 0.0) {
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
static int64_t ph_on_grid(const one_t *s, int64_t at) {
    const ph_run_t *r = &s->ph;
    double g = PH_SYNC_BEATS[(int)clampf(s->val[P_PH_SYNC], 0, 6)];
    if (g <= 0.0 || r->bps0 <= 0.0) return at;
    double b = r->b0 + (double)(at - r->t0) * r->bps0, bq = ceil(b / g - 1e-9) * g;
    int64_t q = r->t0 + (int64_t)ceil((bq - r->b0) / r->bps0 - 1e-6);
    return q < at ? at : q;
}

static void ph_queue(one_t *s, int64_t at, int note, int vel, int sync) {
    ph_run_t *r = &s->ph;
    if (sync)   /* the same note on the same grid line plays once (the harder one) */
        for (int i = 0; i < r->nq; i++)
            if (r->q[i].at == at && r->q[i].note == note) {
                if (vel > r->q[i].vel) r->q[i].vel = (uint8_t)vel;
                return;
            }
    if (r->nq == PH_Q) return;   /* full (only with SYNC on a slow grid and a high MAX NOTES): dropped */
    r->q[r->nq].at = at;
    r->q[r->nq].note = (uint8_t)note;
    r->q[r->nq++].vel = (uint8_t)vel;
}

/* the window's view (seqlock: the counter odd while it changes) */
static void ph_view_write(ph_run_t *r) {
    uint32_t v = r->vseq;
    __atomic_store_n(&r->vseq, v + 1, __ATOMIC_RELAXED);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    int nb = r->cfg.balls ? r->cfg.nballs : 0, np = 0;
    for (int i = 0; i < nb; i++) {
        __atomic_store_n(&r->vx[i], (int16_t)(r->w.b[i].x * 1000.0f), __ATOMIC_RELAXED);
        __atomic_store_n(&r->vy[i], (int16_t)(r->w.b[i].y * 1000.0f), __ATOMIC_RELAXED);
        __atomic_store_n(&r->vf[i], (uint8_t)(r->flash[i] > 0), __ATOMIC_RELAXED);
    }
    for (int i = 0; r->cfg.parts && i < PH_PARTS; i++) {
        const ph_part_t *p = &r->w.p[i];
        if (!p->live) continue;
        __atomic_store_n(&r->vx[nb + np], (int16_t)(clampf(p->x, 0.0f, 1.0f) * 1000.0f), __ATOMIC_RELAXED);
        __atomic_store_n(&r->vy[nb + np], (int16_t)(clampf(p->y, 0.0f, 1.0f) * 1000.0f), __ATOMIC_RELAXED);
        __atomic_store_n(&r->vf[nb + np], (uint8_t)(r->flash[PH_BALLS + i] > 0), __ATOMIC_RELAXED);
        np++;
    }
    int wall = 0;
    for (int k = 0; k < 5; k++) wall |= (r->wall[k] > 0) << k;
    __atomic_store_n(&r->vnb, (uint8_t)nb, __ATOMIC_RELAXED);
    __atomic_store_n(&r->vnp, (uint8_t)np, __ATOMIC_RELAXED);
    __atomic_store_n(&r->vwall, (uint8_t)wall, __ATOMIC_RELAXED);
    __atomic_store_n(&r->vseq, v + 2, __ATOMIC_RELEASE);
}

/* "phys_view": "<balls> <particles> <lit walls>;" then "x,y,lit;" for each ball, then each particle (x, y: 0 .. 1000
 * across and up the box). Safe from any thread at any time (it never waits: after a few tries it gives what it has) */
static int ph_view_text(one_t *s, char *buf, int n) {
    ph_run_t *r = &s->ph;
    int16_t x[PH_VIEW], y[PH_VIEW];
    uint8_t f[PH_VIEW];
    int nb = 0, np = 0, wall = 0;
    for (int tries = 0; tries < 8; tries++) {
        uint32_t a = __atomic_load_n(&r->vseq, __ATOMIC_ACQUIRE);
        if (a & 1u) continue;
        nb = __atomic_load_n(&r->vnb, __ATOMIC_RELAXED);
        np = __atomic_load_n(&r->vnp, __ATOMIC_RELAXED);
        wall = __atomic_load_n(&r->vwall, __ATOMIC_RELAXED);
        if (nb > PH_BALLS) nb = PH_BALLS;
        if (np > PH_PARTS) np = PH_PARTS;
        for (int i = 0; i < nb + np; i++) {
            x[i] = __atomic_load_n(&r->vx[i], __ATOMIC_RELAXED);
            y[i] = __atomic_load_n(&r->vy[i], __ATOMIC_RELAXED);
            f[i] = __atomic_load_n(&r->vf[i], __ATOMIC_RELAXED);
        }
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if (__atomic_load_n(&r->vseq, __ATOMIC_RELAXED) == a) break;
    }
    if (!r->on) nb = np = wall = 0;
    int used = snprintf(buf, n, "%d %d %d;", nb, np, wall);
    for (int i = 0; i < nb + np && used < n - 20; i++) used += snprintf(buf + used, n - used, "%d,%d,%d;", x[i], y[i], f[i]);
    return used < n ? used : n - 1;
}

/* one step of the world at sample t: its hits become notes (capped, on their samples or the grid) */
static void ph_tick(one_t *s, int64_t t) {
    ph_run_t *r = &s->ph;
    const float *p = s->val;
    const float dt = (float)PH_STEP / SR;
    int sync = (int)p[P_PH_SYNC] > 0;
    for (int i = 0; i < PH_VIEW; i++) if (r->flash[i]) r->flash[i]--;
    for (int k = 0; k < 5; k++) if (r->wall[k]) r->wall[k]--;
    if (sync) ph_beat(s, t);
    if (!r->held || (s->running && s->keys.n)) {   /* (HELD KEYS with none held: it waits where it is) */
        r->tokens = fminf(r->tokens + clampf(p[P_PH_MAX], 1.0f, 30.0f) * dt, PH_BURST);
        ph_hit_t h[PH_HITS];
        int nh = ph_step(&r->w, &r->cfg, dt, h);
        for (int i = 0; i < nh; i++) {
            if (r->tokens < 1.0f) break;   /* over MAX NOTES/S: not played */
            r->tokens -= 1.0f;
            int note, vel;
            ph_hit_note(s, &h[i], &note, &vel);
            int64_t at = t + (int64_t)(h[i].frac * PH_STEP);
            ph_queue(s, sync ? ph_on_grid(s, at) : at, note, vel, sync);
            r->flash[h[i].kind <= PH_RIGHT ? h[i].who : PH_BALLS + h[i].who] = PH_FLASH;
            if (h[i].kind <= PH_LINE) r->wall[h[i].kind] = PH_FLASH;
        }
    }
    if (++r->ticks % PH_VIEW_EVERY == 0) ph_view_write(r);
}

/* at each piece of a render (src/synths.h render_to, through synth_due): the steps due, the notes due at this sample;
 * *n shortened to the next of either */
static void ph_due(one_t *s, int *n) {
    ph_run_t *r = &s->ph;
    for (int guard = 0; s->now >= r->t_next; guard++) {
        if (guard == 64) { r->t_next = s->now; break; }   /* (never: the pieces end at each step) */
        ph_tick(s, r->t_next);
        r->t_next += PH_STEP;
    }
    int64_t len = (int64_t)(ph_len_ms(s->val) * 0.001f * SR), d = r->t_next - s->now;
    for (int i = 0; i < r->nq;) {
        if (r->q[i].at <= s->now) {
            play_step(s, r->q[i].note, r->q[i].vel, s->now + (len > 1 ? len : 1));
            r->q[i] = r->q[--r->nq];
        } else {
            if (r->q[i].at - s->now < d) d = r->q[i].at - s->now;
            i++;
        }
    }
    if (d < 1) d = 1;
    if (d < *n) *n = (int)d;
}

/* the knobs shown in their own units */
static int ph_display(const one_t *s, int k, char *buf, int n) {
    const float *p = s->val;
    if (k == P_PH_RATE) return snprintf(buf, n, "%.1f /s", ph_rate(p));
    if (k == P_PH_LIFE) return snprintf(buf, n, "%.1f s", ph_life(p));
    if (k == P_PH_LEN) {
        float ms = ph_len_ms(p);
        return ms < 1000.0f ? snprintf(buf, n, "%.0f ms", ms) : snprintf(buf, n, "%.2f s", ms * 0.001f);
    }
    return 0;
}
