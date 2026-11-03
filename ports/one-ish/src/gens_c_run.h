/* ==== CHORDS, BASS, FOLLOW CHORDS (2026-11-02) ========================================================================
 * Included by engine.c after the other generators (it is part of engine.c and plays its notes the way they do: play_step,
 * so through chord memory and VELOCITY RANDOM, each note's end in the step clock's list). The harmony itself (progressions,
 * chords, voicing, bass lines) is src/gens_c.h.
 *
 * CHORDS plays a progression in its KEY and SCALE (chd_own_*; Follow: the Synth's), each chord RATE long (a decoration:
 * half), GATE of that, STRUM spreading its notes (up to 80 ms each), JAZZ as gens_c.h says, on the host's song position
 * while it plays, at GEN TEMPO. The first chord comes on the next beat; after it the changes fall on whole RATEs.
 * VOICING and INVERSIONS shape it: ROOT stays in root position, SMOOTH moves the fewest semitones, CLIMB and FALL do that
 * while their middle rises or falls an octave, then jumps back. KEYS: Free; Held Keys while keys are held, the key the
 * progression's root (FOLLOW KEYS); Key Launch: each key four chords from that key.
 *
 * BASS plays one note at a time on RATE: the root, root and fifth, octaves, an arpeggio or a walking line with approach
 * notes (PATTERN; Auto: by its JAZZ). FOLLOW (On for a new sound): on the chord CHORDS plays in any Synth; else, or with no
 * CHORDS on, on its own plain chords in its KEY and SCALE, a chord a bar.
 *
 * THE HARMONY shared by the Synths of an instance (hm_share_t, s->harm), audio thread only, no lock: the lowest Synth with
 * CHORDS on leads and writes it. CHORDS on a higher Synth plays the leader's chord in its own voicing, at its own rate.
 * FOLLOW CHORDS (gen_follow_chords, per Synth, Off for every sound old and new): every other generator of that Synth snaps
 * its notes to the nearest chord tone or tension as they play (engine.c gen_play), so also the notes that waited for a tempo
 * grid. Nothing to follow (no CHORDS on, or before its first chord): the notes stay as they are.
 *
 * THE ORDER within a block (synths.h render_to): each piece starts with every Synth's CHORDS, then every Synth's BASS, then
 * all the rest. So a generator or a bass note on the very sample of a chord change, in any Synth, already hears the new
 * chord, whichever Synth leads. A piece never runs past the next chord step, so this holds inside a block.
 *
 * FEED (chd_feed, bass_feed, per Synth, Off for every sound old and new; 2026-11-03, owner): what it plays also goes into the
 * other generators as if held on the keys, in this Synth or all (FEED TO): Play + Feed or Feed Only (not heard itself). Each
 * chord is held until the next one, whatever its GATE, so an arpeggio carries on through the change (the new notes
 * arrive before the old ones leave). Only a Synth with something that takes keys (ROUTE_SEQ: the arp, a generator) receives:
 * nothing else in it sounds. One way only: CHORDS and BASS never read what was fed (gc_hand_keys), so no loops. Keys by
 * hand and fed ones mix; a key by hand stays when the feed leaves.
 *
 * TIME SIGNATURE (time_sig, shared; bar_beats in engine.c): what "a bar" is for CHORD LENGTH's bars, BASS's own chords and the
 * pendulums' cycle. BASS's steps start again on each bar line, so 1/4s fit 7/8 (the last one is short). At 4/4 as before.
 *
 * Off (both): no work at all. Deterministic: each Synth has its own seeds. Nothing here allocates, locks or touches a file. */

static const float GC_RATE_BEATS[] = {16.0f, 8.0f, 4.0f, 2.0f, 1.0f, 0.5f};                          /* "chd_rate" */
static const double GC_BASS_BEATS[] = {2.0, 1.0, 0.5, 0.25, 2.0 / 3, 1.0 / 3};                       /* "bass_rate" */
#define GC_SEED_C 0x43484f52u
#define GC_SEED_B 0x42415353u
#define GC_SEED_W 0x57414c4bu
#define GC_LAUNCH_CHORDS 4
#define GC_LAUNCH_NOTES 8

