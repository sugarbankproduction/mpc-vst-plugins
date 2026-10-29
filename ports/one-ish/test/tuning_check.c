/* Microtuning offline checks (src/tuning.h; the Scala files read by desktop/scala.c, the window's own reader, with the
 * real files in test/scales/): included by offline_check.c after midi_check.c, using its helpers.
 *   12-TET   a loaded 12-tone equal scale gives exactly the samples of no scale (chords, glide, bend, LFO, MACRO)
 *   19-EDO   every key a 19th of an octave from key 60, heard; master tune and fine tune still apply
 *   JI+kbm   5-limit just intonation on D with A at 440 Hz and a key left out (silent)
 *   state    saved with the sound and restored; an old state has none; per Synth; SAVE / INTO SYNTH; CLEAR; damaged
 *            scales and files refused */
#include "../desktop/scala.c"
#define SCALES "ports/one-ish/test/scales/"
static int read_file(const char *path, char *out, int n) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    size_t k = fread(out, 1, (size_t)n - 1, f);
    fclose(f);
    out[k] = 0;
    return 1;
}
/* a Scala file into the compact text (kbm: a keyboard map); "" when it doesn't read */
static const char *scala(const char *file, int kbm, char *out, int n) {
    static char text[65536];
    char err[128] = "", path[256];
    snprintf(path, sizeof path, SCALES "%s", file);
    out[0] = 0;
    if (!read_file(path, text, sizeof text)) { printf("     can't read %s\n", path); return out; }
    if (!(kbm ? kbm_compact : scl_compact)(text, path, out, n, err, sizeof err)) {
        printf("     %s: %s\n", file, err);
        out[0] = 0;
    }
    return out;
}
static void tun_cmd(void *e, const char *cmd, int syn, const char *text) {
    static char b[8192];
    snprintf(b, sizeof b, "%s%d\n%s", cmd, syn, text);
    E->set_param(e, "state", b);
}
static double tun_pitch_of(void *e, const char *pre, int key, int *play) {
    char k[48], b[64];
    snprintf(k, sizeof k, "%stun_pitch%d", pre, key);
    if (E->get_param(e, k, b, sizeof b) <= 0) return NAN;
    if (play) *play = strchr(b, ';') ? atoi(strchr(b, ';') + 1) : -1;
    return atof(b);
}
static double played_hz(void *e, int key) {   /* the frequency key sounds at */
    static float x[N + 4096];
    note(e, 1, key, 100);
    render_f(e, x, N + 4096);
    double hz = peak_hz(x + 4096);
    note(e, 0, key, 0);
    static float tail[16384];
    render_f(e, tail, 16384);   /* (its release, before the next note) */
    return hz;
}

