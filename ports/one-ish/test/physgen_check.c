/* The bouncing balls and particles (src/physgen.h, src/physgen_run.h; 2026-10-29): off is silent and changes nothing,
 * the same settings give the same notes, every note is in the Synth's key and scale, MAX NOTES/S holds, the defaults
 * are moderate, each Synth runs its own, HELD KEYS, KEY RESTARTS, SYNC on the grid, the view, and a saved sound keeps
 * the settings but not the positions. Included from offline_check.c (its helpers: set, get, note, check).
 * (2026-10-30, physgen2_checks) the walls (an open floor plays nothing from the floor, silent walls bounce quietly,
 * particles leave), KEY LAUNCH and KEY EMIT, FOLLOW OCTAVE, and the generators' eight matrix sources (listed, moving,
 * finite, gliding, heard). */
#pragma push_macro("SCALES")   /* (seq.h's SCALES is not tuning_check.c's) */
#undef SCALES
#include "../src/physgen.h"
#undef SCALES
#pragma pop_macro("SCALES")

typedef struct { long long t; int note, vel; } pg_ev_t;
#define PG_MAX 4096
static pg_ev_t pg_a[PG_MAX], pg_b[PG_MAX];

/* render `secs` in blocks of 128, the note-ons of Synth `key` ("gen_log", "s2_gen_log") into ev (from *n on) */
static void pg_run(void *e, double secs, const char *key, pg_ev_t *ev, int *n) {
    int16_t buf[256];
    static char b[8192];
    for (long at = 0; at < (long)(secs * SR); at += 128) {
        E->render(e, buf, 128);
        if (E->get_param(e, key, b, sizeof b) <= 0) continue;
        for (char *p = b; *p;) {
            long long t;
            int nt, v, used;
            if (sscanf(p, "%lld:%d:%d;%n", &t, &nt, &v, &used) != 3) break;
            if (v > 0 && *n < PG_MAX) ev[(*n)++] = (pg_ev_t){t, nt, v};
            p += used;
        }
    }
}

static void *pg_patch(void) {
    void *e = E->create(NULL);
    set(e, "amp_r", 10);
    E->set_param(e, "lfo_bpm", "120");
    return e;
}

static int pg_in_key(const pg_ev_t *ev, int n, int root, int scale) {
    for (int i = 0; i < n; i++) {
        int pc = ((ev[i].note - root) % 12 + 12) % 12, ok = 0;
        for (int k = 0; k < KS_SIZE[scale]; k++) ok |= KS_STEPS[scale][k] == pc;
        if (!ok) return 0;
    }
    return 1;
}

static int pg_same(const pg_ev_t *a, int na, const pg_ev_t *b, int nb, long long shift) {
    if (na != nb) return 0;
    for (int i = 0; i < na; i++)
        if (a[i].t != b[i].t + shift || a[i].note != b[i].note || a[i].vel != b[i].vel) return 0;
    return 1;
}

