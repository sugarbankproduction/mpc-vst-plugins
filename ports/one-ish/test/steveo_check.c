/* (2026-11-02) Steveo's sounds in the main preset list: each one, loaded as a program, plays as its patch file
 * (libraries/<BANK>/*.oneish) loaded as a state: the largest sample difference over a phrase, against the sound's own peak.
 * Every setting is the same; loaded one by one, a sound may still start a free-running part elsewhere (at most about -44 dB
 * of its peak, 2026-11-02: 158 identical, 64 less than that), so the bar is -40 dB.
 * Run on a list made by test/steveo_check.py: name TAB state. Includes golden_check.c for its fresh engine. Usage: steveo_check <list> */
#define main golden_main
#include "golden_check.c"
#undef main
#include <math.h>

enum { BLOCKS = 500 };
static float A[BLOCKS * 256], B[BLOCKS * 256];
static void play(void *e, float *out) {
    static const int chord[3] = {48, 60, 67};
    for (int k = 0; k < 3; k++) midi3(e, 0x90, chord[k], 100);
    for (int b = 0; b < BLOCKS; b++) {
        if (b == 300) for (int k = 0; k < 3; k++) midi3(e, 0x80, chord[k], 0);
        one_ish_render_float(e, out + b * 256, 128);
    }
    E->destroy(e);
}
int main(int argc, char **argv) {
    if (argc < 2) return 2;
    FILE *f = fopen(argv[1], "r");
    if (!f) { perror(argv[1]); return 2; }
    E = mpc_engine();
    static char line[1 << 17];
    int n = 0, gone = 0, same = 0, close = 0, bad = 0;
    double worst = -200;
    char wname[64] = "";
    while (fgets(line, sizeof line, f)) {
        char *name = strtok(line, "\t"), *st = strtok(NULL, "\n");
        if (!name || !st) continue;
        n++;
        int idx = preset_index(name);
        if (idx < 0) { printf("FAIL %s: no such preset\n", name); gone++; continue; }
        void *a = fresh();
        E->set_param(a, "state", st);
        play(a, A);
        void *b = fresh();
        for (int i = 0; i < PRESETS[idx].n; i++)
            E->set_param(b, PARAMS[PRESETS[idx].values[i].param].key, PRESETS[idx].values[i].value);
        play(b, B);
        double d = 0, pk = 1e-9;
        for (int i = 0; i < BLOCKS * 256; i++) {
            double x = fabs((double)A[i] - B[i]);
            if (x > d) d = x;
            if (fabs(A[i]) > pk) pk = fabs(A[i]);
        }
        double db = d > 0 ? 20 * log10(d / pk) : -200;
        if (db > worst) { worst = db; snprintf(wname, sizeof wname, "%s", name); }
        if (d == 0) same++;
        else if (db < -40) close++;
        else { bad++; printf("FAIL %s: differs by %.1f dB of its peak\n", name, db); }
    }
    fclose(f);
    printf("%s Steveo's sounds: %d of them: %d identical, %d within -40 dB, %d further; %d missing; worst %.1f dB (%s); %d presets in all\n",
           bad || gone || n != 222 ? "FAIL" : "ok  ", n, same, close, bad, gone, worst, wname, NPRESETS);
    return bad || gone || n != 222;
}
