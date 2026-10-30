/* ONE-ish randomiser golden check (2026-10-30): RANDOMISE and MUTATE give what they gave before that day's changes,
 * except the values those changes are about.
 *
 *   rnd_golden gen   <fixture>   with the randomiser BEFORE the changes: a hash per call of every Synth 1 / shared
 *                                param (by key, in its own units) and of the seed after it
 *   rnd_golden check <fixture>   with the randomiser AFTER them: the same calls must give the same hashes
 *
 * Calls: RANDOMISE from the defaults, and MUTATE (40%) from the defaults, at WILD 0, 50 and 100%, with no tick button
 * and with ARPEGGIATOR lit, 20 seeds each. Left out of the hash, the deliberate changes of 2026-10-30:
 *   - matrix rows 13-16: the aftertouch, X/Y pad and ribbon rows RANDOMISE always wires (MUTATE leaves them alone)
 *   - the ANALOG page (drift, an_*) and SPREAD: never changed by the randomiser now
 *   - the master effect sends and the synth effect's mix: RANDOMISE makes the effects clearly heard
 *   - params added that day (NEWER), and the Synth 2 / 3 copies (never touched)
 * Every other param, and the seed after each call, must be the same: nothing else drifted.
 * The inputs are the defaults with rows 13-16 as an empty row (row 12's defaults: the same before and after).
 * Build and run: test/run.sh (check against test/rnd_golden.txt).
 * (2026-10-30, the merge with GRAINS) the fixture was made again: GRAINS joined the three effect TYPE lists, and
 * WILD's any-type pick draws over the longer list, so 51 of the 240 calls (WILD 50 / 100% only) pick another type
 * (and what follows from it). Checked before remaking it: against the old randomiser (4f1c1b0) param by param, every
 * call that differed had an effect type change; none differed without one. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "params.h"
#include "randomize.h"

static const char *const NEWER[] = {"dly_on", "rev_on",   /* (2026-10-30) the master effect slots' own ON */
    /* (2026-10-30) GRAINS' controls and the X/Y pad's LINK (no randomiser rule: left at their defaults) */
    "modfx_spray", "modfx_prand", "modfx_reverse", "modfx_gquant", "dly_gpos", "dly_gdens", "dly_genv", "dly_gspray",
    "dly_gprand", "dly_greverse", "dly_gquant", "rev_gpos", "rev_gdens", "rev_genv", "rev_gspray", "rev_gprand",
    "rev_greverse", "rev_gquant", "link_pad",
    /* (2026-10-30) the generators' KEYS, LOW OCTAVE, EUCLID NOTES, GEN TEMPO, FOLLOW KEYS */
    "gen_keys", "gen_octave", "gen_eu_notes", "gen_tempo_src", "gen_bpm", "gen_follow_keys",
    "lfo1_share", "lfo2_share", "lfo3_share", "lfo4_share"};   /* (2026-10-30) SHARE */
static const char *const CHANGED[] = {"drift", "spread", "mfx_send1", "mfx_send2", "modfx_mix",
                                      "link_send"};   /* (LINK SENDS: its default is On now; never randomised) */

static int skipped(const char *k) {
    if (k[0] == 's' && k[1] >= '2' && k[1] <= '9' && k[2] == '_') return 1;
    for (int r = 13; r <= 16; r++) {
        char p[8];
        snprintf(p, sizeof p, "mod%d_", r);
        if (!strncmp(k, p, strlen(p))) return 1;
    }
    if (!strncmp(k, "an_", 3)) return 1;
    for (unsigned i = 0; i < sizeof NEWER / sizeof NEWER[0]; i++)
        if (!strcmp(k, NEWER[i])) return 1;
    for (unsigned i = 0; i < sizeof CHANGED / sizeof CHANGED[0]; i++)
        if (!strcmp(k, CHANGED[i])) return 1;
    return 0;
}
static int by_key(const void *a, const void *b) {
    return strcmp(PARAMS[*(const int *)a].key, PARAMS[*(const int *)b].key);
}
static uint64_t hash(const float *n, uint32_t seed) {
    static int order[NPARAMS], no = -1;
    if (no < 0) {
        no = 0;
        for (int i = 0; i < NPARAMS; i++)
            if (!skipped(PARAMS[i].key)) order[no++] = i;
        qsort(order, (size_t)no, sizeof order[0], by_key);
    }
    uint64_t h = 1469598103934665603ull;
    char t[96];
    for (int j = 0; j < no; j++) {
        int i = order[j];
        snprintf(t, sizeof t, "%s=%.5g;", PARAMS[i].key, rnd_real(i, n[i]));
        if (getenv("RND_GOLDEN_DUMP")) printf("%s\n", t);   /* (to see what differs: run old and new, diff) */
        for (const char *p = t; *p; p++) h = (h ^ (unsigned char)*p) * 1099511628211ull;
    }
    if (getenv("RND_GOLDEN_DUMP")) printf("seed=%u\n", seed);
    snprintf(t, sizeof t, "seed=%u", seed);
    for (const char *p = t; *p; p++) h = (h ^ (unsigned char)*p) * 1099511628211ull;
    return h;
}
static void empty_rows(float *n) {   /* rows 13-16 as an empty row (row 12's defaults, the same before and after) */
    static const char *const F[] = {"src", "dst", "amt", "via", "via_amt", "xf", "xf_amt", "on"};
    for (int r = 13; r <= 16; r++)
        for (unsigned f = 0; f < sizeof F / sizeof F[0]; f++) {
            char k[32], k12[32];
            snprintf(k, sizeof k, "mod%d_%s", r, F[f]);
            snprintf(k12, sizeof k12, "mod12_%s", F[f]);
            int i = rnd_key(k), j = rnd_key(k12);
            if (i < NPARAMS && j < NPARAMS) n[i] = PARAMS[j].def;
        }
}

