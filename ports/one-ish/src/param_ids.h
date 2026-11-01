/* ONE-ish's parameters: the engine's own numbers for them (P_*) and each one's key as params.json names it (KEYS). The
 * place every new param starts. Part of engine.c (included there, one unit with it). */
enum {
    P_O1_WAVE, P_O1_OCT, P_O1_FREQ, P_O1_BEAT, P_O1_ANGLE, P_O1_PW, P_O1_MIX, P_O1_LEVEL, P_O1_SYNC, P_O1_SVF, P_O1_LAD,
    P_O2_WAVE, P_O2_OCT, P_O2_FREQ, P_O2_BEAT, P_O2_ANGLE, P_O2_PW, P_O2_MIX, P_O2_LEVEL, P_O2_SYNC, P_O2_SVF, P_O2_LAD,
    P_O3_WAVE, P_O3_OCT, P_O3_FREQ, P_O3_BEAT, P_O3_ANGLE, P_O3_PW, P_O3_MIX, P_O3_LEVEL, P_O3_SYNC, P_O3_SVF, P_O3_LAD,
    P_NOISE_LEVEL, P_NOISE_SVF, P_NOISE_LAD, P_RING_LEVEL, P_RING_SVF, P_RING_LAD, P_RING_SRC, P_FM_AMT, P_FM_ROUTE,
    P_FLT_CUT, P_FLT_RES, P_FLT_DRIVE, P_FLT_MODE, P_FLT_SLOPE, P_SVF_CUT, P_SVF_RES, P_SVF_MODE, P_FLT_ROUTE, P_FLT_MIX,
    P_ENV_A, P_ENV_D, P_ENV_S, P_ENV_R,                 /* filter envelope */
    P_AMP_A, P_AMP_D, P_AMP_S, P_AMP_R,                 /* amp envelope */
    P_MENV_A, P_MENV_D, P_MENV_S, P_MENV_R,             /* mod envelope */
    P_LAD_ENV, P_LAD_LFO, P_LAD_KBD, P_SVF_ENV, P_SVF_LFO, P_SVF_KBD, P_PITCH_LFO, P_PITCH_ENV,
    P_LFO1_RATE, P_LFO1_WAVE, P_LFO1_SYNC, P_LFO2_RATE, P_LFO2_WAVE, P_LFO2_SYNC,
    P_BEND_RANGE, P_VOICES, P_MOD_BASE,                           /* then MOD_SLOTS x (source, destination, amount, via) */
    P_VOLUME = P_MOD_BASE + MOD_SLOTS * MOD_SLOT_PARAMS,
    P_MODFX_TYPE, P_MODFX_RATE, P_MODFX_DEPTH, P_MODFX_FB, P_MODFX_MIX,
    P_DLY_TIME, P_DLY_SYNC, P_DLY_FB, P_DLY_TONE, P_DLY_PP, P_MFX_SEND1,
    P_REV_DECAY, P_REV_DAMP, P_REV_PRE, P_MFX_SEND2,
    P_GLIDE, P_GLIDE_TIME, P_GLIDE_TYPE, P_GLIDE_LEGATO, P_GLIDE_GATED, P_GLISSANDO,
    P_UNISON, P_UNI_COUNT, P_UNI_DETUNE, P_PITCH_VAR,
    P_SVF_SPACE, P_SVF_PAIR, P_FLT_LINK, P_LAD_FM, P_SVF_FM, P_LAD_FM_SRC, P_SVF_FM_SRC,   /* the filters' MORE page */
    /* each envelope's extras, in ENV_FILTER, ENV_AMP, ENV_MOD order, EX_STRIDE apart (X_* below) */
    P_FENV_DELAY, P_FENV_HOLD, P_FENV_ACURVE, P_FENV_DCURVE, P_FENV_RCURVE, P_FENV_LOOP, P_FENV_LATCH, P_FENV_SYNC,
    P_FENV_MULTI, P_FENV_RESET, P_FENV_SCALE, P_FENV_VEL,
    P_AMP_DELAY, P_AMP_HOLD, P_AMP_ACURVE, P_AMP_DCURVE, P_AMP_RCURVE, P_AMP_LOOP, P_AMP_LATCH, P_AMP_SYNC,
    P_AMP_MULTI, P_AMP_RESET, P_AMP_SCALE, P_AMP_VEL,
    P_MENV_DELAY, P_MENV_HOLD, P_MENV_ACURVE, P_MENV_DCURVE, P_MENV_RCURVE, P_MENV_LOOP, P_MENV_LATCH, P_MENV_SYNC,
    P_MENV_MULTI, P_MENV_RESET, P_MENV_SCALE, P_MENV_VEL,
    /* voice and keyboard options (the polyphony and left-hand MORE pages, tuning) */
    P_NOTE_PRIORITY, P_STEAL_MODE, P_HOLD, P_BEND_UP, P_BEND_DOWN, P_VEL_CURVE, P_TRANSPOSE, P_OCT_SHIFT,
    P_KEY_CENTER, P_MASTER_TUNE, P_FINE_TUNE,
    /* each oscillator's MORE page, XO_STRIDE apart (XO_* below); then the sync sources of oscillators 2 and 3 */
    P_O1_TRACK, P_O1_RESET, P_O1_BEND_UP_OFF, P_O1_BEND_DN_OFF,
    P_O2_TRACK, P_O2_RESET, P_O2_BEND_UP_OFF, P_O2_BEND_DN_OFF,
    P_O3_TRACK, P_O3_RESET, P_O3_BEND_UP_OFF, P_O3_BEND_DN_OFF,
    P_O2_SYNC_SRC, P_O3_SYNC_SRC,
    P_LFO3_RATE, P_LFO3_WAVE, P_LFO3_SYNC, P_LFO4_RATE, P_LFO4_WAVE, P_LFO4_SYNC,
    P_ANGLE_LFO3, P_PW_LFO3,                                      /* LFO 3's hard-wired amounts to the wave shapes */
    P_LFO1_RANGE, P_LFOX_END = P_LFO1_RANGE + NLFO * LX_STRIDE - 1,   /* NLFO x the L_* extras */
    P_MODX_BASE, P_MODX_END = P_MODX_BASE + MOD_SLOTS * MOD_SLOT_EXTRA - 1,
    /* the synth effect's ON and its parameters beyond type, rate, depth, feedback and mix (src/fx.h) */
    P_MODFX_ON, P_MODFX_TIME, P_MODFX_TIME_R, P_MODFX_RATE_R, P_MODFX_FB_R, P_MODFX_DTIME, P_MODFX_DFB, P_MODFX_TONE,
    P_MODFX_SPREAD, P_MODFX_GAIN, P_MODFX_SYNC, P_MODFX_SWEEP, P_MODFX_NOISE, P_MODFX_VRATE, P_MODFX_VDEPTH,
    P_MODFX_FLUTTER, P_MODFX_WOW, P_MODFX_BIAS, P_MODFX_ROOM, P_MODFX_DECAY, P_MODFX_BITS, P_MODFX_SRATE, P_MODFX_RESO,
    P_MODFX_CUT1, P_MODFX_CUT2, P_MODFX_CUT3, P_MODFX_LVL1, P_MODFX_LVL2, P_MODFX_LVL3, P_MODFX_FTYPE,
    P_MODFX_CTRL1, P_MODFX_CTRL2, P_MODFX_C1_DST, P_MODFX_C2_DST,
    /* the master effects: ON, delay into reverb, width; the delay's right time and ping-pong offset; the reverb */
    P_MFX_ON, P_MFX_SERIES, P_MFX_WIDTH, P_DLY_TIME_R, P_DLY_OFFSET,
    P_REV_TYPE, P_REV_SIZE, P_REV_DIFF, P_REV_TONE, P_REV_LOWDAMP, P_REV_MOD, P_REV_EARLY, P_REV_FREEZE,
    P_NOISE_COLOR_PAIR, P_NOISE_COLOR_MIX, P_NOISE_ENV, P_NOISE_A, P_NOISE_R, P_NOISE_SUS,   /* noise colours and EG */
    P_PAN, P_SPREAD, P_DRIFT,
    P_AN_CUT, P_AN_ENV, P_AN_LVL, P_AN_TUNE, P_AN_RES, P_AN_LADDER,   /* ANALOG CHARACTER (with drift) */
    P_RIBBON_DST, P_RIBBON_AMT, P_RIBBON_MODE, P_RIBBON_ORIGIN,   /* the ribbon's quick assignment and behaviour */
    /* the performance section (src/seq.h and "THE PERFORMANCE SECTION" below): arpeggiator, chord memory, generative */
    P_ARP_ON, P_ARP_PATTERN, P_ARP_OCT, P_ARP_DIR, P_ARP_OCT_ORDER, P_ARP_PENDULUM, P_ARP_PEND_MODE, P_ARP_RND_MODE,
    P_ARP_SYNC, P_ARP_RATE, P_ARP_DIV, P_ARP_TIMING, P_ARP_GATE, P_ARP_SWING, P_ARP_LATCH, P_ARP_RESET,
    P_CHORD_LEARN, P_CHORD_ON, P_CHORD_SHAPE, P_CHORD_IV1, P_CHORD_IV5 = P_CHORD_IV1 + CHORD_MAX - 2,
    P_GEN_EUCLID, P_GEN_STEPS, P_GEN_HITS, P_GEN_ROT, P_GEN_PROB, P_GEN_VEL_RAND, P_GEN_TM, P_GEN_TM_LEN,
    P_GEN_TM_CHANGE, P_GEN_SCALE, P_GEN_RANGE,
    /* the MACRO channel (Plaits): model, its three knobs, footage and frequency, main/aux blend, level, filter buttons */
    P_MAC_MODEL, P_MAC_HARM, P_MAC_TIMBRE, P_MAC_MORPH, P_MAC_OCT, P_MAC_FREQ, P_MAC_AUX, P_MAC_LEVEL, P_MAC_SVF,
    P_MAC_LAD,
    /* the open-source synth effect types (src/fx/mi_effects.h, src/fx/airwindows.h): Rings and Clouds, then the
     * Airwindows ones */
    P_MODFX_PITCH, P_MODFX_FOLLOW, P_MODFX_STRUCT, P_MODFX_POS, P_MODFX_MODEL, P_MODFX_SIZE, P_MODFX_DENSITY,
    P_MODFX_TEXTURE, P_MODFX_FREEZE, P_MODFX_GMODE, P_MODFX_QUALITY, P_MODFX_DRIVE, P_MODFX_BUMP, P_MODFX_LOWCUT,
    /* the master effect slots (src/fx.h): the synth effect's two-tap delays and crossover, the buss mode, then
     * each slot's controls beyond what it held before (slot 1: dly_*, slot 2: rev_*; BANK below) */
    P_MODFX_DTIME_R, P_MODFX_DEPTH_R, P_MODFX_OFFSET, P_MODFX_FBL, P_MODFX_FBR, P_MODFX_XOVER, P_MFX_BUSS, P_DLY_TYPE,
    P_DLY_MIX, P_DLY_GAIN, P_DLY_FB_R, P_DLY_FBL, P_DLY_FBR, P_DLY_RATE, P_DLY_RATE_R, P_DLY_DEPTH,
    P_DLY_DEPTH_R, P_DLY_SPREAD, P_DLY_NOISE, P_DLY_SWEEP, P_DLY_DFB, P_DLY_VRATE, P_DLY_VDEPTH, P_DLY_FLUTTER,
    P_DLY_WOW, P_DLY_BIAS, P_DLY_ROOM, P_DLY_DECAY, P_DLY_SIZE, P_DLY_PRE, P_DLY_DAMP, P_DLY_LOWDAMP,
    P_DLY_DIFF, P_DLY_MOD, P_DLY_EARLY, P_DLY_FREEZE, P_DLY_DISTANCE, P_DLY_HIFREQ, P_DLY_HLEVEL, P_DLY_MLEVEL,
    P_DLY_LLEVEL, P_DLY_LDECAY, P_DLY_HDECAY, P_DLY_PITCH, P_DLY_PITCH_B, P_DLY_PDECAY, P_DLY_GRAVITY, P_DLY_XOVER,
    P_DLY_BITS, P_DLY_SRATE, P_DLY_RESO, P_DLY_CUT1, P_DLY_CUT2, P_DLY_CUT3, P_DLY_LVL1, P_DLY_LVL2,
    P_DLY_LVL3, P_DLY_FTYPE, P_REV_MIX, P_REV_GAIN, P_REV_SYNC, P_REV_TIME, P_REV_TIME_R, P_REV_FB,
    P_REV_FB_R, P_REV_FBL, P_REV_FBR, P_REV_RATE, P_REV_RATE_R, P_REV_DEPTH, P_REV_DEPTH_R, P_REV_OFFSET,
    P_REV_SPREAD, P_REV_NOISE, P_REV_SWEEP, P_REV_DFB, P_REV_VRATE, P_REV_VDEPTH, P_REV_FLUTTER, P_REV_WOW,
    P_REV_BIAS, P_REV_ROOM, P_REV_DISTANCE, P_REV_HIFREQ, P_REV_HLEVEL, P_REV_MLEVEL, P_REV_LLEVEL, P_REV_LDECAY,
    P_REV_HDECAY, P_REV_PITCH, P_REV_PITCH_B, P_REV_PDECAY, P_REV_GRAVITY, P_REV_XOVER, P_REV_BITS, P_REV_SRATE,
    P_REV_RESO, P_REV_CUT1, P_REV_CUT2, P_REV_CUT3, P_REV_LVL1, P_REV_LVL2, P_REV_LVL3, P_REV_FTYPE,
    /* the step sequencer (src/stepseq.h, src/stepseq_run.h): its switches and settings; the steps are engine state */
    P_SEQ_ON, P_SEQ_PLAY, P_SEQ_REC, P_SEQ_STEP_MOD, P_SEQ_SYNC, P_SEQ_RATE, P_SEQ_DIV, P_SEQ_TIMING, P_SEQ_LENGTH,
    P_SEQ_FOLLOW_SWING, P_CLOCK_SWING, P_SEQ_NOTE_TRIG, P_SEQ_NOTE_RESET, P_SEQ_RESTART_HOLD, P_SEQ_RESTART_TRIG,
    P_SEQ_TRANSPOSE, P_SEQ_TR_MODE, P_SEQ_ONE_SHOT, P_SEQ_REC_MODE, P_SEQ_PATTERN,
    /* settings: the MIDI channel the instrument listens on (Omni: every channel), and CHORD LEARN heard or silent */
    P_MIDI_CH, P_LEARN_MODE,
    /* multitimbral (src/synths.h): each Synth's setup (ON, MIDI channel, key range); master level and PLAY ALL (shared) */
    P_SYN_ON, P_SYN_CH, P_KEY_LO, P_KEY_HI, P_MASTER_LEVEL, P_SEQ_ALL,
    /* the mixer's EXT channel: the audio input (the desktop plugin's; see one_ish_set_input) into every voice's filters */
    P_EXT_LEVEL, P_EXT_ROUTE,
    /* the vocoders' SIBILANCE and SENSITIVITY: the synth effect's, then each master slot's (src/fx/vocoder.h) */
    P_MODFX_SIBIL, P_MODFX_SENS, P_DLY_SIBIL, P_DLY_SENS, P_REV_SIBIL, P_REV_SENS,
    /* (2026-10-29) each matrix slot's ENABLE; the X/Y pad's polarity and hold, LOAD SAVED POS and the saved wheel and
     * pad positions */
    P_MOD1_ON, P_MOD16_ON = P_MOD1_ON + MOD_SLOTS - 1, P_PAD_POLARITY, P_PAD_HOLD, P_LOAD_POS, P_WHEEL_POS, P_PAD_X_POS,
    P_PAD_Y_POS, P_EG_TIMES,   /* (2026-10-29) the envelopes' MIN and 13 synced values */
    P_ARP_FOLLOW_SWING, P_SEQ_HOST_SS,   /* (2026-10-29) the arp's FOLLOW SWING; the sequencer on the host's start, stop */
    P_SYN_SOLO,          /* (2026-10-29) the Synth soloed (src/synths.h; never saved) */
    P_KEY_ROOT, P_KEY_SCALE,   /* (2026-10-29) the key the generators play in (src/keyscale.h) */
    /* (2026-10-29) the bouncing balls and particles (src/physgen.h, src/physgen_run.h) */
    P_PH_BALLS, P_PH_NBALLS, P_PH_SPEED, P_PH_GRAVITY, P_PH_SIZE, P_PH_BOUNCE, P_PH_PARTS, P_PH_RATE, P_PH_SPREAD,
    P_PH_LIFE, P_PH_ATTRACT, P_PH_TRIGGER, P_PH_KEYS, P_PH_RESTART, P_PH_SYNC, P_PH_LEN, P_PH_OCT, P_PH_RANGE, P_PH_MAX,
    P_PH_DYN,
    /* (2026-10-30) RAIN AND RIPPLES and MARKOV MELODY (src/gens_b.h, src/gens_b_run.h) */
    P_RAIN_ON, P_RAIN_RATE, P_RAIN_SYNC, P_RAIN_SPREAD, P_RAIN_SPEED, P_RAIN_LIFE, P_RAIN_ECHO, P_RAIN_STONES,
    P_RAIN_KEYS, P_RAIN_LEN, P_RAIN_OCT, P_RAIN_RANGE, P_RAIN_MAX,
    P_MKV_ON, P_MKV_ORDER, P_MKV_LEARN, P_MKV_FORGET, P_MKV_CREAT, P_MKV_RHYTHM, P_MKV_QUANT, P_MKV_RATE, P_MKV_GATE,
    P_MKV_RANGE, P_MKV_KEYS, P_MKV_MAX,
    /* (2026-10-30) the window's LINKs (desktop/hub.c): shared, stored only; the engine does nothing with them */
    P_LINK_KEY, P_LINK_SCALE, P_LINK_ARP, P_LINK_SEQ, P_LINK_LFO1, P_LINK_LFO2, P_LINK_LFO3, P_LINK_LFO4, P_LINK_PHYS,
    P_LINK_SEND, P_LINK_CUT,
    /* (2026-10-30) the balls' walls (PLAYS, SILENT, OPEN) and FOLLOW OCTAVE (src/physgen_run.h) */
    P_PH_FLOOR, P_PH_CEIL, P_PH_LEFT, P_PH_RIGHT, P_PH_FOLLOW_OCT,
    /* (2026-10-30) GAME OF LIFE, GRAVITY ORBITS, PENDULUMS (src/gens_a.h, src/gens_a_run.h) */
    P_LIFE_ON, P_LIFE_KEYS, P_LIFE_RATE, P_LIFE_SPEED, P_LIFE_MODE, P_LIFE_RULE, P_LIFE_DENSITY, P_LIFE_SEED,
    P_LIFE_WRAP, P_LIFE_NOTES, P_LIFE_LEN, P_LIFE_OCT, P_LIFE_RANGE, P_LIFE_MAX,
    P_ORB_ON, P_ORB_KEYS, P_ORB_PLANETS, P_ORB_SPEED, P_ORB_GRAVITY, P_ORB_ECC, P_ORB_SPREAD, P_ORB_SPOKES,
    P_ORB_PLANET_TO, P_ORB_QUANT, P_ORB_LEN, P_ORB_OCT, P_ORB_RANGE, P_ORB_MAX,
    P_PEND_ON, P_PEND_KEYS, P_PEND_COUNT, P_PEND_CYCLE, P_PEND_SPEED, P_PEND_SPREAD, P_PEND_DAMP, P_PEND_TRIGGER,
    P_PEND_RESTART, P_PEND_LEN, P_PEND_OCT, P_PEND_MAX, P_PEND_DYN,
    P_LIFE_RESEED,   /* (2026-10-30) LIFE's RESEED button (the engine sets it back to Off) */
    P_GEN_FOLLOW,    /* (2026-10-30) FOLLOW TEMPO: the free-running generators' speeds follow the BPM (shared) */
    P_SEQ_OUTPUT,    /* (2026-10-30) the sequencer's OUTPUT: the Synths its notes play (src/stepseq_run.h) */
    P_DLY_ON, P_REV_ON,   /* (2026-10-30) MASTER FX 1 and 2 each on or off (shared; under the master ON) */
    /* (2026-10-30) GRAINS (src/fx/grains.h): the synth effect's own controls, then each master slot's (BANK) */
    P_MODFX_SPRAY, P_MODFX_PRAND, P_MODFX_REVERSE, P_MODFX_GQUANT,
    P_DLY_GPOS, P_DLY_GDENS, P_DLY_GENV, P_DLY_GSPRAY, P_DLY_GPRAND, P_DLY_GREV, P_DLY_GQUANT,
    P_REV_GPOS, P_REV_GDENS, P_REV_GENV, P_REV_GSPRAY, P_REV_GPRAND, P_REV_GREV, P_REV_GQUANT,
    P_LINK_PAD,      /* (2026-10-30) LINK on the X/Y pad: its CCs reach all three Synths (src/synths.h midi) */
    P_GEN_KEYS, P_GEN_OCT, P_GEN_EU_NOTES,   /* (2026-10-30) Euclid / Turing KEYS (FREE etc.), LOW OCTAVE, EUCLID NOTES */
    P_GEN_TEMPO_SRC, P_GEN_BPM,   /* (2026-10-30) the generators' tempo: GLOBAL (the host's or OWN TEMPO) or this Synth's BPM */
    P_GEN_FOLLOW_KEYS,   /* (2026-10-30) FOLLOW KEYS: the notes a generator plays from the keys, in the key played */
    P_LFO1_SHARE,    /* (2026-10-30) SHARE: LFO n of Synths 2 and 3 is Synth 1's (shared; lfo_owner), 4 of them */
    P_LFO4_SHARE = P_LFO1_SHARE + 3,
    /* (2026-10-30) the ANALOG page's HQ MODE (Off, 2x, 4x: the voice runs oversampled, src/hq.h) and WARM-UP (on, how
     * much, how long: a cold instrument's pitch and cutoffs settling over minutes) */
    P_AN_HQ, P_AN_WARM, P_AN_WARM_AMT, P_AN_WARM_TIME,
    P_SYN_OUT,       /* (2026-10-30) the Synth's output: MAIN, its own pair, or both (src/synths.h; desktop multi-out) */
    P_MOUT_ON, P_MOUT_SEND, P_MOUT_CH,   /* (2026-10-30) MIDI OUT per Synth: on, what it sends, its channel (src/midi_out.h) */
    P_SUS_PEDAL,     /* (2026-10-30) SUSTAIN IN SYNTH: the sustain pedal (CC 64) holds this Synth (On) or not */
    /* (2026-10-31) LINK on the mod wheel (CC 1), the pitch wheel (pitch bend) and the ribbon (CC 16, 17): shared; lit,
     * that controller reaches all three Synths whatever their MIDI channels (src/synths.h midi), as link_pad */
    P_LINK_MOD, P_LINK_BEND, P_LINK_RIBBON,
    P_LINK_GLIDE,    /* (2026-10-31) the window's glide LINK (gen_layout.py LINK_GROUPS): stored, as the other LINKs */
    /* (2026-11-01) ANALOG AMOUNT (shared): every Synth's ANALOG page at once (0% exact, 100% as set, 200% twice) */
    P_ANALOG_AMT,
    P_LINK_POLY,     /* (2026-11-01) the POLYPHONY panel's LINK (the window's kind, as glide's): stored */
    P_LINK_HOLD, P_LINK_OCTAVE,   /* (2026-11-01) the hand controls' HOLD and OCTAVE LINKs (the window's kind): stored */
    P_HOLD_ADD,      /* (2026-11-02) HOLD's ADD: a new key joins what HOLD latched instead of replacing it */
    P_SEQ_ROTATE, P_SEQ_ROT_EVERY, P_SEQ_ROT_BY,   /* (2026-11-02) the sequence nudged, by hand and every so many passes */
    P_LINK_ARP_ALL,  /* (2026-11-01) the ARPEGGIATOR panel's LINK ALL (the window's kind): stored */
    /* (2026-11-02) each generator's own KEY and SCALE (gen_key_in, gen_scale_in): a pair, the SCALE right after its KEY */
    P_GEN_OWN_KEY, P_GEN_OWN_SCALE, P_BALL_OWN_KEY, P_BALL_OWN_SCALE, P_PART_OWN_KEY, P_PART_OWN_SCALE, P_RAIN_OWN_KEY,
    P_RAIN_OWN_SCALE, P_MKV_OWN_KEY, P_MKV_OWN_SCALE, P_LIFE_OWN_KEY, P_LIFE_OWN_SCALE, P_ORB_OWN_KEY, P_ORB_OWN_SCALE,
    P_PEND_OWN_KEY, P_PEND_OWN_SCALE,
    /* (2026-11-02) the ANALOG page's NON-LINEAR SVF, SVF DRIVE and SVF SELF-OSC (src/svf.h svf_tick_nl) */
    P_AN_SVF, P_AN_SVF_DRIVE, P_AN_SVF_OSC,
    /* (2026-11-02) the ANALOG page's MIXER DRIVE and FEEDBACK (render_voice_os) */
    P_AN_MIX_DRIVE, P_AN_FEEDBACK,
    P_COUNT
};

