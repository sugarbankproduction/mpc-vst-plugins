/* ==== THE STEP SEQUENCER: playback, recording, editing ==============================================================
 * Included by engine.c after its performance section (it is part of engine.c and uses its functions: out_on/out_off,
 * chord_notes, start_note ...). The sequence and its text form are src/stepseq.h. After the Moog One manual, p82-95.
 *
 * Switches and settings are params (seq_*): ON (whether it plays the synth), PLAY (START/STOP), REC, STEP MOD, SYNC,
 * a free RATE, the DIVISION and TIMING of a step, LENGTH 1-64, FOLLOW SWING (of clock_swing, the master swing), NOTE TRIG
 * with NOTE RESET, RESTART ON HOLD and RESTART ON TRIG, TRANSPOSE with its mode (start key or interval from middle C), ONE
 * SHOT, the RECORDING MODE (replace, overdub, focused), and the factory sequences (seq_pattern). The steps are engine
 * state: saved in the state ("seq="), edited with commands (sq_command), played here.
 *
 * Playing: each step's lanes start their notes on the step's exact sample, through chord memory (the Moog One's order:
 * keys, arpeggiator, sequencer, then chord); a note lasts GATE of the step's own length (swing makes pairs uneven), or
 * RATCHET re-articulates it 2-4 times inside the step; a TIE carries a lane's note into the next step: the same note
 * goes on without a new attack, another note takes over its voice legato; a REST step plays nothing. Modulation lanes
 * add their step's offset to their destination for the whole step (scaled by their controller: AMOUNT 100% and the
 * controller at its minimum silence the lane), in every voice and, for an effect destination, in the effect pass.
 * The sequencer runs beside the arpeggiator: the arp plays the keys you hold, the sequencer its stored notes; keys
 * reach the sequencer instead of sounding only with NOTE TRIG or TRANSPOSE on.
 * Timing: a clock of its own, as the arpeggiator's (a beat position per sample). Synced, it runs at the host's tempo and,
 * with the transport playing, on the host's song position: a looping sequence started with PLAY lines up with the song
 * (step 1 on the song's start, every LENGTH steps after; a loop back in the host plays it from there); NOTE TRIG and ONE
 * SHOT start where the key or PLAY fell (on the song's step grid). Unsynced it runs at seq_rate.
 * Recording (REC on; keys still sound): stopped, a key or chord goes into the selected step and the selection moves
 * on (step recording); running, notes land on the nearest step (REPLACE clears that step the first time round, OVERDUB
 * adds lanes, FOCUSED replaces the selected step whenever played), their gate as long as the key was held. STEP MOD:
 * moving a knob (a param with a matrix destination) records its move as that step's offset in a lane for that
 * destination (made if missing) instead of moving the knob, on the playing step, or the selected one when stopped.
 * Nothing here allocates, locks or touches a file; edits arrive through set_param (the wrapper runs every engine call
 * one at a time, so an edit lands between two audio blocks like a knob move). */

enum { SQR_REPLACE, SQR_OVERDUB, SQR_FOCUSED };   /* "seq_rec_mode" */
enum { SQT_START_KEY, SQT_INTERVAL };              /* "seq_tr_mode" */
enum { SQ_CHG_PLAY = CHORD_MAX, SQ_CHG_TRIG, SQ_CHG_LENGTH };   /* their bits in s->changed (CHANGED_SLOT) */
_Static_assert(SQ_CHORD == CHORD_MAX, "a lane keeps as many notes as chord memory makes");

static int sq_len(const one_t *s) { return (int)clampf(roundf(s->val[P_SEQ_LENGTH]), 1.0f, (float)SQ_STEPS); }
static int sq_output(const one_t *s) { return s->val[P_SEQ_ON] > 0.5f; }

/* ---- the clock (as the arpeggiator's: engine.c step_t ... until) ---- */
static double sq_t(const sq_run_t *r, int64_t k) { return (double)k * r->step + ((k & 1) ? r->swoff : 0.0); }
static double sq_now(const sq_run_t *r) { return r->pos0 + (double)r->clk * r->bps; }
static void sq_anchor(sq_run_t *r, double pos) { r->pos0 = pos; r->clk = 0; }
static int64_t sq_first(const sq_run_t *r, double p) {   /* the first step at or after beat position p */
    int64_t k = (int64_t)floor(p / r->step) - 1;
    while (sq_t(r, k) < p - 1e-9) k++;
    return k;
}
static int64_t sq_until(const sq_run_t *r, double t) { return (int64_t)ceil((t - r->pos0) / r->bps - 1e-6) - r->clk; }

/* ---- a lane's sound ---- */
static void sq_lane_off(one_t *s, int l) {
    sq_run_t *r = &s->sq;
    for (int i = 0; i < r->lnout[l]; i++) out_off(s, r->lout[l][i], 1);
    r->lnout[l] = 0;
    r->lroot[l] = -1;
    r->ltied[l] = 0;
}
static void sq_lane_on(one_t *s, int l, int root, int vel, int tied) {
    sq_run_t *r = &s->sq;
    int out[CHORD_MAX], n = chord_notes(s, root, out);
    if (r->lroot[l] >= 0) sq_lane_off(s, l);
    for (int i = 0; i < n; i++)
        if (out[i] >= 0 && out[i] <= 127) { out_on(s, out[i], vel); r->lout[l][r->lnout[l]++] = (uint8_t)out[i]; }
    r->lroot[l] = root;
    r->ltied[l] = tied;
}
/* a tie into another note: its voice moves there without a new attack (legato). In mono, a new note before the old one
 * lets go is legato anyway; a chord (or a note another lane or the arp also holds) takes the new notes first. */
