/* ==== THE SYNTHS: three timbres in one instance (multitimbral) ======================================================
 * Included at the end of engine.c (part of it: it uses the per-Synth functions above). After the Moog One manual (p8-9,
 * p14-15, p63-66, p96-101, p129): a preset holds up to three Synths, each a whole sound (oscillators to synth effect,
 * its own arpeggiator, chord memory, generative steps and sequencer) with its own setup: ON, MIDI channel, key range
 * (LOW / HIGH: splits and layers), transpose and octave, poly count, level, pan and the two sends. The master effects
 * (two slots on two sends), master swing, tuning, the MIDI and chord-learn settings, master level and PLAY ALL are
 * shared: one copy, kept in Synth 1's values and mirrored into the others' so their code reads it in place.
 *
 * Params: a Synth's keys as one_t knows them (KEYS) are Synth 1's; Synths 2 and 3 have the same keys prefixed "s2_" and
 * "s3_" (presets/make_synths.py writes them into params.json; the shared ones have no copies). So every project, patch
 * and preset made before multitimbral restores as Synth 1, with Synths 2 and 3 off, sample for sample.
 *
 * Voices: one pool of POOL_VOICES (16) shared by the Synths. Each Synth allocates within its own poly count as before;
 * when that would take the pool past 16 sounding voices, voices of any Synth are stolen to make room (the quietest
 * released ones first, then by the new note's Synth's VOICE STEALING MODE; with No Stealing the new note is
 * dropped). A stolen voice fades out in a few ms (no click). Ages are counted across the Synths.
 *
 * Audio: each live Synth renders its voices and its synth effect; the master effects run once on the sum of every
 * Synth's sends (fx.h fx_master_multi), and the master level and the limiter follow. A Synth that is off and silent
 * costs nothing (its counters move on). The Synths render in lockstep, piece by piece (the control steps line up), so
 * with Synths 2 and 3 off Synth 1 does exactly the arithmetic of the single-Synth engine.
 * Nothing here allocates, locks or touches a file in render() or midi(). */

#ifndef ONEISH_SYNTHS
#define ONEISH_SYNTHS 3     /* Synths per instance: 3 (the instrument's); a build may set 1 (vst.json "defines") */
#endif
#define NSYN ONEISH_SYNTHS
_Static_assert(NSYN >= 1 && NSYN <= 3, "one to three Synths");
#define FX_TAIL_S 4         /* a Synth switched off keeps its synth effect ringing this long, then it stops */

typedef struct { const char *key; signed char syn; short slot; } pkey_t;   /* a param key: whose, which slot */

typedef struct inst {
    one_t *syn[NSYN];
    fx_mst_t mfx;            /* the master effects (shared) */
    unsigned age;            /* note starts across the Synths (voice ages) */
    int heal;                /* a non-finite sample reached the output: heal every Synth and the master */
    pkey_t keys[NPARAMS + P_COUNT];   /* every key the host or a state can name, sorted, and how many */
    int nkeys;
    char state_buf[STATE_MAX];   /* set_state's working copy (allocated with the instance, not per call) */
    float ext[128];          /* the next render's audio input, mono (one_ish_set_input); ext_n frames of it, 0: silence */
    int ext_n;
} inst_t;

/* the shared params: one copy, in Synth 1's values (presets/make_synths.py GLOBAL keeps the same list) */
static int engine_global(int k) {
    static const char *const SHARED[] = {"mfx_on", "mfx_series", "mfx_width", "mfx_buss", "clock_swing", "master_tune",
                                         "fine_tune", "midi_ch", "learn_mode", "master_level", "seq_all"};
    const char *key = KEYS[k];
    if (!strncmp(key, "dly_", 4) || !strncmp(key, "rev_", 4)) return 1;   /* the master effect slots' controls */
    for (unsigned i = 0; i < sizeof SHARED / sizeof SHARED[0]; i++)
        if (!strcmp(key, SHARED[i])) return 1;
    return 0;
}

/* ---- keys: "s2_osc..." is Synth 2's "osc...", the rest Synth 1's (or shared) ---- */
static int key_synth(const char *key, const char **rest) {
    if (key[0] == 's' && key[1] >= '2' && key[1] <= '9' && key[2] == '_') { *rest = key + 3; return key[1] - '1'; }
    *rest = key;
    return 0;
}
static int pkey_cmp(const void *a, const void *b) { return strcmp(((const pkey_t *)a)->key, ((const pkey_t *)b)->key); }
static const pkey_t *key_find(const inst_t *in, const char *key) {
    pkey_t want = {key, 0, 0};
    return bsearch(&want, in->keys, (size_t)in->nkeys, sizeof want, pkey_cmp);
}
/* every PARAMS key (Synth 2 and 3's copies too) and every KEYS name, with its Synth and slot; and each Synth's PARAMS
 * entry per slot (pidx: a shared slot reads Synth 1's) */
