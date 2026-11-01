/* (2026-11-02) each generator's own KEY and SCALE (<generator>_own_key, _own_scale; engine.c gen_key_in, gen_scale_in):
 *   - Follow (the default) plays in the Synth's main KEY and SCALE;
 *   - a custom pair: that generator's notes are in it while the main ones differ, and exactly the notes it would play
 *     had the main ones been set to it;
 *   - one generator's custom pair leaves the others alone;
 *   - FOLLOW KEYS on: a key played still gives the root (the custom SCALE stays); off: the custom KEY stays;
 *   - TURING SCALE other than Held Notes still wins over the SCALE, its root the own KEY;
 *   - RANDOMISE and MUTATE never touch them; each Synth has its own.
 * Included from offline_check.c after gen_keys_check.c (its helpers: pg_run, pg_patch, pg_in_key, pg_same, gk_pcs_ok; set,
 * setkv, note, check, get). */

#define OK_RUN_S 10.0
#define OK_ROOT 6                /* the custom KEY: F# */
#define OK_SCALE KS_PENT_MAJOR   /* the custom SCALE (F#, G#, A#, C#, D#: the main D minor shares only A# / Bb) */

static int ok_play(const char *on, const char *kv1, const char *kv2) {   /* a fresh patch with these, its note-ons in pg_b */
    void *a = pg_patch();
    setkv(a, kv1);
    if (kv2 && *kv2) setkv(a, kv2);
    setkv(a, on);
    int n = 0;
    pg_run(a, OK_RUN_S, "gen_log", pg_b, &n);
    E->destroy(a);
    return n;
}

