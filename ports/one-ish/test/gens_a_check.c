/* GAME OF LIFE, GRAVITY ORBITS, PENDULUMS (src/gens_a.h, src/gens_a_run.h; 2026-10-30): off is silent and changes
 * nothing, the same settings give the same notes, every note is in the Synth's key and scale, MAX NOTES/S holds, the
 * defaults are moderate, each Synth runs its own, HELD KEYS, RESTART ON KEY, the tempo grid, the views, the matrix
 * sources, and a saved sound keeps the settings but not the positions. Included from offline_check.c after
 * physgen_check.c (its helpers: pg_run, pg_same, pg_in_key, pg_patch; and set, setkv, note, check). */
#include "../src/gens_a.h"

static const char *const GA_ON[3] = {"life_on=1", "orb_on=1", "pend_on=1"};
static const char *const GA_NAME[3] = {"LIFE", "ORBITS", "PENDULUMS"};
static const char *const GA_VIEW[3] = {"life_view", "orbit_view", "pend_view"};

static int ga_worst_second(const pg_ev_t *ev, int n) {   /* the most notes in any one second */
    int worst = 0;
    for (int i = 0; i < n; i++) {
        int c = 0;
        for (int j = i; j < n && ev[j].t - ev[i].t < (long long)SR; j++) c++;
        if (c > worst) worst = c;
    }
    return worst;
}
static int ga_opt(const char *key, const char *name) {
    int i = pindex(key);
    for (int k = 0; i >= 0 && k < PARAMS[i].nopts; k++)
        if (!strcmp(PARAMS[i].opts[k], name)) return k;
    return -1;
}