static double gc_beat_at(const gc_run_t *r, int64_t t) { return r->b0 + (double)(t - r->t0) * r->bps0; }
static void gc_beat(one_t *s, int64_t t) {   /* the beat clock (the host's song position while it plays): as ga_beat */
    gc_run_t *r = &s->gc;
    double bps = gen_bpm(s) / 60.0 / SR;
    if (!gen_own(s) && s->sp_playing && s->sp_ppq >= 0.0) {
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
static int gc_hand_keys(const one_t *s) {   /* the keys held by hand: what was fed never drives CHORDS or BASS */
    int n = 0;
    for (int i = 0; i < s->keys.n; i++) n += !s->keys.fed[i];
    return n;
}
static int gc_lowest_key(const one_t *s) {
    int low = 127;
    for (int i = 0; i < s->keys.n; i++) if (!s->keys.fed[i] && s->keys.note[i] < low) low = s->keys.note[i];
    return low;
}

/* ---- the settings (after any param change) ---- */
static void gc_apply(one_t *s) {
    gc_run_t *r = &s->gc;
    const float *p = s->val;
    int on = p[P_SYN_ON] > 0.5f ? (p[P_CHD_ON] > 0.5f) * GC_CHD | (p[P_BASS_ON] > 0.5f) * GC_BASS : 0;
    int fresh = on & ~r->on;
    int cm = (int)clampf(p[P_CHD_KEYS], GK_FREE, GK_LAUNCH), bm = (int)clampf(p[P_BASS_KEYS], GK_FREE, GK_LAUNCH);
    r->cgo &= cm == GK_LAUNCH && r->chd_mode == GK_LAUNCH;
    r->bgo &= bm == GK_LAUNCH && r->bass_mode == GK_LAUNCH;
    r->chd_mode = cm;
    r->bass_mode = bm;
    r->takes = ((on & GC_CHD) && cm != GK_FREE) || ((on & GC_BASS) && bm != GK_FREE);
    if (on && !r->on) { r->t0 = s->now; r->b0 = 0.0; r->bps0 = 0.0; r->follow = 0; }
    int cs = (int)p[P_CHD_SEED], bs = (int)p[P_BASS_SEED];
    if ((fresh & GC_CHD) || ((on & GC_CHD) && cs != r->cseed)) {
        r->cseed = cs;
        hm_seq_reset(&r->seq, ga_seed(s, GC_SEED_C, cs));
        r->nprev = 0;
        r->cfirst = 1;
        r->cwas = 0;
        r->ncq = 0;
        r->centre = 0.0f;
        r->ct_next = s->now;
    }
    if ((fresh & GC_BASS) || ((on & GC_BASS) && bs != r->bseed)) {
        r->bseed = bs;
        memset(&r->bl, 0, sizeof r->bl);
        r->bl.rng = ga_seed(s, GC_SEED_B, bs);
        r->bl.last = -1;
        hm_seq_reset(&r->bw, ga_seed(s, GC_SEED_W, bs));
        r->bgen = 0;
        r->bfirst = 1;
        r->bwas = 0;
        r->bwend = -1.0;
        r->nbq = 0;
        r->bt_next = s->now;
    }
    if (!(on & GC_CHD)) { r->ncq = 0; r->cgo = 0; }
    if (!(on & GC_BASS)) { r->nbq = 0; r->bgo = 0; }
    r->on = on;
}

/* a key: KEY LAUNCH starts from it (the same key, the same phrase) */
static void gc_key_on(one_t *s, int note) {
    gc_run_t *r = &s->gc;
    if ((r->on & GC_CHD) && r->chd_mode == GK_LAUNCH) {
        hm_seq_reset(&r->seq, ga_seed(s, GC_SEED_C ^ 0x4b455953u, note));
        r->ckey = note;
        r->cgo = 1;
        r->cleft = GC_LAUNCH_CHORDS;
        r->cfirst = 1;
        r->canchor = gc_beat_at(r, s->now);
    }
    if ((r->on & GC_BASS) && r->bass_mode == GK_LAUNCH) {
        hm_seq_reset(&r->bw, ga_seed(s, GC_SEED_W ^ 0x4b455953u, note));
        r->bl.rng = ga_seed(s, GC_SEED_B ^ 0x4b455953u, note);
        r->bl.last = -1;
        r->bkey = note;
        r->bgo = 1;
        r->bleft = GC_LAUNCH_NOTES;
        r->bfirst = 1;
        r->bwend = -1.0;
        r->banchor = gc_beat_at(r, s->now);
    }
}

/* ---- the window's views (seqlocks: each counter odd while it changes) ---- */
static void gc_view_chord(gc_run_t *r, const char *name, const uint8_t *notes, int n) {
    uint32_t v = r->cvseq;
    __atomic_store_n(&r->cvseq, v + 1, __ATOMIC_RELAXED);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    for (int k = GC_NAMES - 1; k > 0; k--)
        for (int i = 0; i < GC_NAME_LEN; i++)
            __atomic_store_n(&r->vname[k][i], __atomic_load_n(&r->vname[k - 1][i], __ATOMIC_RELAXED), __ATOMIC_RELAXED);
    for (int i = 0; i < GC_NAME_LEN; i++) __atomic_store_n(&r->vname[0][i], name[i], __ATOMIC_RELAXED);
    for (int i = 0; i < n; i++) __atomic_store_n(&r->vnotes[i], notes[i], __ATOMIC_RELAXED);
    __atomic_store_n(&r->vnn, (uint8_t)n, __ATOMIC_RELAXED);
    __atomic_store_n(&r->cvseq, v + 2, __ATOMIC_RELEASE);
}
static void gc_view_bass(gc_run_t *r, int note) {
    uint32_t v = r->bvseq;
    __atomic_store_n(&r->bvseq, v + 1, __ATOMIC_RELAXED);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    for (int i = GC_BASS_HIST - 1; i > 0; i--)
        __atomic_store_n(&r->vbh[i], __atomic_load_n(&r->vbh[i - 1], __ATOMIC_RELAXED), __ATOMIC_RELAXED);
    __atomic_store_n(&r->vbh[0], (uint8_t)note, __ATOMIC_RELAXED);
    if (r->vbn < GC_BASS_HIST) __atomic_store_n(&r->vbn, (uint8_t)(r->vbn + 1), __ATOMIC_RELAXED);
    __atomic_store_n(&r->bvseq, v + 2, __ATOMIC_RELEASE);
}
/* the views as text, safe from any thread at any time (it never waits: after a few tries it gives what it has):
 *   "chd_view":  "<on> <notes>;" the chord now and the four before, each with ";", then the notes with ","
 *   "bass_view": "<on> <notes>;" its last notes, the newest first, with "," */
static int gc_view_text(one_t *s, int which, char *buf, int n) {
    gc_run_t *r = &s->gc;
    int used = 0;
    if (which == GC_CHD) {
        char nm[GC_NAMES][GC_NAME_LEN];
        uint8_t no[HM_NOTES];
        int nn = 0;
        for (int tries = 0; tries < 8; tries++) {
            uint32_t a = __atomic_load_n(&r->cvseq, __ATOMIC_ACQUIRE);
            if (a & 1u) continue;
            for (int k = 0; k < GC_NAMES; k++)
                for (int i = 0; i < GC_NAME_LEN; i++) nm[k][i] = __atomic_load_n(&r->vname[k][i], __ATOMIC_RELAXED);
            nn = __atomic_load_n(&r->vnn, __ATOMIC_RELAXED);
            if (nn > HM_NOTES) nn = HM_NOTES;
            for (int i = 0; i < nn; i++) no[i] = __atomic_load_n(&r->vnotes[i], __ATOMIC_RELAXED);
            __atomic_thread_fence(__ATOMIC_ACQUIRE);
            if (__atomic_load_n(&r->cvseq, __ATOMIC_RELAXED) == a) break;
        }
        int on = (r->on & GC_CHD) != 0;
        used = snprintf(buf, n, "%d %d;", on, on ? nn : 0);
        for (int k = 0; k < GC_NAMES && used < n - GC_NAME_LEN - 2; k++) {
            nm[k][GC_NAME_LEN - 1] = 0;
            used += snprintf(buf + used, n - used, "%s;", on ? nm[k] : "");
        }
        for (int i = 0; on && i < nn && used < n - 8; i++) used += snprintf(buf + used, n - used, "%d,", no[i]);
    } else {
        uint8_t h[GC_BASS_HIST];
        int nb = 0;
        for (int tries = 0; tries < 8; tries++) {
            uint32_t a = __atomic_load_n(&r->bvseq, __ATOMIC_ACQUIRE);
            if (a & 1u) continue;
            nb = __atomic_load_n(&r->vbn, __ATOMIC_RELAXED);
            if (nb > GC_BASS_HIST) nb = GC_BASS_HIST;
            for (int i = 0; i < nb; i++) h[i] = __atomic_load_n(&r->vbh[i], __ATOMIC_RELAXED);
            __atomic_thread_fence(__ATOMIC_ACQUIRE);
            if (__atomic_load_n(&r->bvseq, __ATOMIC_RELAXED) == a) break;
        }
        int on = (r->on & GC_BASS) != 0;
        used = snprintf(buf, n, "%d %d;", on, on ? nb : 0);
        for (int i = 0; on && i < nb && used < n - 8; i++) used += snprintf(buf + used, n - used, "%d,", h[i]);
    }
    return used < n ? used : n - 1;
}
/* the chords since the last read, for the tests: "<sample>:<root>:<tones>:<tensions>:<name>;" */
static int gc_log_text(one_t *s, char *buf, int n) {
    gc_run_t *r = &s->gc;
    int used = 0;
    buf[0] = 0;
    while (r->log_n && used < n - 48) {
        const __typeof__(r->log[0]) *l = &r->log[r->log_head];
        used += snprintf(buf + used, n - used, "%lld:%d:%d:%d:%s;", (long long)l->t, l->root, l->tones, l->tens, l->name);
        r->log_head = (r->log_head + 1) % GC_LOG;
        r->log_n--;
    }
    return used;
}

/* ---- FEED: what plays, into the generators as keys ---- */
static void gc_feed_load(gc_run_t *r, int k, int64_t at, int64_t until, const uint8_t *notes, int n, int vel) {
    r->fd_at[k] = at;
    r->fd_until[k] = until + 2 * GC_STEP;   /* (the next one arrives first; none does: it leaves) */
    r->fd_n[k] = n;
    r->fd_vel[k] = vel;
    memcpy(r->fd_note[k], notes, (size_t)n);
    r->fd_wait[k] = 1;
}
static int gc_feed_busy(const one_t *s) {
    return s->val[P_CHD_FEED] > 0.5f || s->val[P_BASS_FEED] > 0.5f || s->gc.fd_held[0] || s->gc.fd_held[1];
}
static int gc_note_in(const uint8_t *notes, int n, int note) {
    for (int i = 0; i < n; i++) if (notes[i] == note) return 1;
    return 0;
}
/* the notes one sender holds down in Synth t now become these (none: it lets go). New ones first: the keys never run out
 * between two chords, so an arpeggio goes on */
static void gc_feed_to(one_t *t, int slot, const uint8_t *notes, int n, int vel) {
    uint8_t *old = t->fed_note[slot];
    int nold = t->fed_n[slot];
    if (t->route != ROUTE_SEQ || t->val[P_SYN_ON] < 0.5f) n = 0;
    for (int i = 0; i < n; i++) {
        if (gc_note_in(old, nold, notes[i])) continue;
        ph_key_on(t, notes[i], vel);
        gb_key_on(t, notes[i], vel);
        ga_key_on(t, notes[i]);
        if (sq_key_on(t, notes[i], vel)) continue;   /* NOTE TRIG / TRANSPOSE: it moves the sequencer, as a key does */
        int at = seq_find(&t->keys, notes[i]);
        if (at >= 0 && !t->keys.fed[at]) continue;   /* (held by hand: it stays the hand's) */
        seq_key_on(t, notes[i], vel);
        t->keys.fed[t->keys.n - 1] = 1;
    }
    for (int i = 0; i < nold; i++) {
        int note = old[i], other = gc_note_in(notes, n, note);
        for (int o = 0; o < 6 && !other; o++) other = o != slot && gc_note_in(t->fed_note[o], t->fed_n[o], note);
        if (other || sq_key_off(t, note)) continue;
        int at = seq_find(&t->keys, note);
        if (at < 0 || !t->keys.fed[at]) continue;
        t->phys -= t->keys.down[at];
        seq_remove(&t->keys, at);
        t->adirty = 1;
        if (t->keys.n == 0 && !(t->seq_on && t->gk == GK_LAUNCH)) t->running = 0;
    }
    memcpy(old, notes, (size_t)n);
    t->fed_n[slot] = (uint8_t)n;
}
static void gc_feed_due(one_t *const *syn, int nsyn, int *np) {
    static const int ON[2] = {GC_CHD, GC_BASS}, FEED[2] = {P_CHD_FEED, P_BASS_FEED}, TO[2] = {P_CHD_FEED_TO, P_BASS_FEED_TO};
    for (int c = 0; c < nsyn; c++) {
        one_t *s = syn[c];
        gc_run_t *r = &s->gc;
        for (int k = 0; k < 2; k++) {
            int active = (r->on & ON[k]) && s->val[FEED[k]] > 0.5f, send = 0;
            if (active && r->fd_wait[k]) {
                int64_t d = r->fd_at[k] - s->now;
                if (d <= 0) send = 1;
                else if (d < *np) *np = (int)d;
            }
            int leave = r->fd_held[k] && !send && (!active || s->now > r->fd_until[k]);
            if (!send && !leave) continue;
            int all = s->val[TO[k]] > 0.5f;
            for (int t = 0; t < nsyn; t++) {
                int to = send && (all || t == c);
                gc_feed_to(syn[t], c * 2 + k, r->fd_note[k], to ? r->fd_n[k] : 0, r->fd_vel[k]);
            }
            r->fd_wait[k] = 0;
            r->fd_held[k] = send;
        }
    }
}

/* ---- CHORDS ---- */
static void gc_chord_fire(one_t *s, int64_t at, int64_t len, double beats) {
    gc_run_t *r = &s->gc;
    const float *p = s->val;
    hm_share_t *h = s->harm;
    float jazz = clampf(p[P_CHD_JAZZ] * 0.01f, 0.0f, 1.0f);
    (void)beats;
    if (h->leader >= 0 && h->leader != s->idx) {
        if (!h->valid) return;   /* (the leader has not played yet) */
        r->cur = h->cur;   /* a higher Synth: the leader's chord */
    } else {
        int key = r->chd_mode == GK_LAUNCH ? r->ckey : r->chd_mode == GK_HELD && gc_hand_keys(s) ? gc_lowest_key(s) : -1;
        int tonic = gen_root_of(s, key, P_CHD_OWN_KEY), scale = gen_scale_in(p, P_CHD_OWN_KEY), next = 0;
        hm_item_t it = hm_seq_next(&r->seq, jazz, scale, &next);
        hm_build(&r->cur, &it, tonic, scale, jazz);
        h->cur = r->cur;
        h->next_root = (tonic + next) % 12;
        h->t_change = at + len;
        h->gen++;
        h->valid = 1;
    }
    int oct = (int)clampf(p[P_CHD_OCT], 0, 5) + 2, mode = (int)clampf(p[P_CHD_INV], 0, 3);
    float move = mode == HI_CLIMB ? 2.0f : mode == HI_FALL ? -2.0f : 0.0f;
    r->centre += move;
    if (r->centre > 12.0f || r->centre < -12.0f) r->centre = 0.0f;   /* (an octave on: back to the middle) */
    uint8_t notes[HM_NOTES];
    int n = hm_voice(&r->cur, (int)clampf(p[P_CHD_VOICING], 0, 3), mode, 12 * oct + 10 + (int)r->centre, r->prev, r->nprev, notes);
    memcpy(r->prev, notes, (size_t)n);
    r->nprev = n;
    int vel = (int)clampf(roundf(p[P_CHD_VEL]), 1.0f, 127.0f);
    int64_t off = at + (int64_t)((double)len * clampf(p[P_CHD_GATE] * 0.01f, 0.05f, 1.0f));
    if (off > at + len - 2) off = at + len - 2;   /* (it ends before the next one starts: no tie to a note the same) */
    if (off <= at) off = at + 1;
    int64_t strum = (int64_t)(clampf(p[P_CHD_STRUM], 0.0f, 100.0f) * 0.8f * 0.001f * SR);
    if (p[P_CHD_FEED] > 0.5f) gc_feed_load(r, 0, at, at + len, notes, n, vel);
    for (int i = 0; i < n && r->ncq < GC_Q && p[P_CHD_FEED] < 1.5f; i++) {
        int64_t t = at + strum * i;
        r->cq[r->ncq++] = (gc_note_t){t, off > t ? off : t + 1, notes[i], (uint8_t)vel};
    }
    char name[GC_NAME_LEN];
    memset(name, 0, sizeof name);
    hm_name(&r->cur, name, sizeof name);
    gc_view_chord(r, name, notes, n);
    int li = r->log_n < GC_LOG ? (r->log_head + r->log_n++) % GC_LOG : r->log_head;
    if (li == r->log_head && r->log_n == GC_LOG) r->log_head = (r->log_head + 1) % GC_LOG;
    r->log[li].t = at;
    r->log[li].root = r->cur.root;
    r->log[li].tones = r->cur.tones;
    r->log[li].tens = r->cur.tens;
    memcpy(r->log[li].name, name, GC_NAME_LEN);
}
static void gc_chord_tick(one_t *s, int64_t t) {
    gc_run_t *r = &s->gc;
    const float *p = s->val;
    gc_beat(s, t);
    if (r->bps0 <= 0.0) return;
    double b0 = gc_beat_at(r, t), b1 = gc_beat_at(r, t + GC_STEP), rate = GC_RATE_BEATS[(int)clampf(p[P_CHD_RATE], 0, 5)];
    if ((int)clampf(p[P_CHD_RATE], 0, 5) < 3) rate *= bar_beats(s) / 4.0;   /* (its bars, in the TIME SIGNATURE) */
    if (b0 < r->cblast - 0.5 || b0 > r->cblast + 1.0) r->cfirst = 1;   /* (the song position jumped) */
    r->cblast = b1;
    if (r->chd_mode == GK_HELD) {
        int keys = s->running && gc_hand_keys(s);
        if (!keys) { r->cwas = 0; return; }
        if (!r->cwas) { r->cwas = 1; r->cfirst = 1; r->canchor = b0; }
    } else if (r->chd_mode == GK_LAUNCH) {
        if (!r->cgo) return;
    } else {
        r->canchor = 0.0;
    }
    if (r->cfirst) {   /* free: the next beat; from a key: now */
        double g = rate < 1.0 ? rate : 1.0, x = b0 - r->canchor;
        r->cnext = r->canchor + ceil(x / g - 1e-9) * g;
    }
    if (r->cnext >= b1) return;
    double frac = (r->cnext - b0) / (b1 - b0), at_beat = r->cnext;
    int64_t at = t + (int64_t)(clampf((float)frac, 0.0f, 0.999f) * GC_STEP);
    double len;
    if (r->cfirst) {   /* to the next whole RATE; a very short first chord stays on for the one after */
        double x = at_beat - r->canchor, next = r->canchor + (floor(x / rate + 1e-9) + 1.0) * rate;
        len = next - at_beat;
        if (len < rate * 0.25) len += rate;
        r->cfirst = 0;
    } else {
        /* a fast item (a decoration): half of RATE; the item's mark is read here, before it is taken */
        int leader = !(s->harm->leader >= 0 && s->harm->leader != s->idx);
        hm_seq_fill(&r->seq, clampf(p[P_CHD_JAZZ] * 0.01f, 0.0f, 1.0f), gen_scale_in(p, P_CHD_OWN_KEY));
        len = leader && (r->seq.q[r->seq.pos].flags & HF_FAST) ? rate * 0.5 : rate;
    }
    r->cnext = at_beat + len;
    gc_chord_fire(s, at, (int64_t)(len / r->bps0), len);
    if (r->chd_mode == GK_LAUNCH && --r->cleft <= 0) r->cgo = 0;
}

/* ---- BASS ---- */
static void gc_bass_step(one_t *s, int64_t at, int64_t step, double beat) {
    gc_run_t *r = &s->gc;
    const float *p = s->val;
    const hm_share_t *h = s->harm;
    float adv = clampf(p[P_BASS_JAZZ] * 0.01f, 0.0f, 1.0f);
    int pat = (int)clampf(p[P_BASS_PATTERN], 0, 5), lo = 12 * ((int)clampf(p[P_BASS_OCT], 0, 5) + 2);
    int key = r->bass_mode == GK_LAUNCH ? r->bkey : r->bass_mode == GK_HELD && gc_hand_keys(s) ? gc_lowest_key(s) : -1;
    int tonic = gen_root_of(s, key, P_BASS_OWN_KEY), scale = gen_scale_in(p, P_BASS_OWN_KEY);
    const hm_chord_t *c;
    int next, left;
    if (p[P_BASS_FOLLOW] > 0.5f && h->valid) {
        if (h->gen != r->bgen) { r->bgen = h->gen; hm_bass_chord(&r->bl, pat, adv); }
        c = &h->cur;
        next = h->next_root;
        int64_t d = h->t_change - at;
        left = d > 0 ? (int)((d + step - 1) / step) : 0;
    } else {   /* its own plain chords, a bar each */
        r->bgen = 0;
        if (beat >= r->bwend - 1e-6) {
            hm_item_t it = hm_seq_next(&r->bw, 0.0f, scale, &r->bwnext);
            hm_build(&r->bwc, &it, tonic, scale, 0.0f);
            r->bwend = (floor(beat / bar_beats(s) + 1e-9) + 1.0) * bar_beats(s);
            hm_bass_chord(&r->bl, pat, adv);
        }
        c = &r->bwc;
        next = (tonic + r->bwnext) % 12;
        left = r->bps0 > 0.0 ? (int)ceil((r->bwend - beat) * (1.0 / r->bps0) / (double)step - 1e-6) : 0;
    }
    if (r->bl.last < 0) r->bl.last = lo + ((c->root - lo) % 12 + 12) % 12;
    int note = hm_bass_next(&r->bl, c, next, left, lo, adv, hm_scale_mask(tonic, scale));
    int vel = (int)clampf(roundf(p[P_BASS_VEL]) - (r->bl.pos == 1 ? 0.0f : 8.0f), 1.0f, 127.0f);   /* (a chord's first the loudest) */
    int64_t off = at + (int64_t)((double)step * clampf(p[P_BASS_GATE] * 0.01f, 0.05f, 1.0f));
    if (off > at + step - 2) off = at + step - 2;   /* (one note at a time) */
    if (off <= at) off = at + 1;
    if (p[P_BASS_FEED] > 0.5f) {
        uint8_t one = (uint8_t)note;
        gc_feed_load(r, 1, at, at + step, &one, 1, vel);
    }
    if (r->nbq < 2 && p[P_BASS_FEED] < 1.5f) r->bq[r->nbq++] = (gc_note_t){at, off, (uint8_t)note, (uint8_t)vel};
    gc_view_bass(r, note);
}
static void gc_bass_tick(one_t *s, int64_t t) {
    gc_run_t *r = &s->gc;
    gc_beat(s, t);
    if (r->bps0 <= 0.0) return;
    double b0 = gc_beat_at(r, t), b1 = gc_beat_at(r, t + GC_STEP), step = GC_BASS_BEATS[(int)clampf(s->val[P_BASS_RATE], 0, 5)];
    if (b0 < r->bblast - 0.5 || b0 > r->bblast + 1.0) r->bfirst = 1;
    r->bblast = b1;
    if (r->bass_mode == GK_HELD) {
        int keys = s->running && gc_hand_keys(s);
        if (!keys) { r->bwas = 0; return; }
        if (!r->bwas) { r->bwas = 1; r->bfirst = 1; r->banchor = b0; }
    } else if (r->bass_mode == GK_LAUNCH) {
        if (!r->bgo) return;
    } else {
        r->banchor = 0.0;
    }
    if (r->bfirst) {
        r->bnext = r->banchor + ceil((b0 - r->banchor) / step - 1e-9) * step;
        r->bfirst = 0;
    }
    if (r->bnext >= b1) return;
    double frac = (r->bnext - b0) / (b1 - b0), beat = r->bnext;
    int64_t at = t + (int64_t)(clampf((float)frac, 0.0f, 0.999f) * GC_STEP);
    r->bnext += step;
    if (bar_beats(s) != 4.0) {   /* the steps start again on each bar line: the last of a bar may be shorter */
        double bar = bar_beats(s), line = r->banchor + (floor((beat - r->banchor) / bar + 1e-9) + 1.0) * bar;
        if (r->bnext > line - 1e-6) { r->bnext = line; step = line - beat; }
    }
    gc_bass_step(s, at, (int64_t)(step / r->bps0), beat);
    if (r->bass_mode == GK_LAUNCH && --r->bleft <= 0) r->bgo = 0;
}

/* the notes due at this sample, from a queue; returns the samples to its next one (or d) */
static int64_t gc_play_due(one_t *s, gc_note_t *q, int *nq, int64_t d, int gone) {
    for (int i = 0; i < *nq;) {
        if (q[i].at <= s->now || gone) {
            if (!gone) play_step(s, q[i].note, q[i].vel, q[i].off > s->now ? q[i].off : s->now + 1);
            q[i] = q[--*nq];
        } else {
            if (q[i].at - s->now < d) d = q[i].at - s->now;
            i++;
        }
    }
    return d;
}
/* at the start of each piece of a render (synths.h render_to), every Synth's CHORDS, then every Synth's BASS */
static void gc_chords_due(one_t *s, int *n) {
    gc_run_t *r = &s->gc;
    for (int guard = 0; s->now >= r->ct_next; guard++) {
        if (guard == 64) { r->ct_next = s->now; break; }
        gc_chord_tick(s, r->ct_next);
        r->ct_next += GC_STEP;
    }
    int64_t d = gc_play_due(s, r->cq, &r->ncq, r->ct_next - s->now, 0);
    if (d < 1) d = 1;
    if (d < *n) *n = (int)d;
}
static void gc_bass_due(one_t *s, int *n) {
    gc_run_t *r = &s->gc;
    for (int guard = 0; s->now >= r->bt_next; guard++) {
        if (guard == 64) { r->bt_next = s->now; break; }
        gc_bass_tick(s, r->bt_next);
        r->bt_next += GC_STEP;
    }
    int64_t d = gc_play_due(s, r->bq, &r->nbq, r->bt_next - s->now, 0);
    if (d < 1) d = 1;
    if (d < *n) *n = (int)d;
}