static void microtuning(void) {
    char what[400], s12[4096], s19[4096], sji[4096], kji[2048];
    scala("12-tet.scl", 0, s12, sizeof s12);
    scala("19-edo.scl", 0, s19, sizeof s19);
    scala("ji-5-limit.scl", 0, sji, sizeof sji);
    scala("ji-on-d.kbm", 1, kji, sizeof kji);
    snprintf(what, sizeof what, "scala: the four test files read (12-TET \"%.32s...\", the map \"%.60s\")", s12, kji);
    check(s12[0] && s19[0] && sji[0] && !strncmp(kji, "ji-on-d|12,0,127,62,69,440,12,0,1,2,3,4,5,-1,7", 46), what);

    /* 12-TET loaded: exactly the samples of no scale at all */
    void *a = patch(SAW, 2, 200, 80, 50), *b = patch(SAW, 2, 200, 80, 50);
    static const char *const SOUND = "voices=7,glide=1,glide_time=40,pitch_lfo=10,o2_level=60,o2_freq=0.3,mac_level=30,"
                                     "o1_track=150,key_center=57";
    setkv(a, SOUND);
    setkv(b, SOUND);
    tun_cmd(b, "tunscl", 1, s12);
    static float xa[4410], xb[4410];
    int same = 1;
    static const int keys[] = {48, 55, 60, 64, 67, 72, 36, 84};
    for (unsigned k = 0; k < sizeof keys / sizeof keys[0]; k++) {
        note(a, 1, keys[k], 90);
        note(b, 1, keys[k], 90);
        if (k == 4) {
            uint8_t bend[3] = {0xE0, 0, 80};
            E->midi(a, bend, 3);
            E->midi(b, bend, 3);
        }
        render_f(a, xa, 4410);
        render_f(b, xb, 4410);
        same &= !memcmp(xa, xb, sizeof xa);
        if (k % 2) { note(a, 0, keys[k - 1], 0); note(b, 0, keys[k - 1], 0); }
    }
    snprintf(what, sizeof what, "12-TET: a loaded 12-tone equal scale plays exactly the usual samples (%s)",
             same ? "identical" : "DIFFERENT");
    check(same, what);
    E->destroy(a);
    E->destroy(b);

    /* 19-EDO: key 60 stays, every key one 19th of an octave, key 79 the octave; heard on an oscillator */
    void *e = patch(SAW, 1, 100, 100, 20);
    tun_cmd(e, "tunscl", 1, s19);
    double worst = 0;
    for (int k = 0; k < 128; k++) worst = fmax(worst, fabs(tun_pitch_of(e, "", k, NULL) - (60 + (k - 60) * 12.0 / 19)));
    double h61 = played_hz(e, 61), h79 = played_hz(e, 79), w61 = 261.6255653 * pow(2, 1.0 / 19);
    snprintf(what, sizeof what, "19-EDO: every key within %.4f cents of its 19th; key 61 plays %.2f Hz (want %.2f), "
             "key 79 %.2f (want 523.25)", worst * 100, h61, w61, h79);
    check(worst * 100 < 0.01 && fabs(cents(h61, w61)) < 2 && fabs(cents(h79, 523.2511306)) < 2, what);
    set(e, "master_tune", 432);   /* master tune and fine tune still move everything */
    set(e, "fine_tune", 10);
    double t79 = played_hz(e, 79), w79 = 523.2511306 * 432 / 440 * pow(2, 0.1 / 12);
    snprintf(what, sizeof what, "19-EDO: master tune 432 Hz and fine tune +10 cents still apply (key 79 %.2f Hz, want "
             "%.2f)", t79, w79);
    check(fabs(cents(t79, w79)) < 2, what);
    E->destroy(e);

    /* just intonation with a keyboard map: D (62) is degree 0, A (69) 440 Hz, G# (68) left out */
    e = patch(SAW, 1, 100, 100, 20);
    tun_cmd(e, "tunscl", 1, sji);
    tun_cmd(e, "tunkbm", 1, kji);
    int play68 = -1, play69 = -1;
    double p69 = tun_pitch_of(e, "", 69, &play69), p62 = tun_pitch_of(e, "", 62, NULL), p66 = tun_pitch_of(e, "", 66, NULL);
    double p74 = tun_pitch_of(e, "", 74, NULL);
    tun_pitch_of(e, "", 68, &play68);
    double want62 = 69 - 1200 * log2(1.5) / 100, want66 = 69 + (1200 * log2(1.25) - 1200 * log2(1.5)) / 100;
    double h66 = played_hz(e, 66), w66 = 440 * pow(2, (want66 - 69) / 12);
    note(e, 1, 68, 100);   /* left out: silent */
    double r68 = rms_of(e, 8192);
    note(e, 0, 68, 0);
    snprintf(what, sizeof what, "JI + map: A %.4f, D %.5f (want %.5f), F# %.5f (want %.5f; plays %.2f Hz, want %.2f), "
             "D an octave up %.5f, G# left out (rms %.5f)", p69, p62, want62, p66, want66, h66, w66, p74, r68);
    check(fabs(p69 - 69) < 1e-4 && fabs(p62 - want62) < 1e-4 && fabs(p66 - want66) < 1e-4 && fabs(p74 - p62 - 12) < 1e-4
              && play68 == 0 && play69 == 1 && fabs(cents(h66, w66)) < 2 && r68 < 1e-4,
          what);

    /* saved with the sound: a state round trip; an old state has none; Synth 2's own; SAVE / INTO SYNTH; CLEAR */
    static char st[262144];
    E->get_param(e, "state", st, sizeof st);
    void *f = E->create(NULL);
    E->set_param(f, "state", st);
    double q66 = tun_pitch_of(f, "", 66, NULL);
    E->set_param(f, "state", "v=1;flt_cut=50");
    double old66 = tun_pitch_of(f, "", 66, NULL);
    const char *t = strstr(st, ";tun_scl");
    snprintf(what, sizeof what, "state: the scale and map are saved with the sound (%d bytes), restored (F# %.5f); a "
             "state without them plays the usual pitches (%.1f)", t ? (int)strlen(t) : 0, q66, old66);
    check(strstr(st, ";tun_scl=ji-5-limit|12,") && strstr(st, ";tun_kbm=ji-on-d|") && fabs(q66 - want66) < 1e-4
              && old66 == 66, what);
    tun_cmd(f, "tunscl", 2, s19);
    double s1 = tun_pitch_of(f, "", 61, NULL), s2 = tun_pitch_of(f, "s2_", 61, NULL);
    E->get_param(f, "state", st, sizeof st);
    void *g = E->create(NULL);
    E->set_param(g, "state", st);
    double g2 = tun_pitch_of(g, "s2_", 61, NULL);
    static char one[16384], cmd[17000];
    E->get_param(f, "synth_state2", one, sizeof one);   /* SAVE SYNTH 2, then INTO SYNTH 3 */
    snprintf(cmd, sizeof cmd, "synthload3\n%s", one);
    E->set_param(g, "state", cmd);
    double g3 = tun_pitch_of(g, "s3_", 61, NULL);
    snprintf(what, sizeof what, "per Synth: Synth 2's 19-EDO leaves Synth 1 alone (%.4f, %.4f), is saved as s2_tun_scl "
             "(%.4f), and goes with SAVE / INTO SYNTH (%.4f)", s1, s2, g2, g3);
    check(s1 == 61 && fabs(s2 - (60 + 12.0 / 19)) < 1e-4 && fabs(g2 - s2) < 1e-6 && fabs(g3 - s2) < 1e-6
              && strstr(st, ";s2_tun_scl=19-edo|19,"), what);
    tun_cmd(g, "tunclear", 3, "");
    E->set_param(g, "state", "tunscl1\nbad|0");   /* nonsense: refused, nothing changes */
    E->set_param(g, "state", "tunscl1\nbad|2,100,-50");
    check(tun_pitch_of(g, "s3_", 61, NULL) == 61 && tun_pitch_of(g, "", 61, NULL) == 61,
          "microtuning: CLEAR goes back to the usual pitches; a damaged scale is refused");
    char err[128];
    check(!scl_compact("x\n3\n100.0\n0/1\n", "x.scl", one, sizeof one, err, sizeof err)
              && !kbm_compact("12\n0\n", "y.kbm", one, sizeof one, err, sizeof err),
          "scala: damaged files are refused with a reason");
    E->destroy(e);
    E->destroy(f);
    E->destroy(g);
}

/* (offline_bench) 8 gliding voices with and without a 19-EDO scale: what the table lookup costs */
static void tuning_bench(void) {
    enum { BLOCKS = 20000 };
    int16_t buf[256];
    char s19[4096];
    scala("19-edo.scl", 0, s19, sizeof s19);
    double us[2];
    for (int c = 0; c < 2; c++) {
        void *e = patch(SAW, 5, 300, 70, 250);
        setkv(e, "voices=7,o2_level=80,o3_level=60,glide=1,glide_time=60,pitch_lfo=5");
        if (c) tun_cmd(e, "tunscl", 1, s19);
        for (int k = 0; k < 8; k++) note(e, 1, 48 + 3 * k, 100);
        E->render(e, buf, 128);
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int b = 0; b < BLOCKS; b++) E->render(e, buf, 128);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        us[c] = ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / 1e3 / BLOCKS;
        E->destroy(e);
    }
    printf("bench: 8 voices without a scale %.2f us per block, with 19-EDO %.2f (%+.2f us)\n", us[0], us[1], us[1] - us[0]);
}
