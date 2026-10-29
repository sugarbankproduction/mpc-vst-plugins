/* The step sequencer's offline checks (src/stepseq.h, src/stepseq_run.h): included by offline_check.c after the
 * arpeggiator's, so it uses that file's helpers (check, set, note, patch, gen_render, gen_pick, step_at, setkv, get ...).
 * Notes are read from the engine's "gen_log" (every generated note on and off, by sample). */
#include "stepseq.h"

static uint32_t sq_rnd(uint32_t *x) { *x ^= *x << 13; *x ^= *x >> 17; *x ^= *x << 5; return *x; }

/* commands to the sequencer, as the window sends them ("seqcmd" through the state key) */
static void seqcmd(void *e, const char *cmds) {
    static char b[65536];
    snprintf(b, sizeof b, "seqcmd\n%s", cmds);
    E->set_param(e, "state", b);
}
static void *seq_patch(double bpm) {
    void *e = patch(SAW, 1, 100, 80, 20);
    set(e, "voices", 7);
    char b[32];
    snprintf(b, sizeof b, "%g", bpm);
    E->set_param(e, "lfo_bpm", b);
    set(e, "seq_on", 1);
    return e;
}
/* note-ons and offs from n events into arrays (at most max each) */
typedef struct { int n; long long t[256]; int note[256], vel[256]; } evs_t;
static void seq_events(int n, evs_t *on, evs_t *off) {
    on->n = gen_pick(n, 1, on->note, on->t, on->vel, 256);
    if (off) off->n = gen_pick(n, 0, off->note, off->t, NULL, 256);
}
static long long beats_at(double bpm, double beats) { return (long long)ceil(beats * 60.0 * SR / bpm - 1e-6); }
static double rms_range(const float *x, int from, int to) {
    double e = 0;
    for (int i = from; i < to; i++) e += x[i] * x[i];
    return sqrt(e / (to - from));
}
#ifndef STATE_MAX
#define STATE_MAX 8192   /* the state chunk as the build configures it (the engine's STATE_MAX; MPC: 8192) */
#endif
static int seq_text(void *e, char *out, int n) {   /* the "seq=" part of the state, or "" */
    static char st[STATE_MAX + 16];
    out[0] = 0;
    if (E->get_param(e, "state", st, STATE_MAX) <= 0) return -1;
    const char *p = strstr(st, ";seq=");
    if (!p) return 0;
    p += 5;
    int k = 0;
    while (p[k] && p[k] != ';' && k < n - 1) { out[k] = p[k]; k++; }
    out[k] = 0;
    return k;
}