static void own_keys_checks(void) {
    char what[400], k1[96], k2[96];
    static const struct { const char *name, *pre, *on; } G[] = {
        {"Euclid", "gen_own", "gen_euclid_on=1,gen_keys=0,gen_eu_notes=1,gen_range=1"},
        {"Turing", "gen_own", "gen_tm_on=1,gen_keys=0,gen_tm_change=50,gen_range=1"},
        {"Balls", "phys_ball_own", "phys_balls_on=1,phys_keys=0,phys_max=30,phys_range=2"},
        {"Particles", "phys_part_own", "phys_parts_on=1,phys_keys=0,phys_max=30,phys_range=2"},
        {"Rain", "rain_own", "rain_on=1,rain_keys=0,rain_rate=80,rain_max=30,rain_range=2"},
        {"Markov", "mkv_own", "mkv_on=1,mkv_keys=0,mkv_creat=100,mkv_max=30"},
        {"Life", "life_own", "life_on=1,life_keys=0,life_range=2"},
        {"Orbits", "orb_own", "orb_on=1,orb_keys=0,orb_range=2"},
        {"Pendulums", "pend_own", "pend_on=1,pend_keys=0,pend_cycle=0,pend_count=12"}};
    enum { NG = sizeof G / sizeof G[0] };
    for (int g = 0; g < NG; g++) {
        int na, nb, nf, nm;
        /* custom on this generator, the main ones another key */
        snprintf(k1, sizeof k1, "key_root=2,key_scale=%d,%s_key=%d,%s_scale=%d", KS_MINOR, G[g].pre, OK_ROOT + 1, G[g].pre, OK_SCALE + 1);
        na = ok_play(G[g].on, k1, NULL);
        memcpy(pg_a, pg_b, sizeof(pg_ev_t) * (size_t)na);
        int in_own = na > 2 && pg_in_key(pg_a, na, OK_ROOT, OK_SCALE), in_main = pg_in_key(pg_a, na, 2, KS_MINOR);
        /* the same notes as the main ones set to it, the generator on Follow */
        snprintf(k2, sizeof k2, "key_root=%d,key_scale=%d", OK_ROOT, OK_SCALE);
        nm = ok_play(G[g].on, k2, NULL);
        int same_as_main = pg_same(pg_a, na, pg_b, nm, 0);
        /* Follow: in the main ones */
        snprintf(k1, sizeof k1, "key_root=2,key_scale=%d", KS_MINOR);
        nf = ok_play(G[g].on, k1, NULL);
        memcpy(pg_a, pg_b, sizeof(pg_ev_t) * (size_t)nf);
        int follow = nf > 2 && pg_in_key(pg_a, nf, 2, KS_MINOR);
        /* the next different generator's custom pair changes nothing here */
        int o = (g + 1) % NG;
        while (!strcmp(G[o].pre, G[g].pre)) o = (o + 1) % NG;
        snprintf(k2, sizeof k2, "%s_key=%d,%s_scale=%d", G[o].pre, OK_ROOT + 1, G[o].pre, OK_SCALE + 1);
        nb = ok_play(G[g].on, k1, k2);
        int alone = pg_same(pg_a, nf, pg_b, nb, 0);
        snprintf(what, sizeof what, "%s's own KEY and SCALE: main D minor, own F# pent major: %d notes, all in the own one (%d), "
                 "not all in the main one (%d), the very notes of the main ones set to it (%d); Follow: %d notes in D minor (%d); "
                 "%s's own pair leaves it alone (%d)", G[g].name, na, in_own, in_main, same_as_main, nf, follow, G[o].name, alone);
        check(in_own && !in_main && same_as_main && follow && alone, what);
    }
    /* FOLLOW KEYS: E held, own F# major, main D minor. On: E major (the key played the root, the own SCALE); off: F# major */
    {
        unsigned e_maj = 0, fs_maj = 0;
        for (int k = 0; k < KS_SIZE[KS_MAJOR]; k++) {
            e_maj |= 1u << ((4 + KS_STEPS[KS_MAJOR][k]) % 12);
            fs_maj |= 1u << ((OK_ROOT + KS_STEPS[KS_MAJOR][k]) % 12);
        }
        static const struct { const char *pre, *on; } H[] = {
            {"phys_ball_own", "phys_keys=1,phys_balls_on=1,phys_max=30,phys_range=2"},
            {"rain_own", "rain_keys=1,rain_on=1,rain_rate=80,rain_max=30,rain_range=2"},
            {"mkv_own", "mkv_keys=1,mkv_on=1,mkv_creat=100,mkv_max=30"},
            {"life_own", "life_keys=1,life_on=1,life_range=2"},
            {"orb_own", "orb_keys=1,orb_on=1,orb_range=2"},
            {"pend_own", "pend_keys=1,pend_on=1,pend_cycle=0,pend_count=12"}};
        for (unsigned h = 0; h < sizeof H / sizeof H[0]; h++) {
            int got[2], ok[2];
            for (int f = 0; f < 2; f++) {
                void *a = pg_patch();
                snprintf(k1, sizeof k1, "key_root=2,key_scale=%d,%s_key=%d,%s_scale=%d", KS_MINOR, H[h].pre, OK_ROOT + 1, H[h].pre,
                         KS_MAJOR + 1);
                setkv(a, k1);
                setkv(a, H[h].on);
                set(a, "gen_follow_keys", (float)f);
                note(a, 1, 52, 100);
                int n = 0;
                pg_run(a, 8.0, "gen_log", pg_a, &n);
                E->destroy(a);
                got[f] = n;
                ok[f] = n > 2 && gk_pcs_ok(pg_a, n, f ? e_maj : fs_maj);
            }
            snprintf(what, sizeof what, "own KEY with FOLLOW KEYS (%s): E held, own F# major, main D minor: on, %d notes in E major "
                     "(%d); off, %d notes in F# major (%d)", H[h].on, got[1], ok[1], got[0], ok[0]);
            check(ok[0] && ok[1], what);
        }
    }
    /* TURING SCALE (not Held Notes) still wins over the SCALE; the own KEY gives its root (index 3: Minor; seq.h SCALE) */
    {
        snprintf(k1, sizeof k1, "key_root=2,key_scale=%d,gen_own_key=%d,gen_own_scale=%d", KS_DORIAN, OK_ROOT + 1, KS_MAJOR + 1);
        int n = ok_play("gen_tm_on=1,gen_keys=0,gen_tm_change=50,gen_range=1,gen_scale=3", k1, NULL);
        snprintf(what, sizeof what, "TURING SCALE Minor, own F# major: %d notes in F# minor (%d)", n, pg_in_key(pg_b, n, OK_ROOT, KS_MINOR));
        check(n > 2 && pg_in_key(pg_b, n, OK_ROOT, KS_MINOR), what);
    }
    /* each Synth its own; Follow for a new sound; RANDOMISE and MUTATE never touch them */
    {
        void *e = E->create(NULL);
        static const char *const K[] = {"gen_own", "phys_ball_own", "phys_part_own", "rain_own", "mkv_own", "life_own", "orb_own", "pend_own"};
        int fresh = 1, never = 1;
        for (unsigned i = 0; i < sizeof K / sizeof K[0]; i++) {
            static const char *const PRE[3] = {"", "s2_", "s3_"}, *const END[2] = {"_key", "_scale"};
            for (int c = 0; c < 3; c++)
                for (int t = 0; t < 2; t++) {
                    snprintf(k1, sizeof k1, "%s%s%s", PRE[c], K[i], END[t]);
                    fresh &= get(e, k1) == 0;
                    never &= rnd_section_of(pindex(k1)) == RS_NEVER;
                }
        }
        set(e, "s2_orb_own_key", 3);
        snprintf(what, sizeof what, "own KEY and SCALE: Follow for a new sound (%d), RANDOMISE and MUTATE never touch them (%d), "
                 "each Synth its own (%.0f %.0f)", fresh, never, get(e, "orb_own_key"), get(e, "s2_orb_own_key"));
        check(fresh && never && get(e, "orb_own_key") == 0 && get(e, "s2_orb_own_key") == 3, what);
        E->destroy(e);
    }
}