int main(int argc, char **argv) {
    if (argc >= 2 && !strcmp(argv[1], "stats")) {   /* the effects' sends and mix over 400 RANDOMISE sounds per WILD */
        static float d[NPARAMS], o[NPARAMS];
        for (int i = 0; i < NPARAMS; i++) d[i] = PARAMS[i].def;
        int s1 = rnd_key("mfx_send1"), s2 = rnd_key("mfx_send2"), mx = rnd_key("modfx_mix"), on = rnd_key("modfx_on");
        for (int w = 0; w <= 2; w++) {
            double a1 = 0, a2 = 0, am = 0, nz = 0, non = 0, quiet = 0;
            for (int s = 0; s < 400; s++) {
                uint32_t seed = 9100u + (uint32_t)s;
                rnd_opts_t op = {w * 0.5f, 0, 0, RV_BOTH};
                rnd_generate_x(d, o, 0, &seed, &op);
                float v1 = rnd_real(s1, o[s1]), v2 = rnd_real(s2, o[s2]);
                a1 += v1; a2 += v2;
                quiet += v1 < 15.0f && v2 < 15.0f;
                if (o[on] > 0.5f) { am += rnd_real(mx, o[mx]); non++; }
                nz += v1 > 0 || v2 > 0;
            }
            printf("WILD %d%%: send 1 %.1f%%, send 2 %.1f%% on average; %.0f%% of sounds with a send, %.0f%% with both "
                   "sends under 15%%; synth effect on in %.0f%%, its mix %.1f%% on average\n", w * 50, a1 / 400, a2 / 400,
                   nz / 4, quiet / 4, non / 4, non ? am / non : 0.0);
        }
        return 0;
    }
    if (argc < 3 || (strcmp(argv[1], "gen") && strcmp(argv[1], "check"))) {
        fprintf(stderr, "usage: rnd_golden gen|check <fixture>\n");
        return 2;
    }
    int gen = !strcmp(argv[1], "gen");
    static float def[NPARAMS], out[NPARAMS];
    for (int i = 0; i < NPARAMS; i++) def[i] = PARAMS[i].def;
    empty_rows(def);
    static const float WILD[3] = {0.0f, 0.5f, 1.0f};
    static const unsigned ALSO[2] = {0, 1u << RS_ARP};   /* (not SEQUENCER: its last rules were past rule 255 before, a bug fixed that day) */
    FILE *f = fopen(argv[2], gen ? "w" : "r");
    if (!f) { fprintf(stderr, "rnd_golden: can't open %s\n", argv[2]); return 2; }
    int n = 0, bad = 0;
    for (int w = 0; w < 3; w++)
        for (int a = 0; a < 2; a++)
            for (int s = 0; s < 20; s++) {
                rnd_opts_t o = {WILD[w], ALSO[a], 0, RV_BOTH};
                uint32_t seed = 7000u + 97u * (uint32_t)s + 13u * (uint32_t)w + (uint32_t)a, seed2 = seed ^ 0x5bd1e995u;
                int arch = rnd_generate_x(def, out, 0, &seed, &o);
                if (getenv("RND_GOLDEN_DUMP")) printf("arch=%d\n", arch);
                uint64_t hg = hash(out, seed);
                rnd_mutate_x(def, out, 0.4f, 0, &seed2, &o);
                uint64_t hm = hash(out, seed2);
                if (gen) {
                    fprintf(f, "%016llx %016llx\n", (unsigned long long)hg, (unsigned long long)hm);
                } else {
                    unsigned long long eg = 0, em = 0;
                    if (fscanf(f, "%llx %llx", &eg, &em) != 2) { bad++; continue; }
                    if (eg != hg) { bad++; fprintf(stderr, "RANDOMISE wild %.1f also %u seed %d differs\n", WILD[w], ALSO[a], s); }
                    if (em != hm) { bad++; fprintf(stderr, "MUTATE wild %.1f also %u seed %d differs\n", WILD[w], ALSO[a], s); }
                }
                n++;
            }
    fclose(f);
    if (gen) { printf("rnd_golden: wrote %d calls\n", n); return 0; }
    printf("%s randomiser golden: %d RANDOMISE + %d MUTATE calls as before but for the 2026-10-30 changes%s\n",
           bad ? "FAIL" : "ok  ", n, n, bad ? " (see above)" : "");
    return bad ? 1 : 0;
}