static void sq_lane_legato(one_t *s, int l, int root, int vel, int tied) {
    sq_run_t *r = &s->sq;
    int out[CHORD_MAX], n = chord_notes(s, root, out), from = r->lnout[l] == 1 ? r->lout[l][0] : -1;
    if (from >= 0 && n == 1 && out[0] >= 0 && out[0] <= 127 && !is_mono(s) && s->ocnt[from] == 1 && !s->ocnt[out[0]]) {
        int moved = 0;
        for (int i = 0; i < MAX_VOICES; i++) {
            voice_t *v = &s->voice[i];
            if (v->note == from && v->gate && !v->sustained && v->env[ENV_AMP].stage != ENV_IDLE) {
                start_note(s, v, out[0], 1);
                moved = 1;
            }
        }
        if (moved) {
            s->ocnt[from] = 0;
            s->ocnt[out[0]] = 1;
            forget(s, from);
            if (s->nheld == MAX_HELD) forget(s, s->held[0]);
            s->held[s->nheld++] = out[0];
            glog_add(s, out[0], vel);
            glog_add(s, from, 0);
            r->lout[l][0] = (uint8_t)out[0];
            r->lroot[l] = root;
            r->ltied[l] = tied;
            return;
        }
    }
    uint8_t old[CHORD_MAX];
    int no = r->lnout[l];
    memcpy(old, r->lout[l], (size_t)no);
    r->lnout[l] = 0;
    for (int i = 0; i < n; i++)
        if (out[i] >= 0 && out[i] <= 127) { out_on(s, out[i], vel); r->lout[l][r->lnout[l]++] = (uint8_t)out[i]; }
    for (int i = 0; i < no; i++) out_off(s, old[i], 1);
    r->lroot[l] = root;
    r->ltied[l] = tied;
}

/* ---- notes waiting for their sample: ratchets' re-articulations and note ends ---- */
static void sq_ev_add(one_t *s, int64_t at, int l, int root, int vel, int kind) {
    sq_run_t *r = &s->sq;
    if (r->nev == SQ_EVQ) {   /* full (never with 8 lanes of 4 ratchets): a note's end happens now, a start is dropped */
        if (kind == SQ_EV_OFF) sq_lane_off(s, l);
        return;
    }
    r->ev[r->nev].at = at;
    r->ev[r->nev].lane = (uint8_t)l;
    r->ev[r->nev].root = (uint8_t)root;
    r->ev[r->nev].vel = (uint8_t)vel;
    r->ev[r->nev++].kind = (uint8_t)kind;
}
static void sq_ev_cancel(sq_run_t *r, int l) {
    for (int i = 0; i < r->nev;)
        if (r->ev[i].lane == l) r->ev[i] = r->ev[--r->nev];
        else i++;
}

/* every lane quiet and nothing waiting (a stop, a change of way, All Notes Off) */
static void sq_silence(one_t *s) {
    for (int l = 0; l < SQ_NL; l++) sq_lane_off(s, l);
    s->sq.nev = 0;
}
/* gen_stop() let go of every generated note itself: forget what the lanes held */
static void sq_forget(one_t *s) {
    sq_run_t *r = &s->sq;
    for (int l = 0; l < SQ_NL; l++) { r->lnout[l] = 0; r->lroot[l] = -1; r->ltied[l] = 0; }
    r->nev = 0;
}

/* ---- modulation lanes ---- */
/* the lanes' offsets now: the playing step's while running, the selected step's while STEP MOD is on and it is stopped
 * (to hear what is dialled in); nothing with ON off */
static void sq_moff_at(one_t *s, int step) {
    sq_run_t *r = &s->sq;
    r->mod_live = 0;
    for (int l = 0; l < SQ_ML; l++) {
        const sq_mlane_t *m = &r->q.m[l];
        r->moff[l] = step >= 0 && sq_output(s) && m->dest && m->off[step] != SQ_NO_OFF ? m->off[step] * 0.01f : 0.0f;
        r->mod_live |= r->moff[l] != 0.0f;
    }
}
static void sq_moff_refresh(one_t *s) {
    sq_run_t *r = &s->sq;
    if (r->running && r->cur >= 0) sq_moff_at(s, r->cur);
    else sq_moff_at(s, s->val[P_SEQ_STEP_MOD] > 0.5f ? r->cursor : -1);
}
/* a lane's controller: none = full strength; else AMOUNT 100% with the controller at its minimum silences the lane */
static float sq_ctl_gain(const sq_mlane_t *m, const float *src) {
    if (!m->ctl || m->ctl >= MS_COUNT) return 1.0f;
    float c = clampf(fabsf(src[m->ctl]), 0.0f, 1.0f);
    return 1.0f - m->amt * 0.01f * (1.0f - c);
}
/* the voices' destinations (control_voice, before the LFO destinations are copied) */
static void sq_mod_voice(const one_t *s, const float *src, float *dm) {
    const sq_run_t *r = &s->sq;
    for (int l = 0; l < SQ_ML; l++) {
        int d = r->q.m[l].dest;
        if (r->moff[l] == 0.0f || d <= MD_OFF || d >= MD_COUNT || (d >= MD_FX_FIRST && d <= MD_FX_END)) continue;
        dm[d] += r->moff[l] * sq_ctl_gain(&r->q.m[l], src);
    }
}
/* (2026-10-29) the lanes' offset to one destination now, for a voice whose sources are src: what a note starting on
 * this very step reads (gate_on's MULTI TRIG), before the next control step adds it to the voice */
