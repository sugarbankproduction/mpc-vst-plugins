/* ONE-ish randomiser golden check (2026-10-30): RANDOMISE and MUTATE give what they gave before the X/Y pad and
 * ribbon rows existed, except those three rows (matrix rows 14-16) and params added since.
 *
 *   rnd_golden gen   <fixture>   with the randomiser BEFORE a change: a hash per call of every Synth 1 / shared param
 *                                (by key, in its own units) and of the seed after it
 *   rnd_golden check <fixture>   with the randomiser AFTER it: the same calls must give the same hashes
 *
 * Calls: RANDOMISE from the defaults at WILD 0, 50 and 100%, with no tick button and with ARPEGGIATOR + SEQUENCER lit
 * (20 seeds each), then MUTATE (40%) of each result with rows 14-16 put back to an empty row (so its input is the same
 * before and after). Left out of the hash: the Synth 2 / 3 copies (never touched), rows 14-16 and the keys listed in
 * NEWER (params that did not exist before; the randomiser leaves them alone or at their defaults).
 * Build and run: test/run.sh (check against test/rnd_golden.txt). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "params.h"
#include "randomize.h"

static const char *const NEWER[] = {"dly_on", "rev_on"};   /* (2026-10-30) the master effect slots' own ON */

static int skipped(const char *k) {
    if (k[0] == 's' && k[1] >= '2' && k[1] <= '9' && k[2] == '_') return 1;
    if (!strncmp(k, "mod14_", 6) || !strncmp(k, "mod15_", 6) || !strncmp(k, "mod16_", 6)) return 1;
    for (unsigned i = 0; i < sizeof NEWER / sizeof NEWER[0]; i++)
        if (!strcmp(k, NEWER[i])) return 1;
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
        for (const char *p = t; *p; p++) h = (h ^ (unsigned char)*p) * 1099511628211ull;
    }
    snprintf(t, sizeof t, "seed=%u", seed);
    for (const char *p = t; *p; p++) h = (h ^ (unsigned char)*p) * 1099511628211ull;
    return h;
}
static void empty_rows(float *n) {   /* rows 14-16 as an empty row (row 13's defaults, the same before and after) */
    static const char *const F[] = {"src", "dst", "amt", "via", "via_amt", "xf", "xf_amt", "on"};
    for (int r = 14; r <= 16; r++)
        for (unsigned f = 0; f < sizeof F / sizeof F[0]; f++) {
            char k[32], k13[32];
            snprintf(k, sizeof k, "mod%d_%s", r, F[f]);
            snprintf(k13, sizeof k13, "mod13_%s", F[f]);
            int i = rnd_key(k), j = rnd_key(k13);
            if (i < NPARAMS && j < NPARAMS) n[i] = PARAMS[j].def;
        }
}

int main(int argc, char **argv) {
    if (argc < 3 || (strcmp(argv[1], "gen") && strcmp(argv[1], "check"))) {
        fprintf(stderr, "usage: rnd_golden gen|check <fixture>\n");
        return 2;
    }
    int gen = !strcmp(argv[1], "gen");
    static float def[NPARAMS], out[NPARAMS], mut[NPARAMS];
    for (int i = 0; i < NPARAMS; i++) def[i] = PARAMS[i].def;
    static const float WILD[3] = {0.0f, 0.5f, 1.0f};
    static const unsigned ALSO[2] = {0, 1u << RS_ARP | 1u << RS_SEQ};
    FILE *f = fopen(argv[2], gen ? "w" : "r");
    if (!f) { fprintf(stderr, "rnd_golden: can't open %s\n", argv[2]); return 2; }
    int n = 0, bad = 0;
    for (int w = 0; w < 3; w++)
        for (int a = 0; a < 2; a++)
            for (int s = 0; s < 20; s++) {
                rnd_opts_t o = {WILD[w], ALSO[a], 0};
                uint32_t seed = 7000u + 97u * (uint32_t)s + 13u * (uint32_t)w + (uint32_t)a;
                rnd_generate_x(def, out, 0, &seed, &o);
                uint64_t hg = hash(out, seed);
                empty_rows(out);
                rnd_mutate_x(out, mut, 0.4f, 0, &seed, &o);
                uint64_t hm = hash(mut, seed);
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
    printf("%s randomiser golden: %d RANDOMISE + %d MUTATE calls the same as before but for the pad / ribbon rows%s\n",
           bad ? "FAIL" : "ok  ", n, n, bad ? " (see above)" : "");
    return bad ? 1 : 0;
}
