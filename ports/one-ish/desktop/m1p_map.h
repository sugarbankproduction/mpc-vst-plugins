/* (2026-10-31) The tables that turn a Moog One preset file's settings (.m1p) into Moo Won's (desktop/m1p.c reads
 * them; docs/PATCHES.md "Importing Moog One presets"). One row per setting the importer understands; every other
 * setting in a file is listed in the import report as "no equivalent / not known yet", never guessed at.
 *
 * WHERE THE KEY NAMES COME FROM. A .m1p file is plain text, one setting a line: a key like "/p/part/t/1/p/o1/oct", a
 * space, then the value (a number, true / false, a "quoted" word, or a [list]). A public community tool
 * (github.com/mlazarev/MoogOne, read for facts only, none of its code used) names the oscillator, pulse, mixer and
 * envelope keys. Everything else here was worked out on 2026-10-31 by reading the value ranges of 47 preset files the
 * owner downloaded (not part of the project): e.g. the filter cutoffs are MIDI note numbers (135.08 = 20 kHz), the
 * envelope and glide times are seconds, the LFO rates are Hz, an oscillator's pitch offset is semitones (its knob
 * position is the same value / 7). The sure column says how certain each row is.
 *
 * HOW TO ADD A ROW (when a file shows what a key is):
 *   m1     the Moog One key. '#' stands for the Synth number (1-3, "/p/part/t/#/..."), '@' for an oscillator, envelope
 *          or LFO number (1-4); everything else must match exactly.
 *   ours   Moo Won's key (params.json) as Synth 1's; '@' becomes the same number. For Synths 2 and 3 the importer adds
 *          "s2_" / "s3_" itself; a control Moo Won shares between the Synths is taken from Synth 1 only.
 *   conv   how the value turns into ours:
 *            M_UNIT  a number 0..1 spread over our control's whole range (min..max)
 *            M_NUM   a number in our control's own units (semitones, Hz, %, a note number)
 *            M_MUL   a number times the factor in opts ("100": a -1..+1 amount onto our -100..+100%)
 *            M_SEC   seconds onto our time knobs (1 ms .. 10 s on a log scale; envelopes, glide, the noise EG)
 *            M_CUT   a cutoff as a MIDI note number onto our cutoff knob (20 Hz .. 20 kHz)
 *            M_LFOHZ an LFO rate in Hz onto our rate knob, picking our RANGE (lfo@_range) that holds it
 *            M_OCT   an octave -2..+2 (0 = 8') onto our list 32' 16' 8' 4' 2'
 *            M_TAP   the ladder's output tap 1..4 (poles) onto our slope 6 .. 24 dB
 *            M_ENUM  a code from opts ("v=Tri|F=Saw": the file's code = our option's label; a label "!key" sets that
 *                    key Off instead, e.g. an effect type "none")
 *            M_LABEL a whole number onto the option with that label (unison count, voices, arp octaves); 0 or less:
 *                    left as it is
 *            M_BOOL  true / false onto Off / On
 *            M_FLAG  true: ours set to opts' label ("LP"), and "|key=Label" after it set too; false: nothing
 *            M_COVER nothing set: the setting is the knob position of a value another row already took
 *   sure   'D': documented, or certain from the files' values; 'G': our best reading (the report says "check by ear")
 * Values are clamped to our ranges. Where the Moog One's scale isn't known (resonance, amounts, levels) it is spread
 * linearly, so those may sound a little different from the original even where the key is certain. */
#pragma once

enum { M_UNIT = 1, M_NUM, M_MUL, M_SEC, M_CUT, M_LFOHZ, M_OCT, M_TAP, M_ENUM, M_LABEL, M_BOOL, M_FLAG, M_COVER };

typedef struct {
    const char *m1, *ours;
    char conv, sure;
    const char *opts;
} m1p_row_t;