static float sq_dest_now(const one_t *s, int d, const float *src) {
    const sq_run_t *r = &s->sq;
    float m = 0.0f;
    if (!r->mod_live) return 0.0f;
    for (int l = 0; l < SQ_ML; l++)
        if (r->moff[l] != 0.0f && r->q.m[l].dest == d) m += r->moff[l] * sq_ctl_gain(&r->q.m[l], src);
    return m;
}
/* the effect destinations (fx_mod_step) */
static void sq_mod_fx(const one_t *s, const float *src, float *dm) {
    const sq_run_t *r = &s->sq;
    for (int l = 0; l < SQ_ML; l++) {
        int d = r->q.m[l].dest;
        if (r->moff[l] != 0.0f && d >= MD_FX_FIRST && d <= MD_FX_END) dm[d] += r->moff[l] * sq_ctl_gain(&r->q.m[l], src);
    }
}
/* after the lanes changed: which kinds of destination they reach (the matrix's per-voice paths and the effect pass
 * run for them as for a matrix slot), and the offsets now */
static void sq_lanes_changed(one_t *s) {
    sq_run_t *r = &s->sq;
    r->env_lanes = r->lfo_lanes = r->nenv_lanes = 0;
    memset(r->fx_mask, 0, sizeof r->fx_mask);
    for (int l = 0; l < SQ_ML; l++) {
        int d = r->q.m[l].dest;
        r->env_lanes |= md_env(d);
        r->lfo_lanes |= md_lfo(d);
        r->nenv_lanes |= md_nenv(d);
        if (d >= MD_FX_FIRST && d <= MD_FX_END) r->fx_mask[(d - MD_FX_FIRST) >> 6] |= 1ull << ((d - MD_FX_FIRST) & 63);
    }
    apply_matrix(s);
    sq_moff_refresh(s);
    r->rev++;
}

/* ---- starting and stopping ---- */
static void sq_start(one_t *s, int mode, int resume) {
    sq_run_t *r = &s->sq;
    sq_silence(s);
    r->running = r->start_pending = 1;
    r->mode = mode;
    r->start_mode = mode;
    r->start_resume = resume;
    r->cur = -1;
    memset(r->fresh, 0, sizeof r->fresh);
    for (int i = 0; i < SQ_STEPS; i++) r->repl_k[i] = INT64_MIN;
}
static void sq_stop(one_t *s) {
    sq_run_t *r = &s->sq;
    sq_silence(s);
    r->running = r->start_pending = 0;
    sq_moff_refresh(s);
}
/* ONE SHOT's end: stopped, and PLAY reported back off */
static void sq_finish(one_t *s) {
    sq_run_t *r = &s->sq;
    int by_play = r->mode == SQ_PLAY;
    sq_stop(s);
    r->resume = 0;
    if (by_play && s->val[P_SEQ_PLAY] > 0.5f) {
        s->val[P_SEQ_PLAY] = 0.0f;
        r->was_play = 0;
        s->changed |= 1u << SQ_CHG_PLAY;
    }
}

/* ---- one step: the clock reached step k ---- */
static void sq_fire(one_t *s, int64_t k) {
    sq_run_t *r = &s->sq;
    int len = sq_len(s);
    int64_t e = k - r->k0;
    r->cur_k = k;
    if (s->val[P_SEQ_ONE_SHOT] > 0.5f && (e >= len || e < 0)) { sq_finish(s); return; }
    int idx = (int)((e % len + len) % len), out = sq_output(s), rest = (int)(r->q.rest >> idx & 1);
    r->cur = idx;
    r->resume = (idx + 1) % len;
    sq_moff_at(s, idx);
    double t0 = sq_t(r, k), t1 = sq_t(r, k + 1);
    for (int l = 0; l < SQ_NL; l++) {
        const sq_note_t *n = &r->q.n[idx][l];
        int has = out && !rest && sq_has(n) && !(r->fresh[idx] >> l & 1);
        sq_ev_cancel(r, l);   /* (anything left of the last step's ratchets: a tempo change shortened it) */
        int root = has ? (int)clampf((float)(n->note + r->transpose), 0.0f, 127.0f) : -1;
        int rat = has ? sq_ratchet(n) : 1, tie = has && (n->flags & SQ_TIE), held = tie && rat == 1;
        if (has && r->lroot[l] >= 0 && r->ltied[l]) {   /* the last step tied into this one */
            if (r->lroot[l] == root) r->ltied[l] = held;   /* the same note goes on: no new attack */
            else sq_lane_legato(s, l, root, n->vel, held);
        } else {
            if (r->lroot[l] >= 0) sq_lane_off(s, l);
            if (has) sq_lane_on(s, l, root, n->vel, held);
        }
        if (!has) continue;
        double sub = (t1 - t0) / rat;
        for (int i = 0; i < rat; i++) {
            double on = t0 + i * sub;
            int64_t at = s->now + sq_until(r, on);
            if (i > 0) sq_ev_add(s, at, l, root, n->vel, i == rat - 1 && tie ? SQ_EV_ON_TIED : SQ_EV_ON);
            if (i == rat - 1 && tie) break;   /* tied: it lasts into the next step */
            int64_t off = s->now + sq_until(r, on + n->gate * 0.01 * sub);
            if (off <= at) off = at + 1;
            sq_ev_add(s, off, l, root, 0, SQ_EV_OFF);
        }
    }
    r->fresh[idx] = 0;
}