static void step_sequencer(void) {
    char what[512];
    evs_t on, off;

    /* timing: four steps (velocity and gate per note) at 120 and 90 BPM, started off the block grid */
    for (int c = 0; c < 2; c++) {
        double bpm = c ? 90 : 120;
        void *e = seq_patch(bpm);
        seqcmd(e, "note 0 0 60 100 50 1 0\nnote 1 0 62 90 25 1 0\nnote 2 0 64 80 75 1 0\nnote 3 0 65 70 100 1 0");
        set(e, "seq_length", 4);
        int16_t buf[256];
        E->render(e, buf, 100);
        E->render(e, buf, 77);
        set(e, "seq_play", 1);
        int n = gen_render(e, SR * 3, 0);
        seq_events(n, &on, &off);
        static const int want[4] = {60, 62, 64, 65}, wv[4] = {100, 90, 80, 70}, wg[4] = {50, 25, 75, 100};
        int ok = on.n >= 12 && off.n >= 11;
        for (int k = 0; k < 12 && ok; k++) {
            ok &= on.t[k] == 177 + step_at(bpm, 0.25, k, 0) && on.note[k] == want[k % 4] && on.vel[k] == wv[k % 4];
            ok &= off.t[k] == 177 + beats_at(bpm, 0.25 * k + 0.25 * wg[k % 4] / 100.0) && off.note[k] == want[k % 4];
        }
        snprintf(what, sizeof what, "seq %g BPM: 12 steps of 1/16 on the exact sample (step 1 at %lld, step 2 at %lld), "
                 "note, velocity and gate (50/25/75/100%%) per step, looping after 4", bpm, on.n ? on.t[0] : -1,
                 on.n > 1 ? on.t[1] : -1);
        check(ok, what);
        set(e, "seq_play", 0);
        n = gen_render(e, SR, 0);
        seq_events(n, &on, &off);
        check(on.n == 0 && silent_after(e, 300), "seq STOP: no more notes, nothing hangs");
        E->destroy(e);
    }

    /* length and pages: 20 steps, a note on page 2; loops after 20 */
    {
        void *e = seq_patch(120);
        seqcmd(e, "note 0 0 60 100 50 1 0\nnote 17 0 72 100 50 1 0");
        set(e, "seq_length", 20);
        set(e, "seq_play", 1);
        int n = gen_render(e, SR * 7, 0);
        seq_events(n, &on, NULL);
        snprintf(what, sizeof what, "seq length 20: step 1, step 18 (page 2) at %lld, step 1 again after 20 steps at %lld",
                 on.n > 1 ? on.t[1] : -1, on.n > 2 ? on.t[2] : -1);
        check(on.n >= 4 && on.note[0] == 60 && on.note[1] == 72 && on.t[1] == step_at(120, 0.25, 17, 0)
              && on.note[2] == 60 && on.t[2] == step_at(120, 0.25, 20, 0) && on.t[3] == step_at(120, 0.25, 37, 0), what);
        E->destroy(e);
    }

    /* rest, tie, ratchets, polyphonic steps */
    {
        void *e = seq_patch(120);
        seqcmd(e, "note 0 0 60 100 50 1 0\nnote 1 0 62 100 50 1 0\nrest 1 1\nnote 2 0 64 100 50 1 1\nnote 3 0 64 100 50 1 0\n"
                  "note 4 0 67 100 40 1 1\nnote 5 0 69 100 50 1 0\n"
                  "note 6 0 48 100 50 1 0\nnote 6 1 52 100 50 1 0\nnote 6 2 55 100 50 1 0");
        set(e, "seq_length", 8);
        set(e, "seq_play", 1);
        int n = gen_render(e, SR * 2, 0);   /* 1 s = 8 steps at 1/16, 120 BPM: one pass */
        seq_events(n, &on, &off);
        long long st = step_at(120, 0.25, 1, 0);
        int rest_ok = on.n >= 2 && on.note[1] != 62;
        for (int i = 0; i < on.n; i++) rest_ok &= on.note[i] != 62;
        check(rest_ok, "seq REST: step 2 (a note kept on it) plays nothing");
        /* the tie: 64 on step 3 carries on into step 4 (no second attack), ends at step 4's gate */
        int n64 = 0;
        long long off64 = -1;
        long long pass = step_at(120, 0.25, 8, 0);   /* (the first time through) */
        for (int i = 0; i < on.n; i++) n64 += on.note[i] == 64 && on.t[i] < pass;
        for (int i = 0; i < off.n; i++) if (off.note[i] == 64 && off64 < 0) off64 = off.t[i];
        snprintf(what, sizeof what, "seq TIE: the same note on the next step goes on without a new attack (%d note-on), "
                 "its end at the next step's gate (%lld, want %lld)", n64, off64, beats_at(120, 0.25 * 3 + 0.125));
        check(n64 == 1 && off64 == beats_at(120, 0.25 * 3 + 0.125), what);
        /* a tie into another note: the new note starts as the old one lets go, on step 6's sample */
        long long on69 = -1, off67 = -1;
        for (int i = 0; i < on.n; i++) if (on.note[i] == 69 && on69 < 0) on69 = on.t[i];
        for (int i = 0; i < off.n; i++) if (off.note[i] == 67 && off67 < 0) off67 = off.t[i];
        snprintf(what, sizeof what, "seq TIE into another note: 67 lets go as 69 starts, legato (both at %lld / %lld, want %lld)",
                 on69, off67, step_at(120, 0.25, 5, 0));
        check(on69 == step_at(120, 0.25, 5, 0) && off67 == on69, what);
        int chord = 0;
        for (int i = 0; i < on.n; i++)
            if (on.t[i] == step_at(120, 0.25, 6, 0)) chord |= (on.note[i] == 48) | (on.note[i] == 52) << 1 | (on.note[i] == 55) << 2;
        check(chord == 7, "seq polyphonic step: three lanes start together on step 7");
        (void)st;
        E->destroy(e);
    }
    for (int r = 1; r <= 4; r++) {
        void *e = seq_patch(120);
        char c[96];
        snprintf(c, sizeof c, "note 0 0 60 100 50 %d 0", r);
        seqcmd(e, c);
        set(e, "seq_length", 4);
        set(e, "seq_play", 1);
        int n = gen_render(e, 5000, 0);   /* less than one step */
        seq_events(n, &on, &off);
        int ok = on.n == r && off.n == r;
        for (int i = 0; i < r && ok; i++)
            ok &= on.t[i] == beats_at(120, 0.25 * i / r) && off.t[i] == beats_at(120, 0.25 * i / r + 0.5 * 0.25 / r);
        snprintf(what, sizeof what, "seq RATCHET %d: %d notes evenly inside the step, each half its share", r, on.n);
        check(ok, what);
        E->destroy(e);
    }

    /* transpose: start key vs interval; the key itself doesn't sound */
    for (int mode = 0; mode < 2; mode++) {
        void *e = seq_patch(120);
        seqcmd(e, "note 0 0 48 100 50 1 0\nnote 1 0 55 100 50 1 0");
        set(e, "seq_length", 2);
        set(e, "seq_transpose", 1);
        set(e, "seq_tr_mode", mode);
        set(e, "seq_play", 1);
        gen_render(e, 1000, 0);
        note(e, 1, 65, 100);   /* F above middle C */
        gen_events(e, 0);
        int n = gen_render(e, SR, 0);
        seq_events(n, &on, NULL);
        int add = mode ? 5 : 17, ok = on.n >= 4;   /* (the key itself would be a plain note: not in the log) */
        for (int i = 0; i < on.n; i++) ok &= on.note[i] == 48 + add || on.note[i] == 55 + add;
        snprintf(what, sizeof what, "seq TRANSPOSE %s: key 65 moves the sequence %+d (notes %d %d), the key itself silent",
                 mode ? "Interval (from middle C)" : "Start Key", add, on.note[0], on.n > 1 ? on.note[1] : -1);
        check(ok, what);
        note(e, 0, 65, 0);
        E->destroy(e);
    }

    /* note trig: a key starts it at once and letting go stops it; off, keys play plainly and it doesn't start */
    {
        void *e = seq_patch(120);
        seqcmd(e, "note 0 0 60 100 50 1 0\nnote 1 0 62 100 50 1 0\nnote 2 0 64 100 50 1 0\nnote 3 0 65 100 50 1 0");
        set(e, "seq_length", 4);
        set(e, "seq_note_trig", 1);
        gen_render(e, 3000, 0);
        note(e, 1, 50, 100);
        int n = gen_render(e, step_at(120, 0.25, 2, 0) + 100, 0);
        seq_events(n, &on, NULL);
        snprintf(what, sizeof what, "seq NOTE TRIG: a key starts the sequence at once (step 1 at %lld, want 3000), notes 60 62",
                 on.n ? on.t[0] : -1);
        check(on.n == 3 && on.t[0] == 3000 && on.note[0] == 60 && on.note[1] == 62, what);
        note(e, 0, 50, 0);
        n = gen_render(e, SR, 0);
        seq_events(n, &on, NULL);
        check(on.n == 0 && silent_after(e, 300), "seq NOTE TRIG: letting go of every key stops it");
        /* RESTART ON TRIG off: it carries on from the step after the last one played (step 4) */
        set(e, "seq_restart_trig", 0);
        note(e, 1, 50, 100);
        n = gen_render(e, 2000, 0);
        seq_events(n, &on, NULL);
        snprintf(what, sizeof what, "seq RESTART ON TRIG off: the next trigger resumes on step 4 (note %d, want 65)",
                 on.n ? on.note[0] : -1);
        check(on.n >= 1 && on.note[0] == 65, what);
        note(e, 0, 50, 0);
        set(e, "seq_restart_trig", 1);
        note(e, 1, 50, 100);
        n = gen_render(e, 2000, 0);
        seq_events(n, &on, NULL);
        check(on.n >= 1 && on.note[0] == 60, "seq RESTART ON TRIG on: each trigger starts from step 1");
        note(e, 0, 50, 0);
        gen_render(e, SR / 2, 0);
        /* NOTE RESET off: the key waits for the next 1/16 of the clock (a fresh instance: the clock from sample 0) */
        void *g = seq_patch(120);
        seqcmd(g, "note 0 0 60 100 50 1 0");
        setkv(g, "seq_length=4,seq_note_trig=1,seq_note_reset=0");
        gen_render(g, 2000, 0);
        note(g, 1, 50, 100);
        n = gen_render(g, SR / 2, 0);
        seq_events(n, &on, NULL);
        snprintf(what, sizeof what, "seq NOTE RESET off: a key at sample 2000 starts on the next 1/16 (sample %lld, want %lld)",
                 on.n ? on.t[0] : -1, step_at(120, 0.25, 1, 0));
        check(on.n >= 1 && on.t[0] == step_at(120, 0.25, 1, 0) && on.note[0] == 60, what);
        E->destroy(g);
        /* HOLD keeps it running; RESTART ON HOLD on restarts it with the next key */
        set(e, "hold", 1);
        gen_render(e, SR / 2, 0);
        note(e, 1, 50, 100);
        note(e, 0, 50, 0);
        n = gen_render(e, SR, 0);
        seq_events(n, &on, NULL);
        int held = on.n;
        set(e, "seq_restart_hold", 1);
        gen_render(e, step_at(120, 0.25, 1, 0) + 100, 0);   /* (on step 2 or 3 now) */
        gen_events(e, 0);
        note(e, 1, 52, 100);
        n = gen_render(e, 1000, 0);
        seq_events(n, &on, NULL);
        snprintf(what, sizeof what, "seq HOLD keeps NOTE TRIG running with no key (%d notes in 1 s); RESTART ON HOLD: a new "
                 "key starts again from step 1 (note %d)", held, on.n ? on.note[0] : -1);
        check(held >= 7 && on.n >= 1 && on.note[0] == 60, what);
        note(e, 0, 52, 0);
        set(e, "hold", 0);
        n = gen_render(e, SR / 2, 0);
        seq_events(n, &on, NULL);
        check(on.n <= 1 && silent_after(e, 300), "seq HOLD off with no key held: it stops");
        /* START/STOP and NOTE TRIG exclude each other */
        set(e, "seq_play", 1);
        snprintf(what, sizeof what, "seq PLAY turns NOTE TRIG off (note trig now %g)", get(e, "seq_note_trig"));
        check(get(e, "seq_note_trig") == 0, what);
        set(e, "seq_note_trig", 1);
        check(get(e, "seq_play") == 0, "seq NOTE TRIG turns PLAY off");
        E->destroy(e);
        void *f = seq_patch(120);   /* note trig off: a key plays plainly, nothing starts */
        seqcmd(f, "note 0 0 60 100 50 1 0");
        note(f, 1, 50, 100);
        n = gen_render(f, SR / 2, 0);
        seq_events(n, &on, NULL);
        check(on.n == 0 && !silent_after(f, 10), "seq NOTE TRIG off: a key plays the synth and doesn't start the sequence");
        E->destroy(f);
    }

    /* one shot: plays to the end once, PLAY reported back off */
    {
        void *e = seq_patch(120);
        seqcmd(e, "note 0 0 60 100 50 1 0\nnote 1 0 62 100 50 1 0\nnote 2 0 64 100 50 1 0");
        set(e, "seq_length", 3);
        set(e, "seq_one_shot", 1);
        set(e, "seq_play", 1);
        int n = gen_render(e, SR * 2, 0);
        seq_events(n, &on, NULL);
        char key[64], keys_seen[256] = "";
        for (int i = 0; i < 12 && mpc_engine_changed(e, key, sizeof key) > 0; i++) {
            strncat(keys_seen, key, sizeof keys_seen - strlen(keys_seen) - 2);
            strncat(keys_seen, " ", sizeof keys_seen - strlen(keys_seen) - 1);
        }
        snprintf(what, sizeof what, "seq ONE SHOT: 3 steps once (%d notes), then stopped; PLAY off and reported (%s)", on.n,
                 keys_seen);
        check(on.n == 3 && get(e, "seq_play") == 0 && strstr(keys_seen, "seq_play"), what);
        E->destroy(e);
    }

    /* follow swing, divisions and timing, the free rate */
    {
        static const struct { const char *kv; double bpm, step, swing; const char *name; } T[] = {
            {"clock_swing=75", 120, 0.25, 0.125, "FOLLOW SWING at 75%: every second step 1/8 beat late"},
            {"clock_swing=75,seq_follow_swing=0", 120, 0.25, 0, "FOLLOW SWING off: straight"},
            {"seq_div=3", 120, 0.5, 0, "division 1/8"},
            {"seq_div=3,seq_timing=2", 120, 1.0 / 3, 0, "1/8 triplets"},
            {"seq_div=2,seq_timing=1", 90, 1.5, 0, "1/4 dotted at 90 BPM"},
            {"seq_sync=0,seq_rate=100", 100, 0.25, 0, "free rate 100 BPM (host at 120)"},
        };
        for (unsigned c = 0; c < sizeof T / sizeof T[0]; c++) {
            void *e = seq_patch(T[c].bpm == 100 ? 120 : T[c].bpm);
            seqcmd(e, "note 0 0 60 100 30 1 0\nnote 1 0 62 100 30 1 0");
            set(e, "seq_length", 2);
            setkv(e, T[c].kv);
            set(e, "seq_play", 1);
            int n = gen_render(e, SR * 6, 0);
            seq_events(n, &on, NULL);
            int ok = on.n >= 6;
            for (int k = 0; k < 6 && ok; k++) ok &= on.t[k] == step_at(T[c].bpm, T[c].step, k, T[c].swing);
            snprintf(what, sizeof what, "seq %s: steps at %lld %lld %lld", T[c].name, on.n > 0 ? on.t[0] : -1,
                     on.n > 1 ? on.t[1] : -1, on.n > 2 ? on.t[2] : -1);
            check(ok, what);
            E->destroy(e);
        }
    }

    /* the host's song position: a running sequence lines up with the song; a loop back plays from there */
    {
        void *e = seq_patch(120);
        for (int k = 0; k < 16; k++) {
            char c[64];
            snprintf(c, sizeof c, "note %d 0 %d 100 50 1 0", k, 60 + k);
            seqcmd(e, c);
        }
        set(e, "seq_play", 1);
        double ppq = 3.9;
        int n = gen_render_pos(e, 128 * 80, 0, &ppq, 120);
        seq_events(n, &on, NULL);
        snprintf(what, sizeof what, "seq song position 3.9: step 1 on beat 4.0 (sample %lld, want 2205), step 2 on 4.25 "
                 "(%lld, want 7718)", on.n ? on.t[0] : -1, on.n > 1 ? on.t[1] : -1);
        check(on.n > 1 && on.t[0] == 2205 && on.note[0] == 60 && on.t[1] == 7718 && on.note[1] == 61, what);
        long long jump_at = 128 * 80;
        ppq = 1.0;   /* the host loops back to beat 1: step 5 there */
        n = gen_render_pos(e, 128 * 60, 0, &ppq, 120);
        seq_events(n, &on, NULL);
        snprintf(what, sizeof what, "seq a loop back to beat 1 plays step 5 at once (note %d at %lld after the jump)",
                 on.n ? on.note[0] : -1, on.n ? on.t[0] - jump_at : -1);
        check(on.n > 1 && on.note[0] == 64 && on.t[0] == jump_at && on.note[1] == 65, what);
        E->destroy(e);
    }

    /* modulation lanes: a lane to the amp level silences one step of a held note; its controller scales it */
    {
        static float x[SR * 2];
        double r[3][4];
        for (int c = 0; c < 3; c++) {
            void *e = seq_patch(120);
            char cmd[128];
            snprintf(cmd, sizeof cmd, "lane 0 %d %d %d\noff 0 1 -100\noff 0 3 -50", MD_AMP, c ? MS_WHEEL : 0, c ? 100 : 0);
            seqcmd(e, cmd);
            set(e, "seq_length", 4);
            if (c == 2) { uint8_t m[3] = {0xb0, 1, 127}; E->midi(e, m, 3); }   /* the wheel up: full strength */
            note(e, 1, 57, 100);   /* a plain held note */
            render_f(e, x, 4000);
            set(e, "seq_play", 1);
            render_f(e, x, SR);
            long long st = step_at(120, 0.25, 1, 0);
            for (int k = 0; k < 4; k++) r[c][k] = rms_range(x, (int)(st * k + st / 4), (int)(st * k + st * 3 / 4));
            E->destroy(e);
        }
        snprintf(what, sizeof what, "seq modulation lane to Amp Level: steps 1-4 rms %.3f %.3f %.3f %.3f (step 2 at -100%%: "
                 "silent; step 4 at -50%%: half)", r[0][0], r[0][1], r[0][2], r[0][3]);
        check(r[0][0] > 0.02 && r[0][1] < r[0][0] * 0.01 && fabs(r[0][3] / r[0][0] - 0.5) < 0.05 && fabs(r[0][2] / r[0][0] - 1) < 0.02, what);
        snprintf(what, sizeof what, "seq lane controller Mod Wheel at 100%%: wheel down, no effect (step 2 %.3f of step 1); "
                 "wheel up, the lane at full strength (%.3f)", r[1][1] / r[1][0], r[2][1] / r[2][0]);
        check(fabs(r[1][1] / r[1][0] - 1) < 0.02 && r[2][1] < r[2][0] * 0.01, what);
    }

    /* recording: step recording (stopped) into the selected step, chords into lanes; overdub; real time */
    {
        void *e = seq_patch(120);
        set(e, "seq_rec", 1);
        seqcmd(e, "sel 0 0\ngate 40");
        note(e, 1, 60, 90); note(e, 0, 60, 0);
        note(e, 1, 64, 80); note(e, 1, 67, 70); note(e, 0, 64, 0); note(e, 0, 67, 0);
        char ro[64] = "";
        E->get_param(e, "seq_pos", ro, sizeof ro);
        sq_seq_t q;
        char txt[SQ_TEXT_MAX];
        seq_text(e, txt, sizeof txt);
        int dec = sq_decode(txt, &q, MD_COUNT, MS_COUNT);
        snprintf(what, sizeof what, "seq step recording: a note into step 1, a chord into step 2's lanes, gate 40%%, the "
                 "selection on step 3 (\"%s\")", ro);
        check(dec && q.n[0][0].note == 60 && q.n[0][0].vel == 90 && q.n[0][0].gate == 40 && q.n[1][0].note == 64
              && q.n[1][1].note == 67 && !strcmp(ro, "Edit 3/16"), what);
        set(e, "seq_rec_mode", 1);   /* OVERDUB on step 1 */
        seqcmd(e, "sel 0 0");
        note(e, 1, 72, 100); note(e, 0, 72, 0);
        seq_text(e, txt, sizeof txt);
        sq_decode(txt, &q, MD_COUNT, MS_COUNT);
        check(q.n[0][0].note == 60 && q.n[0][1].note == 72, "seq OVERDUB: a note added to step 1 beside the one there");
        set(e, "seq_rec_mode", 0);   /* REPLACE on step 1 */
        seqcmd(e, "sel 0 0");
        note(e, 1, 50, 100); note(e, 0, 50, 0);
        seq_text(e, txt, sizeof txt);
        sq_decode(txt, &q, MD_COUNT, MS_COUNT);
        check(q.n[0][0].note == 50 && !sq_has(&q.n[0][1]), "seq REPLACE: a new note replaces step 1's");
        /* real time: running, a key early in step 3 goes on step 3 with its gate as long as it was held */
        set(e, "seq_length", 4);
        set(e, "seq_play", 1);
        long long st = step_at(120, 0.25, 1, 0);
        gen_render(e, (int)(st * 2 + st / 8), 0);
        note(e, 1, 70, 100);
        gen_render(e, (int)(st / 4), 0);
        note(e, 0, 70, 0);
        seq_text(e, txt, sizeof txt);
        sq_decode(txt, &q, MD_COUNT, MS_COUNT);
        snprintf(what, sizeof what, "seq real-time recording: a key in step 3 lands there (note %d), gate as held (%d%%, want 25)",
                 q.n[2][0].note, q.n[2][0].gate);
        check(q.n[2][0].note == 70 && abs(q.n[2][0].gate - 25) <= 1, what);
        E->destroy(e);
    }

    /* STEP MOD: a knob's move becomes the step's offset in a lane for its destination; the knob stays */
    {
        void *e = seq_patch(120);
        set(e, "flt_cut", 50);
        seqcmd(e, "sel 3 0");
        set(e, "seq_step_mod", 1);
        set(e, "flt_cut", 60);   /* +10 on a 10-octave knob: +1 octave = +20% of the cutoff destination's 5 */
        set(e, "seq_step_mod", 0);
        sq_seq_t q;
        char txt[SQ_TEXT_MAX];
        seq_text(e, txt, sizeof txt);
        sq_decode(txt, &q, MD_COUNT, MS_COUNT);
        snprintf(what, sizeof what, "seq STEP MOD: moving the ladder cutoff +10 records +%d%% on step 4 in a Ladder Cutoff lane; "
                 "the knob stays at %g", q.m[0].off[3], get(e, "flt_cut"));
        check(q.m[0].dest == MD_LAD_CUT && q.m[0].off[3] == 20 && q.m[0].off[2] == SQ_NO_OFF && get(e, "flt_cut") == 50, what);
        E->destroy(e);
    }

    /* factory sequences, duplicate, damaged text */
    {
        void *e = seq_patch(120);
        set(e, "seq_pattern", 2);
        char key[64];
        int len_rep = 0;
        while (mpc_engine_changed(e, key, sizeof key) > 0) len_rep |= !strcmp(key, "seq_length");
        snprintf(what, sizeof what, "seq factory Chord Stabs: length %g, reported", get(e, "seq_length"));
        check(get(e, "seq_length") == 32 && len_rep, what);
        set(e, "seq_pattern", 0);   /* User: loads nothing, the sequence stays */
        char a[SQ_TEXT_MAX], b[SQ_TEXT_MAX];
        seq_text(e, a, sizeof a);
        check(a[0] != 0, "seq factory User keeps the sequence there");
        seqcmd(e, "clear\nnote 0 0 60 100 50 1 0\nnote 1 0 62 100 50 1 0");
        set(e, "seq_length", 2);
        seqcmd(e, "dup 0 1");
        sq_seq_t q;
        seq_text(e, b, sizeof b);
        sq_decode(b, &q, MD_COUNT, MS_COUNT);
        check(get(e, "seq_length") == 4 && q.n[2][0].note == 60 && q.n[3][0].note == 62, "seq DUPLICATE 1-2: appended as 3-4, length 4");
        sq_seq_t before = q;
        check(!sq_decode("AAAA", &q, MD_COUNT, MS_COUNT) && !sq_decode("!!!", &q, MD_COUNT, MS_COUNT)
              && !memcmp(&q, &before, sizeof q), "seq damaged text is refused and changes nothing");
        E->destroy(e);
    }

    /* the state: a full 64-step sequence with every lane round trip; sizes */
    {
        void *a = seq_patch(120), *b = E->create(NULL);
        uint32_t rng = 12345;
        static char cmds[64 * 9 * 40 + 8 * 70 * 16];
        int used = 0;
        for (int s = 0; s < SQ_STEPS; s++)
            for (int l = 0; l < SQ_NL; l++)
                used += snprintf(cmds + used, sizeof cmds - used, "note %d %d %u %u %u %u %u\n", s, l, sq_rnd(&rng) % 128,
                                 1 + sq_rnd(&rng) % 127, 1 + sq_rnd(&rng) % 100, 1 + sq_rnd(&rng) % 4, sq_rnd(&rng) % 2);
        for (int l = 0; l < SQ_ML; l++) {
            used += snprintf(cmds + used, sizeof cmds - used, "lane %d %d %d %d\n", l, 1 + l * 9, l, l * 10);
            for (int s = 0; s < SQ_STEPS; s++)
                used += snprintf(cmds + used, sizeof cmds - used, "off %d %d %d\n", l, s, (int)(sq_rnd(&rng) % 201) - 100);
        }
        seqcmd(a, cmds);
        seqcmd(a, "rest 5 1\nrest 63 1");
        set(a, "seq_length", 64);
        static char st[STATE_MAX + 16], st2[STATE_MAX + 16];
        int n = E->get_param(a, "state", st, STATE_MAX);   /* the wrapper's chunk */
        E->set_param(b, "state", st);
        int n2 = E->get_param(b, "state", st2, STATE_MAX);
        char t1[SQ_TEXT_MAX];
        int full = seq_text(a, t1, sizeof t1);
        snprintf(what, sizeof what, "seq state round trip: a full 64-step sequence, 8 note lanes, 8 modulation lanes, rests "
                 "(%d characters of steps, the state %d of %d)", full, n, STATE_MAX);
        check(n > 0 && n == n2 && !strcmp(st, st2) && strstr(st, "seq_length=64"), what);
        /* a typical sequence: a factory one (32 steps, 2-4 lanes, a lane) */
        void *c = seq_patch(120);
        set(c, "seq_pattern", 2);
        char t2[SQ_TEXT_MAX];
        int typ = seq_text(c, t2, sizeof t2);
        /* the worst case: every param off its default as well; in an 8 KB chunk (MPC) the steps then don't fit and are
         * left out, with the readout saying so; a larger chunk (the desktop build) keeps them */
        for (int i = 0; i < NPARAMS; i++) {
            const param_t *p = &PARAMS[i];
            if (p->string_display || !strncmp(p->key, "seq_", 4) || !s1_key(p->key)) continue;
            char v[32];
            snprintf(v, sizeof v, "%g", p->nopts ? (p->def > 0.5f ? 0.0 : 1.0) : p->min + (p->max - p->min) * (p->def > 0.5f ? 0.123 : 0.877));
            E->set_param(a, p->key, v);
        }
        n = E->get_param(a, "state", st, STATE_MAX);
        char ro[64] = "";
        E->get_param(a, "seq_pos", ro, sizeof ro);
        int kept = strstr(st, ";seq=") != NULL;
        printf("     seq sizes: a typical sequence %d characters; the fullest %d; worst state (every param off its default "
               "too) %d of %d %s its steps\n", typ, full, n, STATE_MAX, kept ? "with" : "without");
        snprintf(what, sizeof what, "seq the worst case: every param off its default and the fullest sequence in a %d-byte "
                 "chunk: %s (%d bytes; readout \"%s\")", STATE_MAX, kept ? "all kept" : "the sound is saved without the steps "
                 "and the readout says so", n, ro);
        check(n > 0 && n < STATE_MAX && kept == !!strcmp(ro, "Too long to save") && (STATE_MAX > 8192 || !kept)
              && typ > 0 && typ < 1500, what);
        E->destroy(a); E->destroy(b); E->destroy(c);
    }

    /* everything off: the same samples as a fresh instance, after the sequencer ran, recorded and stopped */
    {
        void *a = patch(SAW, 5, 200, 70, 100), *b = patch(SAW, 5, 200, 70, 100);
        static float xa[SR], xb[SR];
        setkv(b, "seq_pattern=1,seq_play=1");   /* a sequence in it, its clock running, ON off */
        render_f(a, xa, SR / 2);
        render_f(b, xb, SR / 2);
        setkv(b, "seq_play=0,seq_rec=1,seq_rec=0,seq_note_trig=1,seq_transpose=1,seq_note_trig=0,seq_transpose=0,"
                 "seq_step_mod=1,seq_step_mod=0");
        render_f(a, xa, SR / 2);
        render_f(b, xb, SR / 2);
        int same = 1;
        for (int pass = 0; pass < 2; pass++) {
            for (int k = 0; k < 3; k++) { note(a, !pass, 48 + 4 * k, 90); note(b, !pass, 48 + 4 * k, 90); }
            render_f(a, xa, SR / 2);
            render_f(b, xb, SR / 2);
            for (int i = 0; i < SR / 2; i++) same &= xa[i] == xb[i];
        }
        check(same, "seq ON off (a sequence in it, its clock run, its switches moved): the same samples as a fresh instance");
        E->destroy(a); E->destroy(b);
    }
}