static void keys_init(inst_t *in, short pidx[NSYN][P_COUNT]) {
    for (int c = 0; c < NSYN; c++)
        for (int k = 0; k < P_COUNT; k++) pidx[c][k] = -1;
    in->nkeys = 0;
    for (int i = 0; i < NPARAMS; i++) {
        const char *rest;
        int c = key_synth(PARAMS[i].key, &rest), k = slot(rest);
        if (c >= NSYN || (c > 0 && k >= 0 && engine_global(k))) k = -1;   /* (a copy of a shared param: none) */
        in->keys[in->nkeys++] = (pkey_t){PARAMS[i].key, (signed char)(c < NSYN ? c : 0), (short)k};
        if (k >= 0) pidx[c][k] = (short)i;
    }
    for (int k = 0; k < P_COUNT; k++) {   /* a KEYS name params.json lacks (none should): still settable, as before */
        if (!KEYS[k][0] || pidx[0][k] >= 0) continue;
        in->keys[in->nkeys++] = (pkey_t){KEYS[k], 0, (short)k};
    }
    for (int c = 1; c < NSYN; c++)
        for (int k = 0; k < P_COUNT; k++)
            if (engine_global(k) || pidx[c][k] < 0) pidx[c][k] = pidx[0][k];
    qsort(in->keys, (size_t)in->nkeys, sizeof in->keys[0], pkey_cmp);
}

/* ---- the voice pool ---- */
static int voice_busy(const voice_t *v) { return v->env[ENV_AMP].stage != ENV_IDLE && !v->dying; }

/* make room in the pool for the voices idx[0..n-1] of Synth s about to start (those not sounding yet): steal voices of
 * any Synth past POOL_VOICES. 0: no room (No Stealing and nothing released), the note is dropped. */
static int pool_room(one_t *s, const int *idx, int n) {
    inst_t *in = s->in;
    int need = 0, busy = 0;
    uint32_t mine = 0;
    for (int k = 0; k < n; k++) {
        mine |= 1u << idx[k];
        need += !voice_busy(&s->voice[idx[k]]);
    }
    if (!need) return 1;
    for (int c = 0; c < NSYN; c++)
        for (int i = 0; i < MAX_VOICES; i++) busy += voice_busy(&in->syn[c]->voice[i]);
    int mode = (int)s->val[P_STEAL_MODE];
    for (int over = busy + need - POOL_VOICES; over > 0; over--) {
        voice_t *best = NULL;
        int pass;
        for (pass = 0; pass < 3 && !best; pass++) {   /* released (quietest); held by a pedal or HOLD; any held */
            if (pass > 0 && mode == STEAL_NONE) break;
            for (int c = 0; c < NSYN; c++)
                for (int i = 0; i < MAX_VOICES; i++) {
                    voice_t *v = &in->syn[c]->voice[i];
                    if (!voice_busy(v) || (in->syn[c] == s && (mine >> i & 1))) continue;
                    if (pass == 0 && v->gate) continue;
                    if (pass == 1 && (!v->sustained || mode != STEAL_OLDEST)) continue;
                    if (!best) { best = v; continue; }
                    float lv = v->env[ENV_AMP].level, lb = best->env[ENV_AMP].level;
                    int better = pass == 0 ? lv < lb
                               : mode == STEAL_QUIETEST ? lv * v->gain < lb * best->gain
                               : mode == STEAL_NEWEST ? v->age > best->age : v->age < best->age;
                    if (better) best = v;
                }
        }
        if (!best) return 0;
        for (int e = 0; e < NENV; e++) env_kill(&best->env[e]);   /* out in a few ms, no click */
        env_kill(&best->nenv);
        best->gate = best->sustained = best->sost = 0;
        best->dying = 1;
    }
    return 1;
}