static void physgen2_checks(void);
static void physgen_checks(void) {
    char what[256];
    int16_t ba[256], bb[256];
    /* off: the very same samples as a fresh instance, with every setting moved */
    {
        void *a = pg_patch(), *b = pg_patch();
        setkv(b, "phys_balls=8,phys_speed=100,phys_gravity=0,phys_rate=100,phys_trigger=1,phys_keys=0,phys_max=30");
        note(a, 1, 60, 100); note(b, 1, 60, 100);
        int same = 1, nb = 0;
        for (int k = 0; k < 800; k++) {
            E->render(a, ba, 128); E->render(b, bb, 128);
            same &= !memcmp(ba, bb, sizeof ba);
        }
        pg_run(b, 0.1, "gen_log", pg_b, &nb);
        check(same && nb == 0, "BALLS / PARTICLES off: the very same samples as before with every setting moved, no notes");
        E->destroy(a); E->destroy(b);
    }
    /* the same settings: the same notes; every note in the key, over every way of playing */
    static const char *const MODES[] = {"phys_balls_on=1", "phys_parts_on=1,phys_trigger=0",
                                        "phys_parts_on=1,phys_trigger=1,phys_attract=60,phys_rate=80",
                                        "phys_parts_on=1,phys_trigger=2", "phys_balls_on=1,phys_parts_on=1,phys_balls=8"};
    static const char *const NAMES[] = {"balls", "particles crossing the line", "particles colliding",
                                        "particles dying", "8 balls and particles"};
    for (int m = 0; m < 5; m++) {
        void *a = pg_patch(), *b = pg_patch();
        setkv(a, "key_root=2,key_scale=10"); setkv(b, "key_root=2,key_scale=10");   /* D pentatonic minor */
        setkv(a, MODES[m]); setkv(b, MODES[m]);
        int na = 0, nb = 0;
        pg_run(a, 12.0, "gen_log", pg_a, &na);
        pg_run(b, 12.0, "gen_log", pg_b, &nb);
        snprintf(what, sizeof what, "%s: %d notes in 12 s, the same again from the same start, all in D pentatonic minor",
                 NAMES[m], na);
        check(na > 3 && pg_same(pg_a, na, pg_b, nb, 0) && pg_in_key(pg_a, na, 2, KS_PENT_MINOR), what);
        E->destroy(a); E->destroy(b);
    }
    /* musical by default: the balls at their defaults play a few notes a second, not a flood */
    {
        void *a = pg_patch();
        set(a, "phys_balls_on", 1);
        int na = 0;
        pg_run(a, 30.0, "gen_log", pg_a, &na);
        snprintf(what, sizeof what, "the defaults (3 balls): %.2f notes a second (want 1 .. 6)", na / 30.0);
        check(na >= 30 && na <= 180, what);
        set(a, "phys_balls_on", 0);
        int16_t b[256];
        E->render(a, b, 128);
        na = 0;
        pg_run(a, 3.0, "gen_log", pg_a, &na);
        check(na == 0, "... and switched off, no more notes");
        E->destroy(a);
    }
    /* MAX NOTES/S: a box full of fast balls plays no more than it allows */
    {
        void *a = pg_patch();
        setkv(a, "phys_balls_on=1,phys_balls=8,phys_speed=100,phys_gravity=0,phys_size=0,phys_bounce=100,phys_max=5");
        int na = 0, worst = 0;
        pg_run(a, 10.0, "gen_log", pg_a, &na);
        for (int i = 0; i < na; i++) {   /* the most in any one second */
            int c = 0;
            for (int j = i; j < na && pg_a[j].t - pg_a[i].t < (long long)SR; j++) c++;
            if (c > worst) worst = c;
        }
        snprintf(what, sizeof what, "MAX NOTES/S 5 with 8 fast balls: %d notes in 10 s, at most %d in any second (want "
                 "<= 7)", na, worst);
        check(na >= 40 && na <= 52 && worst <= 7, what);
        E->destroy(a);
    }
    /* each Synth its own: Synth 2 plays, Synth 1 doesn't; both on with the same settings play different notes */
    {
        void *a = pg_patch();
        setkv(a, "s2_syn_on=1,s2_phys_balls_on=1");
        int n1 = 0, n2 = 0, n3 = 0;
        int16_t b[256];
        static char t[8192];
        for (int k = 0; k < (int)(6.0 * SR / 128); k++) {   /* both logs, block by block */
            E->render(a, b, 128);
            for (int c = 0; c < 2; c++) {
                if (E->get_param(a, c ? "s2_gen_log" : "gen_log", t, sizeof t) <= 0) continue;
                for (char *p = t; *p;) {
                    long long tt; int nt, v, used;
                    if (sscanf(p, "%lld:%d:%d;%n", &tt, &nt, &v, &used) != 3) break;
                    if (v > 0) { if (c) n2++; else n1++; }
                    p += used;
                }
            }
        }
        E->get_param(a, "s3_phys_view", t, sizeof t);
        n3 = atoi(t);
        snprintf(what, sizeof what, "Synth 2's balls play (%d notes) and Synth 1's and 3's don't (%d, %d balls)", n2, n1, n3);
        check(n2 > 3 && n1 == 0 && n3 == 0, what);
        E->destroy(a);
        a = pg_patch();
        setkv(a, "s2_syn_on=1,s2_phys_balls_on=1,phys_balls_on=1");
        int na = 0, nb = 0;
        for (int k = 0; k < (int)(6.0 * SR / 128); k++) {
            E->render(a, b, 128);
            for (int c = 0; c < 2; c++) {
                if (E->get_param(a, c ? "s2_gen_log" : "gen_log", t, sizeof t) <= 0) continue;
                for (char *p = t; *p;) {
                    long long tt; int nt, v, used;
                    if (sscanf(p, "%lld:%d:%d;%n", &tt, &nt, &v, &used) != 3) break;
                    if (v > 0) { if (c && nb < PG_MAX) pg_b[nb++] = (pg_ev_t){tt, nt, v}; else if (!c && na < PG_MAX) pg_a[na++] = (pg_ev_t){tt, nt, v}; }
                    p += used;
                }
            }
        }
        check(na > 3 && nb > 3 && !pg_same(pg_a, na, pg_b, nb, 0), "Synths 1 and 2 with the same settings: each its own "
              "balls (different notes)");
        E->destroy(a);
    }
    /* HELD KEYS: nothing without a key; from the lowest key held, in the key; nothing after it lets go */
    {
        void *a = pg_patch();
        setkv(a, "phys_balls_on=1,phys_keys=1,key_root=2,key_scale=1,phys_balls=6,phys_max=12");
        int na = 0;
        pg_run(a, 2.0, "gen_log", pg_a, &na);
        int before = na;
        note(a, 1, 50, 100); note(a, 1, 57, 100);
        na = 0;
        pg_run(a, 6.0, "gen_log", pg_a, &na);
        int low = 127;
        for (int i = 0; i < na; i++) if (pg_a[i].note < low) low = pg_a[i].note;
        note(a, 0, 50, 0); note(a, 0, 57, 0);
        int nb = 0;
        pg_run(a, 0.01, "gen_log", pg_b, &nb);
        nb = 0;
        pg_run(a, 3.0, "gen_log", pg_b, &nb);
        snprintf(what, sizeof what, "HELD KEYS: %d notes with no key, %d holding D3 + A3 (lowest %d, want >= 50, in D minor), "
                 "%d after letting go", before, na, low, nb);
        check(before == 0 && na > 5 && low >= 50 && pg_in_key(pg_a, na, 2, KS_MINOR) && nb == 0, what);
        E->destroy(a);
    }
    /* KEY RESTARTS: a key after 3 s plays what a key at the start plays, from the key on */
    {
        void *a = pg_patch(), *b = pg_patch();
        setkv(a, "phys_balls_on=1,phys_restart=1"); setkv(b, "phys_balls_on=1,phys_restart=1");
        int na = 0, nb = 0;
        note(b, 1, 64, 100);
        pg_run(a, 3.0, "gen_log", pg_a, &na);
        note(a, 1, 64, 100);
        na = 0;
        long long shift = (long long)((3.0 * SR + 127) / 128) * 128;   /* the blocks pg_run rendered before the key */
        pg_run(a, 5.0, "gen_log", pg_a, &na);
        pg_run(b, 5.0, "gen_log", pg_b, &nb);
        for (int i = 0; i < na; i++) pg_a[i].t -= shift;
        snprintf(what, sizeof what, "KEY RESTARTS: a key after 3 s gives the same %d notes as a key at the start (%d)", na, nb);
        check(na > 3 && pg_same(pg_a, na, pg_b, nb, 0), what);
        E->destroy(a); E->destroy(b);
    }
    /* SYNC 1/16 at 120 BPM: every note on a 16th (5512.5 samples) */
    {
        void *a = pg_patch();
        setkv(a, "phys_balls_on=1,phys_sync=3,phys_balls=6,phys_max=16");
        int na = 0, off = 0;
        pg_run(a, 8.0, "gen_log", pg_a, &na);
        for (int i = 0; i < na; i++) {
            double g = pg_a[i].t / 5512.5;
            if (fabs(g - floor(g + 0.5)) * 5512.5 > 1.0) off++;
        }
        snprintf(what, sizeof what, "SYNC 1/16 at 120 BPM: %d notes, %d off the grid", na, off);
        check(na > 5 && off == 0, what);
        E->destroy(a);
    }
    /* the view: the balls inside the box; a saved sound keeps the settings, not the positions */
    {
        void *a = pg_patch(), *b = pg_patch();
        setkv(a, "phys_balls_on=1,phys_parts_on=1,phys_balls=5,phys_rate=70");
        int na = 0;
        pg_run(a, 4.0, "gen_log", pg_a, &na);
        static char v[2048], st1[65536], st2[65536];
        E->get_param(a, "phys_view", v, sizeof v);
        int nb = -1, np = -1, wl = -1, inside = 1, used = 0, cnt = 0;
        sscanf(v, "%d %d %d;%n", &nb, &np, &wl, &used);
        for (char *p = v + used; *p; cnt++) {
            int x, y, f, u;
            if (sscanf(p, "%d,%d,%d;%n", &x, &y, &f, &u) != 3) break;
            inside &= x >= 0 && x <= 1000 && y >= 0 && y <= 1000 && (f == 0 || f == 1);
            p += u;
        }
        snprintf(what, sizeof what, "the view: %d balls, %d particles, every one inside the box (\"%.40s...\")", nb, np, v);
        check(nb == 5 && np > 0 && cnt == nb + np && inside, what);
        E->get_param(a, "state", st1, sizeof st1);
        E->set_param(b, "state", st1);
        E->get_param(b, "state", st2, sizeof st2);
        void *c = pg_patch();
        setkv(c, "phys_balls_on=1,phys_parts_on=1,phys_balls=5,phys_rate=70");
        int n1 = 0, n2 = 0;
        pg_run(b, 4.0, "gen_log", pg_b, &n1);
        pg_run(c, 4.0, "gen_log", pg_a, &n2);
        check(!strcmp(st1, st2) && !strstr(st1, "phys_view") && get(b, "phys_balls") == 5 && n1 > 3 &&
              pg_same(pg_b, n1, pg_a, n2, 0), "a saved sound keeps the settings (the same state back) and starts the balls "
              "and particles from their start, not where they were");
        E->destroy(a); E->destroy(b); E->destroy(c);
    }
    physgen2_checks();   /* (2026-10-30) the walls, the keys' new ways, FOLLOW OCTAVE, the matrix sources */
}