/* the sequencer's CPU on this PC: its clock and steps alone (a full 64-step sequence of 8 lanes, 4 ratchets each, at
 * 1/32, ON off so no voice plays), and 8 modulation lanes on 8 held voices; each against the same without it */
static double seq_us(void *e, int blocks) {
    int16_t buf[256];
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int b = 0; b < blocks; b++) E->render(e, buf, 128);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    return ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / 1e3 / blocks;
}
static void seq_bench(void) {
    enum { BLOCKS = 40000 };
    static char cmds[64 * 8 * 32];
    int used = 0;
    for (int s = 0; s < SQ_STEPS; s++)
        for (int l = 0; l < SQ_NL; l++) used += snprintf(cmds + used, sizeof cmds - used, "note %d %d %d 100 50 4 0\n", s, l, 40 + l);
    void *a = seq_patch(120), *b = seq_patch(120);
    set(a, "seq_on", 0); set(b, "seq_on", 0);
    seqcmd(b, cmds);
    setkv(b, "seq_length=64,seq_div=5,seq_play=1");
    double ua = seq_us(a, BLOCKS), ub = seq_us(b, BLOCKS);
    printf("bench: sequencer clock and steps alone (64 steps x 8 lanes x 4 ratchets at 1/32, nothing sounding): %.2f us per "
           "block (%.2f without)\n", ub, ua);
    E->destroy(a); E->destroy(b);
    a = seq_patch(120); b = seq_patch(120);
    /* 8 lanes, each to a per-voice destination that doesn't switch on more of the voice (no levels, FM or pan), an
     * offset on every step */
    static const int DST[SQ_ML] = {MD_LAD_RES, MD_SVF_RES, MD_LAD_DRIVE, MD_FLT_MIX, MD_O1_ANGLE, MD_O1_MIX, MD_O1_PW, MD_PITCH};
    for (int l = 0; l < SQ_ML; l++) {
        char c[64];
        snprintf(c, sizeof c, "lane %d %d %d 50", l, DST[l], l ? MS_WHEEL : 0);
        seqcmd(b, c);
        for (int s = 0; s < 16; s++) { snprintf(c, sizeof c, "off %d %d %d", l, s, (s * 13 + l * 7) % 41 - 20); seqcmd(b, c); }
    }
    set(a, "seq_play", 1); set(b, "seq_play", 1);
    for (int k = 0; k < 8; k++) { note(a, 1, 48 + 3 * k, 100); note(b, 1, 48 + 3 * k, 100); }
    ua = seq_us(a, BLOCKS / 4); ub = seq_us(b, BLOCKS / 4);
    printf("bench: 8 voices with 8 modulation lanes running: %.2f us per block (%.2f without the lanes: %.2f per voice "
           "more)\n", ub, ua, (ub - ua) / 8);
    E->destroy(a); E->destroy(b);
}