/* ---- MIDI: each message to the Synths it is for ---- */
enum { CH_OMNI = 0, CH_PER_SYNTH = 17 };   /* "midi_ch": Omni, 1 .. 16, Per Synth */
static int chan_ok(const inst_t *in, int c, int ch) {
    int mode = (int)in->syn[0]->val[P_MIDI_CH];
    if (mode == CH_OMNI) return 1;
    if (mode != CH_PER_SYNTH) return ch == mode - 1;
    int own = (int)in->syn[c]->val[P_SYN_CH];   /* "syn_ch": Omni, 1 .. 16 */
    return own == 0 || ch == own - 1;
}
static void midi(void *inst, const uint8_t *m, int len) {
    inst_t *in = inst;
    if (len < 2 || m[0] >= 0xf0) return;
    int st = m[0] & 0xf0, ch = m[0] & 0x0f, note = m[1] & 0x7f, vel = len > 2 ? m[2] & 0x7f : 0;
    for (int c = 0; c < NSYN; c++) {
        one_t *s = in->syn[c];
        if (!chan_ok(in, c, ch)) continue;
        if (st == 0x90 && len > 2 && vel) {   /* a key: to the Synths on, whose range has it */
            if (s->val[P_SYN_ON] < 0.5f || note < s->val[P_KEY_LO] || note > s->val[P_KEY_HI]) continue;
            s->kin[note] = 1;
        } else if ((st == 0x80 || st == 0x90) && len > 2) {   /* its release: where the key went */
            if (!s->kin[note]) continue;
            s->kin[note] = 0;
        } else if (st == 0xb0 && len > 2 && (note == 120 || note == 123)) {
            memset(s->kin, 0, sizeof s->kin);
        }
        synth_midi(s, m, len);
    }
}

/* ---- params ---- */
/* a Synth switched on or off (k: its slot, -1 a whole state): off lets go of everything it plays */
static void synth_switch(one_t *s) {
    int on = s->val[P_SYN_ON] > 0.5f;
    if (s->on && !on) {
        gen_stop(s);
        sq_stop(s);
        all_off(s, 0);
        memset(s->kin, 0, sizeof s->kin);
        if (s->val[P_SEQ_PLAY] > 0.5f) { s->val[P_SEQ_PLAY] = 0.0f; s->changed |= 1u << SQ_CHG_PLAY; }
    }
    s->on = on;
}

/* PLAY ALL: every Synth that is on with its sequencer ON starts (or stops) its sequence, all on the same sample */
static void seq_all(inst_t *in) {
    int play = in->syn[0]->val[P_SEQ_ALL] > 0.5f;
    for (int c = 0; c < NSYN; c++) {
        one_t *s = in->syn[c];
        if (play && (s->val[P_SYN_ON] < 0.5f || s->val[P_SEQ_ON] < 0.5f)) continue;
        if ((s->val[P_SEQ_PLAY] > 0.5f) == play) continue;
        s->val[P_SEQ_PLAY] = play ? 1.0f : 0.0f;
        s->changed |= 1u << SQ_CHG_PLAY;
        synth_after(s, P_SEQ_PLAY);
    }
}

static void set_state(inst_t *in, const char *str);
static void synth_load(inst_t *in, int c, const char *str);
static void set_param(void *inst, const char *key, const char *val) {
    inst_t *in = inst;
    if (!strcmp(key, "lfo_bpm")) {   /* the host's tempo (HAS_LFO_BPM), not a param */
        float bpm = (float)atof(val);
        if (isfinite(bpm) && bpm >= 20.0f && bpm <= 400.0f)
            for (int c = 0; c < NSYN; c++) synth_bpm(in->syn[c], bpm);
        return;
    }
    if (!strcmp(key, "state")) {
        if (!strncmp(val, "seqcmd", 6)) {   /* an edit from the window (src/stepseq_run.h): "seqcmd\n" Synth 1, "seqcmd2\n" .. */
            int c = val[6] >= '1' && val[6] <= '9' ? val[6] - '1' : 0;
            const char *nl = strchr(val, '\n');
            if (nl && c < NSYN) sq_command(in->syn[c], nl + 1);
            return;
        }
        if (!strncmp(val, "synthload", 9)) {   /* "synthload<n>\n<state>": a sound's Synth 1 into Synth n only */
            int c = val[9] >= '1' && val[9] <= '9' ? val[9] - '1' : 0;
            const char *nl = strchr(val, '\n');
            if (nl && c < NSYN) synth_load(in, c, nl + 1);
            return;
        }
        set_state(in, val);
        return;
    }
    if (store_retired(in->syn[0], key, val)) {   /* an older project's retired key (Synth 1's; dly_pp a shared one) */
        for (int c = 0; c < NSYN; c++) {
            for (int k = 0; k < P_COUNT && c; k++)
                if (engine_global(k)) in->syn[c]->val[k] = in->syn[0]->val[k];
            synth_after(in->syn[c], -1);
        }
        return;
    }
    const pkey_t *e = key_find(in, key);
    if (!e || e->slot < 0) return;
    int k = e->slot;
    if (engine_global(k)) {
        for (int c = 0; c < NSYN; c++) synth_set(in->syn[c], k, val);
        if (k == P_SEQ_ALL) seq_all(in);
    } else {
        synth_set(in->syn[(int)e->syn], k, val);
    }
}