/* ==== (2026-10-30) the walls, KEY LAUNCH and KEY EMIT, FOLLOW OCTAVE, the generators as matrix sources ==== */
static int pg_view(void *e, int *np) {   /* the balls in the box now (and the particles) */
    static char v[2048];
    int nb = 0, p = 0, w = 0;
    E->get_param(e, "phys_view", v, sizeof v);
    sscanf(v, "%d %d %d;", &nb, &p, &w);
    if (np) *np = p;
    return nb;
}
static int pg_src(void *e, float *m) {   /* the eight sources ("gen_src"); 1 when all are read and finite in 0..1 */
    static char t[256];
    E->get_param(e, "gen_src", t, sizeof t);
    int ok = sscanf(t, "%f %f %f %f %f %f %f %f", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5], &m[6], &m[7]) == 8;
    for (int i = 0; i < 8; i++) ok &= isfinite(m[i]) && m[i] >= 0.0f && m[i] <= 1.0f;
    return ok;
}

static void physgen2_checks(void) {
    char what[320];
    /* the defaults written out keep today's sound: every wall PLAYS, FREE keys, FOLLOW OCTAVE off */
    {
        void *a = pg_patch(), *b = pg_patch();
        setkv(a, "phys_balls_on=1,phys_parts_on=1");
        setkv(b, "phys_balls_on=1,phys_parts_on=1,phys_floor=0,phys_ceil=0,phys_left=0,phys_right=0,phys_keys=0,"
                 "phys_follow_oct=0");
        E->set_param(b, "state", "kboct\n2");   /* (ignored with FOLLOW OCTAVE off) */
        int na = 0, nb = 0;
        pg_run(a, 8.0, "gen_log", pg_a, &na);
        pg_run(b, 8.0, "gen_log", pg_b, &nb);
        check(na > 5 && pg_same(pg_a, na, pg_b, nb, 0), "walls PLAYS, FREE keys, FOLLOW OCTAVE off written out: the same "
              "notes as before (the old sounds' golden check, in the full run, covers the rest)");
        E->destroy(a); E->destroy(b);
    }
    /* an OPEN floor plays nothing from the floor: with the other walls SILENT, no notes at all, the balls still in the
     * box (thrown in again as they fall out); the same with the floor PLAYS: notes */
    {
        void *a = pg_patch(), *b = pg_patch();
        setkv(a, "phys_balls_on=1,phys_balls=4,phys_ceil=1,phys_left=1,phys_right=1,phys_floor=2");
        setkv(b, "phys_balls_on=1,phys_balls=4,phys_ceil=1,phys_left=1,phys_right=1,phys_floor=0");
        int na = 0, nb = 0, inside = 1;
        for (int k = 0; k < 10; k++) {
            pg_run(a, 0.5, "gen_log", pg_a, &na);
            inside &= pg_view(a, NULL) == 4;
        }
        pg_run(b, 5.0, "gen_log", pg_b, &nb);
        snprintf(what, sizeof what, "OPEN floor, other walls SILENT: %d notes in 5 s, 4 balls always in the box (%s); the "
                 "floor PLAYS instead: %d notes", na, inside ? "yes" : "no", nb);
        check(na == 0 && inside && nb > 5, what);
        E->destroy(a); E->destroy(b);
    }
    /* SILENT walls bounce quietly: no notes, the balls bounce on (BALL HIT pulses, the balls move) */
    {
        void *a = pg_patch();
        setkv(a, "phys_balls_on=1,phys_balls=4,phys_floor=1,phys_ceil=1,phys_left=1,phys_right=1");
        int na = 0, ok = 1;
        float m[8], hit = 0.0f, ylo = 1.0f, yhi = 0.0f;
        for (int k = 0; k < 400; k++) {
            pg_run(a, 0.01, "gen_log", pg_a, &na);
            ok &= pg_src(a, m);
            hit = fmaxf(hit, m[3]);
            ylo = fminf(ylo, m[0]);
            yhi = fmaxf(yhi, m[0]);
        }
        snprintf(what, sizeof what, "every wall SILENT: %d notes in 4 s; BALL HIT up to %.2f, BALL 1 HEIGHT %.2f .. %.2f",
                 na, hit, ylo, yhi);
        check(na == 0 && ok && hit > 0.5f && yhi - ylo > 0.3f, what);
        E->destroy(a);
    }
    /* particles leave through an OPEN floor: dying ones play far less (they fall out first) */
    {
        void *a = pg_patch(), *b = pg_patch();
        setkv(a, "phys_parts_on=1,phys_trigger=2,phys_gravity=100,phys_life=80,phys_rate=70,phys_floor=2");
        setkv(b, "phys_parts_on=1,phys_trigger=2,phys_gravity=100,phys_life=80,phys_rate=70");
        int na = 0, nb = 0, pa = 0, pb = 0;
        pg_run(a, 8.0, "gen_log", pg_a, &na);
        pg_run(b, 8.0, "gen_log", pg_b, &nb);
        pg_view(a, &pa); pg_view(b, &pb);
        snprintf(what, sizeof what, "particles, OPEN floor: %d dying notes in 8 s (%d in the box) against %d (%d) with the "
                 "floor closed", na, pa, nb, pb);
        check(na * 4 < nb && pa < pb, what);
        E->destroy(a); E->destroy(b);
    }
    /* KEY LAUNCH: nothing before a key; each key throws in a ball (BALLS at most), its notes from that key, in the key */
    {
        void *a = pg_patch();
        setkv(a, "phys_balls_on=1,phys_balls=3,phys_keys=2,key_root=2,key_scale=1,phys_max=12");
        int na = 0, n1, n4, low = 127, quiet;
        pg_run(a, 2.0, "gen_log", pg_a, &na);
        quiet = na == 0 && pg_view(a, NULL) == 0;
        note(a, 1, 62, 100); note(a, 0, 62, 0);
        pg_run(a, 0.1, "gen_log", pg_a, &na);
        n1 = pg_view(a, NULL);
        int keys[] = {64, 65, 67};
        for (int i = 0; i < 3; i++) { note(a, 1, keys[i], 100); note(a, 0, keys[i], 0); pg_run(a, 0.05, "gen_log", pg_a, &na); }
        n4 = pg_view(a, NULL);
        na = 0;
        pg_run(a, 4.0, "gen_log", pg_a, &na);
        for (int i = 0; i < na; i++) if (pg_a[i].note < low) low = pg_a[i].note;
        snprintf(what, sizeof what, "KEY LAUNCH: silent and empty before a key (%s); 1 key: %d ball, 4 keys: %d (BALLS 3); "
                 "%d notes in 4 s, lowest %d (want >= 62), in D minor", quiet ? "yes" : "no", n1, n4, na, low);
        check(quiet && n1 == 1 && n4 == 3 && na > 3 && low >= 62 && pg_in_key(pg_a, na, 2, KS_MINOR), what);
        E->destroy(a);
        a = pg_patch();   /* the particles: a burst for each key, bigger with velocity */
        setkv(a, "phys_parts_on=1,phys_keys=2,phys_life=100");
        int p0, p1, p2;
        pg_run(a, 1.0, "gen_log", pg_a, &na);
        pg_view(a, &p0);
        note(a, 1, 60, 20);
        pg_run(a, 0.1, "gen_log", pg_a, &na);
        pg_view(a, &p1);
        note(a, 1, 72, 127);
        pg_run(a, 0.1, "gen_log", pg_a, &na);
        pg_view(a, &p2);
        snprintf(what, sizeof what, "KEY LAUNCH particles: %d before a key, a soft key's burst %d, a hard key's %d more", p0,
                 p1, p2 - p1);
        check(p0 == 0 && p1 >= 3 && p1 <= 5 && p2 - p1 == 12, what);
        E->destroy(a);
    }
    /* KEY EMIT: a ball per key held; particles stream only while keys are held, faster with velocity */
    {
        void *a = pg_patch();
        setkv(a, "phys_balls_on=1,phys_balls=4,phys_keys=3");
        int b0, b2, b1, bz, na = 0;
        pg_run(a, 0.5, "gen_log", pg_a, &na);
        b0 = pg_view(a, NULL);
        note(a, 1, 60, 100); note(a, 1, 67, 100);
        pg_run(a, 0.5, "gen_log", pg_a, &na);
        b2 = pg_view(a, NULL);
        note(a, 0, 60, 0);
        pg_run(a, 0.1, "gen_log", pg_a, &na);
        b1 = pg_view(a, NULL);
        note(a, 0, 67, 0);
        pg_run(a, 0.1, "gen_log", pg_a, &na);
        bz = pg_view(a, NULL);
        snprintf(what, sizeof what, "KEY EMIT balls: %d with no key, %d holding two, %d after one lets go, %d after both",
                 b0, b2, b1, bz);
        check(b0 == 0 && b2 == 2 && b1 == 1 && bz == 0, what);
        E->destroy(a);
        int cnt[2];
        for (int v = 0; v < 2; v++) {   /* PARTICLES PLAY dying (short lives): the notes count the particles sent out */
            a = pg_patch();
            setkv(a, "phys_parts_on=1,phys_keys=3,phys_trigger=2,phys_life=0,phys_rate=60,phys_max=30");
            na = 0;
            pg_run(a, 1.0, "gen_log", pg_a, &na);
            int before = na;
            note(a, 1, 60, v ? 127 : 32);
            na = 0;
            pg_run(a, 4.0, "gen_log", pg_a, &na);
            cnt[v] = na;
            note(a, 0, 60, 0);
            pg_run(a, 1.0, "gen_log", pg_a, &na);
            na = 0;
            pg_run(a, 2.0, "gen_log", pg_a, &na);
            int np = 0;
            pg_view(a, &np);
            snprintf(what, sizeof what, "KEY EMIT particles at velocity %d: %d notes before a key, %d in 4 s held, %d after "
                     "letting go (%d particles left)", v ? 127 : 32, before, cnt[v], na, np);
            check(before == 0 && cnt[v] > 3 && na == 0 && np == 0, what);
            E->destroy(a);
        }
        snprintf(what, sizeof what, "KEY EMIT: velocity 127 sends out more than velocity 32 (%d against %d)", cnt[1], cnt[0]);
        check(cnt[1] > 2 * cnt[0], what);
    }
    /* FOLLOW OCTAVE: the window's keyboard an octave up moves FREE notes an octave up; off, it doesn't */
    {
        void *a = pg_patch(), *b = pg_patch(), *c = pg_patch();
        setkv(a, "phys_balls_on=1,phys_follow_oct=1");
        setkv(b, "phys_balls_on=1,phys_follow_oct=1");
        setkv(c, "phys_balls_on=1");
        E->set_param(b, "state", "kboct\n1");
        E->set_param(c, "state", "kboct\n1");
        int na = 0, nb = 0, nc = 0, up = 1, same = 1;
        static pg_ev_t pc[PG_MAX];
        pg_run(a, 6.0, "gen_log", pg_a, &na);
        pg_run(b, 6.0, "gen_log", pg_b, &nb);
        pg_run(c, 6.0, "gen_log", pc, &nc);
        for (int i = 0; i < na && i < nb; i++) up &= pg_b[i].note == pg_a[i].note + 12 && pg_b[i].t == pg_a[i].t;
        for (int i = 0; i < na && i < nc; i++) same &= pc[i].note == pg_a[i].note;
        snprintf(what, sizeof what, "FOLLOW OCTAVE: keyboard octave +1 plays the same %d notes an octave up (%s); with "
                 "FOLLOW off it changes nothing (%s)", na, up ? "yes" : "no", same ? "yes" : "no");
        check(na > 5 && na == nb && up && na == nc && same, what);
        E->destroy(a); E->destroy(b); E->destroy(c);
    }
    /* the generators as matrix sources: listed at the end of the source and via lists; each moves, stays finite in
     * 0..1 and changes by little per control step (no zipper) */
    {
        int named = 0;
        for (int i = 0; i < NPARAMS; i++)
            if (!strcmp(PARAMS[i].key, "mod1_src") || !strcmp(PARAMS[i].key, "s3_mod16_via"))
                named += PARAMS[i].nopts == MS_COUNT && !strcmp(PARAMS[i].opts[MS_BALL_Y], "Ball 1 Height") &&
                         !strcmp(PARAMS[i].opts[MS_EUCLID], "Euclid Gate");
        check(named == 2, "the eight generator sources end the matrix's source and via lists (Ball 1 Height .. Euclid "
              "Gate)");
        void *a = pg_patch();
        setkv(a, "phys_balls_on=1,phys_parts_on=1,phys_rate=70,gen_euclid_on=1,gen_tm_on=1,gen_tm_change=50");
        note(a, 1, 60, 100);
        float m[8], lo[8], hi[8], prev[8], step[8] = {0};
        int ok = 1;
        int16_t buf[64];
        for (int i = 0; i < 8; i++) { lo[i] = 1.0f; hi[i] = 0.0f; }
        for (int k = 0; k < (int)(6.0 * SR / 16); k++) {   /* a control step at a time */
            E->render(a, buf, 16);
            ok &= pg_src(a, m);
            for (int i = 0; i < 8; i++) {
                lo[i] = fminf(lo[i], m[i]);
                hi[i] = fmaxf(hi[i], m[i]);
                if (k) step[i] = fmaxf(step[i], fabsf(m[i] - prev[i]));
                prev[i] = m[i];
            }
        }
        static const char *const NM[] = {"ball 1 height", "ball 1 x", "balls speed", "ball hit", "particle count",
                                         "particle hit", "turing value", "euclid gate"};
        int moved = 1, smooth = 1;
        for (int i = 0; i < 8; i++) {
            moved &= hi[i] - lo[i] > 0.1f;
            smooth &= step[i] <= 0.2f;
            snprintf(what, sizeof what, "source %s: %.3f .. %.3f, at most %.3f a control step", NM[i], lo[i], hi[i], step[i]);
            check(hi[i] - lo[i] > 0.1f && step[i] <= 0.2f, what);
        }
        check(ok && moved && smooth, "the eight generator sources move, stay finite in 0..1, and glide (no zipper)");
        E->destroy(a);
        /* in use: BALL 1 HEIGHT to the pitch changes a held note (the balls climb it) */
        void *b = patch(SAW, 5, 300, 70, 250), *c = patch(SAW, 5, 300, 70, 250);
        char kv[200];
        snprintf(kv, sizeof kv, "phys_balls_on=1,phys_floor=1,phys_ceil=1,phys_left=1,phys_right=1,mod1_on=1,mod1_src=%d,"
                 "mod1_dst=%d,mod1_amt=100,mod1_via=0", MS_BALL_Y, MD_PITCH);
        setkv(b, kv);
        setkv(c, kv);
        set(c, "phys_balls_on", 0);   /* (the same slot, no balls: the source stays at 0) */
        note(b, 1, 48, 100); note(c, 1, 48, 100);
        static float fb[22050], fc[22050];
        double dif = 0.0, lvl = 0.0;
        int fin = 1;
        for (int k = 0; k < 8; k++) {
            render_f(b, fb, 22050);
            render_f(c, fc, 22050);
            for (int i = 0; i < 22050; i++) {
                dif += (double)(fb[i] - fc[i]) * (fb[i] - fc[i]);
                lvl += (double)fc[i] * fc[i];
                fin &= isfinite(fb[i]);
            }
        }
        snprintf(what, sizeof what, "BALL 1 HEIGHT -> Pitch: the held note follows the ball (difference %.1f dB "
                 "under the note)", 10.0 * log10((dif + 1e-30) / (lvl + 1e-30)));
        check(dif > 1e-3 * lvl && lvl > 0.0 && fin, what);
        E->destroy(b); E->destroy(c);
    }
}

