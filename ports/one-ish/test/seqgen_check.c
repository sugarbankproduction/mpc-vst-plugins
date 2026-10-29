/* The sequence generator (src/seqgen.h, 2026-10-29): every note in the Synth's key and scale, the same seed giving the
 * same pattern, DENSITY and RANGE doing what they say, motifs repeating with VARIATION at 0, MODS filling lanes, and a
 * generated pattern surviving the engine's state (into Synth 2 only, as the window's GEN view sends it). Included by
 * offline_check.c after seqx_check.c; it uses the sequencer's helpers (seqcmd, seq_patch, seq_text, gen_render ...). */
#include "seqgen.h"

static int sgc_in_scale(int note, int root, int scale) {
    int pc = ((note - root) % 12 + 12) % 12;
    for (int i = 0; i < KS_SIZE[scale]; i++) if (KS_STEPS[scale][i] == pc) return 1;
    return 0;
}
static int sgc_steps(const sq_seq_t *q, int len) {   /* steps holding a note */
    int c = 0;
    for (int s = 0; s < len; s++) c += !sq_step_empty(q, s);
    return c;
}
static void sgc_span(const sq_seq_t *q, int *lo, int *hi) {
    *lo = 127; *hi = 0;
    for (int s = 0; s < SQ_STEPS; s++)
        for (int l = 0; l < SQ_NL; l++)
            if (sq_has(&q->n[s][l])) {
                if (q->n[s][l].note < *lo) *lo = q->n[s][l].note;
                if (q->n[s][l].note > *hi) *hi = q->n[s][l].note;
            }
}