/* ---- the state: Synth 1 and the shared values, then Synths 2 and 3, then the sequences ----
 * As one Synth's (engine.c, "v=1" / "v=2"), with Synth 2 and 3's values as "s2_<key>" / "s3_<key>" (only the ones off
 * their own defaults) and their sequences as "s2_seq=" / "s3_seq=". When it doesn't fit the chunk, "v=2" (shared
 * prefixes); still too long, the Synths past the first are dropped from the end (Synth 3 first), then the sequences are
 * left out (each Synth's readout says so). Synth 1 and the shared values never: should they alone not fit, nothing is
 * saved (0, and stderr says so). */
static int state_keeps(const one_t *s, int i) {
    if (!KEYS[i][0] || s->val[i] == s->def[i] || i == P_SEQ_PLAY || i == P_SEQ_REC || i == P_SEQ_STEP_MOD) return 0;
    return i != P_SEQ_ALL && !(s->idx > 0 && engine_global(i));
}
static int state_put(char *buf, int n, int used, const char *fmt, const char *key, float v) {   /* counts past n */
    return used + snprintf(used < n ? buf + used : NULL, used < n ? (size_t)(n - used) : 0, fmt, key, v);
}
/* Synth s's values at `used` (v2: shared prefixes against prev, the last key written); returns the new length (past n:
 * it didn't fit) */
static int state_synth(const one_t *s, int v2, char *prev, char *buf, int n, int used) {
    char key[64];
    for (int i = 0; i < P_COUNT; i++) {
        if (!state_keeps(s, i)) continue;
        if (s->idx) snprintf(key, sizeof key, "s%d_%s", s->idx + 1, KEYS[i]);
        else snprintf(key, sizeof key, "%s", KEYS[i]);
        const char *u = strchr(key, '_');
        size_t pl = u ? (size_t)(u + 1 - key) : 0;
        if (v2 && pl && prev[0] && !strncmp(prev, key, pl) && strchr(prev, '_') == prev + pl - 1)
            used = state_put(buf, n, used, ";~%s=%g", key + pl, s->val[i]);
        else
            used = state_put(buf, n, used, ";%s=%g", key, s->val[i]);
        snprintf(prev, 64, "%s", key);
    }
    return used;
}
static int state_seq(one_t *s, char *buf, int n, int used) {   /* its sequence, when it has one and it fits */
    int lanes = 0;
    for (int l = 0; l < SQ_ML; l++) lanes |= s->sq.q.m[l].dest;
    if (!sq_used_steps(&s->sq.q) && !lanes) return used;
    int len = sq_encode(&s->sq.q, s->sq_text, sizeof s->sq_text);
    char key[24];
    if (s->idx) snprintf(key, sizeof key, "s%d_seq", s->idx + 1);
    else snprintf(key, sizeof key, "seq");
    if (used + (int)strlen(key) + 2 + len < n) used += snprintf(buf + used, n - used, ";%s=%s", key, s->sq_text);
    else s->sq.lost = 1;
    return used;
}
static int get_state(inst_t *in, char *buf, int n) {
    char prev[64];
    int used = 0, v2, last = NSYN - 1;
    for (v2 = 0; v2 < 2; v2++) {   /* every Synth in v=1, else in v=2 */
        prev[0] = 0;
        used = snprintf(buf, n, v2 ? "v=2" : "v=1");
        for (int c = 0; c < NSYN; c++) used = state_synth(in->syn[c], v2, prev, buf, n, used);
        if (used < n) break;
    }
    while (used >= n && last > 0) {   /* too long even so: the last Synths left out */
        last--;
        prev[0] = 0;
        used = snprintf(buf, n, "v=2");
        for (int c = 0; c <= last; c++) used = state_synth(in->syn[c], 1, prev, buf, n, used);
    }
    if (used >= n) {
        if (n > 0) buf[0] = 0;
        fprintf(stderr, "ONE-ish: the sound's state is %d bytes, more than the %d-byte chunk: NOT SAVED\n", used + 1, n);
        return 0;
    }
    for (int c = 0; c < NSYN; c++) {
        in->syn[c]->sq.lost = c > last;   /* (a Synth left out: its readout says its sequence wasn't saved) */
        if (c <= last) used = state_seq(in->syn[c], buf, n, used);
    }
    if (last < NSYN - 1) fprintf(stderr, "ONE-ish: the state is too long for the chunk: Synths %d-%d not saved\n",
                                 last + 2, NSYN);
    return used;
}