#define T "/p/part/t/#/"
#define ENV(n, o)                                                                                                     \
    {T "p/e" #n "/a", o "_a", M_SEC, 'D', NULL}, {T "p/e" #n "/d", o "_d", M_SEC, 'D', NULL},                          \
    {T "p/e" #n "/r", o "_r", M_SEC, 'D', NULL}, {T "p/e" #n "/s", o "_s", M_UNIT, 'D', NULL},                         \
    {T "p/e" #n "/delay", o "_delay", M_SEC, 'D', NULL}, {T "p/e" #n "/h", o "_hold", M_SEC, 'D', NULL},               \
    {T "p/e" #n "/ak", o "_acurve", M_MUL, 'G', "50"}, {T "p/e" #n "/dk", o "_dcurve", M_MUL, 'G', "50"},              \
    {T "p/e" #n "/rk", o "_rcurve", M_MUL, 'G', "50"}, {T "p/e" #n "/velmod", o "_vel", M_MUL, 'G', "100"},            \
    {T "p/e" #n "/multi-trig", o "_multi", M_BOOL, 'D', NULL}, {T "p/e" #n "/sync", o "_sync", M_BOOL, 'D', NULL},     \
    {T "p/e" #n "/loop", o "_loop", M_BOOL, 'D', NULL}, {T "p/e" #n "/latch", o "_latch", M_BOOL, 'D', NULL},          \
    {T "p/e" #n "/rst", o "_reset", M_BOOL, 'G', NULL}, {T "p/e" #n "/knobs/a", NULL, M_COVER, 'D', NULL},             \
    {T "p/e" #n "/knobs/d", NULL, M_COVER, 'D', NULL}, {T "p/e" #n "/knobs/r", NULL, M_COVER, 'D', NULL},              \
    {T "p/e" #n "/knobs/delay", NULL, M_COVER, 'D', NULL}, {T "p/e" #n "/knobs/h", NULL, M_COVER, 'D', NULL}

#define FX_TYPES                                                                                                      \
    "delay=Delay|bbdDelay=BBD Delay|tapeDelay=Tape Delay|stereoDelay=Stereo Delay|pingpongDelay=Ping Pong|"           \
    "flanger=Flanger|stereoFlanger=Stereo Flanger|chorus=Chorus|dualChorus=Dual Chorus|ensemble=Ensemble|"            \
    "stereoChorus=Stereo Chorus|phaser6stage=6-Stage Phaser|phaser12stage=12-Stage Phaser|timePhaser=Time Phaser|"    \
    "bitcrusher=Bit Crusher|resonator=Resonator|vocoder10=10 Band Vocoder|vocoder16=16 Band Vocoder"
#define MASTER_TYPES FX_TYPES "|et_plate=Plate|et_room=Room|et_hall=Hall|et_shimmer=Shimmer|et_blackhole=Blackhole"

static const m1p_row_t M1P_MAP[] = {
    /* the Synth as a whole */
    {T "en", "syn_on", M_BOOL, 'G', NULL},
    {T "p/g", "volume", M_UNIT, 'G', NULL},
    {T "p/pan", "pan", M_UNIT, 'D', NULL},
    {T "p/pan-spread", "spread", M_UNIT, 'G', NULL},
    {T "p/pbup", "bend_up", M_NUM, 'D', NULL},
    {T "p/pbdn", "bend_down", M_NUM, 'D', NULL},
    {T "p/ktc", "key_center", M_NUM, 'G', NULL},
    {T "in/local/note-min", "key_lo", M_NUM, 'G', NULL},
    {T "in/local/note-max", "key_hi", M_NUM, 'G', NULL},
    {T "mix/aux1", "mfx_send1", M_UNIT, 'G', NULL},
    {T "mix/aux2", "mfx_send2", M_UNIT, 'G', NULL},
    /* the oscillators */
    {T "p/o@/m", "o@_wave", M_ENUM, 'D', "v=Tri|F=Saw"},
    {T "p/o@/oct", "o@_oct", M_OCT, 'D', NULL},
    {T "p/o@/d", "o@_angle", M_UNIT, 'D', NULL},
    {T "p/o@/po", "o@_freq", M_NUM, 'D', NULL},
    {T "p/o@/knobs/po", NULL, M_COVER, 'D', NULL},
    {T "p/o@/fo", "o@_beat", M_NUM, 'G', NULL},
    {T "p/o@/pt", "o@_track", M_MUL, 'G', "100"},
    {T "p/o@/krst", "o@_reset", M_BOOL, 'G', NULL},
    {T "p/o@/pbup", "o@_bend_up_off", M_NUM, 'G', NULL},
    {T "p/o@/pbdn", "o@_bend_dn_off", M_NUM, 'G', NULL},
    {T "p/o2/sync1", "o2_sync", M_FLAG, 'G', "On|o2_sync_src=Osc 1"},
    {T "p/o2/sync3", "o2_sync", M_FLAG, 'G', "On|o2_sync_src=Osc 3"},
    {T "p/o3/sync1", "o3_sync", M_FLAG, 'G', "On|o3_sync_src=Osc 1"},
    {T "p/o3/sync2", "o3_sync", M_FLAG, 'G', "On|o3_sync_src=Osc 2"},
    {T "p/p@/d", "o@_pw", M_UNIT, 'D', NULL},
    {T "p/fm/amt", "fm_amt", M_UNIT, 'G', NULL},
    {T "p/fm/route", "fm_route", M_ENUM, 'D', "[1, 2]=1 > 2|[1, 3]=1 > 3|[3, 1]=3 > 1"},
    {T "p/ringsrc3", "ring_src", M_ENUM, 'G', "false=1 x 2|true=2 x 3"},
    /* the mixer: each oscillator, the noise, the ring mod: its level and which filters it feeds (fa SVF, fb ladder) */
    {T "p/mix/o@/g", "o@_level", M_UNIT, 'D', NULL},
    {T "p/mix/o@/fa", "o@_svf", M_BOOL, 'D', NULL},
    {T "p/mix/o@/fb", "o@_lad", M_BOOL, 'D', NULL},
    {T "p/mix/o@/plsbal", "o@_mix", M_UNIT, 'G', NULL},
    {T "p/mix/n/g", "noise_level", M_UNIT, 'D', NULL},
    {T "p/mix/n/fa", "noise_svf", M_BOOL, 'D', NULL},
    {T "p/mix/n/fb", "noise_lad", M_BOOL, 'D', NULL},
    {T "p/mix/rm/g", "ring_level", M_UNIT, 'D', NULL},
    {T "p/mix/rm/fa", "ring_svf", M_BOOL, 'D', NULL},
    {T "p/mix/rm/fb", "ring_lad", M_BOOL, 'D', NULL},
    {T "p/noise/mode", "noise_color_pair", M_ENUM, 'G', "wr=Red+White|rp=Red+Purple|wp=White+Purple"},
    {T "p/noise/mix", "noise_color_mix", M_UNIT, 'G', NULL},
    {T "p/noise/ar", "noise_env", M_BOOL, 'G', NULL},
    {T "p/noise/a", "noise_a", M_SEC, 'G', NULL},
    {T "p/noise/r", "noise_r", M_SEC, 'G', NULL},
    /* the ladder (fb) and the SVF pair (fa) */
    {T "p/fb/p", "flt_cut", M_CUT, 'D', NULL},
    {T "p/fb/r", "flt_res", M_UNIT, 'G', NULL},
    {T "p/fb/hp", "flt_mode", M_ENUM, 'D', "false=LP|true=HP"},
    {T "p/fb/tap", "flt_slope", M_TAP, 'G', NULL},
    {T "p/fb/eg_p", "lad_env", M_MUL, 'G', "100"},
    {T "p/fb/lfo_p", "lad_lfo", M_MUL, 'G', "100"},
    {T "p/fb/p_fp", "lad_kbd", M_MUL, 'G', "100"},
    {T "p/fb/fm/idx", "lad_fm", M_UNIT, 'G', NULL},
    {T "p/fa/p", "svf_cut", M_CUT, 'D', NULL},
    {T "p/fa/r", "svf_res", M_UNIT, 'G', NULL},
    {T "p/fa/eg_p", "svf_env", M_MUL, 'G', "100"},
    {T "p/fa/lfo_p", "svf_lfo", M_MUL, 'G', "100"},
    {T "p/fa/p_fp", "svf_kbd", M_MUL, 'G', "100"},
    {T "p/fa/fm/idx", "svf_fm", M_UNIT, 'G', NULL},
    {T "p/fa/par", "svf_pair", M_ENUM, 'G', "true=Parallel|false=Series"},
    {T "p/fa/1/bp", "svf_mode", M_FLAG, 'G', "BP"},
    {T "p/fa/1/hp", "svf_mode", M_FLAG, 'G', "HP"},
    {T "p/fa/1/lp", "svf_mode", M_FLAG, 'G', "LP"},
    {T "p/fafb", "flt_route", M_ENUM, 'G', "false=Parallel|true=Series"},
    {T "p/fbal", "flt_mix", M_UNIT, 'G', NULL},   /* 0 the SVF, 1 the ladder (a sound fed to the SVF alone has it low) */
    {T "p/flink", "flt_link", M_BOOL, 'D', NULL},
    /* the three envelopes: e1 filter, e2 amp, e3 mod (e1's sustain is mostly 0 in the files, e2's and e3's 1) */
    ENV(1, "fenv"),
    ENV(2, "amp"),
    ENV(3, "menv"),
    /* the four LFOs */
    {T "p/l@/f", "lfo@_rate", M_LFOHZ, 'D', NULL},
    {T "p/l@/r", NULL, M_COVER, 'D', NULL},
    {T "p/l@/knobs/f", NULL, M_COVER, 'D', NULL},
    {T "p/l@/w", "lfo@_wave", M_ENUM, 'G',
     "tri=Triangle|sine=Sine|sin=Sine|sq=Square|square=Square|saw=Saw|ramp=Ramp|s-h=S&H|noise=Noise|pulse=Pulse"},
    {T "p/l@/sync", "lfo@_sync", M_BOOL, 'D', NULL},
    {T "p/l@/pol", "lfo@_polarity", M_ENUM, 'G', "true=Bipolar|false=Unipolar"},
    {T "p/l@/var", "lfo@_var", M_UNIT, 'G', NULL},
    {T "p/l@/ph", "lfo@_phase", M_UNIT, 'G', NULL},
    {T "p/l@/rst", "lfo@_reset", M_ENUM, 'G', "none=Off|trig=On"},
    /* the modulation amounts on the panel */
    {T "p/m/l1_p", "pitch_lfo", M_MUL, 'G', "100"},
    {T "p/m/e3_p", "pitch_env", M_MUL, 'G', "50"},
    {T "p/m/l3_od", "angle_lfo3", M_UNIT, 'G', NULL},
    {T "p/m/l3_pd", "pw_lfo3", M_UNIT, 'G', NULL},
    /* glide, unison, voices */
    {T "p/pt/en", "glide", M_BOOL, 'D', NULL},
    {T "p/pt/t", "glide_time", M_SEC, 'G', NULL},
    {T "p/pt/mode", "glide_type", M_ENUM, 'D', "lcr=LCR|lct=LCT|exp=EXP"},
    {T "p/pt/legato", "glide_legato", M_BOOL, 'D', NULL},
    {T "p/pt/gated", "glide_gated", M_BOOL, 'D', NULL},
    {T "p/pt/gliss", "glissando", M_BOOL, 'D', NULL},
    {T "a/unison/en", "unison", M_BOOL, 'D', NULL},
    {T "a/unison/max-stack", "uni_count", M_LABEL, 'G', NULL},
    {T "a/unison/knobs/detune", "uni_detune", M_UNIT, 'G', NULL},
    {T "a/max-voices", "voices", M_LABEL, 'G', NULL},
    {T "a/mono", "voices", M_FLAG, 'G', "Mono"},
    {T "a/note-pri", "note_priority", M_ENUM, 'D', "low=Low|high=High|last=Last"},
    {T "a/note-steal", "steal_mode", M_ENUM, 'G', "off=No Stealing|lru=Oldest|quiet=Quietest|mru=Newest"},
    /* the arpeggiator */
    {T "p/arp/p/on", "arp_on", M_BOOL, 'D', NULL},
    {T "p/arp/p/direction", "arp_dir", M_ENUM, 'G', "u=Up|d=Down|ud=Up/Down|du=Down/Up"},
    {T "p/arp/p/pattern", "arp_pattern", M_ENUM, 'G', "u=Up|d=Down|o=Order|r=Random"},
    {T "p/arp/p/div", "arp_div", M_ENUM, 'D',
     "1/1=1/1|1/2=1/2|1/4=1/4|1/8=1/8|1/16=1/16|1/32=1/32|1/64=1/64|1/128=1/128"},
    {T "p/arp/p/octave", "arp_oct", M_LABEL, 'D', NULL},
    {T "p/arp/p/gate_length", "arp_gate", M_UNIT, 'G', NULL},
    {T "p/arp/p/sync", "arp_sync", M_BOOL, 'D', NULL},
    /* the Synth's effect, and the two master effects (FX 1 and FX 2) */
    {T "p/fx/ins/en", "modfx_on", M_BOOL, 'D', NULL},
    {T "p/fx/ins/type", "modfx_type", M_ENUM, 'D', "none=!modfx_on|" FX_TYPES},
    {T "p/fx/ins/mix", "modfx_mix", M_UNIT, 'G', NULL},
    {"/p/part/las-fx/1/en", "dly_on", M_BOOL, 'G', NULL},
    {"/p/part/las-fx/1/type", "dly_type", M_ENUM, 'G', "none=!dly_on|" MASTER_TYPES},
    {"/p/part/las-fx/2/en", "rev_on", M_BOOL, 'G', NULL},
    {"/p/part/las-fx/2/type", "rev_type", M_ENUM, 'G', "none=!rev_on|" MASTER_TYPES},
};
#define M1P_ROWS ((int)(sizeof M1P_MAP / sizeof M1P_MAP[0]))

/* ---- the modulation matrix ("/p/part/t/#/p/modmatrix/entry/<n>/src", ctl, dst, src_a, ctl_a, tr, tr_a, mute) ----
 * A destination is a key path of the file: it is turned into ours through M1P_MAP, then into the name our matrix
 * gives it here; a few destinations Moo Won has only as a matrix destination are named directly. A row's SOURCE is
 * src; with no src ("-"), its CONTROLLER (ctl) is the source. With both, ctl becomes our VIA. All of it is 'G'. */
typedef struct { const char *key, *label; } m1p_pair_t;
static const m1p_pair_t M1P_DST[] = {   /* our key -> our matrix destination ('@' the same number in both) */
    {"pitch_lfo", "Pitch LFO1 Amt"}, {"pitch_env", "Pitch EG Amt"}, {"angle_lfo3", "Angle LFO3 Amt"},
    {"pw_lfo3", "PW LFO3 Amt"}, {"flt_cut", "Ladder Cutoff"}, {"flt_res", "Ladder Res"}, {"svf_cut", "SVF Cutoff"},
    {"svf_res", "SVF Res"}, {"lad_env", "Ladder EG Amt"}, {"svf_env", "SVF EG Amt"}, {"lad_fm", "Ladder FM Amt"},
    {"svf_fm", "SVF FM Amt"}, {"flt_mix", "Filter Mix"}, {"volume", "Amp Level"}, {"pan", "Pan"}, {"spread", "Spread"}, {"fm_amt", "FM Amount"},
    {"o@_freq", "Osc @ Pitch"}, {"o@_beat", "Osc @ Beat"}, {"o@_level", "Osc @ Level"}, {"o@_angle", "Osc @ Angle"},
    {"o@_pw", "Osc @ PW"}, {"o@_mix", "Osc @ Wave Mix"}, {"noise_level", "Noise Level"}, {"ring_level", "Ring Level"},
    {"noise_color_mix", "Noise Color Mix"}, {"lfo@_rate", "LFO @ Rate"}, {"lfo@_var", "LFO @ Variation"},
    {"fenv_a", "Filter Env Attack"}, {"fenv_d", "Filter Env Decay"}, {"fenv_s", "Filter Env Sustain"},
    {"fenv_r", "Filter Env Release"}, {"amp_a", "Amp Env Attack"}, {"amp_d", "Amp Env Decay"},
    {"amp_s", "Amp Env Sustain"}, {"amp_r", "Amp Env Release"}, {"menv_a", "Mod Env Attack"},
    {"menv_d", "Mod Env Decay"}, {"menv_s", "Mod Env Sustain"}, {"menv_r", "Mod Env Release"},
    {"modfx_mix", "Synth FX Mix"}, {"mfx_send1", "Send 1 (FX1)"}, {"mfx_send2", "Send 2 (FX2)"}};
static const m1p_pair_t M1P_DST_ONLY[] = {   /* a file key -> our matrix destination, where there is no param to map */
    {T "p/po", "Pitch"}, {T "p/fa/2/p", "SVF Spacing"},
    {T "p/e1/tscale", "Filter Env Time Scale"}, {T "p/e2/tscale", "Amp Env Time Scale"},
    {T "p/e3/tscale", "Mod Env Time Scale"}};
static const m1p_pair_t M1P_SRC[] = {   /* a source or controller name in the file -> ours */
    {"MOD WHEEL", "Mod Wheel"}, {"VELOCITY", "Velocity"}, {"AFTERTOUCH", "Aftertouch"}, {"CONSTANT", "Constant"},
    {"PAD X", "Pad X (CC 2)"}, {"PAD Y", "Pad Y (CC 3)"}, {"LFO 1", "LFO 1"}, {"LFO 2", "LFO 2"}, {"LFO 3", "LFO 3"},
    {"LFO 4", "LFO 4"}, {"KEY PITCH", "Keyboard"}, {"FILTER EG", "Filter Env"}, {"AMPLIFIER EG", "Amp Env"},
    {"MODULATION EG", "Mod Env"}, {"NOISE EG", "Noise EG"}, {"PITCH WHEEL", "Pitch Wheel"}, {"RIBBON", "Ribbon"},
    {"SUSTAIN", "Sustain"}, {"GATE", "KB Gate"}};
static const m1p_pair_t M1P_XFORM[] = {
    {"SQUARED", "Squared"}, {"CUBED", "Cubed"}, {"LIMIT LOW", "Limit Low"}, {"LIMIT HIGH", "Limit High"}};
#define M1P_N(a) ((int)(sizeof a / sizeof a[0]))
#undef T