/* the CPU: the world alone (8 balls; the full pool of 32 particles colliding), and the engine with them playing */
static void physgen_bench(void) {
    enum { STEPS = 400000 };
    ph_world_t w;
    ph_hit_t h[PH_HITS];
    ph_cfg_t c = {1, 8, 0.6f, 1.6f, 0.9f, 0, PHT_LINE, 1.0f, 0.35f, 1.6f, 0.0f, {0}, 0, 0};
    long hits = 0;
    struct timespec t0, t1;
    ph_restart(&w, 1, c.speed);
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int k = 0; k < STEPS; k++) hits += ph_step(&w, &c, (float)PH_STEP / SR, h);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double us = ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / 1e3 / STEPS * (128.0 / PH_STEP);
    printf("bench: bouncing balls, 8 balls (the world alone): %6.3f us per 128-frame block (%ld hits)\n", us, hits);
    ph_cfg_t p = {0, 0, 0.6f, 0.5f, 0.9f, 1, PHT_COLLIDE, 200.0f, 1.0f, 9.0f, 6.0f, {0}, 0, 0};
    int live = 0;
    ph_restart(&w, 1, p.speed);
    for (int k = 0; k < 2000; k++) ph_step(&w, &p, (float)PH_STEP / SR, h);   /* the pool filled */
    hits = 0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int k = 0; k < STEPS; k++) hits += ph_step(&w, &p, (float)PH_STEP / SR, h);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    for (int i = 0; i < PH_PARTS; i++) live += w.p[i].live;
    us = ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / 1e3 / STEPS * (128.0 / PH_STEP);
    printf("bench: particles, the full pool colliding (%d live): %6.3f us per 128-frame block (%ld hits)\n", live, us, hits);
    enum { BLOCKS = 20000 };
    int16_t buf[256];
    double eng[4];
    for (int k = 0; k < 4; k++) {   /* a held 4-note chord; then the same with 8 balls and the full pool playing too;
                                     * (2026-10-30) then with a silent floor, an open left wall and three slots driven
                                     * by the generators' sources (BALL HIT, BALL 1 HEIGHT, PARTICLE COUNT); then the
                                     * same three slots from LFO 1, Mod Env and Velocity (what any three slots cost) */
        void *e = patch(SAW, 5, 300, 70, 250);
        set(e, "voices", 7);
        for (int n = 0; n < 4; n++) note(e, 1, 48 + 3 * n, 100);
        if (k) setkv(e, "phys_balls_on=1,phys_balls=8,phys_parts_on=1,phys_rate=100,phys_life=100,phys_trigger=1,"
                        "phys_attract=50,phys_max=8,phys_len=20");
        if (k >= 2) {
            char kv[256];
            snprintf(kv, sizeof kv, "phys_floor=1,phys_left=2,mod2_src=%d,mod2_dst=%d,mod2_amt=40,mod3_src=%d,mod3_dst=%d,"
                     "mod3_amt=20,mod4_src=%d,mod4_dst=%d,mod4_amt=30", k == 2 ? MS_BALL_HIT : MS_LFO1, MD_LAD_CUT,
                     k == 2 ? MS_BALL_Y : MS_MENV, MD_SVF_CUT, k == 2 ? MS_PART_COUNT : MS_VEL, MD_AMP);
            setkv(e, kv);
        }
        for (int b = 0; b < 2000; b++) E->render(e, buf, 128);
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int b = 0; b < BLOCKS; b++) E->render(e, buf, 128);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        eng[k] = ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / 1e3 / BLOCKS;
        E->destroy(e);
    }
    printf("bench: engine, 4 held voices %6.2f us; with 8 balls and 32 particles playing (up to 8 notes/s into the 8 "
           "voices) %6.2f us per 128-frame block\n", eng[0], eng[1]);
    printf("bench: engine, the same with a silent floor, an open wall and 3 matrix slots from the generators' sources "
           "%6.2f us (the same 3 slots from LFO 1, Mod Env, Velocity: %6.2f us) per 128-frame block\n", eng[2], eng[3]);
}