/* a state's key=value pairs in turn (the "~" of "v=2" expanded), into fn; str is copied into the instance first */
static void state_walk(inst_t *in, const char *str, void (*fn)(inst_t *, int, const char *, const char *), int arg) {
    char *buf = in->state_buf, key[64] = "", *save = NULL;
    snprintf(buf, sizeof in->state_buf, "%s", str);
    for (char *kv = strtok_r(buf, ";", &save); kv; kv = strtok_r(NULL, ";", &save)) {
        char *eq = strchr(kv, '=');
        if (!eq) continue;
        *eq = 0;
        if (kv[0] == '~') {   /* "v=2": the previous key's prefix (up to its first '_') and this suffix */
            char *u = strchr(key, '_');
            if (!u || (size_t)(u + 1 - key) + strlen(kv + 1) >= sizeof key) continue;
            strcpy(u + 1, kv + 1);
        } else if (strlen(kv) < sizeof key) {
            strcpy(key, kv);
        } else {
            continue;
        }
        fn(in, arg, key, eq + 1);
    }
}
static void state_value(inst_t *in, int arg, const char *key, const char *val) {   /* one value of a whole state */
    (void)arg;
    const char *rest;
    int c = key_synth(key, &rest);
    if (c >= NSYN) return;   /* (a Synth this build doesn't have) */
    if (!strcmp(rest, "seq")) { sq_decode(val, &in->syn[c]->sq.q, MD_COUNT, MS_COUNT); return; }   /* (damaged: none) */
    if (c == 0 && store_retired(in->syn[0], key, val)) return;
    const pkey_t *e = key_find(in, key);
    if (!e || e->slot < 0) return;
    if (engine_global(e->slot)) for (int d = 0; d < NSYN; d++) store_k(in->syn[d], e->slot, val);
    else store_k(in->syn[(int)e->syn], e->slot, val);
}
static void set_state(inst_t *in, const char *str) {
    if (strlen(str) >= sizeof in->state_buf) return;   /* never restore half a state */
    for (int c = 0; c < NSYN; c++) {   /* a state saved before a param (or a Synth) existed leaves it at its default */
        load_defaults(in->syn[c]);
        sq_clear(&in->syn[c]->sq.q);   /* and one saved without a sequence has none */
    }
    state_walk(in, str, state_value, 0);
    for (int c = 0; c < NSYN; c++) {
        one_t *s = in->syn[c];
        for (int k = 0; k < P_COUNT && c; k++)
            if (engine_global(k)) s->val[k] = in->syn[0]->val[k];   /* (the retired keys set Synth 1's) */
        s->sq.lost = 0;
        sq_lanes_changed(s);
        synth_after(s, -1);
    }
}

/* LOAD INTO SYNTH n: a sound's own values (a single-Synth patch or preset, or a performance's Synth 1) into Synth c,
 * which keeps its setup (on, channel, key range) ; the shared values and the other Synths stay as they are */