static void gens_a_checks(void) {
    char what[320];
    int16_t ba[256], bb[256];
    /* off: the very same samples as a fresh instance, with every setting moved; no notes */
    {
        void *a = pg_patch(), *b = pg_patch();
        setkv(b, "life_rate=6,life_mode=0,life_rule=2,life_density=70,life_seed=9,life_notes=8,life_max=30,life_keys=0,"
                 "orb_planets=6,orb_speed=100,orb_gravity=100,orb_ecc=80,orb_spokes=8,orb_quant=0,orb_max=30,"
                 "pend_count=12,pend_speed=32,pend_cycle=0,pend_trigger=2,pend_restart=1,pend_max=30");
        note(a, 1, 60, 100); note(b, 1, 60, 100);
        int same = 1, nb = 0;
        for (int k = 0; k < 800; k++) {
            E->render(a, ba, 128); E->render(b, bb, 128);
            same &= !memcmp(ba, bb, sizeof ba);
        }
        pg_run(b, 0.1, "gen_log", pg_b, &nb);
        check(same && nb == 0, "LIFE / ORBITS / PENDULUMS off: the very same samples as before with every setting moved, "
              "no notes");
        E->destroy(a); E->destroy(b);
    }
    /* the same settings: the same notes; every note in the key, over every way of playing */
    static const char *const MODES[] = {
        "life_on=1,life_mode=0", "life_on=1,life_mode=1", "life_on=1,life_mode=2,life_rule=1",
        "life_on=1,life_mode=0,life_rule=2,life_wrap=0", "life_on=1,life_rate=6,life_speed=70,life_notes=4",
        "orb_on=1", "orb_on=1,orb_planets=6,orb_gravity=100,orb_ecc=80,orb_quant=0,orb_planet_to=1",
        "pend_on=1", "pend_on=1,pend_trigger=1,pend_count=8,pend_damp=60", "pend_on=1,pend_trigger=2,pend_cycle=2"};
    for (unsigned m = 0; m < sizeof MODES / sizeof MODES[0]; m++) {
        void *a = pg_patch(), *b = pg_patch();
        setkv(a, "key_root=2,key_scale=10"); setkv(b, "key_root=2,key_scale=10");   /* D pentatonic minor */
        setkv(a, MODES[m]); setkv(b, MODES[m]);
        int na = 0, nb = 0;
        pg_run(a, 12.0, "gen_log", pg_a, &na);
        pg_run(b, 12.0, "gen_log", pg_b, &nb);
        snprintf(what, sizeof what, "%s: %d notes in 12 s, the same again from the same start, all in D pentatonic minor",
                 MODES[m], na);
        check(na > 3 && pg_same(pg_a, na, pg_b, nb, 0) && pg_in_key(pg_a, na, 2, KS_PENT_MINOR), what);
        E->destroy(a); E->destroy(b);
    }
    /* musical by default: each at its defaults plays a few notes a second, not a flood; off again, no more */
    for (int g = 0; g < 3; g++) {
        void *a = pg_patch();
        setkv(a, GA_ON[g]);
        int na = 0;
        pg_run(a, 30.0, "gen_log", pg_a, &na);
        int worst = ga_worst_second(pg_a, na);
        snprintf(what, sizeof what, "%s at its defaults: %.2f notes a second (want 0.5 .. 6), at most %d in a second",
                 GA_NAME[g], na / 30.0, worst);
        check(na >= 15 && na <= 180 && worst <= 10, what);
        char off[16];
        snprintf(off, sizeof off, "%.*s0", (int)strlen(GA_ON[g]) - 1, GA_ON[g]);
        setkv(a, off);
        int16_t b[256];
        E->render(a, b, 128);
        na = 0;
        pg_run(a, 3.0, "gen_log", pg_a, &na);
        snprintf(what, sizeof what, "... %s switched off, no more notes (%d)", GA_NAME[g], na);
        check(na == 0, what);
        E->destroy(a);
    }
    /* MAX NOTES/S: each turned up to its busiest plays no more than it allows */
    static const char *const BUSY[3] = {
        "life_on=1,life_rate=3,life_mode=0,life_rule=2,life_notes=8,life_density=50,life_max=5",
        "orb_on=1,orb_planets=6,orb_speed=100,orb_spokes=8,orb_quant=0,orb_ecc=60,orb_max=5",
        "pend_on=1,pend_count=12,pend_speed=32,pend_spread=4,pend_cycle=0,pend_trigger=2,pend_max=5"};
    for (int g = 0; g < 3; g++) {
        void *a = pg_patch();
        setkv(a, BUSY[g]);
        int na = 0;
        pg_run(a, 10.0, "gen_log", pg_a, &na);
        int worst = ga_worst_second(pg_a, na);
        snprintf(what, sizeof what, "%s at its busiest with MAX NOTES/S 5: %d notes in 10 s, at most %d in any second "
                 "(want <= 7)", GA_NAME[g], na, worst);
        check(na >= 40 && na <= 52 && worst <= 7, what);
        E->destroy(a);
    }
    /* each Synth its own: Synth 2 plays, Synth 1 doesn't; both on with the same settings play different notes */
    for (int g = 0; g < 3; g++) {
        static char t[8192];
        int16_t b[256];
        for (int both = 0; both < 2; both++) {
            void *a = pg_patch();
            char kv[96];
            /* (the pendulums have no chance in them: the same settings swing the same, so Synth 2 gets 9 of them) */
            snprintf(kv, sizeof kv, "s2_syn_on=1,s2_%s%s%s%s", GA_ON[g], both ? "," : "", both ? GA_ON[g] : "",
                     both && g == 2 ? ",s2_pend_count=9" : "");
            setkv(a, kv);
            int na = 0, nb = 0;
            for (int k = 0; k < (int)(8.0 * SR / 128); k++) {
                E->render(a, b, 128);
                for (int c = 0; c < 2; c++) {
                    if (E->get_param(a, c ? "s2_gen_log" : "gen_log", t, sizeof t) <= 0) continue;
                    for (char *p = t; *p;) {
                        long long tt; int nt, v, used;
                        if (sscanf(p, "%lld:%d:%d;%n", &tt, &nt, &v, &used) != 3) break;
                        if (v > 0) {
                            if (c && nb < PG_MAX) pg_b[nb++] = (pg_ev_t){tt, nt, v};
                            else if (!c && na < PG_MAX) pg_a[na++] = (pg_ev_t){tt, nt, v};
                        }
                        p += used;
                    }
                }
            }
            char v3[64];
            snprintf(v3, sizeof v3, "s3_%s", GA_VIEW[g]);
            E->get_param(a, v3, t, sizeof t);
            int n3 = atoi(t + (g == 0 ? 2 : 0));   /* (life: "<mode> <head> <pop>": the head; 0 while off) */
            if (!both) snprintf(what, sizeof what, "%s: Synth 2's plays (%d notes), Synth 1's doesn't (%d), Synth 3's view "
                                "is empty (%d)", GA_NAME[g], nb, na, n3);
            else snprintf(what, sizeof what, "%s: Synths 1 and 2 both on%s each play their own (%d and %d notes, "
                          "different)", GA_NAME[g], g == 2 ? " (5 and 9 pendulums)" : " with the same settings", na, nb);
            check(both ? na > 3 && nb > 3 && !pg_same(pg_a, na, pg_b, nb, 0) : nb > 3 && na == 0 && n3 == 0, what);
            E->destroy(a);
        }
    }
    /* HELD KEYS: nothing without a key; from the lowest key held, in the key; nothing after it lets go */
    static const char *const HELD[3] = {"life_on=1,life_keys=1,life_rate=2", "orb_on=1,orb_keys=1,orb_planets=5",
                                        "pend_on=1,pend_keys=1,pend_cycle=1"};
    for (int g = 0; g < 3; g++) {
        void *a = pg_patch();
        setkv(a, HELD[g]);
        setkv(a, "key_root=2,key_scale=1");
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
        snprintf(what, sizeof what, "%s HELD KEYS: %d notes with no key, %d holding D3 + A3 (lowest %d, want >= 50, in D "
                 "minor), %d after letting go", GA_NAME[g], before, na, low, nb);
        check(before == 0 && na > 3 && low >= 50 && pg_in_key(pg_a, na, 2, KS_MINOR) && nb == 0, what);
        E->destroy(a);
    }
    /* RESTART ON KEY: a key after 3 s plays what a key at the start plays, from the key on */
    {
        void *a = pg_patch(), *b = pg_patch();
        setkv(a, "pend_on=1,pend_restart=1,pend_cycle=1,pend_max=20"); setkv(b, "pend_on=1,pend_restart=1,pend_cycle=1,pend_max=20");
        int na = 0, nb = 0;
        note(b, 1, 64, 100);
        pg_run(a, 3.0, "gen_log", pg_a, &na);
        note(a, 1, 64, 100);
        na = 0;
        long long shift = (long long)((3.0 * SR + 127) / 128) * 128;
        pg_run(a, 5.0, "gen_log", pg_a, &na);
        pg_run(b, 5.0, "gen_log", pg_b, &nb);
        int near = na == nb && na > 3;
        for (int i = 0; near && i < na; i++)   /* (within a few samples: the key's beat is worked out at its block) */
            near &= llabs(pg_a[i].t - shift - pg_b[i].t) <= 2 && pg_a[i].note == pg_b[i].note && abs(pg_a[i].vel - pg_b[i].vel) <= 1;
        snprintf(what, sizeof what, "PENDULUMS RESTART ON KEY: a key after 3 s gives the same %d notes as a key at the "
                 "start (%d)", na, nb);
        check(near, what);
        E->destroy(a); E->destroy(b);
    }
    /* the tempo grid at 120 BPM: LIFE at 1/16 (scanning) and ORBITS synced to 1/16 put every note on a 16th; the
     * pendulums' first one, at 4 swings an 8-bar cycle, passes the centre every 4 beats */
    {
        static const char *const G[3] = {"life_on=1,life_rate=2,life_mode=1", "orb_on=1,orb_quant=3,orb_planets=6",
                                         "pend_on=1,pend_count=2,pend_spread=8"};
        for (int g = 0; g < 3; g++) {
            void *a = pg_patch();
            setkv(a, G[g]);
            int na = 0, off = 0, on_beat = 0;
            pg_run(a, 16.0, "gen_log", pg_a, &na);
            for (int i = 0; i < na; i++) {
                double q = pg_a[i].t / 5512.5;
                if (fabs(q - floor(q + 0.5)) * 5512.5 > 1.0) off++;
                double bt = pg_a[i].t / 22050.0;   /* beats */
                on_beat += fabs(bt - 2.0 - 4.0 * floor((bt - 2.0) / 4.0 + 0.5)) * 22050.0 <= 2.0;
            }
            if (g < 2) {
                snprintf(what, sizeof what, "%s on the 1/16 grid at 120 BPM: %d notes, %d off it", GA_NAME[g], na, off);
                check(na > 5 && off == 0, what);
            } else {   /* the first passes the centre at beats 2, 6, 10 ...; the second, 12 swings, three times as often */
                snprintf(what, sizeof what, "PENDULUMS at 120 BPM: %d notes in 16 s (want 32: 8 + 24), %d of them at beats "
                         "2, 6, 10 ... (want >= 8)", na, on_beat);
                check(na == 32 && on_beat >= 8, what);
            }
            E->destroy(a);
        }
    }
    /* RESEED (a plain button): a new grid each press, the button back off at once and the host told; the same presses
     * give the same grids */
    {
        void *a = pg_patch(), *b = pg_patch(), *c = pg_patch();
        setkv(a, "life_on=1"); setkv(b, "life_on=1"); setkv(c, "life_on=1");
        int na = 0, nb = 0;
        pg_run(a, 1.0, "gen_log", pg_a, &na);
        pg_run(b, 1.0, "gen_log", pg_b, &nb);
        set(b, "life_reseed", 1);
        set(c, "life_reseed", 1);   /* (c: pressed at the start, then a second time like b) */
        double back = get(b, "life_reseed");
        char key[64] = "";
        int told = 0;
        while (mpc_engine_changed(b, key, sizeof key)) told |= !strcmp(key, "life_reseed");
        static char v1[256], v2[256];
        for (int k = 0; k < 4; k++) { E->render(a, ba, 128); E->render(b, bb, 128); }   /* (the views written again) */
        E->get_param(a, "life_view", v1, sizeof v1);
        E->get_param(b, "life_view", v2, sizeof v2);
        int differs = strcmp(strchr(v1, ';'), strchr(v2, ';')) != 0;   /* (the grids, past the playhead) */
        na = nb = 0;
        pg_run(a, 6.0, "gen_log", pg_a, &na);
        pg_run(b, 6.0, "gen_log", pg_b, &nb);
        int moved = !pg_same(pg_a, na, pg_b, nb, 0);
        void *d = pg_patch();
        setkv(d, "life_on=1");
        set(d, "life_reseed", 1);
        int n1 = 0, n2 = 0;
        pg_run(c, 3.0, "gen_log", pg_a, &n1);
        pg_run(d, 3.0, "gen_log", pg_b, &n2);
        snprintf(what, sizeof what, "LIFE RESEED: back off at once (%g), the host told (%d), a new grid (%d) that plays "
                 "other notes (%d), the same press the same grid (%d)", back, told, differs, moved,
                 pg_same(pg_a, n1, pg_b, n2, 0));
        check(back == 0.0 && told && differs && moved && n1 > 3 && pg_same(pg_a, n1, pg_b, n2, 0), what);
        E->destroy(a); E->destroy(b); E->destroy(c); E->destroy(d);
    }
    /* the views: in range; a saved sound keeps the settings, not the positions */
    {
        void *a = pg_patch(), *b = pg_patch(), *c = pg_patch();
        const char *all = "life_on=1,orb_on=1,pend_on=1,orb_planets=5,pend_count=7";
        setkv(a, all); setkv(c, all);
        int na = 0;
        pg_run(a, 4.0, "gen_log", pg_a, &na);
        static char v[2048], st1[65536], st2[65536];
        int ok[3] = {0, 0, 0};
        E->get_param(a, "life_view", v, sizeof v);
        {
            int mode = -1, head = -1, pop = -1, used = 0, rows = 0, live = 0;
            sscanf(v, "%d %d %d;%n", &mode, &head, &pop, &used);
            for (char *p = v + used; *p; rows++) {
                unsigned x; int u;
                if (sscanf(p, "%x,%n", &x, &u) != 1) break;
                if (rows < LF_ROWS) live += __builtin_popcount(x);
                p += u;
            }
            ok[0] = mode == 1 && head >= 0 && head < LF_COLS && rows == 2 * LF_ROWS && live == pop && pop > 0;
        }
        E->get_param(a, "orbit_view", v, sizeof v);
        {
            int np = -1, sp = -1, lit = -1, used = 0, cnt = 0, in = 1;
            sscanf(v, "%d %d %d;%n", &np, &sp, &lit, &used);
            for (char *p = v + used; *p; cnt++) {
                int x, y, f, u;
                if (sscanf(p, "%d,%d,%d;%n", &x, &y, &f, &u) != 3) break;
                in &= abs(x) <= 1000 && abs(y) <= 1000 && x * x + y * y > 100 * 100;
                p += u;
            }
            ok[1] = np == 5 && sp == 4 && cnt == 5 && in;
        }
        E->get_param(a, "pend_view", v, sizeof v);
        {
            int np = -1, used = 0, cnt = 0, in = 1;
            sscanf(v, "%d;%n", &np, &used);
            for (char *p = v + used; *p; cnt++) {
                int x, f, u;
                if (sscanf(p, "%d,%d;%n", &x, &f, &u) != 2) break;
                in &= abs(x) <= 1000 && (f == 0 || f == 1);
                p += u;
            }
            ok[2] = np == 7 && cnt == 7 && in;
        }
        snprintf(what, sizeof what, "the views: the grid (%d), the planets round the sun (%d), the pendulums (%d)",
                 ok[0], ok[1], ok[2]);
        check(ok[0] && ok[1] && ok[2], what);
        E->get_param(a, "state", st1, sizeof st1);
        E->set_param(b, "state", st1);
        E->get_param(b, "state", st2, sizeof st2);
        int n1 = 0, n2 = 0;
        pg_run(b, 4.0, "gen_log", pg_b, &n1);
        pg_run(c, 4.0, "gen_log", pg_a, &n2);
        check(!strcmp(st1, st2) && !strstr(st1, "_view") && get(b, "orb_planets") == 5 && n1 > 3 &&
              pg_same(pg_b, n1, pg_a, n2, 0), "a saved sound keeps the settings (the same state back) and starts LIFE, "
              "the orbits and the pendulums from their start, not where they were");
        E->destroy(a); E->destroy(b); E->destroy(c);
    }
    /* the matrix sources: each moves the sound (a held note's cutoff), and the sound stays finite and heard */
    {
        static const char *const SRC[5] = {"Life Population", "Orbit 1 X", "Orbit 1 Y", "Pendulum 1", "Pendulum Sync"};
        int moved = 0, heard = 1;
        for (int k = 0; k < 5; k++) {
            double e2[2];
            for (int on = 0; on < 2; on++) {
                void *a = pg_patch();
                setkv(a, "life_on=1,orb_on=1,pend_on=1,life_keys=1,orb_keys=1,pend_keys=1,flt_cut=40,pend_cycle=0,"
                         "pend_speed=3");
                char kv[96];
                snprintf(kv, sizeof kv, "mod2_src=%d,mod2_dst=%d,mod2_amt=%d", ga_opt("mod2_src", SRC[k]),
                         ga_opt("mod2_dst", "Ladder Cutoff"), on ? 100 : 0);
                setkv(a, kv);
                set(a, "amp_s", 100);
                note(a, 1, 36, 100);   /* (HELD KEYS: the three generators play from it) */
                int16_t b[256];
                double s2 = 0.0;
                for (int i = 0; i < (int)(6.0 * SR / 128); i++) {
                    E->render(a, b, 128);
                    for (int j = 0; j < 256; j++) s2 += (double)b[j] * b[j];
                }
                e2[on] = s2;
                E->destroy(a);
            }
            moved += fabs(e2[1] - e2[0]) > 1e-3 * e2[0];
            heard &= e2[0] > 0.0 && e2[1] > 0.0 && isfinite(e2[1]);
        }
        snprintf(what, sizeof what, "the matrix sources LIFE POPULATION, ORBIT 1 X / Y, PENDULUM 1, PENDULUM SYNC: %d of 5 "
                 "move a cutoff, all heard and finite", moved);
        check(moved == 5 && heard, what);
    }
}

