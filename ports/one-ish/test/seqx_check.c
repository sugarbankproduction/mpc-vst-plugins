/* ONE-ish sequencer leftovers (audit package 2, 2026-10-29): RETURN TO ZERO, a step's notes edited together
 * ("stepnotes"), deleting a step ("clearstep"), the arpeggiator following the master swing (arp_follow_swing), and the
 * host transport's start and stop starting and stopping the sequence (seq_host_ss: RECEIVE START/STOP). Included by
 * offline_check.c after lfoenv_check.c; it uses the sequencer's and the arpeggiator's helpers (seqcmd, seq_patch,
 * seq_text, gen_render, gen_pick, step_at ...). The window's parts (the RTZ, ALL, DEL STEP and AUTO buttons, notes in
 * .oneseq files) are load_test's. */

/* render `frames` in blocks of 128 telling the engine the song position first (playing or stopped); returns the log */
static int sx_render_host(void *e, int frames, int n, double *ppq, double bpm, int playing) {
    int16_t buf[256];
    for (int at = 0; at < frames; at += 128) {
        int m = frames - at < 128 ? frames - at : 128;
        E->song_pos(e, *ppq, bpm, playing, m);
        if (playing) *ppq += m * bpm / 60.0 / SR;
        E->render(e, buf, m);
        n = gen_events(e, n);
    }
    return n;
}