/* once per render while it runs (or waits to start, or has notes to end): tempo, step length, swing, the host's song
 * position, a start's first step */
static void sq_block(one_t *s) {
    sq_run_t *r = &s->sq;
    const float *p = s->val;
    int sync = p[P_SEQ_SYNC] > 0.5f, one_shot = p[P_SEQ_ONE_SHOT] > 0.5f;
    double bpm = sync ? (s->sp_bpm > 0.0 ? s->sp_bpm : (double)s->bpm) : (double)clampf(p[P_SEQ_RATE], 10.0f, 280.0f);
    double bps = bpm / 60.0 / SR, now = sq_now(r);
    static const double DIV[8] = {4, 2, 1, 0.5, 0.25, 0.125, 0.0625, 0.03125}, TIMING[3] = {1.0, 1.5, 2.0 / 3.0};
    double step = DIV[(int)clampf(p[P_SEQ_DIV], 0, 7)] * TIMING[(int)clampf(p[P_SEQ_TIMING], 0, 2)];   /* (beats) */
    /* FOLLOW SWING (synced): the master swing, the on-step's share of each pair of steps (50%: none) */
    double sw = sync && p[P_SEQ_FOLLOW_SWING] > 0.5f ? clampf(p[P_CLOCK_SWING] * 0.01f, 0.1f, 0.9f) : 0.5;
    double swoff = (2.0 * sw - 1.0) * step;
    int restep = step != r->step || swoff != r->swoff, jump = 0;
    int64_t e = r->next_k - r->k0;   /* the pattern position of the next step, kept across a new step length */
    r->step = step;
    r->swoff = swoff;
    int follow = sync && s->sp_playing && s->sp_ppq >= 0.0;
    int song_grid = r->running && r->mode == SQ_PLAY && !one_shot;   /* a looping sequence lines up with the song */
    if (follow && r->sp_new) {
        jump = !r->follow || fabs(now - s->sp_ppq) > JUMP_BEATS;
        sq_anchor(r, s->sp_ppq);
        if (jump) {
            r->next_k = sq_first(r, s->sp_ppq - 0.002);   /* (0.002: the host's jitter) */
            if (song_grid) r->k0 = 0;
            else r->k0 = r->next_k - e;
        }
    } else if (bps != r->bps) {
        sq_anchor(r, now);
    }
    r->bps = bps;
    r->follow = follow;
    r->sp_new = 0;
    if (restep && !jump) {
        r->next_k = sq_first(r, sq_now(r));
        r->k0 = follow && song_grid ? 0 : r->next_k - e;
    }
    if (!r->start_pending) return;
    r->start_pending = 0;
    int resume = r->start_resume;
    if (!follow && (r->start_mode == SQ_PLAY || p[P_SEQ_NOTE_RESET] > 0.5f)) {   /* now: its first step at once */
        sq_anchor(r, 0.0);
        r->next_k = 0;
        r->k0 = -resume;
        return;
    }
    double at = sq_now(r);   /* else the next step on the grid, or the last one if it was only just now */
    int64_t k = sq_first(r, at);
    if (at - sq_t(r, k - 1) < LATE_FRAC * (sq_t(r, k) - sq_t(r, k - 1))) k--;
    r->next_k = k;
    r->k0 = follow && r->start_mode == SQ_PLAY && !one_shot ? 0 : k - resume;
}

/* everything due at this sample: note ends, then ratchets' starts, then the step */
static void sq_due(one_t *s) {
    sq_run_t *r = &s->sq;
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < r->nev;) {
            if (r->ev[i].at > s->now || (r->ev[i].kind == SQ_EV_OFF) != (pass == 0)) { i++; continue; }
            int l = r->ev[i].lane, root = r->ev[i].root, vel = r->ev[i].vel, kind = r->ev[i].kind;
            r->ev[i] = r->ev[--r->nev];
            if (kind == SQ_EV_OFF) sq_lane_off(s, l);
            else sq_lane_on(s, l, root, vel, kind == SQ_EV_ON_TIED);
            i = 0;   /* (the list changed order) */
        }
    for (int guard = 0; r->running && !r->start_pending && sq_until(r, sq_t(r, r->next_k)) <= 0; guard++) {
        if (guard == 16) { r->next_k = sq_first(r, sq_now(r)); break; }   /* a huge jump: no burst of notes */
        int64_t k = r->next_k++;
        sq_fire(s, k);
    }
}

/* samples until the next thing it does (at least 1) */
static int64_t sq_next(const one_t *s) {
    const sq_run_t *r = &s->sq;
    int64_t d = INT64_MAX;
    if (r->running && !r->start_pending) d = sq_until(r, sq_t(r, r->next_k));
    for (int i = 0; i < r->nev; i++)
        if (r->ev[i].at - s->now < d) d = r->ev[i].at - s->now;
    return d < 1 ? 1 : d;
}
static int sq_live(const one_t *s) { return s->sq.running || s->sq.nev > 0; }

/* ---- the keys ---- */
static void sq_set_transpose(one_t *s, int note) {
    sq_run_t *r = &s->sq;
    if ((int)s->val[P_SEQ_TR_MODE] == SQT_INTERVAL) { r->transpose = note - 60; return; }   /* from middle C */
    int first = sq_first_note(&r->q, sq_len(s));   /* START KEY: the key becomes the sequence's first note */
    r->transpose = first >= 0 ? note - first : 0;
}