static int load_skip(int k) { return engine_global(k) || k == P_SYN_ON || k == P_SYN_CH || k == P_KEY_LO || k == P_KEY_HI; }
static void load_value(inst_t *in, int c, const char *key, const char *val) {
    one_t *s = in->syn[c];
    if (!strcmp(key, "seq")) { sq_decode(val, &s->sq.q, MD_COUNT, MS_COUNT); return; }
    if (store_retired(s, key, val)) {   /* (an old patch's bend range; dly_pp, a shared one, is put back) */
        for (int k = 0; k < P_COUNT; k++)
            if (engine_global(k)) s->val[k] = in->syn[0]->val[k];
        return;
    }
    int k = slot(key);   /* Synth 1's keys only (unprefixed) */
    if (k < 0 || load_skip(k) || key[0] == '\0') return;
    if (key[0] == 's' && key[1] >= '2' && key[1] <= '9' && key[2] == '_') return;
    store_k(s, k, val);
}
static void synth_load(inst_t *in, int c, const char *str) {
    one_t *s = in->syn[c];
    if (strlen(str) >= sizeof in->state_buf) return;
    float keep[P_COUNT];
    memcpy(keep, s->val, sizeof keep);
    load_defaults(s);
    for (int k = 0; k < P_COUNT; k++)
        if (load_skip(k)) s->val[k] = keep[k];
    sq_clear(&s->sq.q);
    state_walk(in, str, load_value, c);
    s->sq.lost = 0;
    sq_lanes_changed(s);
    synth_after(s, -1);
    for (int k = 0; k < P_COUNT; k++)   /* the host's controls follow every value that moved */
        if (s->val[k] != keep[k]) s->moved = 1;
}

/* SAVE SYNTH n: Synth c's own values as a single-Synth sound (as Synth 1's keys, against Synth 1's defaults; its setup
 * and the shared values left out), and its sequence */
static int synth_state(inst_t *in, int c, char *buf, int n) {
    one_t *s = in->syn[c];
    const one_t *s0 = in->syn[0];
    int used = snprintf(buf, n, "v=1");
    for (int i = 0; i < P_COUNT; i++) {
        if (!KEYS[i][0] || load_skip(i) || s->val[i] == s0->def[i] || i == P_SEQ_PLAY || i == P_SEQ_REC
            || i == P_SEQ_STEP_MOD)
            continue;
        used = state_put(buf, n, used, ";%s=%g", KEYS[i], s->val[i]);
    }
    if (used >= n) { if (n > 0) buf[0] = 0; return 0; }
    int lanes = 0;
    for (int l = 0; l < SQ_ML; l++) lanes |= s->sq.q.m[l].dest;
    if (sq_used_steps(&s->sq.q) || lanes) {
        int len = sq_encode(&s->sq.q, s->sq_text, sizeof s->sq_text);
        if (used + 5 + len < n) used += snprintf(buf + used, n - used, ";seq=%s", s->sq_text);
    }
    return used;
}

static int get_param(void *inst, const char *key, char *buf, int n) {
    inst_t *in = inst;
    if (!strcmp(key, "state")) return get_state(in, buf, n);
    if (!strncmp(key, "synth_state", 11)) {   /* "synth_state<n>": Synth n as a single-Synth sound (SAVE SYNTH) */
        int c = key[11] >= '1' && key[11] <= '9' ? key[11] - '1' : 0;
        return c < NSYN ? synth_state(in, c, buf, n) : 0;
    }
    if (!strcmp(key, "voices_total")) {   /* a test readout: the voices sounding in every Synth (the pool) */
        int t = 0;
        for (int c = 0; c < NSYN; c++)
            for (int i = 0; i < MAX_VOICES; i++) t += voice_busy(&in->syn[c]->voice[i]);
        return snprintf(buf, n, "%d", t);
    }
    if (!strcmp(key, "heals")) {
        int h = 0;
        for (int c = 0; c < NSYN; c++) h += in->syn[c]->heals;
        return snprintf(buf, n, "%d", h);
    }
    const char *rest;
    int c = key_synth(key, &rest);
    if (c >= NSYN) return 0;
    one_t *s = in->syn[c];
    size_t len = strlen(key);
    if (len > 8 && !strcmp(key + len - 8, "_display")) {   /* "<key>_display" (a copy's: "s2_<key>_display") */
        char base[48];
        if (len - 8 >= sizeof base) return 0;
        memcpy(base, key, len - 8);
        base[len - 8] = 0;
        const pkey_t *e = key_find(in, base);
        return !e || e->slot < 0 ? 0 : display_text(in->syn[(int)e->syn], e->slot, buf, n);
    }
    const pkey_t *e = key_find(in, key);
    if (e && e->slot >= 0) return snprintf(buf, n, "%g", in->syn[(int)e->syn]->val[e->slot]);
    return synth_get(s, rest, buf, n);   /* the readouts: seq_pos, gen_log, gen_euclid, voices_sounding */
}