static void seq_extras(void) {
    char what[320];
    /* ---- RETURN TO ZERO: the next step is step 1 again ---- */
    {
        void *e = seq_patch(120);
        set(e, "seq_length", 8);
        for (int s = 0; s < 8; s++) {
            char c[64];
            snprintf(c, sizeof c, "note %d 0 %d 100 50 1 0", s, 60 + s);
            seqcmd(e, c);
        }
        set(e, "seq_play", 1);
        int n = gen_render(e, beats_at(120, 0.25 * 2) + 10, 0);   /* steps 1-3 (1/16 at 120 BPM: the third at 2/4 beat) */
        seqcmd(e, "rtz");
        n = gen_render(e, beats_at(120, 0.25 * 2), n);
        int got[16], c = gen_pick(n, 1, got, NULL, NULL, 16);
        snprintf(what, sizeof what, "seq RTZ after 3 steps: then %d %d (step 1 and 2 again; %d notes)", c > 3 ? got[3] : -1,
                 c > 4 ? got[4] : -1, c);
        check(c >= 5 && got[0] == 60 && got[2] == 62 && got[3] == 60 && got[4] == 61, what);
        E->destroy(e);
    }
    /* ---- a step's notes edited together, and a step deleted ---- */
    {
        void *e = seq_patch(120);
        seqcmd(e, "note 2 0 60 100 50 1 0\nnote 2 1 64 80 40 1 1\nnote 2 2 67 90 60 3 0\nnote 3 0 50 100 50 1 0");
        seqcmd(e, "stepnotes 2 2 0 0 0");    /* every note of step 3 up a tone, the rest kept */
        seqcmd(e, "stepnotes 2 0 70 25 2");  /* velocity, gate, ratchet of all of them */
        static char txt[8192];
        sq_seq_t q;
        seq_text(e, txt, sizeof txt);
        int ok = sq_decode(txt, &q, MD_COUNT, MS_COUNT);
        const sq_note_t *n = q.n[2];
        ok &= n[0].note == 62 && n[1].note == 66 && n[2].note == 69 && q.n[3][0].note == 50;
        for (int l = 0; l < 3; l++) ok &= n[l].vel == 70 && n[l].gate == 25 && sq_ratchet(&n[l]) == 2;
        ok &= (n[1].flags & SQ_TIE) && !(n[0].flags & SQ_TIE);   /* the ties stay */
        seqcmd(e, "stepnotes 2 100 0 0 0");  /* past the top: every note kept within 0-127 */
        seq_text(e, txt, sizeof txt);
        sq_decode(txt, &q, MD_COUNT, MS_COUNT);
        int kept = q.n[2][2].note <= 127 && q.n[2][0].note == 127 - 7;   /* (the chord moves as one: the top note at 127) */
        snprintf(what, sizeof what, "seq STEP NOTES: a step's three notes moved up a tone and given one velocity, gate and "
                 "ratchet, ties kept (%d); moved past the top they stop as a chord at 127 (%d, %d)", ok, q.n[2][0].note,
                 q.n[2][2].note);
        check(ok && kept, what);
        seqcmd(e, "clearstep 2");
        seq_text(e, txt, sizeof txt);
        sq_decode(txt, &q, MD_COUNT, MS_COUNT);
        snprintf(what, sizeof what, "seq DELETE STEP: step 3 empty (%d), step 4 kept (%d)", sq_step_empty(&q, 2),
                 q.n[3][0].note == 50);
        check(sq_step_empty(&q, 2) && q.n[3][0].note == 50, what);
        E->destroy(e);
    }
    /* ---- the arpeggiator following the master swing ---- */
    for (int c = 0; c < 2; c++) {
        void *e = arp_patch();
        set(e, "arp_on", 1); set(e, "arp_gate", 25);
        set(e, "clock_swing", 75);                 /* the master swing: the off-step 1/8 beat late */
        if (c) set(e, "arp_follow_swing", 1);
        note(e, 1, 60, 100);
        int n = gen_render(e, SR * 2, 0);
        long long on[32];
        int non = gen_pick(n, 1, NULL, on, NULL, 32), ok = non >= 8;
        for (int k = 0; k < 8 && ok; k++) ok &= on[k] - on[0] == step_at(120, 0.25, k, c ? 0.125 : 0);
        snprintf(what, sizeof what, "arp FOLLOW SWING %s: master swing 75%%, step 2 at %lld samples (%s)", c ? "on" : "off",
                 non > 1 ? on[1] - on[0] : -1, c ? "1/8 beat late" : "straight: the arp's own swing, 50%");
        check(ok, what);
        E->destroy(e);
    }
    /* ---- RECEIVE START/STOP: the host's transport starts and stops the sequence ---- */
    for (int c = 0; c < 2; c++) {
        void *e = seq_patch(120);
        set(e, "seq_sync", 1);
        set(e, "seq_pattern", 3);   /* the ratchet run: notes on most steps */
        if (c) set(e, "seq_host_ss", 1);
        double ppq = 0;
        int n = sx_render_host(e, SR / 2, 0, &ppq, 120, 0);    /* stopped */
        int before = gen_pick(n, 1, NULL, NULL, NULL, 256);
        n = sx_render_host(e, SR, n, &ppq, 120, 1);             /* the transport plays */
        int playing = gen_pick(n, 1, NULL, NULL, NULL, 256) - before, play_on = get(e, "seq_play") > 0.5;
        char key[64];
        int reported = 0;
        while (mpc_engine_changed(e, key, sizeof key)) reported |= !strcmp(key, "seq_play");
        n = sx_render_host(e, SR / 10, n, &ppq, 120, 0);        /* it stops */
        int at_stop = gen_pick(n, 1, NULL, NULL, NULL, 256);
        n = sx_render_host(e, SR, n, &ppq, 120, 0);
        int after = gen_pick(n, 1, NULL, NULL, NULL, 256) - at_stop, play_off = get(e, "seq_play") < 0.5;
        snprintf(what, sizeof what, "seq RECEIVE START/STOP %s: %d notes before the transport, %d while it plays (PLAY %d, "
                 "the host told %d), %d after it stops (PLAY off %d)", c ? "on" : "off", before, playing, play_on, reported,
                 after, play_off);
        if (c) check(before == 0 && playing >= 6 && play_on && reported && after == 0 && play_off, what);
        else check(before == 0 && playing == 0 && !play_on, what);
        E->destroy(e);
    }
}