/* recording a key: stopped (or FOCUSED), into the selected step, a chord into its lanes, the selection moving on when
 * every key is up; running, onto the nearest step */
static void sq_record_on(one_t *s, int note, int vel) {
    sq_run_t *r = &s->sq;
    int len = sq_len(s), mode = (int)s->val[P_SEQ_REC_MODE], step, lane;
    if (r->rk_on[note]) return;
    if (r->running && r->cur >= 0 && mode != SQR_FOCUSED) {
        double pos = sq_now(r), t0 = sq_t(r, r->cur_k), t1 = sq_t(r, r->cur_k + 1);
        int64_t k = r->cur_k;
        step = r->cur;
        if (pos - t0 > 0.5 * (t1 - t0)) { step = (step + 1) % len; k++; }   /* nearer the next step */
        if (mode == SQR_REPLACE && r->repl_k[step] != k) {   /* the first note into this step this time round */
            memset(r->q.n[step], SQ_EMPTY, sizeof r->q.n[step]);
            r->q.rest &= ~(1ull << step);
            r->repl_k[step] = k;
        }
        if ((lane = sq_free_lane(&r->q, step)) < 0) return;
        sq_set_note(&r->q, step, lane, note, vel, r->def_gate, 1, 0);
        if (step != r->cur) r->fresh[step] |= (uint8_t)(1u << lane);   /* sounding from the key: not again just now */
        r->rk_t[note] = pos;
    } else {
        step = r->cursor % len;
        if (r->rec_held == 0) {   /* a new entry */
            r->rec_step = step;
            if (mode != SQR_OVERDUB) {
                memset(r->q.n[step], SQ_EMPTY, sizeof r->q.n[step]);
                r->q.rest &= ~(1ull << step);
            }
        }
        step = r->rec_step;
        if ((lane = sq_free_lane(&r->q, step)) < 0) return;
        sq_set_note(&r->q, step, lane, note, vel, r->def_gate, 1, 0);
        r->rk_t[note] = -1.0;   /* (step recording: the gate is the one set, not the time held) */
        r->rec_held++;
    }
    r->rk_on[note] = 1;
    r->rk_step[note] = (int8_t)step;
    r->rk_lane[note] = (int8_t)lane;
    r->rev++;
}
static void sq_record_off(one_t *s, int note) {
    sq_run_t *r = &s->sq;
    if (!r->rk_on[note]) return;
    r->rk_on[note] = 0;
    int step = r->rk_step[note], lane = r->rk_lane[note];
    if (r->rk_t[note] >= 0.0) {   /* real time: the gate as long as the key was held */
        sq_note_t *n = &r->q.n[step][lane];
        if (n->note == note && r->step > 0.0) {
            double g = (sq_now(r) - r->rk_t[note]) / r->step * 100.0;
            n->gate = (uint8_t)clampf((float)round(g), 1.0f, 100.0f);
            r->rev++;
        }
        return;
    }
    if (r->rec_held > 0 && --r->rec_held == 0 && !(r->running && (int)s->val[P_SEQ_REC_MODE] == SQR_FOCUSED)) {
        r->cursor = (r->rec_step + 1) % sq_len(s);   /* the chord is in: on to the next step */
        r->rev++;
    }
}

/* a key before the arpeggiator and chord memory see it: 1 = the sequencer took it (NOTE TRIG / TRANSPOSE: it doesn't
 * sound). Recording takes notes and lets them sound too. */
static int sq_key_on(one_t *s, int note, int vel) {
    sq_run_t *r = &s->sq;
    if (s->val[P_SEQ_REC] > 0.5f) { sq_record_on(s, note, vel); return 0; }
    int tr = s->val[P_SEQ_TRANSPOSE] > 0.5f, trig = s->val[P_SEQ_NOTE_TRIG] > 0.5f;
    if (!sq_output(s) || (!tr && !trig)) return 0;
    int first = r->ntrig == 0;
    if (!r->trig[note]) { r->trig[note] = 1; r->ntrig++; }
    if (tr) sq_set_transpose(s, note);
    if (trig && first) {
        if (!r->running) {   /* RESTART ON TRIG: from step 1, else on from where it stopped */
            sq_start(s, SQ_TRIG, s->val[P_SEQ_RESTART_TRIG] > 0.5f ? 0 : r->resume % sq_len(s));
        } else if (r->mode == SQ_TRIG && s->val[P_SEQ_RESTART_HOLD] > 0.5f) {   /* HOLD kept it running */
            sq_start(s, SQ_TRIG, 0);
        }
    }
    return 1;
}
static int sq_key_off(one_t *s, int note) {
    sq_run_t *r = &s->sq;
    sq_record_off(s, note);
    if (!r->trig[note]) return 0;
    r->trig[note] = 0;
    r->ntrig--;
    if (r->ntrig == 0 && r->running && r->mode == SQ_TRIG && !hold_on(s)) sq_stop(s);   /* NOTE TRIG: every key up */
    return 1;
}