static void seqgen_checks(void) {
    char what[400];
    static const char *const STY[SG_STYLES] = {"bass", "arp", "melody", "chords", "random"};
    /* ---- every note in the key and scale, inside the range, the density exact, past the length nothing ---- */
    {
        int bad_scale = 0, bad_range = 0, bad_dens = 0, bad_len = 0, bad_text = 0, runs = 0;
        uint32_t pick = 12345;
        for (int style = 0; style < SG_STYLES; style++)
            for (int scale = 0; scale < KS_COUNT; scale++)
                for (int k = 0; k < 6; k++) {
                    seqgen_opts_t o;
                    seqgen_defaults(&o);
                    o.style = style;
                    o.rhythm = k % SG_RHYTHMS;
                    o.len = (int[]){16, 32, 64, 12, 7, 48}[k];
                    o.density = 10 + (int)(sq_rnd(&pick) % 91);
                    o.range = 1 + k % 4;
                    o.variation = (int)(sq_rnd(&pick) % 101);
                    o.accent = (int)(sq_rnd(&pick) % 101);
                    o.fills = (int)(sq_rnd(&pick) % 101);
                    o.mods = (int)(sq_rnd(&pick) % 101);
                    int root = (int)(sq_rnd(&pick) % 12);
                    uint32_t seed = sq_rnd(&pick);
                    sq_seq_t q, q2;
                    seqgen_make(&o, root, scale, &seed, &q);
                    int base = seqgen_base(style, root), want = 0, bar = o.len >= 16 ? 16 : o.len >= 8 ? 8 : o.len;
                    for (int st = 0; st < o.len; st += bar)   /* DENSITY % of each bar's steps (at least one note) */
                        want += (o.density * (o.len - st < bar ? o.len - st : bar) + 50) / 100;
                    if (want < 1) want = 1;
                    for (int s = 0; s < SQ_STEPS; s++)
                        for (int l = 0; l < SQ_NL; l++) {
                            const sq_note_t *n = &q.n[s][l];
                            if (!sq_has(n)) continue;
                            bad_scale += !sgc_in_scale(n->note, root, scale);
                            bad_range += n->note < base || n->note > base + 12 * o.range;
                            bad_len += s >= o.len;
                        }
                    bad_dens += sgc_steps(&q, o.len) != want;
                    char t[SQ_TEXT_MAX];
                    sq_encode(&q, t, sizeof t);
                    bad_text += !sq_decode(t, &q2, MD_COUNT, MS_COUNT) || memcmp(&q, &q2, sizeof q);
                    runs++;
                }
        snprintf(what, sizeof what, "seqgen: %d patterns (5 styles x 14 scales, random options): notes off the scale %d, "
                 "outside the range %d, past the length %d, density not as set %d, text round trip failed %d", runs,
                 bad_scale, bad_range, bad_len, bad_dens, bad_text);
        check(!bad_scale && !bad_range && !bad_len && !bad_dens && !bad_text, what);
    }
    /* ---- the same seed: the same pattern; another seed: another; the seed moves on ---- */
    {
        int same = 1, differ = 0, moved = 1;
        for (int style = 0; style < SG_STYLES; style++) {
            seqgen_opts_t o;
            seqgen_defaults(&o);
            o.style = style;
            o.len = 32;
            sq_seq_t a, b, c;
            uint32_t s1 = 777, s2 = 777, s3 = 778;
            seqgen_make(&o, 2, KS_DORIAN, &s1, &a);
            seqgen_make(&o, 2, KS_DORIAN, &s2, &b);
            seqgen_make(&o, 2, KS_DORIAN, &s3, &c);
            same &= !memcmp(&a, &b, sizeof a);
            differ += memcmp(&a, &c, sizeof a) != 0;
            moved &= s1 == s2 && s1 != 777;
        }
        snprintf(what, sizeof what, "seqgen: the same seed gives the same pattern (%d), the next seed another (%d of 5 styles), "
                 "the seed moves on for the next call (%d)", same, differ, moved);
        check(same && differ == 5 && moved, what);
    }
    /* ---- DENSITY and RANGE ---- */
    {
        int dok = 1, r1ok = 1, wider = 0, tries = 0;
        static const int DEN[4] = {25, 50, 75, 100};
        for (int i = 0; i < 4; i++) {
            seqgen_opts_t o;
            seqgen_defaults(&o);
            o.style = SG_MELODY;
            o.len = 32;
            o.density = DEN[i];
            uint32_t seed = 99;
            sq_seq_t q;
            int got = seqgen_make(&o, 0, KS_MAJOR, &seed, &q);
            dok &= got == DEN[i] * 32 / 100 && sgc_steps(&q, 32) == got;
        }
        for (int style = SG_BASS; style <= SG_MELODY; style++)
            for (uint32_t k = 1; k <= 8; k++) {
                seqgen_opts_t o;
                seqgen_defaults(&o);
                o.style = style;
                o.len = 32;
                o.density = 80;
                o.range = 1;
                uint32_t seed = k * 31;
                sq_seq_t q1, q4;
                seqgen_make(&o, 5, KS_MINOR, &seed, &q1);
                o.range = 4;
                seed = k * 31;
                seqgen_make(&o, 5, KS_MINOR, &seed, &q4);
                int lo1, hi1, lo4, hi4;
                sgc_span(&q1, &lo1, &hi1);
                sgc_span(&q4, &lo4, &hi4);
                r1ok &= hi1 - lo1 <= 12;
                wider += hi4 - lo4 > hi1 - lo1;
                tries++;
            }
        snprintf(what, sizeof what, "seqgen: DENSITY 25/50/75/100%% of 32 steps plays 8/16/24/32 steps (%d); RANGE 1 keeps "
                 "within an octave (%d), RANGE 4 spreads wider in %d of %d bass, arp and melody patterns", dok, r1ok, wider,
                 tries);
        check(dok && r1ok && wider >= tries * 3 / 4, what);
    }
    /* ---- motifs: VARIATION 0 repeats the bar exactly; 100 wanders; the bass sits on the root; chords are chords ---- */
    {
        seqgen_opts_t o;
        seqgen_defaults(&o);
        o.len = 32;
        o.variation = 0;
        o.fills = 0;
        o.mods = 0;
        int rep = 1, wander = 0, roots = 0, bhits = 0, chords = 0, chits = 0, home = 1;
        for (uint32_t k = 1; k <= 10; k++) {
            for (int style = SG_BASS; style <= SG_MELODY; style += 2) {
                uint32_t seed = k;
                sq_seq_t q;
                o.style = style;
                o.variation = 0;
                seqgen_make(&o, 9, KS_MINOR, &seed, &q);
                for (int s = 0; s < 12; s++)   /* (the last beat is the phrase ending) */
                    rep &= !memcmp(q.n[s], q.n[s + 16], sizeof q.n[s]);
                o.variation = 100;
                seed = k;
                seqgen_make(&o, 9, KS_MINOR, &seed, &q);
                int d = 0;
                for (int s = 0; s < 12; s++) d |= memcmp(q.n[s], q.n[s + 16], sizeof q.n[s]) != 0;
                wander += d;
                int last = -1;
                for (int s = 0; s < 32; s++) if (sq_has(&q.n[s][0])) last = s;
                home &= last >= 0 && (q.n[last][0].note - 9) % 12 == 0;   /* the phrase ends on the key's root */
                if (style != SG_BASS) continue;
                for (int s = 0; s < 32; s++)
                    if (sq_has(&q.n[s][0])) { bhits++; roots += (q.n[s][0].note - 9) % 12 == 0; }
            }
            uint32_t seed = k;
            sq_seq_t q;
            o.style = SG_CHORDS;
            o.variation = 30;
            seqgen_make(&o, 9, KS_MINOR, &seed, &q);
            for (int s = 0; s < 32; s++) {
                int nn = 0;
                for (int l = 0; l < SQ_NL; l++) nn += sq_has(&q.n[s][l]);
                chits += nn > 0;
                chords += nn >= 3;
            }
        }
        snprintf(what, sizeof what, "seqgen: VARIATION 0 repeats bar 1 in bar 2 (%d), VARIATION 100 changes it (%d of 20); "
                 "phrases end on the root (%d); the bass line is on the root %d of %d hits; chord stabs of 3+ notes %d of %d",
                 rep, wander, home, roots, bhits, chords, chits);
        check(rep && wander >= 16 && home && roots * 3 >= bhits && chords * 10 >= chits * 9, what);
    }
    /* ---- MODS: none at 0, four lanes at 100, offsets within -100..100 ---- */
    {
        seqgen_opts_t o;
        seqgen_defaults(&o);
        int ok = 1;
        for (int style = 0; style < SG_STYLES; style++) {
            sq_seq_t q;
            uint32_t seed = 5;
            o.style = style;
            o.mods = 0;
            seqgen_make(&o, 0, KS_MAJOR, &seed, &q);
            for (int l = 0; l < SQ_ML; l++) ok &= !q.m[l].dest;
            o.mods = 100;
            seed = 5;
            seqgen_make(&o, 0, KS_MAJOR, &seed, &q);
            int lanes = 0;
            for (int l = 0; l < SQ_ML; l++) {
                lanes += q.m[l].dest > 0 && q.m[l].dest < MD_COUNT;
                for (int s = 0; s < SQ_STEPS; s++)
                    ok &= q.m[l].off[s] == SQ_NO_OFF || (q.m[l].off[s] >= -100 && q.m[l].off[s] <= 100);
            }
            ok &= lanes == 4 && q.m[0].dest == MD_LAD_CUT && q.m[1].dest == MD_SVF_CUT && q.m[0].off[0] != SQ_NO_OFF;
        }
        check(ok, "seqgen: MODS 0 leaves the modulation lanes empty, MODS 100 fills four (both cutoffs, resonance, one more)");
    }
    /* ---- in the engine: a pattern loaded into Synth 2 only survives a state save and restore, and plays in its key ---- */
    {
        seqgen_opts_t o;
        seqgen_defaults(&o);
        o.style = SG_ARP;
        o.mods = 60;
        uint32_t seed = 4242;
        sq_seq_t q;
        seqgen_make(&o, 2, KS_DORIAN, &seed, &q);
        static char t[SQ_TEXT_MAX], cmd[SQ_TEXT_MAX + 32], st[STATE_MAX + 16], back[STATE_MAX + 16];
        sq_encode(&q, t, sizeof t);
        void *a = E->create(NULL), *b = E->create(NULL);
        snprintf(cmd, sizeof cmd, "seqcmd2\nload %s", t);
        E->set_param(a, "state", cmd);
        int n = E->get_param(a, "state", st, STATE_MAX);
        const char *p2 = strstr(st, ";s2_seq="), *p1 = strstr(st, ";seq=");
        sq_seq_t e1;
        sq_clear(&e1);
        if (p1) sq_decode(p1 + 5, &e1, MD_COUNT, MS_COUNT);
        int s1_empty = sq_used_steps(&e1) == 0;
        E->set_param(b, "state", st);
        E->get_param(b, "state", back, STATE_MAX);
        const char *b2 = strstr(back, ";s2_seq=");
        int kept = p2 && b2 && !strncmp(b2 + 8, t, strlen(t)) && (b2[8 + strlen(t)] == ';' || b2[8 + strlen(t)] == 0);
        snprintf(what, sizeof what, "seqgen: a pattern loaded into Synth 2 is in its state (%d, %d bytes), survives a save "
                 "and restore exactly (%d), and Synth 1's sequence stays empty (%d)", p2 != NULL, n, kept, s1_empty);
        check(p2 && kept && s1_empty, what);
        E->destroy(a);
        E->destroy(b);
        /* Synth 1 playing a generated melody in E Phrygian: every note it plays is in the scale */
        void *e = seq_patch(120);
        o.style = SG_MELODY;
        o.len = 16;
        seed = 31337;
        seqgen_make(&o, 4, KS_PHRYGIAN, &seed, &q);
        sq_encode(&q, t, sizeof t);
        snprintf(cmd, sizeof cmd, "load %s", t);
        seqcmd(e, cmd);
        set(e, "seq_length", 16);
        set(e, "seq_play", 1);
        int ev = gen_render(e, beats_at(120, 8), 0), got[256];
        int c = gen_pick(ev, 1, got, NULL, NULL, 256), off = 0;
        for (int i = 0; i < c; i++) off += !sgc_in_scale(got[i], 4, KS_PHRYGIAN);
        snprintf(what, sizeof what, "seqgen: a generated E Phrygian melody played by the sequencer: %d notes, %d off the scale",
                 c, off);
        check(c >= 8 && !off, what);
        E->destroy(e);
    }
    /* a line per style, to read (the first bar's notes) */
    for (int style = 0; style < SG_STYLES; style++) {
        seqgen_opts_t o;
        seqgen_defaults(&o);
        o.style = style;
        uint32_t seed = 2026;
        sq_seq_t q;
        seqgen_make(&o, 0, KS_MINOR, &seed, &q);
        char line[256];
        int k = 0;
        for (int s = 0; s < 16 && k < 200; s++) {
            char nm[8] = "-";
            if (sq_has(&q.n[s][0])) sq_note_name(q.n[s][0].note, nm, sizeof nm);
            k += snprintf(line + k, sizeof line - k, "%s%s", s ? " " : "", nm);
        }
        printf("     seqgen %-6s C minor: %s\n", STY[style], line);
    }
}