/* the CPU: each world alone (its busiest), and the engine with all three playing */
static double ga_us(struct timespec t0, struct timespec t1, int steps) {
    return ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / 1e3 / steps * (128.0 / GA_STEP);
}
static void gens_a_bench(void) {
    enum { STEPS = 400000 };
    struct timespec t0, t1;
    volatile long sink = 0;
    lf_world_t lf;
    lf_seed(&lf, 7, 0.4f);
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int k = 0; k < STEPS / 4; k++) { lf_step(&lf, LFR_HIGHLIFE, 1); if (!lf_pop(&lf)) lf_fill(&lf, 0.4f); sink += lf.loops; }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double one = ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / 1e3 / (STEPS / 4);
    printf("bench: game of life, one generation of the 16 x 8 grid: %6.3f us (at 1/32 and 300 BPM, 20 a second: "
           "%6.4f us per 128-frame block on average)\n", one, one * 20.0 * 128.0 / SR);
    ob_world_t ob;
    ob_cfg_t oc = {6, 8, 0.03f, 0.6f, 1.0f};
    ob_hit_t oh[OB_HITS];
    ob_restart(&ob, &oc, 3);
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int k = 0; k < STEPS; k++) sink += ob_step(&ob, &oc, 1.04f * GA_STEP / SR, oh);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    printf("bench: gravity orbits, 6 planets pulling on each other, 8 spokes (the world alone): %6.3f us per 128-frame "
           "block\n", ga_us(t0, t1, STEPS));
    pd_hit_t ph[8];
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int k = 0; k < STEPS; k++)
        for (int i = 0; i < PD_MAX; i++) {
            double c0 = k * 0.0003, c1 = c0 + 0.0003;
            sink += pd_hits(i, pd_half(i, 32, 8, c0), pd_half(i, 32, 8, c1), PDT_BOTH, ph, 0, 8);
            sink += (long)(cosf((float)fmod(pd_half(i, 32, 8, c1), 2.0) * 3.1415927f) * 10.0f);
        }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    printf("bench: pendulums, 12 swinging (the world alone): %6.3f us per 128-frame block (%ld)\n", ga_us(t0, t1, STEPS),
           (long)sink % 10);
    enum { BLOCKS = 20000 };
    int16_t buf[256];
    static const char *const ON[5] = {NULL, "life_on=1,life_rate=3,life_mode=0,life_rule=1,life_notes=8,life_max=8,life_len=20",
                                      "orb_on=1,orb_planets=6,orb_gravity=100,orb_spokes=8,orb_max=8,orb_len=20",
                                      "pend_on=1,pend_count=12,pend_speed=32,pend_trigger=2,pend_max=8,pend_len=20",
                                      "life_on=1,life_rate=3,life_mode=0,life_notes=8,orb_on=1,orb_planets=6,orb_gravity=100,"
                                      "pend_on=1,pend_count=12,pend_speed=32,life_len=20,orb_len=20,pend_len=20"};
    double eng[5];
    for (int k = 0; k < 5; k++) {   /* a held 4-note chord; then the same with each generator, then all three, playing */
        void *e = patch(SAW, 5, 300, 70, 250);
        set(e, "voices", 7);
        for (int n = 0; n < 4; n++) note(e, 1, 48 + 3 * n, 100);
        if (ON[k]) setkv(e, ON[k]);
        for (int b = 0; b < 2000; b++) E->render(e, buf, 128);
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int b = 0; b < BLOCKS; b++) E->render(e, buf, 128);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        eng[k] = ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / 1e3 / BLOCKS;
        E->destroy(e);
    }
    printf("bench: engine, 4 held voices %6.2f us; with LIFE playing %6.2f, ORBITS %6.2f, PENDULUMS %6.2f, all three "
           "%6.2f us per 128-frame block (each up to 8 notes/s into the voices)\n", eng[0], eng[1], eng[2], eng[3], eng[4]);
}