/* engine.h: the params the engine set itself (CHORD LEARN, the sequencer's PLAY ...), one key per call */
int mpc_engine_changed(void *inst, char *key, int len) {
    inst_t *in = inst;
    for (int c = 0; c < NSYN; c++) {
        one_t *s = in->syn[c];
        if (s->moved) {   /* LOAD INTO SYNTH moved everything: its ON as the marker (the window reads them all) */
            s->moved = 0;
            if (c) snprintf(key, (size_t)len, "s%d_%s", c + 1, KEYS[P_SYN_ON]);
            else snprintf(key, (size_t)len, "%s", KEYS[P_SYN_ON]);
            return 1;
        }
        if (!s->changed) continue;
        int i = __builtin_ctz(s->changed);
        s->changed &= s->changed - 1;
        if (c) snprintf(key, (size_t)len, "s%d_%s", c + 1, KEYS[CHANGED_SLOT[i]]);
        else snprintf(key, (size_t)len, "%s", KEYS[CHANGED_SLOT[i]]);
        return 1;
    }
    return 0;
}

static void song_pos(void *inst, double ppq, double bpm, int playing, int frames) {
    inst_t *in = inst;
    for (int c = 0; c < NSYN; c++) synth_song_pos(in->syn[c], ppq, bpm, playing, frames);
}

/* ---- create, destroy ---- */
static void destroy(void *inst) {
    inst_t *in = inst;
    for (int c = 0; c < NSYN; c++)
        if (in->syn[c]) synth_destroy(in->syn[c]);
    free(in);
}
static void *create(const char *data_dir) {
    (void)data_dir;
    inst_t *in = calloc(1, sizeof *in);
    static short pidx[NSYN][P_COUNT];   /* (the same for every instance; worked out again, identically, by each) */
    if (!in) return NULL;
    fx_touch(&in->mfx, sizeof in->mfx);   /* the effect buffers' pages, mapped now rather than in the audio thread */
    keys_init(in, pidx);
    for (int c = 0; c < NSYN; c++)
        if (!(in->syn[c] = synth_create(in, c, pidx[c]))) { destroy(in); return NULL; }
    return in;
}

/* ---- audio ---- */
/* a non-finite sample reached the output (masked to 0 there): whatever holds it would otherwise keep the output at 0
 * for good (a delay line or reverb tank full of NaN, a filter's state). Before the next piece: every synth effect and
 * both master effect slots stop and clear their memory (fx.h's own path for a type switch) and every voice's filters,
 * noise colours and FM feedback start clean. */
static void life_clear(fxlife_t *l) {
    if (l->state == FS_OFF) return;
    l->state = FS_CLEAR;
    l->at = 0;
    l->switching = 1;
    l->tone[0] = l->tone[1] = 0.0f;
}
static void heal(inst_t *in) {
    in->heal = 0;
    life_clear(&in->mfx.m[0].s);
    life_clear(&in->mfx.m[1].s);
    for (int c = 0; c < NSYN; c++) {
        one_t *s = in->syn[c];
        s->heals++;
        life_clear(&s->fx.ss);
        for (int i = 0; i < MAX_VOICES; i++) {
            voice_t *v = &s->voice[i];
            ladder_reset(&v->lad);
            svf2_reset(&v->svf);
            v->red = v->wprev = v->fm_prev = 0.0f;
        }
    }
}

/* a Synth renders while it is on, or while anything of it still sounds (a release, a synth effect's tail) */
static int synth_live(one_t *s) {
    if (s->on || s->seq_on || s->npend > 0 || sq_live(s)) return 1;
    for (int i = 0; i < MAX_VOICES; i++)
        if (s->voice[i].env[ENV_AMP].stage != ENV_IDLE) return 1;
    return s->fx.ss.state == FS_RUN && s->quiet < (int64_t)(FX_TAIL_S * SR);
}