/* test/out/seq_demo.wav: the factory sequences played as the presets do (hold a key; transposed by the key), and a
 * sequence with ratchets and a cutoff lane recorded by STEP MOD, to listen to */
static void seq_wav(const char *demo_path) {
    enum { SEC = SR };
    static int16_t pcm[34 * SEC * 2];
    char path[320];
    snprintf(path, sizeof path, "%s", demo_path);
    char *slash = strrchr(path, '/');
    if (slash) slash[1] = 0; else path[0] = 0;
    strncat(path, "seq_demo.wav", sizeof path - strlen(path) - 1);
    int16_t *o = pcm;
    /* 1: the bass line, NOTE TRIG + TRANSPOSE: C, then F, then G (each key held two bars) */
    void *e = patch(SAW, 2, 300, 70, 90);
    setkv(e, "o2_level=75,o2_oct=1,o2_mix=100,o2_freq=0.05,flt_res=45,flt_drive=30,lad_env=50,lad_kbd=40,voices=0,volume=65");
    set(e, "flt_cut", knob_for_hz(320)); set(e, "fenv_d", knob_ms(220)); set(e, "fenv_s", 15);
    E->set_param(e, "lfo_bpm", "118");
    setkv(e, "seq_on=1,seq_pattern=1,seq_note_trig=1,seq_transpose=1,clock_swing=56,mfx_send1=12,dly_sync=1,dly_time=60");
    static const int keys[3] = {36, 41, 43};
    for (int k = 0; k < 3; k++) {
        note(e, 1, keys[k], 100);
        o = rest(e, o, (int)(4.0 * 60 / 118 * 2 * SEC / 128));
        note(e, 0, keys[k], 0);
    }
    { uint8_t m[3] = {0xb0, 1, 127}; E->midi(e, m, 3); }   /* the mod wheel brings the cutoff lane in */
    note(e, 1, 36, 100);
    o = rest(e, o, (int)(4.0 * 60 / 118 * 2 * SEC / 128));
    note(e, 0, 36, 0);
    o = rest(e, o, SEC / 2 / 128);
    E->destroy(e);
    /* 2: chord stabs, 32 steps, through the SVF lane */
    e = patch(SAW, 2, 450, 40, 260);
    setkv(e, "o1_mix=100,o1_pw=40,o2_level=80,o2_freq=0.08,o1_svf=1,o2_svf=1,svf_res=30,svf_env=25,flt_mix=40,voices=7,"
             "volume=55,mfx_send1=22,dly_sync=1,dly_time=60,dly_fb=35,mfx_send2=30");
    set(e, "svf_cut", knob_for_hz(1600)); set(e, "flt_cut", knob_for_hz(2400));
    E->set_param(e, "lfo_bpm", "118");
    setkv(e, "seq_on=1,seq_pattern=2,seq_note_trig=1,seq_transpose=1");
    note(e, 1, 48, 100);
    o = rest(e, o, (int)(4.0 * 60 / 118 * 2 * SEC / 128));
    note(e, 0, 48, 0);
    o = rest(e, o, SEC / 128);
    E->destroy(e);
    /* 3: the ratchet run, then a cutoff sweep recorded with STEP MOD while it plays */
    e = patch(SAW, 1, 250, 30, 120);
    setkv(e, "o1_mix=60,flt_res=55,lad_env=40,voices=7,volume=60,mfx_send2=25");
    set(e, "flt_cut", knob_for_hz(900)); set(e, "fenv_d", knob_ms(180)); set(e, "fenv_s", 10);
    E->set_param(e, "lfo_bpm", "118");
    setkv(e, "seq_on=1,seq_pattern=3,seq_play=1");
    o = rest(e, o, (int)(4.0 * 60 / 118 * SEC / 128));
    set(e, "seq_step_mod", 1);
    for (int k = 0; k < 16; k++) {   /* a knob turned up as it plays: each step a little brighter */
        o = rest(e, o, (int)(0.25 * 60 / 118 * SEC / 128));
        set(e, "flt_cut", knob_for_hz(900) + k * 2.5);
    }
    set(e, "seq_step_mod", 0);
    o = rest(e, o, (int)(4.0 * 60 / 118 * 2 * SEC / 128));
    set(e, "seq_play", 0);
    o = rest(e, o, SEC / 128);
    E->destroy(e);
    write_wav(path, pcm, (uint32_t)((o - pcm) / 2));
    printf("wrote %s (the bass line held on C, F, G, then with the wheel up; chord stabs; a ratchet run with a cutoff "
           "sweep recorded by STEP MOD)\n", path);
}
