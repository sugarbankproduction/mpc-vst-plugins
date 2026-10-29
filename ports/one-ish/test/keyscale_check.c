/* KEY and SCALE (src/keyscale.h): the tables the generators play from; each Synth keeps its own key and scale */
#include "../src/keyscale.h"
static void keyscale_checks(void) {
    char what[200];
    int ok = 1;
    for (int s = 0; s < KS_COUNT; s++)
        for (int i = 1; i < KS_SIZE[s]; i++) ok &= KS_STEPS[s][i] > KS_STEPS[s][i - 1] && KS_STEPS[s][i] < 12;
    check(ok, "KEY / SCALE: every scale's steps rise within one octave");
    int d0 = ks_degree(2, KS_MINOR, 5, 0), d2 = ks_degree(2, KS_MINOR, 5, 2), d7 = ks_degree(2, KS_MINOR, 5, 7),
        dm1 = ks_degree(2, KS_MINOR, 5, -1);
    snprintf(what, sizeof what, "D minor: the root %d, the third %d, an octave up %d, one below %d (want 62 65 74 60)",
             d0, d2, d7, dm1);
    check(d0 == 62 && d2 == 65 && d7 == 74 && dm1 == 60, what);
    int q1 = ks_quantize(61, 0, KS_MAJOR), q2 = ks_quantize(66, 0, KS_PENT_MAJOR), q3 = ks_quantize(127, 1, KS_MAJOR);
    snprintf(what, sizeof what, "quantize: C# in C major -> %d, F# in C pentatonic -> %d, 127 in C# major -> %d", q1, q2, q3);
    check(q1 == 60 && q2 == 67 && q3 <= 127 && q3 >= 116, what);
    void *e = E->create(NULL);
    set(e, "key_root", 2); set(e, "s2_key_root", 7); set(e, "s3_key_scale", 9);
    snprintf(what, sizeof what, "each Synth has its own KEY and SCALE (%.0f %.0f %.0f, %.0f %.0f %.0f)", get(e, "key_root"),
             get(e, "s2_key_root"), get(e, "s3_key_root"), get(e, "key_scale"), get(e, "s2_key_scale"), get(e, "s3_key_scale"));
    check(get(e, "key_root") == 2 && get(e, "s2_key_root") == 7 && get(e, "s3_key_root") == 0 && get(e, "key_scale") == 1 &&
          get(e, "s3_key_scale") == 9, what);
    E->destroy(e);
}
