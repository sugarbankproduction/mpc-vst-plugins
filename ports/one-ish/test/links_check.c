/* (2026-10-31) the LINKs of the hands' controllers (included by offline_check.c): with Per Synth MIDI channels the mod
 * wheel (CC 1), the pitch wheel (pitch bend) and the ribbon (CC 16, CC 17) on channel 1 reach Synth 1 only; with its
 * LINK lit (link_mod, link_bend, link_ribbon), all three Synths. Each LINK moves only its own controller.
 * Read back with the "hands_now" readout (wheel, bend, ribbon target, ribbon touch). */
static void links_checks(void) {
    char what[600];
    static const char *const LINK[3] = {"link_mod", "link_bend", "link_ribbon"};
    for (int which = 0; which < 3; which++)
        for (int link = 0; link < 2; link++) {
            void *e = E->create(NULL);
            set(e, "midi_ch", 17); set(e, "s2_syn_on", 1); set(e, "s3_syn_on", 1);
            set(e, LINK[which], link);
            char before[3][64], after[3][64];
            static const char *const RD[3] = {"hands_now", "s2_hands_now", "s3_hands_now"};
            for (int c = 0; c < 3; c++) E->get_param(e, RD[c], before[c], sizeof before[c]);
            /* every controller once, on channel 1: only the linked one may reach Synths 2 and 3 */
            uint8_t m[4][3] = {{0xB0, 1, 100}, {0xE0, 0, 100}, {0xB0, 16, 20}, {0xB0, 17, 127}};
            for (int k = 0; k < 4; k++) E->midi(e, m[k], 3);
            int16_t buf[256];
            E->render(e, buf, 128);
            for (int c = 0; c < 3; c++) E->get_param(e, RD[c], after[c], sizeof after[c]);
            float v[3][4];
            for (int c = 0; c < 3; c++) sscanf(after[c], "%f %f %f %f", &v[c][0], &v[c][1], &v[c][2], &v[c][3]);
            int s1 = fabsf(v[0][0] - 100 / 127.0f) < 1e-3f && v[0][1] > 0.5f && fabsf(v[0][2] - 20) < 1e-3f && v[0][3] > 0.5f;
            int ok = s1;
            for (int c = 1; c < 3; c++) {
                float b[4];
                sscanf(before[c], "%f %f %f %f", &b[0], &b[1], &b[2], &b[3]);
                int mod = fabsf(v[c][0] - 100 / 127.0f) < 1e-3f, bend = v[c][1] > 0.5f,
                    rib = fabsf(v[c][2] - 20) < 1e-3f && v[c][3] > 0.5f;
                int mod0 = v[c][0] == b[0], bend0 = v[c][1] == b[1], rib0 = v[c][2] == b[2] && v[c][3] == b[3];
                int want_mod = link && which == 0, want_bend = link && which == 1, want_rib = link && which == 2;
                ok &= (want_mod ? mod : mod0) && (want_bend ? bend : bend0) && (want_rib ? rib : rib0);
            }
            snprintf(what, sizeof what, "%s %s, Per Synth channels, every controller on channel 1: Synth 1 \"%s\", "
                     "Synth 2 \"%s\" (was \"%s\"), Synth 3 \"%s\" (was \"%s\"): %s", LINK[which], link ? "on" : "off",
                     after[0], after[1], before[1], after[2], before[2],
                     link ? "its controller (only) reaches all three" : "Synth 1 alone");
            check(ok, what);
            E->destroy(e);
        }
}