static const char *const KEYS[P_COUNT] = {
#define OSC_KEYS(n) "o" #n "_wave", "o" #n "_oct", "o" #n "_freq", "o" #n "_beat", "o" #n "_angle", "o" #n "_pw", \
                    "o" #n "_mix", "o" #n "_level"
    OSC_KEYS(1), "", "o1_svf", "o1_lad",
    OSC_KEYS(2), "o2_sync", "o2_svf", "o2_lad",
    OSC_KEYS(3), "o3_sync", "o3_svf", "o3_lad",
#undef OSC_KEYS
    "noise_level", "noise_svf", "noise_lad", "ring_level", "ring_svf", "ring_lad", "ring_src", "fm_amt", "fm_route",
    "flt_cut", "flt_res", "flt_drive", "flt_mode", "flt_slope", "svf_cut", "svf_res", "svf_mode", "flt_route", "flt_mix",
    "fenv_a", "fenv_d", "fenv_s", "fenv_r",
    "amp_a", "amp_d", "amp_s", "amp_r",
    "menv_a", "menv_d", "menv_s", "menv_r",
    "lad_env", "lad_lfo", "lad_kbd", "svf_env", "svf_lfo", "svf_kbd", "pitch_lfo", "pitch_env",
    "lfo1_rate", "lfo1_wave", "lfo1_sync", "lfo2_rate", "lfo2_wave", "lfo2_sync",
    "" /* the retired "bend_range": store() maps it onto bend_up / bend_down */, "voices",
#define SLOT_KEYS(n) [P_MOD_BASE + 4 * ((n) - 1)] = "mod" #n "_src", [P_MOD_BASE + 4 * ((n) - 1) + 1] = "mod" #n "_dst", \
                     [P_MOD_BASE + 4 * ((n) - 1) + 2] = "mod" #n "_amt", [P_MOD_BASE + 4 * ((n) - 1) + 3] = "mod" #n "_via"
    SLOT_KEYS(1), SLOT_KEYS(2), SLOT_KEYS(3), SLOT_KEYS(4), SLOT_KEYS(5), SLOT_KEYS(6), SLOT_KEYS(7), SLOT_KEYS(8),
    SLOT_KEYS(9), SLOT_KEYS(10), SLOT_KEYS(11), SLOT_KEYS(12), SLOT_KEYS(13), SLOT_KEYS(14), SLOT_KEYS(15),
    SLOT_KEYS(16),
#undef SLOT_KEYS
    [P_VOLUME] = "volume",
    "modfx_type", "modfx_rate", "modfx_depth", "modfx_fb", "modfx_mix",
    "dly_time", "dly_sync", "dly_fb", "dly_tone", "" /* the retired "dly_pp": store() maps it onto dly_type */,
    "mfx_send1",
    "rev_decay", "rev_damp", "rev_pre", "mfx_send2",
    "glide", "glide_time", "glide_type", "glide_legato", "glide_gated", "glissando",
    "unison", "uni_count", "uni_detune", "pitch_var",
    "svf_space", "svf_pair", "flt_link", "lad_fm", "svf_fm", "lad_fm_src", "svf_fm_src",
#define ENV_EXTRA_KEYS(e) e "_delay", e "_hold", e "_acurve", e "_dcurve", e "_rcurve", e "_loop", e "_latch", e "_sync", \
                          e "_multi", e "_reset", e "_scale", e "_vel"
    ENV_EXTRA_KEYS("fenv"), ENV_EXTRA_KEYS("amp"), ENV_EXTRA_KEYS("menv"),
#undef ENV_EXTRA_KEYS
    [P_NOTE_PRIORITY] = "note_priority", [P_STEAL_MODE] = "steal_mode", [P_HOLD] = "hold", [P_BEND_UP] = "bend_up",
    [P_BEND_DOWN] = "bend_down", [P_VEL_CURVE] = "vel_curve", [P_TRANSPOSE] = "transpose",
    [P_OCT_SHIFT] = "octave_shift", [P_KEY_CENTER] = "key_center", [P_MASTER_TUNE] = "master_tune",
    [P_FINE_TUNE] = "fine_tune",
#define OSC_MORE_KEYS(n) [P_O##n##_TRACK] = "o" #n "_track", [P_O##n##_RESET] = "o" #n "_reset", \
                         [P_O##n##_BEND_UP_OFF] = "o" #n "_bend_up_off", [P_O##n##_BEND_DN_OFF] = "o" #n "_bend_dn_off"
    OSC_MORE_KEYS(1), OSC_MORE_KEYS(2), OSC_MORE_KEYS(3),
#undef OSC_MORE_KEYS
    [P_O2_SYNC_SRC] = "o2_sync_src", [P_O3_SYNC_SRC] = "o3_sync_src",
    "lfo3_rate", "lfo3_wave", "lfo3_sync", "lfo4_rate", "lfo4_wave", "lfo4_sync",
    "angle_lfo3", "pw_lfo3",
#define LFO_EXTRA_KEYS(n) "lfo" #n "_range", "lfo" #n "_var", "lfo" #n "_polarity", "lfo" #n "_phase", "lfo" #n "_reset", \
                          "lfo" #n "_delay", "lfo" #n "_fade_in", "lfo" #n "_fade_out", "lfo" #n "_repeats", \
                          "lfo" #n "_smooth", "lfo" #n "_timing"
    LFO_EXTRA_KEYS(1), LFO_EXTRA_KEYS(2), LFO_EXTRA_KEYS(3), LFO_EXTRA_KEYS(4),
#undef LFO_EXTRA_KEYS
#define SLOTX_KEYS(n) "mod" #n "_via_amt", "mod" #n "_xf", "mod" #n "_xf_amt"
    SLOTX_KEYS(1), SLOTX_KEYS(2), SLOTX_KEYS(3), SLOTX_KEYS(4), SLOTX_KEYS(5), SLOTX_KEYS(6), SLOTX_KEYS(7),
    SLOTX_KEYS(8), SLOTX_KEYS(9), SLOTX_KEYS(10), SLOTX_KEYS(11), SLOTX_KEYS(12), SLOTX_KEYS(13), SLOTX_KEYS(14),
    SLOTX_KEYS(15), SLOTX_KEYS(16),
#undef SLOTX_KEYS
    "modfx_on", "modfx_time", "modfx_time_r", "modfx_rate_r", "modfx_fb_r", "modfx_dtime", "modfx_dfb", "modfx_tone",
    "modfx_spread", "modfx_gain", "modfx_sync", "modfx_sweep", "modfx_noise", "modfx_vrate", "modfx_vdepth",
    "modfx_flutter", "modfx_wow", "modfx_bias", "modfx_room", "modfx_decay", "modfx_bits", "modfx_srate", "modfx_reso",
    "modfx_cut1", "modfx_cut2", "modfx_cut3", "modfx_lvl1", "modfx_lvl2", "modfx_lvl3", "modfx_ftype",
    "modfx_ctrl1", "modfx_ctrl2", "modfx_c1_dst", "modfx_c2_dst",
    "mfx_on", "mfx_series", "mfx_width", "dly_time_r", "dly_offset",
    "rev_type", "rev_size", "rev_diff", "rev_tone", "rev_lowdamp", "rev_mod", "rev_early", "rev_freeze",
    "noise_color_pair", "noise_color_mix", "noise_env", "noise_a", "noise_r", "noise_sus",
    "pan", "spread", "drift",
    "an_cut", "an_env", "an_lvl", "an_tune", "an_res", "an_ladder",
    "ribbon_dst", "ribbon_amt", "ribbon_mode", "ribbon_origin",
    [P_ARP_ON] = "arp_on", "arp_pattern", "arp_oct", "arp_dir", "arp_oct_order", "arp_pendulum", "arp_pend_mode",
    "arp_rnd_mode", "arp_sync", "arp_rate", "arp_div", "arp_timing", "arp_gate", "arp_swing", "arp_latch", "arp_reset",
    [P_CHORD_LEARN] = "chord_learn", "chord_on", "chord_shape", "chord_iv1", "chord_iv2", "chord_iv3", "chord_iv4",
    "chord_iv5",
    [P_GEN_EUCLID] = "gen_euclid_on", "gen_steps", "gen_hits", "gen_rot", "gen_prob", "gen_vel_rand", "gen_tm_on",
    "gen_tm_len", "gen_tm_change", "gen_scale", "gen_range",
    "mac_model", "mac_harm", "mac_timbre", "mac_morph", "mac_oct", "mac_freq", "mac_aux", "mac_level", "mac_svf",
    "mac_lad",
    "modfx_pitch", "modfx_follow", "modfx_struct", "modfx_pos", "modfx_model", "modfx_size", "modfx_density",
    "modfx_texture", "modfx_freeze", "modfx_gmode", "modfx_quality", "modfx_drive", "modfx_bump", "modfx_lowcut",
    [P_MODFX_DTIME_R] = "modfx_dtime_r",
    "modfx_depth_r", "modfx_offset", "modfx_fbl", "modfx_fbr", "modfx_xover", "mfx_buss", "dly_type", "dly_mix",
    "dly_gain", "dly_fb_r", "dly_fbl", "dly_fbr", "dly_rate", "dly_rate_r", "dly_depth", "dly_depth_r",
    "dly_spread", "dly_noise", "dly_sweep", "dly_dfb", "dly_vrate", "dly_vdepth", "dly_flutter", "dly_wow",
    "dly_bias", "dly_room", "dly_decay", "dly_size", "dly_pre", "dly_damp", "dly_lowdamp", "dly_diff", "dly_mod",
    "dly_early", "dly_freeze", "dly_distance", "dly_hifreq", "dly_hlevel", "dly_mlevel", "dly_llevel", "dly_ldecay",
    "dly_hdecay", "dly_pitch", "dly_pitch_b", "dly_pdecay", "dly_gravity", "dly_xover", "dly_bits", "dly_srate",
    "dly_reso", "dly_cut1", "dly_cut2", "dly_cut3", "dly_lvl1", "dly_lvl2", "dly_lvl3", "dly_ftype", "rev_mix",
    "rev_gain", "rev_sync", "rev_time", "rev_time_r", "rev_fb", "rev_fb_r", "rev_fbl", "rev_fbr", "rev_rate",
    "rev_rate_r", "rev_depth", "rev_depth_r", "rev_offset", "rev_spread", "rev_noise", "rev_sweep", "rev_dfb",
    "rev_vrate", "rev_vdepth", "rev_flutter", "rev_wow", "rev_bias", "rev_room", "rev_distance", "rev_hifreq",
    "rev_hlevel", "rev_mlevel", "rev_llevel", "rev_ldecay", "rev_hdecay", "rev_pitch", "rev_pitch_b", "rev_pdecay",
    "rev_gravity", "rev_xover", "rev_bits", "rev_srate", "rev_reso", "rev_cut1", "rev_cut2", "rev_cut3", "rev_lvl1",
    "rev_lvl2", "rev_lvl3", "rev_ftype",
    "seq_on", "seq_play", "seq_rec", "seq_step_mod", "seq_sync", "seq_rate", "seq_div", "seq_timing", "seq_length",
    "seq_follow_swing", "clock_swing", "seq_note_trig", "seq_note_reset", "seq_restart_hold", "seq_restart_trig",
    "seq_transpose", "seq_tr_mode", "seq_one_shot", "seq_rec_mode", "seq_pattern",
    "midi_ch", "learn_mode",
    "syn_on", "syn_ch", "key_lo", "key_hi", "master_level", "seq_all",
    "ext_level", "ext_route",
    "modfx_sibil", "modfx_sens", "dly_sibil", "dly_sens", "rev_sibil", "rev_sens",
    "mod1_on", "mod2_on", "mod3_on", "mod4_on", "mod5_on", "mod6_on", "mod7_on", "mod8_on", "mod9_on", "mod10_on",
    "mod11_on", "mod12_on", "mod13_on", "mod14_on", "mod15_on", "mod16_on", "pad_polarity", "pad_hold", "load_pos",
    "wheel_pos", "pad_x_pos", "pad_y_pos", "eg_times", "arp_follow_swing", "seq_host_ss",
    "syn_solo", "key_root", "key_scale",
    "phys_balls_on", "phys_balls", "phys_speed", "phys_gravity", "phys_size", "phys_bounce", "phys_parts_on", "phys_rate",
    "phys_spread", "phys_life", "phys_attract", "phys_trigger", "phys_keys", "phys_restart", "phys_sync", "phys_len",
    "phys_octave", "phys_range", "phys_max", "phys_dyn",
    /* (2026-10-30) rain and ripples, the Markov melody */
    "rain_on", "rain_rate", "rain_sync", "rain_spread", "rain_speed", "rain_life", "rain_echo", "rain_stones",
    "rain_keys", "rain_len", "rain_octave", "rain_range", "rain_max",
    "mkv_on", "mkv_order", "mkv_learn", "mkv_forget", "mkv_creat", "mkv_rhythm", "mkv_quant", "mkv_rate", "mkv_gate",
    "mkv_range", "mkv_keys", "mkv_max",
    "link_key", "link_scale", "link_arp", "link_seq", "link_lfo1", "link_lfo2", "link_lfo3", "link_lfo4", "link_phys",
    "link_send", "link_cut",
    "phys_floor", "phys_ceil", "phys_left", "phys_right", "phys_follow_oct",   /* (2026-10-30) */
    /* (2026-10-30) GAME OF LIFE, GRAVITY ORBITS, PENDULUMS */
    "life_on", "life_keys", "life_rate", "life_speed", "life_mode", "life_rule", "life_density", "life_seed",
    "life_wrap", "life_notes", "life_len", "life_octave", "life_range", "life_max",
    "orb_on", "orb_keys", "orb_planets", "orb_speed", "orb_gravity", "orb_ecc", "orb_spread", "orb_spokes",
    "orb_planet_to", "orb_quant", "orb_len", "orb_octave", "orb_range", "orb_max",
    "pend_on", "pend_keys", "pend_count", "pend_cycle", "pend_speed", "pend_spread", "pend_damp", "pend_trigger",
    "pend_restart", "pend_len", "pend_octave", "pend_max", "pend_dyn",
    "life_reseed",
    "gen_follow_tempo",   /* (2026-10-30) */
    "seq_output",   /* (2026-10-30) */
    "dly_on", "rev_on",   /* (2026-10-30) */
    "modfx_spray", "modfx_prand", "modfx_reverse", "modfx_gquant",   /* (2026-10-30) GRAINS */
    "dly_gpos", "dly_gdens", "dly_genv", "dly_gspray", "dly_gprand", "dly_greverse", "dly_gquant",
    "rev_gpos", "rev_gdens", "rev_genv", "rev_gspray", "rev_gprand", "rev_greverse", "rev_gquant",
    "link_pad",
    "gen_keys", "gen_octave", "gen_eu_notes",   /* (2026-10-30) */
    "gen_tempo_src", "gen_bpm", "gen_follow_keys",   /* (2026-10-30) */
    "lfo1_share", "lfo2_share", "lfo3_share", "lfo4_share",   /* (2026-10-30) */
    "an_hq", "an_warm", "an_warm_amt", "an_warm_time",   /* (2026-10-30) */
    "syn_out",   /* (2026-10-30) */
    "mout_on", "mout_send", "mout_ch", "sus_pedal",   /* (2026-10-30) MIDI OUT, SUSTAIN IN SYNTH */
    "link_mod", "link_bend", "link_ribbon",   /* (2026-10-31) the wheels' and the ribbon's LINKs */
    "link_glide",   /* (2026-10-31) */
    "analog_amt",   /* (2026-11-01) ANALOG AMOUNT */
    "link_poly",    /* (2026-11-01) */
    "link_hold", "link_octave",   /* (2026-11-01) */
    "hold_add",     /* (2026-11-02) */
    "seq_rotate", "seq_rot_every", "seq_rot_by",   /* (2026-11-02) */
    "link_arp_all", /* (2026-11-01) */
    "gen_own_key", "gen_own_scale", "phys_ball_own_key", "phys_ball_own_scale", "phys_part_own_key", "phys_part_own_scale",
    "rain_own_key", "rain_own_scale", "mkv_own_key", "mkv_own_scale", "life_own_key", "life_own_scale", "orb_own_key",
    "orb_own_scale", "pend_own_key", "pend_own_scale",   /* (2026-11-02) */
    "an_svf", "an_svf_drive", "an_svf_osc",   /* (2026-11-02) */
    "an_mix_drive", "an_feedback",   /* (2026-11-02) */
};