/* ---- the switches: what turning each on or off does (after any set_param; k = -1: a whole state) ---- */
static void sq_sync_params(one_t *s) {
    sq_run_t *r = &s->sq;
    const float *p = s->val;
    int on = p[P_SEQ_ON] > 0.5f, play = p[P_SEQ_PLAY] > 0.5f, trig = p[P_SEQ_NOTE_TRIG] > 0.5f;
    int tr = p[P_SEQ_TRANSPOSE] > 0.5f, rec = p[P_SEQ_REC] > 0.5f, sm = p[P_SEQ_STEP_MOD] > 0.5f, hold = hold_on(s);
    if (trig && !r->was_trig && play) {   /* NOTE TRIG and START/STOP: selecting one disables the other */
        s->val[P_SEQ_PLAY] = 0.0f;
        play = 0;
        s->changed |= 1u << SQ_CHG_PLAY;
    } else if (play && !r->was_play && trig) {
        s->val[P_SEQ_NOTE_TRIG] = 0.0f;
        trig = 0;
        s->changed |= 1u << SQ_CHG_TRIG;
    }
    if (play && !r->was_play) sq_start(s, SQ_PLAY, 0);   /* START: from the first step */
    else if (!play && r->running && r->mode == SQ_PLAY) sq_stop(s);
    if (!trig && r->running && r->mode == SQ_TRIG) sq_stop(s);
    if (!hold && r->was_hold && r->running && r->mode == SQ_TRIG && r->ntrig == 0) sq_stop(s);   /* HOLD let go */
    if (!on && r->was_on) sq_silence(s);
    if ((!tr && r->was_tr) || (!trig && !tr)) {   /* the keys play the synth again */
        if (!tr) r->transpose = 0;
        memset(r->trig, 0, sizeof r->trig);
        r->ntrig = 0;
    }
    if (!rec && r->was_rec) r->rec_held = 0;
    r->was_on = on; r->was_play = play; r->was_trig = trig; r->was_tr = tr; r->was_rec = rec; r->was_sm = sm;
    r->was_hold = hold;
    sq_moff_refresh(s);
}

/* STEP MOD: a knob's move becomes the step's offset for its destination (1: taken; the knob itself stays) */
static int sq_step_mod(one_t *s, int k, const char *val) {
    sq_run_t *r = &s->sq;
    if (k < 0 || s->val[P_SEQ_STEP_MOD] < 0.5f || s->sq_md[k] < 0) return 0;
    float v = (float)atof(val);
    if (!isfinite(v)) return 1;
    int d = s->sq_md[k], step = r->running && r->cur >= 0 ? r->cur : r->cursor % sq_len(s), lane = -1;
    for (int l = 0; l < SQ_ML && lane < 0; l++)
        if (r->q.m[l].dest == d) lane = l;
    for (int l = 0; l < SQ_ML && lane < 0; l++)
        if (!r->q.m[l].dest) { lane = l; sq_clear_mlane(&r->q.m[l]); r->q.m[l].dest = (uint16_t)d; }
    if (lane < 0) return 1;   /* every lane is in use */
    float off = clampf((v - s->val[k]) * s->sq_sc[k], -1.0f, 1.0f);
    r->q.m[lane].off[step] = (int8_t)lroundf(off * 100.0f);
    sq_lanes_changed(s);
    return 1;
}