static void render_to(void *inst, int16_t *out, float *fout, int frames) {
    inst_t *in = inst;
    one_t *live[NSYN], *s0 = in->syn[0];
    float sl[NSYN][128], sr[NSYN][128], ml[128], mr[128];
    fx_send_t *snd[NSYN];
    const fx_params_t *ps[NSYN];
    const float *pl[NSYN], *pr[NSYN];
    int nl = 0;
    for (int c = 0; c < NSYN; c++) {
        one_t *s = in->syn[c];
        if (synth_live(s)) {
            live[nl] = s;
            snd[nl] = &s->snd;
            ps[nl] = &s->fxp;
            pl[nl] = sl[nl];
            pr[nl] = sr[nl];
            nl++;
            synth_block(s);
        } else if (s->fx.ss.state == FS_RUN || s->snd.send1 > 0.0f || s->snd.send2 > 0.0f) {   /* now silent: stopped */
            if (s->fx.ss.state == FS_RUN) { s->fx.ss.state = FS_CLEAR; s->fx.ss.at = 0; s->fx.ss.switching = 0; }
            s->snd.send1 = s->snd.send2 = 0.0f;
        }
    }
    if (s0->fx_stale) {   /* the master effects' settings (Synth 1's values) even while Synth 1 is silent */
        s0->fx_stale = 0;
        fx_params(s0, &s0->fxp);
    }
    float level = s0->val[P_MASTER_LEVEL] * 0.01f;
    const float *ext = in->ext_n == frames ? in->ext : NULL;   /* the audio input for this render, if it was given */
    in->ext_n = 0;
    int done = 0;
    while (done < frames) {
        if (in->heal) heal(in);
        int n = CR - s0->phase;
        if (n > frames - done) n = frames - done;
        for (int j = 0; j < nl; j++) synth_due(live[j], &n);   /* steps and note ends at this sample, first */
        if (n > 128) n = 128;
        for (int j = 0; j < nl; j++) live[j]->ext = ext ? ext + done : NULL;
        for (int j = 0; j < nl; j++) synth_piece(live[j], n, sl[j], sr[j]);
        float *L = ml, *R = mr;   /* the mix of the Synths (one: its own buffers, as they are) */
        if (nl == 1) {
            L = sl[0];
            R = sr[0];
        } else {
            memset(ml, 0, sizeof(float) * n);
            memset(mr, 0, sizeof(float) * n);
            for (int j = 0; j < nl; j++)
                for (int i = 0; i < n; i++) { ml[i] += sl[j][i]; mr[i] += sr[j][i]; }
        }
        s0->fxp.ramp = CR - s0->phase;
        fx_master_multi(&in->mfx, &s0->fxp, nl, snd, ps, pl, pr, L, R, n);
        int bad = 0;
        for (int i = 0; i < n; i++) {   /* a last guard: never NaN to the host (and heal what made it: see heal()) */
            int okl = isfinite(L[i]), okr = isfinite(R[i]);
            bad |= !okl || !okr;
            float xl = level != 1.0f ? L[i] * level : L[i], xr = level != 1.0f ? R[i] * level : R[i];
            float yl = okl ? soft_limit(xl) : 0.0f, yr = okr ? soft_limit(xr) : 0.0f;
            if (out) {
                out[2 * (done + i)] = (int16_t)(yl * 32767.0f);
                out[2 * (done + i) + 1] = (int16_t)(yr * 32767.0f);
            } else {
                fout[2 * (done + i)] = yl;
                fout[2 * (done + i) + 1] = yr;
            }
        }
        in->heal |= bad;
        done += n;
        for (int c = 0; c < NSYN; c++) synth_advance(in->syn[c], n);
    }
}

static void render(void *inst, int16_t *out, int frames) { render_to(inst, out, NULL, frames); }

/* Not in the engine contract either: the desktop build's audio input (desktop/hostrate.c), interleaved L R in -1..1, for
 * the next render() of the same number of frames (up to 128; a render without one hears silence). Kept as its mono
 * sum, the EXT channel's signal (and the vocoders' modulator); an input of zeros is silence (the EXT channel then does
 * nothing at all). Called under the wrapper's engine lock, just before that render; the MPC build never calls it. */
void one_ish_set_input(void *inst, const float *in_lr, int frames) {
    inst_t *in = inst;
    int live = 0;
    if (frames < 1 || frames > 128) frames = 0;
    for (int i = 0; i < frames; i++) {
        float m = 0.5f * (in_lr[2 * i] + in_lr[2 * i + 1]);
        if (!isfinite(m)) m = 0.0f;
        in->ext[i] = m;
        live |= m != 0.0f;
    }
    in->ext_n = live ? frames : 0;
}

/* Not in the engine contract (wrapper/engine.h): the desktop build's float output (desktop/hostrate.c). The same as
 * render() before its int16 step: interleaved L R in -1..1, where render() writes (int16_t)(x * 32767). Called under
 * the wrapper's engine lock, in render()'s place; the MPC build never calls it. */
void one_ish_render_float(void *inst, float *out_lr, int frames) { render_to(inst, NULL, out_lr, frames); }

static const mpc_engine_t ENGINE = {create, destroy, midi, set_param, get_param, render, NULL, song_pos};
const mpc_engine_t *mpc_engine(void) { return &ENGINE; }