/* the knobs STEP MOD can record, and how far each moves its destination: 1 / the knob's travel for +-100% of it */
static void sq_map_init(one_t *s) {
    for (int k = 0; k < P_COUNT; k++) s->sq_md[k] = -1;
#define SQ_MAP(k, d, per100) (s->sq_md[(k)] = (short)(d), s->sq_sc[(k)] = 1.0f / (per100))
    for (int n = 0; n < NOSC; n++) {
        int o = P_O1_WAVE + OSC_STRIDE * n;
        SQ_MAP(o + O_FREQ, MD_O1_PITCH + n, MOD_PITCH_ST);   /* semitones */
        SQ_MAP(o + O_BEAT, MD_O1_BEAT + n, BEAT_HZ);
        SQ_MAP(o + O_ANGLE, MD_O1_ANGLE + n, 100.0f);
        SQ_MAP(o + O_PW, MD_O1_PW + n, 90.0f);   /* (the PW destination moves 90% at full) */
        SQ_MAP(o + O_MIX, MD_O1_MIX + n, 100.0f);
    }
    for (int c = 0; c < NSRC; c++) SQ_MAP(LEVEL_SLOT[c], LEVEL_MD[c], 100.0f);
    SQ_MAP(P_FM_AMT, MD_FM_AMT, 100.0f);
    SQ_MAP(P_FLT_CUT, MD_LAD_CUT, 100.0f / 10.0f * MOD_CUT_OCT);   /* the knob spans 10 octaves */
    SQ_MAP(P_SVF_CUT, MD_SVF_CUT, 100.0f / 10.0f * MOD_CUT_OCT);
    SQ_MAP(P_FLT_RES, MD_LAD_RES, 100.0f);
    SQ_MAP(P_FLT_DRIVE, MD_LAD_DRIVE, 100.0f);
    SQ_MAP(P_SVF_RES, MD_SVF_RES, 100.0f);
    SQ_MAP(P_FLT_MIX, MD_FLT_MIX, 100.0f);
    SQ_MAP(P_SVF_SPACE, MD_SVF_SPACE, 100.0f);
    SQ_MAP(P_PITCH_LFO, MD_PITCH_LFO1, 100.0f);
    SQ_MAP(P_PITCH_ENV, MD_PITCH_ENV, 100.0f);
    SQ_MAP(P_ANGLE_LFO3, MD_ANGLE_LFO3, 100.0f);
    SQ_MAP(P_PW_LFO3, MD_PW_LFO3, 100.0f);
    SQ_MAP(P_SVF_ENV, MD_SVF_ENV, 100.0f);
    SQ_MAP(P_SVF_LFO, MD_SVF_LFO2, 100.0f);
    SQ_MAP(P_SVF_FM, MD_SVF_FM, 100.0f);
    SQ_MAP(P_LAD_ENV, MD_LAD_ENV, 100.0f);
    SQ_MAP(P_LAD_LFO, MD_LAD_LFO2, 100.0f);
    SQ_MAP(P_LAD_FM, MD_LAD_FM, 100.0f);
    for (int n = 0; n < NENV; n++) {
        int e = P_ENV_A + ENV_STRIDE * n, x = P_FENV_DELAY + EX_STRIDE * n, md = MD_FENV_DELAY + MDE_COUNT * n;
        SQ_MAP(x + X_DELAY, md + MDE_DELAY, 100.0f);
        SQ_MAP(e + 0, md + MDE_ATTACK, 100.0f);
        SQ_MAP(x + X_HOLD, md + MDE_HOLD, 100.0f);
        SQ_MAP(e + 1, md + MDE_DECAY, 100.0f);
        SQ_MAP(e + 2, md + MDE_SUSTAIN, 100.0f);
        SQ_MAP(e + 3, md + MDE_RELEASE, 100.0f);
        SQ_MAP(x + X_SCALE, md + MDE_SCALE, 100.0f);
    }
    for (int n = 0; n < NLFO; n++) {
        int x = P_LFO1_RANGE + LX_STRIDE * n, md = MD_LFO1_RATE + MDL_COUNT * n;
        SQ_MAP(lfo_base(n), md + MDL_RATE, 100.0f);
        SQ_MAP(x + L_VAR, md + MDL_VAR, 100.0f);
        SQ_MAP(x + L_DELAY, md + MDL_DELAY, 100.0f);
        SQ_MAP(x + L_FADE_IN, md + MDL_FADE_IN, 100.0f);
        SQ_MAP(x + L_FADE_OUT, md + MDL_FADE_OUT, 100.0f);
    }
    SQ_MAP(P_PAN, MD_PAN, 100.0f);
    SQ_MAP(P_NOISE_COLOR_MIX, MD_NOISE_MIX, 100.0f);
    SQ_MAP(P_DRIFT, MD_DRIFT, 100.0f);
    SQ_MAP(P_SPREAD, MD_SPREAD, 100.0f);
    SQ_MAP(P_MAC_HARM, MD_MAC_HARM, 100.0f);
    SQ_MAP(P_MAC_TIMBRE, MD_MAC_TIMBRE, 100.0f);
    SQ_MAP(P_MAC_MORPH, MD_MAC_MORPH, 100.0f);
    /* (2026-10-29) the manual's step-mod-only list: footages, the noise EG, the EG switches, the LFO waves (a switch or
     * list's move becomes as many steps of it) */
    for (int n = 0; n < NOSC; n++) SQ_MAP(P_O1_WAVE + OSC_STRIDE * n + O_OCT, MD_O1_OCT + n, (float)(NOCT - 1));
    SQ_MAP(P_NOISE_A, MD_NOISE_A, 100.0f);
    SQ_MAP(P_NOISE_SUS, MD_NOISE_SUS, 1.0f);
    SQ_MAP(P_NOISE_R, MD_NOISE_R, 100.0f);
    for (int n = 0; n < NENV; n++) {
        int x = P_FENV_DELAY + EX_STRIDE * n, md = MD_FENV_MULTI + MDS_COUNT * n;
        SQ_MAP(x + X_MULTI, md + MDS_MULTI, 1.0f);
        SQ_MAP(x + X_SYNC, md + MDS_SYNC, 1.0f);
        SQ_MAP(x + X_LOOP, md + MDS_LOOP, 1.0f);
        SQ_MAP(x + X_LATCH, md + MDS_LATCH, 1.0f);
    }
    for (int n = 0; n < NLFO; n++) SQ_MAP(lfo_base(n) + 1, MD_LFO1_WAVE + n, (float)(LFO_WAVES - 1));
    for (int k = 0; k < MDF_COUNT; k++)   /* the effect controls: +-100% of their range */
        if (s->fx_hi[k] > s->fx_lo[k]) SQ_MAP(FXMD_SLOT[k], MD_FX_FIRST + k, s->fx_hi[k] - s->fx_lo[k]);
#undef SQ_MAP
}

/* ---- edits from the window (set_param "state" = "seqcmd\n<command>\n..."): one command per line ----
 *   note S L N V G R T   step S (0-63), lane L: note N (-1: none), velocity, gate %, ratchet 1-4, tie 0/1
 *   rest S 0|1           clearstep S            clear            dup X Y (steps X..Y after the length)
 *   lane L D C A         modulation lane L: destination (0: remove the lane), controller, amount %
 *   off L S V            lane L's offset on step S (-100..100; x: none)
 *   sel S L              the selected step and lane            gate G   new notes' gate %
 *   rtz                  RETURN TO ZERO              load <text>  a whole sequence (src/stepseq.h's form)
 * Bad numbers are clamped; an unknown command is skipped. */
static void sq_command(one_t *s, const char *text) {
    sq_run_t *r = &s->sq;
    int lanes = 0;
    while (*text) {
        char line[SQ_TEXT_MAX + 16], word[16];
        int n = 0;
        while (*text && *text != '\n' && n < (int)sizeof line - 1) line[n++] = *text++;
        line[n] = 0;
        while (*text && *text != '\n') text++;   /* (an over-long line is cut) */
        if (*text) text++;
        int a[7] = {0}, got = 0, used = 0;
        if (sscanf(line, "%15s%n", word, &used) != 1) continue;
        const char *args = line + used;
        if (!strcmp(word, "load")) {
            while (*args == ' ') args++;
            if (sq_decode(args, &r->q, MD_COUNT, MS_COUNT)) lanes = 1;
            continue;
        }
        if (!strcmp(word, "off")) {   /* (its value may be "x") */
            char v[8] = "";
            if (sscanf(args, "%d %d %7s", &a[0], &a[1], v) == 3 && a[0] >= 0 && a[0] < SQ_ML && a[1] >= 0 && a[1] < SQ_STEPS) {
                r->q.m[a[0]].off[a[1]] = v[0] == 'x' ? SQ_NO_OFF : (int8_t)clampf((float)atoi(v), -100.0f, 100.0f);
                lanes = 1;
            }
            continue;
        }
        got = sscanf(args, "%d %d %d %d %d %d %d", &a[0], &a[1], &a[2], &a[3], &a[4], &a[5], &a[6]);
        int st = a[0] < 0 ? 0 : a[0] >= SQ_STEPS ? SQ_STEPS - 1 : a[0];
        if (!strcmp(word, "note") && got == 7 && a[1] >= 0 && a[1] < SQ_NL) {
            if (a[2] < 0 || a[2] > 127) r->q.n[st][a[1]].note = SQ_EMPTY;
            else sq_set_note(&r->q, st, a[1], a[2], a[3], a[4], a[5], a[6]);
        } else if (!strcmp(word, "rest") && got == 2) {
            r->q.rest = (r->q.rest & ~(1ull << st)) | ((uint64_t)(a[1] != 0) << st);
        } else if (!strcmp(word, "clearstep") && got == 1) {
            sq_clear_step(&r->q, st);
            lanes = 1;
        } else if (!strcmp(word, "clear")) {
            sq_clear(&r->q);
            lanes = 1;
        } else if (!strcmp(word, "dup") && got == 2) {
            int to = sq_duplicate(&r->q, st, a[1] < st ? st : a[1], sq_len(s));
            if (to != sq_len(s)) { s->val[P_SEQ_LENGTH] = (float)to; s->changed |= 1u << SQ_CHG_LENGTH; }
            lanes = 1;
        } else if (!strcmp(word, "lane") && got == 4 && a[0] >= 0 && a[0] < SQ_ML) {
            sq_mlane_t *m = &r->q.m[a[0]];
            if (a[1] <= MD_OFF || a[1] >= MD_COUNT) sq_clear_mlane(m);
            else {
                m->dest = (uint16_t)a[1];
                m->ctl = (uint8_t)(a[2] > 0 && a[2] < MS_COUNT ? a[2] : 0);
                m->amt = (uint8_t)clampf((float)a[3], 0.0f, 100.0f);
            }
            lanes = 1;
        } else if (!strcmp(word, "sel") && got >= 1) {
            r->cursor = st;
            r->cursor_lane = got > 1 && a[1] >= 0 && a[1] < SQ_NL ? a[1] : 0;
            lanes = 1;   /* (STEP MOD's stopped preview follows the selection) */
        } else if (!strcmp(word, "gate") && got == 1) {
            r->def_gate = (int)clampf((float)a[0], 1.0f, 100.0f);
        } else if (!strcmp(word, "rtz")) {   /* RETURN TO ZERO: step 1 next */
            if (r->running) r->k0 = r->next_k;
            r->resume = r->cursor = 0;
        }
    }
    if (lanes) sq_lanes_changed(s);
    else r->rev++;
}

/* a factory sequence (seq_pattern; User loads nothing), with its length */
static void sq_load_factory(one_t *s, int id) {
    if (id <= SQF_USER || id >= SQF_COUNT) return;
    int len = sq_factory(&s->sq.q, id, MD_LAD_CUT, MD_SVF_CUT, MS_WHEEL);
    s->val[P_SEQ_LENGTH] = (float)len;
    s->changed |= 1u << SQ_CHG_LENGTH;
    sq_lanes_changed(s);
}

/* "seq_pos": what MPC's screen (and the window) shows: "Step 5/16" while it runs, "Edit 7/16" (the selected step)
 * while stopped, or that the last saved project had no room for the sequence */
static int sq_readout(const one_t *s, char *buf, int n) {
    const sq_run_t *r = &s->sq;
    if (r->lost) return snprintf(buf, n, "Too long to save");
    if (r->running && r->cur >= 0) return snprintf(buf, n, "Step %d/%d", r->cur + 1, sq_len(s));
    return snprintf(buf, n, "Edit %d/%d", r->cursor % sq_len(s) + 1, sq_len(s));
}

static void sq_init(one_t *s) {
    sq_run_t *r = &s->sq;
    sq_clear(&r->q);
    r->bps = 120.0 / 60.0 / SR;
    r->step = 0.25;
    r->def_gate = 50;
    r->cur = -1;
    for (int l = 0; l < SQ_NL; l++) r->lroot[l] = -1;
    for (int i = 0; i < SQ_STEPS; i++) r->repl_k[i] = INT64_MIN;
    sq_map_init(s);
}
