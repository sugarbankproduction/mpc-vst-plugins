"""Factory bank B (2026-10-30): 128 original sounds made for ONE-ish, played from an MPC Key 37 (aftertouch, mod wheel,
pitch wheel, the pads' X / Y and the ribbon). Pads, strings, bells, effects, arpeggios, step sequences, drums, three-Synth
performances and generative sounds that play themselves. Not copies of any manufacturer's factory presets.

How the sounds use the controllers (most of them, through matrix rows 2 and up; row 1 stays the mod wheel vibrato
unless a sound says otherwise): the MOD WHEEL opens or moves something (vibrato, an LFO, a sweep), AFTERTOUCH opens the
filter or swells the level, VELOCITY the brightness, PAD X and PAD Y the synth effect and the reverb send, the RIBBON a
filter or pitch. Single sounds switch Synths 2 and 3 off themselves.

The last part, TRIM, evens the loudness out: a few dB per sound, measured by test/bank_check.c (bank_b_checks), applied
to every Synth's VOLUME that is on."""
from make_presets import (amp, chorus, delay, env_div, fenv, fxdiv, fxrate, hz, lfo, lfo_div, logk, menv, ms, reverb,
                          synth)

MW, AT, VEL, KBD = "Mod Wheel", "Aftertouch", "Velocity", "Keyboard"
PX, PY, RIB = "Pad X (CC 2)", "Pad Y (CC 3)", "Ribbon"
OFF23 = {"s2_syn_on": "Off", "s3_syn_on": "Off"}


def rows(*rs, start=2):
    """matrix rows from `start` on: (source, destination, amount[, {extra keys without the modN_ prefix}])"""
    d = {}
    for i, r in enumerate(rs):
        n = start + i
        d.update({"mod%d_src" % n: r[0], "mod%d_dst" % n: r[1], "mod%d_amt" % n: r[2]})
        if len(r) > 3:
            d.update({"mod%d_%s" % (n, k): v for k, v in r[3].items()})
    return d


def vib(rate=5.2, amt=4):   # row 1, the mod wheel's vibrato (the default row), at this rate and depth
    return {"lfo1_rate": lfo(rate), "lfo1_wave": "Sine", "mod1_amt": amt}


def space(send2, sec=None, **kw):   # the reverb: reverb() with hall decays by default
    kw.setdefault("rev_type", "Hall")
    return reverb(send2, sec, **kw)


def stage(**kw):   # a single sound: Synths 2 and 3 off
    return {**kw, **OFF23}


def tags(cat, mood, kind="Single"):
    return {"type": kind, "category": cat, "mood": mood}


B = []   # (name, values, tags)


def add(name, values, cat, mood, kind="Single"):
    if kind == "Single":
        values = {**values, **OFF23}
    B.append((name, values, tags(cat, mood, kind)))


# ---------------------------------------------------------------- PAD (28)
add("Velvet Horizon", {**amp(900, 2000, 85, 2200), **fenv(1400, 3000, 55, 2200),
                       "o2_level": 85, "o2_freq": 0.08, "o3_level": 70, "o3_freq": -0.09, "o3_oct": "16'",
                       "flt_cut": hz(650), "flt_res": 18, "lad_env": 25, "lad_kbd": 35, "voices": "8", "spread": 70,
                       "drift": 45, **vib(4.8, 3),
                       **rows((AT, "Ladder Cutoff", 30), (MW, "Ladder LFO2 Amt", 25), (PX, "Synth FX Mix", 45),
                              (PY, "Send 2 (FX2)", 35), (VEL, "Ladder Cutoff", 10)),
                       "lfo2_rate": lfo(0.18), "lfo2_wave": "Sine", "ribbon_dst": "Ladder Cutoff", "ribbon_amt": 30,
                       **chorus(0.22, 9.5, 40, 35), **space(32, 4.2)}, "Pad", "Warm")
add("Slow Tide", {**amp(1500, 2500, 90, 3000), **fenv(2500, 4000, 40, 3000),
                  "o1_wave": "Tri", "o1_angle": 40, "o2_level": 80, "o2_freq": 0.06, "o2_mix": 70,
                  "angle_lfo3": 55, "pw_lfo3": 40, "lfo3_rate": lfo(0.09), "lfo3_wave": "Sine",
                  "o1_svf": "On", "o2_svf": "On", "svf_mode": "BP", "svf_cut": hz(800), "svf_res": 40,
                  "flt_cut": hz(1200), "flt_res": 15, "flt_mix": 55, "lad_env": 20,
                  "lfo4_wave": "Noise", "lfo4_rate": lfo(0.3), "lfo4_smooth": 60,
                  **rows(("LFO 4", "SVF Cutoff", 30), (MW, "LFO 3 Rate (st)", 60), (AT, "SVF Res", 25),
                         (PX, "Synth FX Depth", 40), (PY, "Send 2 (FX2)", 30)),
                  "voices": "8", "spread": 80, "drift": 60, **chorus(0.15, 11, 45, 35),
                  **space(35, 5.5)}, "Pad", "Evolving")
add("Ember Choir", {**amp(700, 2000, 85, 1800), "o1_level": 30, "o1_oct": "16'", "mac_model": "Formant",
                    "mac_level": 85, "mac_harm": 40, "mac_timbre": 35, "mac_morph": 30, "flt_cut": hz(3200),
                    "flt_res": 10, "lfo2_rate": lfo(0.12), "lfo2_wave": "Triangle", "voices": "8", "spread": 60,
                    **vib(5.0, 3),
                    **rows(("LFO 2", "Macro Morph", 30), (AT, "Macro Timbre", 30), (PX, "Macro Harmonics", 30),
                           (PY, "Send 2 (FX2)", 30), (VEL, "Macro Timbre", 10)),
                    "ribbon_dst": "Macro Morph", "ribbon_amt": 40,
                    **chorus(0.3, 8, 35, 35), **space(35, 3.8)}, "Pad", "Warm")
add("Night Glass", {**amp(600, 1800, 80, 2000), **fenv(800, 2000, 30, 1500),
                    "o1_wave": "Tri", "o1_angle": 50, "o2_wave": "Tri", "o2_angle": 50, "o2_level": 80,
                    "o2_oct": "4'", "o2_freq": 0.05, "o1_svf": "On", "o1_lad": "Off", "o2_svf": "On", "o2_lad": "Off",
                    "svf_mode": "Notch", "svf_cut": hz(1100), "svf_res": 35, "svf_lfo": 35, "lfo2_rate": lfo(0.2),
                    "lfo2_wave": "Sine", "flt_mix": 0, "voices": "8", "spread": 50,
                    **rows((AT, "SVF Cutoff", 20), (MW, "SVF LFO2 Amt", 30), (PX, "Synth FX Mix", 40),
                           (PY, "Send 2 (FX2)", 30)),
                    "modfx_on": "On", "modfx_type": "12-Stage Phaser", "modfx_rate": fxrate(0.08), "modfx_depth": 60,
                    "modfx_fb": 30, "modfx_mix": 35, **space(35, 4.5, rev_tone=55)}, "Pad", "Dark")
add("Aurora Bloom", {**amp(1200, 2000, 70, 2500), **fenv(1500, 2500, 60, 2000),
                     "o2_level": 80, "o2_oct": "4'", "o2_freq": 0.04, "o3_level": 60, "o3_freq": 7.0,
                     "flt_cut": hz(1500), "flt_res": 12, "lad_env": 20, "voices": "8", "spread": 70,
                     **rows((AT, "Amp Level", 35), (AT, "Ladder Cutoff", 25), (MW, "Send 2 (FX2)", 30),
                            (PX, "Synth FX Mix", 40), (PY, "Osc 3 Level", -50)),
                     **chorus(0.25, 9, 35, 30), **reverb(45, 5.0, rev_type="Shimmer", rev_size=70)}, "Pad", "Bright")
add("Deep Drone Field", {**amp(2500, 3000, 100, 4000), "o1_oct": "16'", "o2_level": 85, "o2_oct": "32'",
                         "o2_freq": 0.04, "noise_level": 25, "noise_color_pair": "Red+White", "noise_color_mix": 20,
                         "flt_cut": hz(240), "flt_res": 35, "flt_drive": 20, "lad_lfo": 35, "lfo2_rate": lfo(0.05),
                         "lfo2_wave": "Sine", "voices": "4", "drift": 80, "spread": 60, **vib(0.15, 2),
                         **rows((AT, "Ladder Cutoff", 30), (MW, "Ladder Res", 30), (PX, "Noise Level", 40),
                                (PY, "Send 2 (FX2)", 40)),
                         "ribbon_dst": "Ladder Cutoff", "ribbon_amt": 40,
                         **space(40, 7.0, rev_type="Space", rev_decay=80, rev_size=85, rev_tone=40)}, "Pad", "Dark")
add("Wavetable Drift", {**amp(800, 2000, 85, 2000), **fenv(1000, 2500, 50, 1800),
                        "o1_level": 0, "mac_model": "Wavetable", "mac_level": 85, "mac_harm": 55, "mac_timbre": 40,
                        "mac_morph": 30, "flt_cut": hz(2200), "flt_res": 20, "lad_env": 20,
                        "lfo2_rate": lfo(0.07), "lfo2_wave": "Triangle", "lfo3_rate": lfo(0.11), "lfo3_wave": "Sine",
                        **rows(("LFO 2", "Macro Morph", 40), ("LFO 3", "Macro Timbre", 25), (AT, "Ladder Cutoff", 30),
                               (MW, "Macro Harmonics", 35), (PX, "Synth FX Mix", 40), (PY, "Send 2 (FX2)", 30)),
                        "voices": "8", "spread": 60, **chorus(0.3, 7.5, 35, 30), **space(32, 4.0)}, "Pad", "Evolving")
add("Cloud Chamber", {**amp(1500, 2500, 85, 3000), **fenv(1800, 3000, 45, 2500),
                      "o2_level": 80, "o2_freq": 0.07, "o3_level": 50, "o3_oct": "4'", "flt_cut": hz(1500),
                      "flt_res": 20, "lad_env": 25, "voices": "8", "spread": 70,
                      "modfx_on": "On", "modfx_type": "Granular", "modfx_pos": 35, "modfx_size": 70,
                      "modfx_density": 72, "modfx_texture": 55, "modfx_pitch": 7, "modfx_spread": 90, "modfx_fb": 30,
                      "modfx_room": 55, "modfx_mix": 50,
                      **rows((MW, "Synth FX Position", 40), (AT, "Synth FX Density", 25), (PX, "Synth FX Texture", 40),
                             (PY, "Synth FX Mix", 40), (VEL, "Ladder Cutoff", 10)),
                      **reverb(35, rev_type="Space", rev_decay=78, rev_size=80, rev_tone=50)}, "Pad", "Evolving")
add("Soft Focus", {**amp(500, 1500, 85, 1600), **fenv(600, 1500, 60, 1200),
                   "o1_wave": "Tri", "o1_angle": 50, "o2_wave": "Tri", "o2_angle": 50, "o2_level": 85, "o2_freq": 0.1,
                   "o3_level": 40, "o3_oct": "4'", "o3_wave": "Tri", "o3_angle": 50, "flt_cut": hz(1800),
                   "flt_slope": "12 dB", "voices": "8", "spread": 40, **vib(4.5, 3),
                   **rows((AT, "Ladder Cutoff", 25), (PX, "Synth FX Mix", 40), (PY, "Send 2 (FX2)", 30)),
                   **chorus(0.35, 7, 30, 30), **space(30, 3.0)}, "Pad", "Soft")
add("Pressure Bloom", {**amp(300, 2500, 45, 1800), **fenv(200, 2500, 40, 1500),
                       "o2_level": 85, "o2_freq": 0.09, "o2_wave": "Saw", "o3_level": 60, "o3_oct": "16'",
                       "o3_mix": 100, "o3_pw": 40, "flt_cut": hz(500), "flt_res": 25, "lad_env": 30, "voices": "8",
                       "spread": 60, **vib(5.0, 3),
                       **rows((AT, "Amp Level", 45, {"xf": "Squared"}), (AT, "Ladder Cutoff", 45),
                              (MW, "Ladder Res", 25), (PX, "Synth FX Mix", 40), (PY, "Send 2 (FX2)", 30)),
                       **chorus(0.25, 9, 35, 30), **space(30, 3.5)}, "Pad", "Warm")
add("Analog Dusk", {**amp(600, 2000, 80, 1800), **fenv(900, 2500, 40, 1600),
                    "o1_mix": 100, "o1_pw": 35, "o2_mix": 100, "o2_pw": 60, "o2_level": 85, "o2_freq": 0.07,
                    "o3_level": 55, "o3_oct": "16'", "flt_cut": hz(800), "flt_res": 30, "flt_drive": 20,
                    "lad_env": 30, "lad_kbd": 40, "lfo3_rate": lfo(0.4), "lfo3_wave": "Triangle", "pw_lfo3": 35,
                    "an_cut": 55, "an_env": 60, "an_lvl": 50, "an_tune": 45, "an_res": 40, "an_ladder": 70,
                    "voices": "8", "spread": 60, "drift": 55,
                    **rows((AT, "Ladder Cutoff", 30), (MW, "PW LFO3 Amt", 40), (PX, "Synth FX Mix", 45),
                           (PY, "Send 2 (FX2)", 30), (VEL, "Ladder Cutoff", 12)),
                    **chorus(0.28, 9, 30, 30), **space(28, 3.2, rev_type="Plate")}, "Pad", "Warm")
add("Frozen Lake", {**amp(900, 2000, 85, 2500), "o1_wave": "Tri", "o1_angle": 30, "o2_level": 70, "o2_oct": "4'",
                    "o2_freq": 0.05, "noise_level": 20, "noise_color_pair": "White+Purple", "noise_color_mix": 60,
                    "o1_svf": "On", "o2_svf": "On", "noise_svf": "On", "svf_mode": "HP", "svf_cut": hz(350),
                    "svf_res": 20, "flt_route": "Series", "flt_cut": hz(6000), "flt_res": 10, "voices": "8",
                    "spread": 80,
                    **rows((AT, "Ladder Cutoff", 25), (MW, "Noise Level", 40), (PX, "Synth FX Mix", 40),
                           (PY, "SVF Cutoff", 30)),
                    **chorus(0.2, 10, 40, 30), **reverb(40, 5.5, rev_type="Shimmer", rev_size=65)}, "Pad", "Clean")
add("Rust Belt", {**amp(400, 1500, 85, 1500), **fenv(300, 1500, 45, 1200),
                  "o2_level": 90, "o2_freq": -0.12, "o3_level": 70, "o3_oct": "16'", "flt_cut": hz(900),
                  "flt_res": 35, "flt_drive": 65, "lad_env": 25, "voices": "6", "spread": 50,
                  "modfx_on": "On", "modfx_type": "Bit Crusher", "modfx_bits": 9, "modfx_srate": logk(14000, 128, 44100),
                  "modfx_mix": 35,
                  **rows((AT, "Ladder Drive", 30), (MW, "Synth FX Mix", 50), (PX, "Synth FX Bit Depth", -30),
                         (PY, "Send 2 (FX2)", 30), (VEL, "Ladder Cutoff", 15)),
                  **delay("1/4.", 30, 12, True), **space(22, 2.5, rev_type="Room")}, "Pad", "Dirty")
add("Hollow Moon", {**amp(1000, 2500, 85, 2500), "o1_wave": "Tri", "o1_angle": 50, "o2_freq": 3.0, "o2_level": 0,
                    "o2_wave": "Tri", "o2_angle": 50, "ring_level": 70, "ring_src": "1 x 2", "o1_level": 70,
                    "flt_cut": hz(900), "flt_res": 20, "lfo2_rate": lfo(0.1), "lfo2_wave": "Sine", "lad_lfo": 20,
                    "voices": "6", "spread": 60, "drift": 40,
                    **rows((AT, "Ring Level", 30), (MW, "Osc 2 Pitch", 10), (PX, "Synth FX Mix", 40),
                           (PY, "Send 2 (FX2)", 30)),
                    **chorus(0.18, 10, 40, 35), **space(40, 6.0)}, "Pad", "Dark")
add("Breathing Room", {**amp(400, 1500, 90, 1800), "fenv_a": ms(1800), "fenv_d": ms(2200), "fenv_s": 0,
                       "fenv_r": ms(1500), "fenv_loop": "On",
                       "o2_level": 85, "o2_freq": 0.08, "o3_level": 60, "o3_oct": "16'", "flt_cut": hz(450),
                       "flt_res": 30, "lad_env": 40, "voices": "8", "spread": 60,
                       **rows((MW, "Filter Env Time Scale", -40), (AT, "Ladder Cutoff", 30), (PX, "Synth FX Mix", 40),
                              (PY, "Send 2 (FX2)", 30)),
                       **chorus(0.25, 8.5, 35, 30), **space(30, 3.5)}, "Pad", "Evolving")
add("Golden Hour", {**amp(500, 1500, 85, 1500), **fenv(700, 1800, 60, 1400),
                    "o2_level": 85, "o2_freq": 0.1, "o3_level": 60, "o3_oct": "4'", "o3_freq": -0.08,
                    "flt_cut": hz(2500), "flt_res": 15, "lad_env": 20, "voices": "8", "spread": 70,
                    "modfx_on": "On", "modfx_type": "6-Stage Phaser", "modfx_rate": fxrate(0.15), "modfx_depth": 60,
                    "modfx_fb": 35, "modfx_mix": 40, **vib(5.0, 3),
                    **rows((AT, "Ladder Cutoff", 20), (MW, "Synth FX Rate", 30), (PX, "Synth FX Feedback", 30),
                           (PY, "Send 2 (FX2)", 30), (VEL, "Ladder Cutoff", 10)),
                    **space(28, 3.0)}, "Pad", "Bright")
add("Undertow", {**amp(1800, 3000, 95, 3500), **fenv(2000, 3000, 50, 3000),
                 "o1_oct": "16'", "o2_level": 85, "o2_freq": 0.13, "o2_oct": "16'", "o3_level": 70, "o3_oct": "32'",
                 "o3_wave": "Tri", "o3_angle": 50, "flt_cut": hz(300), "flt_res": 30, "lad_env": 25,
                 "lfo4_wave": "Noise", "lfo4_rate": lfo(0.15), "lfo4_smooth": 80, "voices": "4", "spread": 90,
                 "drift": 70,
                 **rows(("LFO 4", "Pitch", 1), ("LFO 4", "Ladder Cutoff", 10), (AT, "Ladder Cutoff", 30),
                        (MW, "Ladder Res", 30), (PY, "Send 2 (FX2)", 40)),
                 **space(35, 6.5, rev_tone=45)}, "Pad", "Dark")
add("Halo Swell", {**amp(1400, 2500, 80, 2500), **menv(2500, 3000, 70, 2500),
                   "o1_wave": "Tri", "o1_angle": 40, "o2_level": 70, "o2_freq": 0.06, "o2_oct": "4'",
                   "o2_wave": "Tri", "o2_angle": 45, "flt_cut": hz(2000), "voices": "8", "spread": 60,
                   **rows(("Mod Env", "Send 2 (FX2)", 25), (AT, "Ladder Cutoff", 20), (MW, "Osc 2 Level", -40),
                          (PX, "Synth FX Mix", 40), (PY, "Ladder Cutoff", 20)),
                   **chorus(0.3, 8, 30, 30), **space(25, 4.5)}, "Pad", "Soft")
add("Prism Pad", {**amp(600, 1500, 85, 1600), **fenv(500, 1500, 55, 1200),
                  "o2_level": 70, "o2_freq": 7.0, "o3_level": 55, "o3_oct": "4'", "o3_freq": 0.05,
                  "flt_cut": hz(2800), "flt_res": 15, "lad_env": 15, "voices": "8", "spread": 70,
                  **rows((AT, "Ladder Cutoff", 20), (MW, "Osc 2 Level", -50), (PX, "Synth FX Mix", 40),
                         (PY, "Send 2 (FX2)", 30), (VEL, "Ladder Cutoff", 10)),
                  **chorus(0.3, 8.5, 35, 30), **space(28, 3.0)}, "Pad", "Bright")
add("Tape Memory", {**amp(500, 1500, 85, 1500), **fenv(600, 1800, 50, 1200),
                    "o1_mix": 60, "o2_level": 80, "o2_freq": 0.06, "flt_cut": hz(1400), "flt_res": 15, "lad_env": 20,
                    "voices": "8", "spread": 40, "drift": 70,
                    "modfx_on": "On", "modfx_type": "Tape Color", "modfx_drive": 55, "modfx_bump": 55,
                    "modfx_flutter": 45, "modfx_tone": 45, "modfx_mix": 100,
                    **rows((MW, "Synth FX Flutter", 40), (AT, "Ladder Cutoff", 25), (PX, "Synth FX Drive", 30),
                           (PY, "Send 2 (FX2)", 30)),
                    **delay("1/4", 25, 12, True), **space(22, 2.0, rev_type="Room")}, "Pad", "Dirty")
add("Polar Night", {**amp(2000, 3000, 95, 4500), **fenv(3000, 4000, 40, 3500),
                    "o1_oct": "16'", "o2_level": 80, "o2_freq": 0.05, "flt_cut": hz(200), "flt_res": 55,
                    "lad_env": 25, "lad_lfo": 25, "lfo2_rate": lfo(0.04), "lfo2_wave": "Triangle", "voices": "6",
                    "spread": 70, "drift": 60,
                    **rows((AT, "Ladder Cutoff", 30), (MW, "Ladder LFO2 Amt", 30), (PX, "Ladder Res", 20),
                           (PY, "Send 2 (FX2)", 30)),
                    "ribbon_dst": "Ladder Cutoff", "ribbon_amt": 40,
                    **reverb(40, rev_type="Blackhole", rev_decay=55, rev_size=60, rev_tone=40)}, "Pad", "Dark")
add("Glass Harmonica Pad", {**amp(700, 2000, 80, 2200), "o1_level": 0, "mac_model": "Additive", "mac_level": 85,
                            "mac_harm": 35, "mac_timbre": 50, "mac_morph": 40, "flt_cut": hz(5000), "voices": "8",
                            "spread": 50, "lfo2_rate": lfo(0.2), "lfo2_wave": "Sine", **vib(5.5, 2),
                            **rows(("LFO 2", "Macro Timbre", 15), (AT, "Macro Harmonics", 30), (MW, "Macro Morph", 40),
                                   (PX, "Synth FX Mix", 40), (PY, "Send 2 (FX2)", 30)),
                            **chorus(0.3, 7, 25, 25), **space(32, 4.0)}, "Pad", "Clean")
add("Moving Stillness", {**amp(1000, 2000, 85, 2200), **fenv(1200, 2500, 50, 2000),
                         "o1_mix": 100, "o1_pw": 50, "o2_mix": 100, "o2_level": 80, "o2_freq": 0.05,
                         "lfo3_rate": lfo(0.25), "lfo3_wave": "Sine", "pw_lfo3": 50,
                         "o1_svf": "On", "o2_svf": "On", "svf_mode": "BP", "svf_pair": "Parallel", "svf_space": 30,
                         "svf_cut": hz(1000), "svf_res": 45, "flt_mix": 50, "flt_cut": hz(1300), "lad_env": 20,
                         "lfo4_wave": "S&H", "lfo4_rate": lfo(0.5), "lfo4_smooth": 70, "voices": "8", "spread": 70,
                         **rows(("LFO 4", "SVF Spacing", 40), (MW, "LFO 4 Rate (st)", 50), (AT, "SVF Cutoff", 30),
                                (PX, "Filter Mix", -40), (PY, "Send 2 (FX2)", 30)),
                         **chorus(0.2, 9, 35, 30), **space(30, 4.0)}, "Pad", "Evolving")
add("Sync Haze", {**amp(800, 2000, 85, 2000), **menv(2500, 4000, 30, 2000),
                  "o1_level": 0, "o2_level": 90, "o2_sync": "On", "o2_freq": 2.0, "o3_level": 60, "o3_freq": 0.08,
                  "o3_oct": "16'", "flt_cut": hz(1800), "flt_res": 15, "voices": "6", "spread": 60,
                  **rows(("Mod Env", "Osc 2 Pitch", 25), (MW, "Osc 2 Pitch", 20), (AT, "Ladder Cutoff", 25),
                         (PX, "Synth FX Mix", 40), (PY, "Send 2 (FX2)", 30)),
                  "ribbon_dst": "Osc 2 Pitch", "ribbon_amt": 30,
                  **chorus(0.25, 9, 40, 35), **space(30, 3.8)}, "Pad", "Evolving")
add("Velour", {**amp(600, 1800, 85, 1800), **fenv(800, 2000, 50, 1600),
               "o2_level": 80, "o2_freq": 0.06, "flt_cut": hz(900), "flt_res": 20, "lad_env": 25, "lad_kbd": 30,
               "voices": "4", "unison": "On", "uni_count": "3", "uni_detune": 30, "spread": 80, "drift": 40,
               **vib(5.0, 3),
               **rows((AT, "Ladder Cutoff", 30), (PX, "Synth FX Mix", 40), (PY, "Send 2 (FX2)", 30),
                      (VEL, "Ladder Cutoff", 10)),
               **chorus(0.25, 9, 30, 30), **space(30, 3.5)}, "Pad", "Warm")
add("Distant Signal", {**amp(1200, 2500, 85, 2500), **menv(3000, 3000, 40, 2500),
                       "o1_wave": "Tri", "o1_angle": 50, "o1_level": 0, "o1_oct": "4'", "o1_freq": 0.03,
                       "o2_wave": "Tri", "o2_angle": 50, "o2_level": 90, "fm_amt": 20, "fm_route": "1 > 2",
                       "o3_level": 40, "o3_oct": "16'", "o3_wave": "Tri", "o3_angle": 50, "flt_cut": hz(3000),
                       "voices": "8", "spread": 60,
                       **rows(("Mod Env", "FM Amount", 20), (MW, "FM Amount", 30), (AT, "Ladder Cutoff", 20),
                              (PX, "Synth FX Mix", 40), (PY, "Send 2 (FX2)", 35)),
                       **delay("1/4.", 35, 15, True), **space(30, 5.0)}, "Pad", "Evolving")
add("Low Cathedral", {**amp(400, 1500, 95, 3000), "o1_wave": "Tri", "o1_angle": 50, "o1_oct": "16'",
                      "o2_wave": "Tri", "o2_angle": 50, "o2_level": 70, "o2_oct": "8'", "o3_level": 50,
                      "o3_oct": "4'", "o3_wave": "Tri", "o3_angle": 50, "flt_cut": hz(1500), "voices": "8",
                      "spread": 40, **vib(6.0, 2),
                      **rows((AT, "Osc 3 Level", 40), (MW, "Synth FX Rate", 40), (PX, "Synth FX Mix", 40),
                             (PY, "Send 2 (FX2)", 30)),
                      "modfx_on": "On", "modfx_type": "Stereo Chorus", "modfx_rate": fxrate(0.8), "modfx_depth": 40,
                      "modfx_mix": 35, **space(40, 6.0, rev_size=80)}, "Pad", "Dark")
add("Feather Bed", {**amp(1200, 2500, 85, 2500), "o1_wave": "Tri", "o1_angle": 50, "o1_level": 70,
                    "noise_level": 35, "noise_color_pair": "Red+White", "noise_color_mix": 70, "flt_cut": hz(1400),
                    "flt_slope": "12 dB", "flt_res": 15, "voices": "8", "spread": 70,
                    **rows((AT, "Ladder Cutoff", 20), (MW, "Noise Level", -40), (PX, "Synth FX Mix", 40),
                           (PY, "Send 2 (FX2)", 30)),
                    **chorus(0.2, 10, 35, 30), **space(35, 4.5)}, "Pad", "Soft")

# ---------------------------------------------------------------- STRINGS (16)
STR_VIB = {"lfo1_delay": ms(300), "lfo1_fade_in": ms(600), "lfo1_reset": "On", "pitch_lfo": 1, "lfo1_rate": lfo(5.4),
           "lfo1_wave": "Sine"}   # a delayed vibrato that comes in after each bow
add("Silk Section", {**amp(350, 1500, 85, 1200), **fenv(400, 1500, 60, 1000),
                     "o1_mix": 100, "o1_pw": 40, "o2_level": 85, "o2_freq": 0.09, "o3_level": 60, "o3_freq": -0.08,
                     "flt_cut": hz(2600), "flt_res": 5, "lad_env": 15, "voices": "8", **STR_VIB,
                     **rows((AT, "Ladder Cutoff", 20), (AT, "Pitch LFO1 Amt", 2), (PX, "Synth FX Mix", 30),
                            (PY, "Send 2 (FX2)", 30), (VEL, "Amp Env Attack", -20)),
                     "modfx_on": "On", "modfx_type": "Ensemble", "modfx_rate": fxrate(0.6), "modfx_time": 45,
                     "modfx_depth": 50, "modfx_vdepth": 25, "modfx_spread": 80, "modfx_mix": 50,
                     **space(30, 3.2, rev_size=65, rev_pre=25)}, "Strings", "Warm")
add("Chamber Bows", {**amp(250, 1200, 85, 900), "o2_level": 85, "o2_freq": 0.11,
                     "o1_svf": "On", "o2_svf": "On", "svf_mode": "BP", "svf_cut": hz(1100), "svf_res": 30,
                     "flt_route": "Series", "flt_cut": hz(4000), "flt_res": 5, "voices": "6", **STR_VIB,
                     **rows((AT, "SVF Cutoff", 20), (AT, "Amp Level", 15), (MW, "Pitch LFO1 Amt", 3),
                            (PX, "SVF Res", 30), (PY, "Send 2 (FX2)", 30)),
                     **chorus(0.4, 7, 25, 25), **space(28, 2.2, rev_type="Room")}, "Strings", "Soft")
add("Tremolo Violas", {**amp(150, 1200, 85, 700), "o1_mix": 100, "o1_pw": 45, "o2_level": 85, "o2_freq": 0.1,
                       "flt_cut": hz(3200), "flt_res": 5, "voices": "8", "lfo2_rate": lfo(7.0), "lfo2_wave": "Sine",
                       "mod1_src": "LFO 2", "mod1_dst": "Amp Level", "mod1_amt": -45, "mod1_via": "Mod Wheel",
                       **rows((AT, "Ladder Cutoff", 20), (PX, "LFO 2 Rate", 30), (PY, "Send 2 (FX2)", 30),
                              (VEL, "Ladder Cutoff", 10)),
                       "modfx_on": "On", "modfx_type": "Ensemble", "modfx_rate": fxrate(0.5), "modfx_time": 45,
                       "modfx_depth": 45, "modfx_vdepth": 20, "modfx_spread": 70, "modfx_mix": 45,
                       **space(28, 2.8)}, "Strings", "Bright")
add("Cello Ensemble", {**amp(300, 1500, 85, 1000), **fenv(300, 1200, 60, 900),
                       "o1_oct": "16'", "o1_mix": 100, "o1_pw": 42, "o2_level": 85, "o2_freq": 0.08, "o2_oct": "16'",
                       "flt_cut": hz(1300), "flt_res": 10, "lad_env": 15, "lad_kbd": 40, "voices": "6", **STR_VIB,
                       **rows((AT, "Ladder Cutoff", 25), (AT, "Pitch LFO1 Amt", 2), (PX, "Synth FX Mix", 30),
                              (PY, "Send 2 (FX2)", 30)),
                       "modfx_on": "On", "modfx_type": "Ensemble", "modfx_rate": fxrate(0.5), "modfx_time": 50,
                       "modfx_depth": 45, "modfx_vdepth": 20, "modfx_spread": 60, "modfx_mix": 40,
                       **space(28, 3.0, rev_tone=60)}, "Strings", "Dark")
add("Pizzicato Garden", {**amp(1, 450, 0, 350), **fenv(1, 250, 0, 200),
                         "o2_level": 70, "o2_freq": 0.08, "flt_cut": hz(700), "flt_res": 15, "lad_env": 45,
                         "lad_kbd": 50, "voices": "8",
                         **rows((VEL, "Ladder Cutoff", 20), (MW, "Amp Env Decay", 30), (AT, "Ladder Cutoff", 15),
                                (PY, "Send 2 (FX2)", 30)),
                         **space(28, 2.0, rev_type="Room")}, "Strings", "Clean")
add("Orbit Strings", {**amp(400, 1500, 85, 1300), "o1_level": 45, "o1_mix": 100, "o1_pw": 40,
                      "mac_model": "String Machine", "mac_level": 100,
                      "mac_harm": 45, "mac_timbre": 55, "mac_morph": 40, "flt_cut": hz(4000), "flt_res": 5,
                      "voices": "8", **vib(5.0, 3),
                      **rows((AT, "Macro Timbre", 25), (PX, "Macro Morph", 30), (PY, "Send 2 (FX2)", 30),
                             (VEL, "Macro Timbre", 10)),
                      "ribbon_dst": "Ladder Cutoff", "ribbon_amt": 35,
                      **chorus(0.5, 7, 30, 30), **space(30, 3.2)}, "Strings", "Warm")
add("Solo Fiddle", {**amp(80, 800, 90, 400), **fenv(100, 600, 60, 300),
                    "o1_mix": 100, "o1_pw": 45, "o2_level": 50, "o2_freq": 0.05, "o1_svf": "On", "o2_svf": "On",
                    "svf_mode": "BP", "svf_cut": hz(1600), "svf_res": 25, "flt_route": "Series", "flt_cut": hz(5000),
                    "lad_env": 10, "voices": "Mono", "glide": "On", "glide_time": ms(60), "glide_legato": "On",
                    **STR_VIB,
                    **rows((AT, "Pitch LFO1 Amt", 3), (AT, "SVF Cutoff", 15), (MW, "Pitch LFO1 Amt", 2),
                           (PX, "SVF Res", 30), (PY, "Send 2 (FX2)", 30)),
                    "ribbon_dst": "Pitch", "ribbon_amt": 8,
                    **space(25, 2.2, rev_type="Room")}, "Strings", "Bright")
add("Swell Strings", {**amp(200, 2000, 40, 1200), "o1_mix": 100, "o1_pw": 42, "o2_level": 85, "o2_freq": 0.1,
                      "o3_level": 50, "o3_oct": "16'", "flt_cut": hz(900), "flt_res": 10, "voices": "8", **STR_VIB,
                      **rows((AT, "Amp Level", 50, {"xf": "Squared"}), (AT, "Ladder Cutoff", 40),
                             (MW, "Pitch LFO1 Amt", 2), (PX, "Synth FX Mix", 30), (PY, "Send 2 (FX2)", 30)),
                      "modfx_on": "On", "modfx_type": "Ensemble", "modfx_rate": fxrate(0.6), "modfx_time": 45,
                      "modfx_depth": 50, "modfx_vdepth": 25, "modfx_spread": 80, "modfx_mix": 45,
                      **space(30, 3.5)}, "Strings", "Warm")
add("Frost Strings", {**amp(400, 1500, 85, 1500), "o1_oct": "4'", "o1_mix": 100, "o1_pw": 40, "o2_level": 80,
                      "o2_freq": 0.09, "o2_oct": "4'", "flt_cut": hz(5000), "flt_res": 5,
                      "o1_svf": "On", "o2_svf": "On", "svf_mode": "HP", "svf_cut": hz(500), "flt_route": "Series",
                      "voices": "8", **STR_VIB,
                      **rows((AT, "Ladder Cutoff", 15), (PX, "Synth FX Mix", 30), (PY, "Send 2 (FX2)", 20),
                             (MW, "SVF Cutoff", 25)),
                      "modfx_on": "On", "modfx_type": "Ensemble", "modfx_rate": fxrate(0.7), "modfx_time": 40,
                      "modfx_depth": 50, "modfx_vdepth": 25, "modfx_spread": 90, "modfx_mix": 50,
                      **reverb(35, 5.0, rev_type="Shimmer", rev_size=60)}, "Strings", "Bright")
add("Dark Ensemble", {**amp(500, 1500, 85, 1500), "o1_mix": 100, "o1_pw": 40, "o2_level": 85, "o2_freq": 0.1,
                      "o3_level": 60, "o3_oct": "16'", "flt_cut": hz(700), "flt_res": 15, "flt_slope": "18 dB",
                      "voices": "8", **STR_VIB,
                      **rows((AT, "Ladder Cutoff", 30), (MW, "Ladder Cutoff", 25), (PX, "Synth FX Mix", 30),
                             (PY, "Send 2 (FX2)", 30)),
                      "modfx_on": "On", "modfx_type": "Ensemble", "modfx_rate": fxrate(0.45), "modfx_time": 50,
                      "modfx_depth": 50, "modfx_vdepth": 20, "modfx_spread": 70, "modfx_mix": 45,
                      **space(32, 4.0, rev_tone=45)}, "Strings", "Dark")
add("Staccato Run", {**amp(8, 250, 0, 180), **fenv(5, 200, 20, 150),
                     "o1_mix": 100, "o1_pw": 45, "o2_level": 80, "o2_freq": 0.1, "flt_cut": hz(1800), "flt_res": 5,
                     "lad_env": 25, "voices": "8",
                     **rows((VEL, "Ladder Cutoff", 20), (MW, "Amp Env Decay", 40), (AT, "Ladder Cutoff", 20),
                            (PY, "Send 2 (FX2)", 30)),
                     "modfx_on": "On", "modfx_type": "Ensemble", "modfx_rate": fxrate(0.6), "modfx_time": 45,
                     "modfx_depth": 40, "modfx_vdepth": 20, "modfx_spread": 70, "modfx_mix": 40,
                     **space(25, 2.0, rev_type="Room")}, "Strings", "Clean")
add("Wide Orchestra", {**amp(450, 1800, 85, 1600), **fenv(500, 1500, 60, 1200),
                       "o1_mix": 100, "o1_pw": 42, "o2_level": 85, "o2_freq": 0.1, "flt_cut": hz(2000),
                       "flt_res": 5, "lad_env": 15, "voices": "4", "unison": "On", "uni_count": "3",
                       "uni_detune": 25, "spread": 100, **STR_VIB,
                       **rows((AT, "Ladder Cutoff", 25), (AT, "Amp Level", 15), (PX, "Synth FX Mix", 30),
                              (PY, "Send 2 (FX2)", 30)),
                       **chorus(0.3, 9, 30, 25), **space(35, 4.0, rev_size=75, rev_pre=30)}, "Strings", "Warm")
add("Glass Bows", {**amp(600, 2000, 80, 1800), "o1_mix": 100, "o1_pw": 30, "o2_level": 60, "o2_oct": "4'",
                   "o2_freq": 0.06, "ring_level": 25, "o1_svf": "On", "o2_svf": "On", "ring_svf": "On",
                   "svf_mode": "HP", "svf_cut": hz(700), "svf_res": 25, "flt_route": "Series", "flt_cut": hz(6000),
                   "voices": "8", **STR_VIB, "lfo3_rate": lfo(0.3), "lfo3_wave": "Sine", "pw_lfo3": 40,
                   **rows((AT, "Ring Level", 30), (MW, "SVF Cutoff", 25), (PX, "Synth FX Mix", 30),
                          (PY, "Send 2 (FX2)", 30)),
                   **chorus(0.25, 9, 35, 30), **space(35, 4.5)}, "Strings", "Evolving")
add("Bowed Metal", {**amp(500, 1500, 85, 1200), "o1_level": 0, "noise_level": 70, "noise_color_pair": "Red+White",
                    "noise_color_mix": 40, "flt_cut": hz(2500), "flt_res": 10, "voices": "6",
                    "modfx_on": "On", "modfx_type": "Modal Resonator", "modfx_model": "String", "modfx_struct": 55,
                    "modfx_tone": 50, "modfx_decay": 70, "modfx_pos": 40, "modfx_mix": 85, "modfx_gain": 28,
                    **rows((AT, "Noise Level", 30), (MW, "Synth FX Structure", 30), (PX, "Synth FX Position", 40),
                           (PY, "Send 2 (FX2)", 30)),
                    **space(30, 4.0)}, "Strings", "Evolving")
add("Quartet Air", {**amp(200, 1200, 85, 800), "o1_mix": 100, "o1_pw": 45, "o2_level": 70, "o2_freq": 0.13,
                    "flt_cut": hz(3500), "flt_res": 5, "voices": "4", **STR_VIB,
                    **rows((AT, "Ladder Cutoff", 20), (AT, "Pitch LFO1 Amt", 2), (PX, "Synth FX Mix", 30),
                           (PY, "Send 2 (FX2)", 30), (VEL, "Amp Env Attack", -25)),
                    **chorus(0.5, 6, 20, 20), **space(25, 1.8, rev_type="Room")}, "Strings", "Clean")
add("Grit Strings", {**amp(250, 1200, 85, 900), **fenv(200, 1000, 50, 700),
                     "o2_level": 85, "o2_freq": 0.12, "flt_cut": hz(1800), "flt_res": 25, "flt_drive": 60,
                     "lad_env": 20, "voices": "6", **STR_VIB,
                     "modfx_on": "On", "modfx_type": "6-Stage Phaser", "modfx_rate": fxrate(0.3), "modfx_depth": 55,
                     "modfx_fb": 40, "modfx_mix": 40,
                     **rows((AT, "Ladder Drive", 30), (MW, "Synth FX Rate", 40), (PX, "Synth FX Feedback", 30),
                            (PY, "Send 2 (FX2)", 25)),
                     **space(22, 2.5, rev_type="Plate")}, "Strings", "Dirty")

# ---------------------------------------------------------------- BELLS (12)
add("Crystal Tines", {**amp(1, 2200, 0, 1500), **menv(1, 1000, 0, 800),
                      "o1_wave": "Tri", "o1_angle": 50, "o1_level": 0, "o1_oct": "2'", "o1_freq": 1.5,
                      "o2_wave": "Tri", "o2_angle": 50, "o2_level": 100, "fm_amt": 35, "fm_route": "1 > 2", "flt_cut": hz(9000),
                      "voices": "8",
                      **rows(("Mod Env", "FM Amount", 30), (VEL, "FM Amount", 20), (MW, "FM Amount", 25),
                             (AT, "Pitch LFO1 Amt", 2), (PY, "Send 2 (FX2)", 30)),
                      "lfo1_rate": lfo(5.0), "lfo1_wave": "Sine",
                      **delay("1/8.", 25, 15, True), **space(30, 3.0)}, "Bells", "Bright")
add("Temple Gong", {**amp(5, 6000, 0, 4000), **fenv(1, 3000, 20, 3000),
                    "o1_oct": "16'", "o1_wave": "Tri", "o1_angle": 50, "o1_level": 30, "o2_oct": "16'",
                    "o2_freq": 4.3, "ring_level": 90, "ring_src": "1 x 2", "flt_cut": hz(1500), "flt_res": 15,
                    "lad_env": 30, "voices": "4",
                    **rows((VEL, "Ladder Cutoff", 20), (AT, "Ring Level", 20), (MW, "Osc 2 Pitch", 5),
                           (PY, "Send 2 (FX2)", 30)),
                    **space(35, 7.0, rev_tone=45)}, "Bells", "Dark")
add("Modal Chimes", {**amp(1, 3000, 0, 2000), "o1_level": 0, "mac_model": "Modal", "mac_level": 90,
                     "mac_harm": 55, "mac_timbre": 65, "mac_morph": 60, "flt_cut": hz(10000), "voices": "8",
                     **rows((VEL, "Macro Timbre", 20), (MW, "Macro Harmonics", 30), (AT, "Macro Morph", 20),
                            (PX, "Synth FX Mix", 40), (PY, "Send 2 (FX2)", 30)),
                     **chorus(0.4, 6, 20, 20), **space(30, 3.5)}, "Bells", "Clean")
add("Steel Drum Sun", {**amp(1, 700, 0, 500), **menv(1, 250, 0, 200),
                       "o1_wave": "Tri", "o1_angle": 50, "o1_level": 0, "o1_oct": "4'", "o1_freq": 3.0,
                       "o2_wave": "Tri", "o2_angle": 50, "o2_level": 100, "fm_amt": 30, "flt_cut": hz(7000), "voices": "8",
                       **rows(("Mod Env", "FM Amount", 30), (VEL, "FM Amount", 20), (MW, "Amp Env Decay", 30),
                              (PY, "Send 2 (FX2)", 30)),
                       **delay("1/8", 20, 12, True), **space(22, 1.6, rev_type="Room")}, "Bells", "Bright")
add("Celesta Box", {**amp(1, 1500, 0, 900), "o1_wave": "Tri", "o1_angle": 50, "o2_level": 45, "o2_oct": "2'",
                    "o2_wave": "Tri", "o2_angle": 50, "o2_freq": 0.04, "flt_cut": hz(5000), "flt_slope": "12 dB",
                    "voices": "8",
                    **rows((VEL, "Osc 2 Level", 30), (MW, "Amp Env Decay", 30), (AT, "Ladder Cutoff", 15),
                           (PY, "Send 2 (FX2)", 30)),
                    **space(28, 2.5)}, "Bells", "Soft")
add("Tubular Hall", {**amp(1, 5000, 0, 3500), **menv(1, 2500, 0, 2000),
                     "o1_wave": "Tri", "o1_angle": 50, "o1_level": 0, "o1_oct": "8'", "o1_freq": 4.0,
                     "o2_wave": "Tri", "o2_angle": 50, "o2_level": 100, "fm_amt": 22, "o3_level": 30, "o3_oct": "4'",
                     "o3_wave": "Tri", "o3_angle": 50, "o3_freq": 3.1, "flt_cut": hz(6000), "voices": "8",
                     **rows(("Mod Env", "FM Amount", 25), (VEL, "FM Amount", 15), (MW, "Osc 3 Level", 40),
                            (PY, "Send 2 (FX2)", 30)),
                     **space(35, 4.5, rev_size=80)}, "Bells", "Clean")
add("Ring Bowl", {**amp(30, 5000, 0, 4000), "o1_wave": "Tri", "o1_angle": 50, "o1_level": 30, "o2_freq": 3.3,
                  "o2_oct": "4'", "o2_wave": "Tri", "o2_angle": 50, "ring_level": 85, "flt_cut": hz(4000),
                  "lfo2_rate": lfo(0.7), "lfo2_wave": "Sine", "voices": "6",
                  **rows(("LFO 2", "Ring Level", -20), (MW, "LFO 2 Rate", 40), (AT, "Osc 2 Beat", 20),
                         (VEL, "Ring Level", 15), (PY, "Send 2 (FX2)", 30)),
                  **chorus(0.2, 10, 40, 30), **space(35, 5.0)}, "Bells", "Evolving")
add("Six-Op Glass", {**amp(1, 2500, 20, 1500), "o1_level": 0, "mac_model": "6-Op FM 2", "mac_level": 85,
                     "mac_harm": 20, "mac_timbre": 60, "mac_morph": 35, "flt_cut": hz(10000), "voices": "8",
                     **rows((VEL, "Macro Timbre", 25), (MW, "Macro Morph", 30), (AT, "Macro Timbre", 15),
                            (PX, "Synth FX Mix", 40), (PY, "Send 2 (FX2)", 30)),
                     **chorus(0.35, 7, 25, 25), **delay("1/4", 25, 12, True), **space(28, 2.8)}, "Bells", "Bright")
add("Dark Carillon", {**amp(1, 4500, 0, 3500), **menv(1, 2000, 0, 1500),
                      "o1_wave": "Tri", "o1_angle": 50, "o1_level": 0, "o1_oct": "8'", "o1_freq": 2.0,
                      "o2_wave": "Tri", "o2_angle": 50, "o2_level": 100, "o2_oct": "16'", "fm_amt": 30, "flt_cut": hz(2200),
                      "flt_res": 10, "voices": "6",
                      **rows(("Mod Env", "FM Amount", 30), (VEL, "Ladder Cutoff", 20), (MW, "FM Amount", 20),
                             (AT, "Ladder Cutoff", 15), (PY, "Send 2 (FX2)", 30)),
                      **space(38, 6.0, rev_tone=45)}, "Bells", "Dark")
add("Kalimba Rain", {**amp(1, 350, 0, 250), **fenv(1, 150, 0, 100),
                     "o1_mix": 100, "o2_level": 0, "flt_cut": hz(2500), "flt_res": 10, "lad_env": 25, "voices": "8",
                     "modfx_on": "On", "modfx_type": "Modal Resonator", "modfx_model": "Modal", "modfx_struct": 15,
                     "modfx_tone": 45, "modfx_decay": 45, "modfx_pos": 25, "modfx_mix": 75, "modfx_gain": 38,
                     **rows((VEL, "Ladder Cutoff", 20), (MW, "Synth FX Structure", 30), (PX, "Synth FX Position", 30),
                            (PY, "Send 2 (FX2)", 30)),
                     **delay("1/8.", 30, 15, True), **space(25, 2.2, rev_type="Room")}, "Bells", "Soft")
add("Clockwork Bells", {**amp(1, 900, 0, 600), **menv(1, 400, 0, 300),
                        "o1_wave": "Tri", "o1_angle": 50, "o1_level": 0, "o1_oct": "2'", "o1_freq": 0.0,
                        "o2_wave": "Tri", "o2_angle": 50, "o2_level": 100, "o2_oct": "4'", "fm_amt": 28, "flt_cut": hz(9000),
                        "voices": "8",
                        **rows(("Mod Env", "FM Amount", 25), (VEL, "FM Amount", 15), (MW, "Send 1 (FX1)", 30),
                               (PY, "Send 2 (FX2)", 25)),
                        **delay("1/16", 45, 25, True), **space(20, 1.8, rev_type="Plate"), "mfx_series": 30},
    "Bells", "Bright")
add("Broken Music Box", {**amp(1, 1600, 0, 900), "o1_wave": "Tri", "o1_angle": 50, "o2_level": 40, "o2_oct": "2'",
                         "o2_wave": "Tri", "o2_angle": 50, "o2_freq": 0.2, "flt_cut": hz(4500), "voices": "8",
                         "drift": 100, "pitch_var": 18,
                         "modfx_on": "On", "modfx_type": "Bit Crusher", "modfx_bits": 7,
                         "modfx_srate": logk(11000, 128, 44100), "modfx_mix": 50,
                         **rows((MW, "Synth FX Mix", 40), (AT, "Pitch", -2), (PX, "Synth FX Sample Rate", -30),
                                (PY, "Send 2 (FX2)", 30)),
                         **delay(260, 30, 15), **space(25, 2.5, rev_type="Room")}, "Bells", "Dirty")

# ---------------------------------------------------------------- FX (14)
add("Rising Tide", {**amp(2500, 1000, 100, 1500), **fenv(4000, 1000, 100, 1500), **menv(4000, 1000, 100, 1500),
                    "o2_level": 80, "o2_freq": 0.2, "noise_level": 60, "noise_color_pair": "White+Purple",
                    "flt_cut": hz(200), "flt_res": 45, "lad_env": 70, "voices": "4", "spread": 80,
                    "pitch_env": 50, "lfo2_rate": lfo(4), "lfo2_sync": "Off", "lad_lfo": 10,
                    **rows(("Mod Env", "LFO 2 Rate", 60), (MW, "Ladder Res", 30), (AT, "Pitch", 3),
                           (PY, "Send 2 (FX2)", 30)),
                    **delay("1/8", 40, 20, True), **space(40, 4.0)}, "FX", "Evolving")
add("Falling Star", {**amp(1, 2500, 0, 2000), **menv(1, 2000, 0, 1500), "o1_wave": "Tri", "o1_angle": 50,
                     "o2_level": 50, "o2_oct": "4'", "o2_freq": 0.3, "pitch_env": 70, "flt_cut": hz(6000),
                     "voices": "4",
                     **rows((MW, "Mod Env Decay", 40), (VEL, "Pitch EG Amt", 20), (PY, "Send 2 (FX2)", 30)),
                     **delay("1/8.", 50, 30, True), **space(35, 4.0, rev_type="Shimmer"), "mfx_series": 40},
    "FX", "Bright")
add("Wind Tunnel", {**amp(1500, 2000, 100, 2500), "o1_level": 0, "noise_level": 100,
                    "noise_color_pair": "Red+White", "noise_color_mix": 50, "o1_svf": "Off", "noise_svf": "On",
                    "noise_lad": "Off", "svf_mode": "BP", "svf_cut": hz(700), "svf_res": 70, "svf_lfo": 50,
                    "lfo2_rate": lfo(0.12), "lfo2_wave": "Triangle", "svf_kbd": 100, "flt_mix": 0, "voices": "4",
                    "spread": 100,
                    **rows((MW, "SVF Cutoff", 40), (AT, "SVF Res", 20), (PX, "LFO 2 Rate", 40),
                           (PY, "Send 2 (FX2)", 30)),
                    "ribbon_dst": "SVF Cutoff", "ribbon_amt": 50, **space(35, 4.0)}, "FX", "Soft")
add("Laser Grid", {**amp(1, 300, 0, 200), **menv(1, 180, 0, 150), "o1_wave": "Saw", "o2_level": 70,
                   "o2_sync": "On", "o2_freq": 5.0, "o1_level": 0, "pitch_env": 60, "flt_cut": hz(5000),
                   "flt_res": 30, "voices": "6",
                   **rows(("Mod Env", "Osc 2 Pitch", 40), (MW, "Mod Env Decay", 50), (AT, "Ladder Res", 25),
                          (PY, "Send 2 (FX2)", 25)),
                   **delay("1/16", 55, 30, True), **space(15, 1.5, rev_type="Plate")}, "FX", "Aggressive")
add("Radio Ghost", {**amp(200, 1500, 90, 1500), "o1_level": 0, "mac_model": "Speech", "mac_level": 85,
                    "mac_harm": 40, "mac_timbre": 50, "mac_morph": 50, "noise_level": 20, "flt_cut": hz(3500),
                    "flt_res": 30, "o1_svf": "Off", "voices": "4",
                    "lfo2_rate": lfo(0.6), "lfo2_wave": "S&H", "lfo3_rate": lfo(0.25), "lfo3_wave": "Noise",
                    "modfx_on": "On", "modfx_type": "Bit Crusher", "modfx_bits": 6,
                    "modfx_srate": logk(6000, 128, 44100), "modfx_mix": 60,
                    **rows(("LFO 2", "Macro Morph", 40), ("LFO 3", "Macro Timbre", 30), (MW, "Synth FX Mix", 40),
                           (AT, "Noise Level", 30), (PY, "Send 2 (FX2)", 30)),
                    **delay(380, 40, 20), **space(30, 3.0, rev_type="Room")}, "FX", "Dirty")
add("Sweep Machine", {**amp(10, 1000, 100, 800), "o2_level": 85, "o2_freq": 0.1, "o3_level": 60, "o3_oct": "16'",
                      "flt_cut": hz(500), "flt_res": 75, "lad_lfo": 50, "lfo2_sync": "On",
                      "lfo2_rate": lfo_div("1 bar"), "lfo2_wave": "Triangle", "voices": "4",
                      **rows((MW, "LFO 2 Rate (st)", 60), (AT, "Ladder Res", 15), (PX, "Ladder LFO2 Amt", 30),
                             (PY, "Send 2 (FX2)", 30)),
                      **delay("1/8.", 35, 20, True), **space(25, 2.5)}, "FX", "Evolving")
add("Subterranean", {**amp(1500, 3000, 100, 4000), "o1_oct": "32'", "o1_wave": "Tri", "o1_angle": 40,
                     "noise_level": 50, "noise_color_pair": "Red+White", "noise_color_mix": 0, "flt_cut": hz(150),
                     "flt_res": 40, "lad_lfo": 30, "lfo2_rate": lfo(0.08), "lfo2_wave": "Sine", "voices": "2",
                     "drift": 100,
                     **rows((AT, "Ladder Cutoff", 30), (MW, "Ladder Res", 30), (PY, "Send 2 (FX2)", 40)),
                     **reverb(40, rev_type="Blackhole", rev_decay=50, rev_size=70, rev_tone=30)}, "FX", "Dark")
add("Signal Flare", {**amp(5, 1000, 100, 600), "o2_level": 60, "o2_freq": 0.1, "flt_cut": hz(3000), "flt_res": 20,
                     "lfo1_wave": "Square", "lfo1_rate": lfo(2.5), "pitch_lfo": 25, "mod1_amt": 0, "voices": "2",
                     **rows((MW, "LFO 1 Rate", 30), (AT, "Pitch LFO1 Amt", 15), (PY, "Send 2 (FX2)", 25)),
                     **delay("1/8", 35, 20, True), **space(20, 2.0, rev_type="Plate")}, "FX", "Aggressive")
add("Bubble Pool", {**amp(1, 800, 80, 500), "o1_wave": "Tri", "o1_angle": 50, "flt_cut": hz(900), "flt_res": 80,
                    "lad_lfo": 45, "lfo2_wave": "S&H", "lfo2_sync": "On", "lfo2_rate": lfo_div("1/16"),
                    "voices": "4",
                    **rows((MW, "LFO 2 Rate (st)", 40), (AT, "Ladder Res", 15), (PX, "Ladder LFO2 Amt", 30),
                           (PY, "Send 2 (FX2)", 30)),
                    **delay("1/8.", 40, 25, True), **space(25, 2.5)}, "FX", "Clean")
add("Static Storm", {**amp(500, 1500, 90, 2000), "o1_level": 0, "noise_level": 90, "noise_color_pair": "Red+Purple",
                     "noise_color_mix": 60, "flt_cut": hz(2500), "flt_res": 35, "lad_lfo": 40, "lfo2_wave": "S&H",
                     "lfo2_rate": lfo(6), "voices": "2", "spread": 100,
                     "modfx_on": "On", "modfx_type": "Bit Crusher", "modfx_bits": 4,
                     "modfx_srate": logk(4000, 128, 44100), "modfx_mix": 55,
                     **rows((MW, "Synth FX Sample Rate", -40), (AT, "Ladder Cutoff", 30), (PX, "LFO 2 Rate", 40),
                            (PY, "Send 2 (FX2)", 30)),
                     **space(30, 3.0, rev_type="Room")}, "FX", "Dirty")
add("Space Freeze", {**amp(2000, 3000, 90, 5000), "o1_wave": "Tri", "o1_angle": 40, "o2_level": 70,
                     "o2_freq": 7.0, "o3_level": 50, "o3_oct": "4'", "flt_cut": hz(2000), "flt_res": 20,
                     "lad_lfo": 20, "lfo2_rate": lfo(0.15), "voices": "6", "spread": 100,
                     **rows((AT, "Send 2 (FX2)", 30), (MW, "FX2 Decay", 30), (PX, "Ladder Cutoff", 30),
                            (PY, "FX2 Size", 30)),
                     **reverb(55, rev_type="Blackhole", rev_decay=75, rev_size=80, rev_tone=55)}, "FX", "Evolving")
add("Impact Boom", {**amp(1, 3000, 0, 3000), **menv(1, 900, 0, 800), **fenv(1, 1500, 0, 1500),
                    "o1_oct": "16'", "o1_wave": "Tri", "o1_angle": 50, "noise_level": 50, "pitch_env": 30,
                    "flt_cut": hz(300), "flt_res": 20, "lad_env": 60, "voices": "2",
                    **rows((VEL, "Ladder Cutoff", 20), (MW, "Pitch EG Amt", 30), (PY, "Send 2 (FX2)", 30)),
                    **space(40, 6.0, rev_tone=40)}, "FX", "Dark")
add("Metal Scrape", {**amp(80, 1500, 80, 1200), "o1_level": 50, "o2_freq": 6.5, "o2_oct": "4'", "ring_level": 80,
                     "noise_level": 30, "flt_cut": hz(3000), "flt_res": 45, "flt_drive": 40, "lad_lfo": 25,
                     "lfo2_rate": lfo(3), "lfo2_wave": "Noise", "voices": "4",
                     "modfx_on": "On", "modfx_type": "Resonator", "modfx_mix": 45,
                     **rows((AT, "Osc 2 Pitch", 10), (MW, "Ring Level", 30), (PX, "Synth FX Mix", 40),
                            (PY, "Send 2 (FX2)", 30)),
                     **space(25, 2.5, rev_type="Plate")}, "FX", "Aggressive")
add("Granular Mist", {**amp(1000, 2000, 90, 3000), "o1_wave": "Tri", "o1_angle": 50, "o2_level": 60,
                      "o2_oct": "4'", "o2_wave": "Tri", "o2_angle": 50, "flt_cut": hz(3000), "voices": "6",
                      "modfx_on": "On", "modfx_type": "Granular", "modfx_pos": 60, "modfx_size": 35,
                      "modfx_density": 85, "modfx_texture": 75, "modfx_pitch": 12, "modfx_spread": 100,
                      "modfx_fb": 45, "modfx_room": 70, "modfx_mix": 70,
                      **rows((MW, "Synth FX Pitch", 25), (AT, "Synth FX Density", 20), (PX, "Synth FX Size", 40),
                             (PY, "Synth FX Texture", 30)),
                      **reverb(30, rev_type="Space", rev_decay=70, rev_size=75, rev_tone=55)}, "FX", "Soft")

# ---------------------------------------------------------------- ARP (12)
ARP = {"arp_on": "On", "arp_sync": "On"}
add("Neon Steps", {**amp(1, 250, 30, 180), **fenv(1, 200, 10, 150), **ARP, "arp_pattern": "Up", "arp_oct": "2",
                   "arp_div": "1/16", "arp_gate": 55,
                   "o2_level": 70, "o2_freq": 0.08, "flt_cut": hz(1000), "flt_res": 35, "lad_env": 45, "voices": "8",
                   **rows((MW, "Ladder Cutoff", 30), (AT, "Ladder Res", 20), (VEL, "Ladder Cutoff", 15),
                          (PX, "Send 1 (FX1)", 30), (PY, "Send 2 (FX2)", 25)),
                   **delay("1/8.", 35, 22, True), **space(20, 2.0, rev_type="Plate")}, "Arp", "Bright")
add("Glass Ladder", {**amp(1, 600, 0, 400), **menv(1, 300, 0, 200), **ARP, "arp_pattern": "Up", "arp_oct": "3",
                     "arp_dir": "Up/Down", "arp_div": "1/16", "arp_timing": "Triplet", "arp_gate": 60,
                     "o1_wave": "Tri", "o1_angle": 50, "o1_level": 0, "o1_oct": "4'", "o2_wave": "Tri",
                     "o2_angle": 50, "o2_level": 100, "fm_amt": 20, "flt_cut": hz(8000), "voices": "8",
                     **rows(("Mod Env", "FM Amount", 20), (MW, "FM Amount", 25), (AT, "Amp Env Decay", 20),
                            (PX, "Send 1 (FX1)", 30), (PY, "Send 2 (FX2)", 25)),
                     **delay("1/4", 35, 20, True), **space(25, 2.8)}, "Arp", "Clean")
add("Midnight Pulse", {**amp(1, 300, 40, 150), **fenv(1, 250, 10, 150), **ARP, "arp_pattern": "Down", "arp_oct": "1",
                       "arp_div": "1/8", "arp_gate": 50,
                       "o1_oct": "16'", "o2_level": 70, "o2_oct": "16'", "o2_freq": 0.06, "flt_cut": hz(350),
                       "flt_res": 40, "flt_drive": 25, "lad_env": 45, "lad_kbd": 30, "voices": "6",
                       **rows((MW, "Ladder Cutoff", 30), (AT, "Ladder Res", 15), (VEL, "Ladder Cutoff", 15),
                              (PX, "Send 1 (FX1)", 30), (PY, "Send 2 (FX2)", 25)),
                       **delay("1/4.", 30, 18, True), **space(15, 2.0)}, "Arp", "Dark")
add("Random Fireflies", {**amp(1, 400, 0, 300), **fenv(1, 250, 0, 200), **ARP, "arp_pattern": "Random",
                         "arp_oct": "3", "arp_div": "1/16", "arp_gate": 40,
                         "o1_wave": "Tri", "o1_angle": 50, "o2_level": 40, "o2_oct": "4'", "flt_cut": hz(2500),
                         "flt_res": 15, "lad_env": 30, "voices": "8", "spread": 100,
                         **rows((MW, "Ladder Cutoff", 25), (AT, "Send 1 (FX1)", 20), (VEL, "Ladder Cutoff", 10),
                                (PX, "Spread", 30), (PY, "Send 2 (FX2)", 30)),
                         **delay("1/8.", 40, 28, True), **space(30, 3.5), "mfx_series": 30}, "Arp", "Soft")
add("Order Of Things", {**amp(1, 450, 30, 300), **fenv(1, 300, 20, 250), **ARP, "arp_pattern": "Order",
                        "arp_oct": "2", "arp_div": "1/8", "arp_timing": "Dotted", "arp_gate": 60,
                        "o1_mix": 100, "o1_pw": 40, "o2_level": 70, "o2_freq": 0.07, "flt_cut": hz(1400),
                        "flt_res": 20, "lad_env": 30, "voices": "8",
                        **rows((MW, "Ladder Cutoff", 25), (AT, "Ladder Cutoff", 15), (PX, "Send 1 (FX1)", 30),
                               (PY, "Send 2 (FX2)", 25)),
                        **chorus(0.3, 8, 30, 30), **delay("1/4", 30, 18, True), **space(22, 2.4)}, "Arp", "Warm")
add("Gated Shimmer", {**amp(1, 150, 0, 120), **ARP, "arp_pattern": "Up", "arp_oct": "2", "arp_div": "1/32",
                      "arp_gate": 30, "o1_wave": "Tri", "o1_angle": 30, "o2_level": 60, "o2_oct": "4'",
                      "flt_cut": hz(5000), "voices": "8",
                      **rows((MW, "Amp Env Decay", 40), (AT, "Send 2 (FX2)", 25), (PX, "Send 1 (FX1)", 30),
                             (PY, "Ladder Cutoff", -30)),
                      **delay("1/8", 30, 15, True), **reverb(40, 4.5, rev_type="Shimmer", rev_size=60)}, "Arp", "Bright")
add("Swing Pluck", {**amp(1, 350, 0, 250), **fenv(1, 220, 0, 180), **ARP, "arp_pattern": "Up", "arp_oct": "2",
                    "arp_dir": "Down/Up", "arp_div": "1/16", "arp_gate": 50, "arp_swing": 62,
                    "o2_level": 60, "o2_freq": 0.06, "flt_cut": hz(900), "flt_res": 25, "lad_env": 50, "voices": "8",
                    **rows((MW, "Ladder Cutoff", 30), (AT, "Ladder Res", 20), (VEL, "Ladder Cutoff", 15),
                           (PY, "Send 2 (FX2)", 25)),
                    **delay("1/8.", 30, 18, True), **space(20, 2.0, rev_type="Room")}, "Arp", "Warm")
add("Acid Spiral", {**amp(1, 250, 60, 80), **fenv(1, 200, 0, 80), **ARP, "arp_pattern": "Up", "arp_oct": "2",
                    "arp_dir": "Up/Down", "arp_div": "1/16", "arp_gate": 70,
                    "o1_oct": "16'", "o1_mix": 100, "flt_cut": hz(300), "flt_res": 80, "flt_drive": 50,
                    "lad_env": 55, "lad_kbd": 40, "voices": "Mono", "glide": "On", "glide_time": ms(40),
                    "glide_legato": "On",
                    **rows((MW, "Ladder Cutoff", 35), (AT, "Ladder Drive", 25), (VEL, "Ladder EG Amt", 20),
                           (PX, "Filter Env Decay", 40), (PY, "Send 1 (FX1)", 30)),
                    **delay("1/8.", 35, 15, True), **space(10, 1.5, rev_type="Room")}, "Arp", "Aggressive")
add("Pendulum Keys", {**amp(2, 800, 30, 400), **fenv(1, 500, 20, 300), **ARP, "arp_pattern": "Up", "arp_oct": "3",
                      "arp_pendulum": "On", "arp_pend_mode": "Twice", "arp_div": "1/16", "arp_gate": 60,
                      "o1_wave": "Tri", "o1_angle": 50, "o2_level": 55, "o2_oct": "4'", "o2_mix": 100,
                      "flt_cut": hz(4000), "lad_env": 20, "voices": "8",
                      **rows((MW, "Ladder Cutoff", -30), (AT, "Amp Env Decay", 25), (PX, "Send 1 (FX1)", 30),
                             (PY, "Send 2 (FX2)", 25)),
                      **chorus(0.4, 7, 25, 25), **delay("1/8.", 25, 15, True), **space(22, 2.2)}, "Arp", "Clean")
add("Triplet Rain", {**amp(1, 500, 0, 400), **menv(1, 350, 0, 250), **ARP, "arp_pattern": "Down", "arp_oct": "3",
                     "arp_div": "1/8", "arp_timing": "Triplet", "arp_gate": 50,
                     "o1_wave": "Tri", "o1_angle": 50, "o1_level": 0, "o1_oct": "4'", "o1_freq": 7.0,
                     "o2_wave": "Tri", "o2_angle": 50, "o2_level": 100, "fm_amt": 15, "flt_cut": hz(6000), "voices": "8",
                     **rows(("Mod Env", "FM Amount", 15), (MW, "FM Amount", 40), (AT, "Send 1 (FX1)", 25),
                            (PY, "Send 2 (FX2)", 25)),
                     **delay("1/4.", 40, 22, True), **space(28, 3.0)}, "Arp", "Evolving")
add("Octave Bounce", {**amp(1, 300, 40, 200), **fenv(1, 250, 20, 200), **ARP, "arp_pattern": "Up", "arp_oct": "4",
                      "arp_oct_order": "On", "arp_div": "1/16", "arp_gate": 45,
                      "o1_mix": 100, "o1_pw": 30, "o2_level": 60, "o2_freq": 0.05, "flt_cut": hz(2000),
                      "flt_res": 25, "lad_env": 35, "voices": "8",
                      **rows((MW, "Ladder Cutoff", 30), (AT, "Osc 1 PW", 20), (PX, "Send 1 (FX1)", 30),
                             (PY, "Send 2 (FX2)", 25)),
                      **delay("1/8", 25, 15, True), **space(18, 2.0, rev_type="Plate")}, "Arp", "Bright")
add("Dusty Arp", {**amp(1, 400, 20, 250), **fenv(1, 250, 10, 200), **ARP, "arp_pattern": "Up", "arp_oct": "2",
                  "arp_div": "1/16", "arp_gate": 50, "arp_swing": 57,
                  "o1_wave": "Tri", "o1_angle": 50, "o2_level": 50, "o2_oct": "4'", "flt_cut": hz(2500),
                  "flt_res": 15, "lad_env": 30, "voices": "8", "drift": 80,
                  "modfx_on": "On", "modfx_type": "Tape Color", "modfx_drive": 65, "modfx_bump": 60,
                  "modfx_flutter": 55, "modfx_tone": 40, "modfx_mix": 100,
                  **rows((MW, "Synth FX Flutter", 40), (AT, "Synth FX Drive", 25), (PX, "Send 1 (FX1)", 30),
                         (PY, "Send 2 (FX2)", 25)),
                  **delay("1/8.", 35, 15, True), **space(18, 1.6, rev_type="Room")}, "Arp", "Dirty")

# ---------------------------------------------------------------- SEQUENCE (12)
# NOTE TRIG and TRANSPOSE: hold a key and the factory sequence plays from it (START KEY: the key is its first note)
TRIG = {"seq_on": "On", "seq_note_trig": "On", "seq_transpose": "On"}
add("Night Drive Bass", {**amp(2, 280, 70, 90), **fenv(1, 220, 10, 100), **TRIG, "seq_pattern": "Bass Line",
                         "seq_length": 16, "o1_oct": "16'", "o2_level": 75, "o2_oct": "16'", "o2_mix": 100,
                         "o2_freq": 0.06, "flt_cut": hz(260), "flt_res": 45, "flt_drive": 30, "lad_env": 50,
                         "lad_kbd": 40, "voices": "Mono", "octave_shift": "-1",
                         **rows((AT, "Ladder Cutoff", 25), (VEL, "Ladder Cutoff", 15), (PX, "Ladder Res", 25),
                                (PY, "Send 1 (FX1)", 30)),
                         **delay("1/8.", 25, 10, True)}, "Sequence", "Dark")
add("Ratchet Lights", {**amp(1, 250, 0, 180), **fenv(1, 200, 0, 150), **TRIG, "seq_pattern": "Ratchet Run",
                       "seq_length": 16, "o2_level": 60, "o2_freq": 0.07, "o2_oct": "4'", "flt_cut": hz(1500),
                       "flt_res": 30, "lad_env": 40, "voices": "6",
                       **rows((MW, "Ladder Cutoff", 30), (AT, "Ladder Res", 20), (PX, "Send 1 (FX1)", 30),
                              (PY, "Send 2 (FX2)", 25)),
                       **delay("1/8.", 35, 22, True), **space(18, 2.0, rev_type="Plate")}, "Sequence", "Bright")
add("Stab Cascade", {**amp(2, 500, 30, 300), **fenv(1, 400, 15, 250), **TRIG, "seq_pattern": "Chord Stabs",
                     "seq_length": 32, "o1_mix": 100, "o1_pw": 40, "o2_level": 80, "o2_freq": 0.08,
                     "flt_cut": hz(1800), "flt_res": 20, "lad_env": 25, "voices": "8",
                     **rows((MW, "Ladder Cutoff", 30), (AT, "Ladder Cutoff", 15), (PX, "Send 1 (FX1)", 30),
                            (PY, "Send 2 (FX2)", 30)),
                     **chorus(0.3, 8, 30, 30), **delay("1/4.", 45, 25, True), **space(28, 3.0),
                     "mfx_series": 35}, "Sequence", "Warm")
add("Half-Time Groove", {**amp(2, 400, 60, 150), **fenv(1, 300, 15, 150), **TRIG, "seq_pattern": "Bass Line",
                         "seq_length": 16, "seq_div": "1/8", "o2_level": 70, "o2_oct": "16'", "o2_freq": 0.05,
                         "flt_cut": hz(400), "flt_res": 30, "lad_env": 40, "voices": "Mono", "octave_shift": "-1",
                         "modfx_on": "On", "modfx_type": "Tape Color", "modfx_drive": 50, "modfx_bump": 60,
                         "modfx_flutter": 15, "modfx_tone": 55, "modfx_mix": 100,
                         **rows((MW, "Ladder Cutoff", 30), (AT, "Ladder Res", 15), (VEL, "Ladder Cutoff", 10),
                                (PY, "Send 1 (FX1)", 25)),
                         **delay("1/4", 25, 10, True)}, "Sequence", "Warm")
add("Triplet Machine", {**amp(1, 300, 20, 200), **fenv(1, 250, 10, 150), **TRIG, "seq_pattern": "Ratchet Run",
                        "seq_length": 12, "seq_div": "1/16", "seq_timing": "Triplet",
                        "o1_wave": "Tri", "o1_angle": 40, "o2_level": 60, "o2_sync": "On", "o2_freq": 4.0,
                        "flt_cut": hz(2200), "flt_res": 20, "lad_env": 30, "voices": "6",
                        "lfo3_rate": lfo(0.2), "lfo3_wave": "Sine",
                        **rows(("LFO 3", "Osc 2 Pitch", 10), (MW, "Osc 2 Pitch", 15), (AT, "Ladder Cutoff", 20),
                               (PX, "Send 1 (FX1)", 30), (PY, "Send 2 (FX2)", 25)),
                        **delay("1/8", 35, 20, True), **space(20, 2.2)}, "Sequence", "Evolving")
add("Short Loop", {**amp(1, 220, 50, 80), **fenv(1, 160, 0, 80), **TRIG, "seq_pattern": "Bass Line",
                   "seq_length": 8, "o1_mix": 100, "o2_level": 60, "o2_freq": 0.1, "flt_cut": hz(450),
                   "flt_res": 65, "flt_drive": 55, "lad_env": 55, "voices": "Mono",
                   **rows((MW, "Ladder Cutoff", 35), (AT, "Ladder Drive", 25), (VEL, "Ladder Cutoff", 15),
                          (PY, "Send 1 (FX1)", 30)),
                   **delay("1/16", 40, 15, True), **space(10, 1.2, rev_type="Room")}, "Sequence", "Aggressive")
add("Stab Dub", {**amp(2, 350, 20, 250), **fenv(1, 300, 10, 200), **TRIG, "seq_pattern": "Chord Stabs",
                 "seq_length": 32, "o1_mix": 100, "o1_pw": 45, "o2_level": 70, "o2_freq": 0.05, "flt_cut": hz(900),
                 "flt_res": 30, "lad_env": 30, "voices": "8", "octave_shift": "-1",
                 **rows((MW, "Send 1 (FX1)", 35), (AT, "Ladder Cutoff", 20), (PX, "FX1 Feedback", 25),
                        (PY, "Send 2 (FX2)", 30)),
                 **delay("1/4.", 60, 35, True), **space(25, 3.5, rev_tone=50), "mfx_series": 50}, "Sequence", "Dark")
add("Pentatonic Run", {**amp(1, 1200, 0, 800), **menv(1, 500, 0, 300), **TRIG, "seq_pattern": "Ratchet Run",
                       "seq_length": 16, "o1_wave": "Tri", "o1_angle": 50, "o1_level": 0, "o1_oct": "4'",
                       "o1_freq": 0.0, "o2_wave": "Tri", "o2_angle": 50, "o2_level": 100, "fm_amt": 18, "flt_cut": hz(8000),
                       "voices": "8",
                       **rows(("Mod Env", "FM Amount", 18), (MW, "FM Amount", 25), (AT, "Send 1 (FX1)", 20),
                              (PY, "Send 2 (FX2)", 25)),
                       **delay("1/8.", 30, 18, True), **space(25, 2.8)}, "Sequence", "Clean")
add("Two-Synth Pulse", {**amp(2, 280, 70, 90), **fenv(1, 220, 10, 100), **TRIG, "seq_pattern": "Bass Line",
                        "seq_length": 16, "seq_output": "1+2", "o2_level": 70, "o2_oct": "16'", "o2_freq": 0.05,
                        "flt_cut": hz(300), "flt_res": 40, "lad_env": 45, "voices": "Mono", "octave_shift": "-1",
                        **rows((AT, "Ladder Cutoff", 25), (VEL, "Ladder Cutoff", 15), (PY, "Send 1 (FX1)", 20)),
                        # Synth 2: a bright pluck two octaves up on the same notes (only the sequence plays it: its
                        # key range is the lowest key alone)
                        **synth(2, {"syn_on": "On", "key_lo": 0, "key_hi": 0, **amp(1, 200, 0, 150),
                                    **fenv(1, 150, 0, 100), "o1_wave": "Tri", "o1_angle": 40, "o2_level": 50,
                                    "o2_oct": "4'", "octave_shift": "+1", "flt_cut": hz(2500), "flt_res": 20,
                                    "lad_env": 35, "voices": "6", "volume": 55, "pan": 25, "mfx_send1": 35,
                                    "mfx_send2": 20, **rows((MW, "Ladder Cutoff", 30), (AT, "Amp Level", 20))}),
                        **delay("1/8.", 35, 20, True), **space(10, 2.0, rev_type="Plate")}, "Sequence", "Bright",
    "Layer")
add("Triple Stack Sequence", {**amp(2, 450, 40, 250), **fenv(1, 350, 15, 220), **TRIG, "seq_pattern": "Chord Stabs",
                              "seq_length": 32, "seq_output": "1+2+3", "o1_mix": 100, "o1_pw": 40, "o2_level": 70,
                              "o2_freq": 0.08, "flt_cut": hz(1400), "flt_res": 20, "lad_env": 25, "voices": "6",
                              "volume": 50, **rows((MW, "Ladder Cutoff", 30), (PY, "Send 2 (FX2)", 25)),
                              # Synth 2: a sub an octave down; Synth 3: soft bells an octave up (sequence only)
                              **synth(2, {"syn_on": "On", "key_lo": 0, "key_hi": 0, **amp(2, 400, 50, 200),
                                          "o1_wave": "Tri", "o1_angle": 50, "octave_shift": "-1",
                                          "flt_cut": hz(500), "voices": "6", "volume": 50,
                                          **rows((AT, "Amp Level", 20))}),
                              **synth(3, {"syn_on": "On", "key_lo": 0, "key_hi": 0, **amp(1, 900, 0, 700),
                                          **menv(1, 400, 0, 300), "o1_wave": "Tri", "o1_angle": 50, "o1_level": 0,
                                          "o1_oct": "4'", "o2_wave": "Tri", "o2_angle": 50, "o2_level": 100, "fm_amt": 20,
                                          "octave_shift": "+1", "voices": "4", "volume": 40, "pan": -20,
                                          "mfx_send1": 30, "mfx_send2": 30,
                                          **rows(("Mod Env", "FM Amount", 15), (MW, "FM Amount", 20))}),
                              **chorus(0.3, 8, 25, 25), **delay("1/4.", 35, 15, True), **space(25, 2.8)},
    "Sequence", "Warm", "Layer")
add("Interval Walk", {**amp(1, 350, 30, 200), **fenv(1, 250, 10, 150), **TRIG, "seq_pattern": "Ratchet Run",
                      "seq_length": 16, "seq_tr_mode": "Interval", "seq_div": "1/8", "o1_mix": 100, "o1_pw": 35,
                      "o2_level": 60, "o2_freq": 0.06, "flt_cut": hz(1200), "flt_res": 25, "lad_env": 35,
                      "voices": "6", "glide": "On", "glide_time": ms(30),
                      **rows((MW, "Ladder Cutoff", 30), (AT, "Ladder Res", 15), (PX, "Send 1 (FX1)", 30),
                             (PY, "Send 2 (FX2)", 25)),
                      **chorus(0.4, 7, 25, 25), **delay("1/4", 30, 18, True), **space(20, 2.2)}, "Sequence", "Soft")
add("Ratchet Dirt", {**amp(1, 200, 40, 100), **fenv(1, 180, 0, 100), **TRIG, "seq_pattern": "Ratchet Run",
                     "seq_length": 16, "o2_level": 70, "o2_freq": -0.1, "flt_cut": hz(800), "flt_res": 45,
                     "flt_drive": 70, "lad_env": 45, "voices": "4", "octave_shift": "-1",
                     "modfx_on": "On", "modfx_type": "Bit Crusher", "modfx_bits": 6,
                     "modfx_srate": logk(9000, 128, 44100), "modfx_mix": 50,
                     **rows((MW, "Synth FX Mix", 40), (AT, "Ladder Drive", 25), (VEL, "Ladder Cutoff", 15),
                            (PY, "Send 1 (FX1)", 25)),
                     **delay("1/16", 35, 15, True), **space(12, 1.5, rev_type="Room")}, "Sequence", "Dirty")

# ---------------------------------------------------------------- DRUM (8)
add("Round Kick", {**amp(1, 650, 0, 300), "volume": 100, "flt_drive": 45, **menv(1, 70, 0, 60), "o1_wave": "Tri", "o1_angle": 50, "o1_oct": "16'",
                   "pitch_env": 100, "flt_cut": hz(3000), "o1_track": 0, "voices": "4", "key_center": 36,
                   **rows((VEL, "Pitch EG Amt", 20), (MW, "Amp Env Decay", 40), (AT, "Pitch", -3),
                          (PY, "Send 2 (FX2)", 25))}, "Drum", "Clean")
add("Snap Snare", {**amp(1, 280, 0, 200), "volume": 100, "flt_drive": 35, **menv(1, 40, 0, 40), **fenv(1, 120, 0, 100),
                   "o1_wave": "Tri", "o1_angle": 50, "o1_level": 85, "o1_track": 0, "key_center": 50,
                   "noise_level": 100, "noise_color_pair": "White+Purple", "noise_color_mix": 30, "pitch_env": 30,
                   "flt_cut": hz(6000), "flt_res": 15, "lad_env": 40, "voices": "4",
                   **rows((VEL, "Noise Level", 20), (MW, "Amp Env Decay", 40), (PX, "Ladder Cutoff", 30),
                          (PY, "Send 2 (FX2)", 25)),
                   **space(18, 1.2, rev_type="Room")}, "Drum", "Bright")
add("Tight Hat", {**amp(1, 110, 0, 80), "volume": 100, "o1_level": 0, "noise_level": 100, "noise_color_pair": "White+Purple",
                  "noise_color_mix": 60, "flt_mode": "HP", "flt_cut": hz(7000), "flt_res": 25, "voices": "4",
                  **rows((VEL, "Amp Env Decay", 20), (MW, "Amp Env Decay", 50), (PX, "Ladder Cutoff", -20),
                         (PY, "Send 2 (FX2)", 20)),
                  **space(10, 1.0, rev_type="Room")}, "Drum", "Clean")
add("Open Hat Sizzle", {**amp(1, 500, 0, 400), "o1_level": 0, "noise_level": 90, "noise_color_pair": "White+Purple",
                        "noise_color_mix": 50, "ring_level": 25, "o2_freq": 6.3, "o2_oct": "2'", "o1_oct": "2'",
                        "flt_mode": "HP", "flt_cut": hz(6000), "flt_res": 20, "voices": "4",
                        **rows((VEL, "Amp Env Decay", 25), (MW, "Amp Env Decay", -50), (AT, "Amp Env Decay", -40),
                               (PY, "Send 2 (FX2)", 20)),
                        **space(12, 1.2, rev_type="Room")}, "Drum", "Bright")
add("Floor Tom", {**amp(1, 900, 0, 500), "volume": 100, "flt_drive": 30, **menv(1, 250, 0, 200), "o1_wave": "Tri", "o1_angle": 50,
                  "o1_oct": "16'", "noise_level": 15, "pitch_env": 25, "flt_cut": hz(1500), "voices": "4",
                  **rows((VEL, "Pitch EG Amt", 10), (MW, "Amp Env Decay", 40), (PY, "Send 2 (FX2)", 25)),
                  **space(20, 1.5, rev_type="Room")}, "Drum", "Warm")
add("Clap Stack", {**amp(1, 650, 0, 400), "noise_lad": "On", "flt_cut": hz(2600),
                   "flt_drive": 80, "volume": 100, **fenv(1, 220, 0, 200), "o1_level": 0, "noise_level": 100, "noise_color_pair": "Red+White",
                   "noise_color_mix": 80, "o1_svf": "Off", "noise_svf": "On", "svf_mode": "BP",
                   "svf_cut": hz(1400), "svf_res": 10, "svf_env": 25, "flt_mix": 85, "voices": "4",
                   **rows((VEL, "SVF Cutoff", 10), (MW, "SVF Cutoff", 25), (PY, "Send 2 (FX2)", 25)),
                   "amp_hold": ms(20), **space(45, 1.6, rev_type="Room")}, "Drum", "Dirty")
add("Macro Kick Deep", {**amp(1, 900, 0, 600), "o1_level": 0, "mac_model": "Bass Drum", "mac_level": 95,
                        "mac_harm": 35, "mac_timbre": 40, "mac_morph": 55, "flt_cut": hz(8000), "flt_drive": 30,
                        "voices": "4",
                        **rows((VEL, "Macro Timbre", 20), (MW, "Macro Morph", 30), (AT, "Ladder Drive", 30),
                               (PY, "Send 2 (FX2)", 15))}, "Drum", "Aggressive")
add("Wood And Metal", {**amp(1, 250, 0, 200), **menv(1, 60, 0, 50), "o1_wave": "Tri", "o1_angle": 50,
                       "o1_level": 0, "o1_oct": "4'", "o1_freq": 5.0, "o2_wave": "Tri", "o2_angle": 50, "o2_level": 100,
                       "fm_amt": 45, "flt_cut": hz(6000), "voices": "6",
                       **rows(("Mod Env", "FM Amount", 30), (VEL, "FM Amount", 15), (MW, "Osc 1 Pitch", 20),
                              (PY, "Send 2 (FX2)", 20)),
                       **space(15, 1.0, rev_type="Room")}, "Drum", "Clean")

# ---------------------------------------------------------------- PERFORMANCE (14)
# three Synths at once: splits (key_lo / key_hi), layers, a sequence driving the others. The master effects are shared.
BASS = {**amp(2, 400, 80, 120), **fenv(1, 320, 20, 180), "o2_level": 85, "o2_freq": -0.07, "o3_level": 70,
        "o3_oct": "16'", "o3_mix": 100, "flt_cut": hz(240), "flt_res": 25, "flt_drive": 35, "lad_env": 45,
        "lad_kbd": 30, "voices": "Mono", **rows((AT, "Ladder Cutoff", 25), (VEL, "Ladder Cutoff", 10))}
PAD = {**amp(700, 1500, 85, 1800), **fenv(1000, 2500, 50, 2000), "o2_level": 85, "o2_freq": 0.09, "o3_level": 60,
       "o3_freq": -0.08, "o3_oct": "4'", "flt_cut": hz(800), "flt_res": 20, "lad_env": 25, "voices": "6",
       "spread": 60, **chorus(0.25, 9, 40, 35), **rows((AT, "Ladder Cutoff", 30), (PY, "Send 2 (FX2)", 25))}
LEAD = {**amp(3, 600, 85, 300), **fenv(2, 500, 50, 300), "o2_level": 80, "o2_freq": 0.06, "o2_sync": "On",
        "flt_cut": hz(2400), "flt_res": 25, "lad_env": 25, "voices": "Mono", "glide": "On",
        "glide_time": ms(80), "glide_legato": "On", "lfo1_rate": lfo(5.4), "lfo1_wave": "Sine", "mod1_amt": 4,
        **rows((AT, "Pitch LFO1 Amt", 3), (MW, "Osc 2 Pitch", 20), (VEL, "Ladder Cutoff", 10))}
BELL = {**amp(1, 2000, 0, 1400), **menv(1, 1000, 0, 700), "o1_wave": "Tri", "o1_angle": 50, "o1_level": 0,
        "o1_oct": "4'", "o1_freq": 2.0, "o2_wave": "Tri", "o2_angle": 50, "o2_level": 100, "fm_amt": 30, "voices": "4",
        **rows(("Mod Env", "FM Amount", 25), (VEL, "FM Amount", 15))}
STRINGS = {**amp(350, 1500, 85, 1200), "o1_mix": 100, "o1_pw": 42, "o2_level": 85, "o2_freq": 0.1, "flt_cut": hz(2400),
           "flt_res": 5, "voices": "6", **STR_VIB, "modfx_on": "On", "modfx_type": "Ensemble",
           "modfx_rate": fxrate(0.6), "modfx_time": 45, "modfx_depth": 50, "modfx_vdepth": 25, "modfx_spread": 80,
           "modfx_mix": 45, **rows((AT, "Ladder Cutoff", 20), (AT, "Amp Level", 10))}
add("Bass Pad Lead Split", {**BASS, "key_hi": 47, "volume": 62,
                            **synth(2, {**PAD, "syn_on": "On", "key_lo": 48, "key_hi": 71, "volume": 48, "pan": -10,
                                        "mfx_send2": 30}),
                            **synth(3, {**LEAD, "syn_on": "On", "key_lo": 72, "volume": 55, "pan": 10,
                                        "mfx_send1": 25, "mfx_send2": 15}),
                            **delay("1/8.", 30, 0, True), **space(0, 3.0)}, "Performance", "Warm", "Split")
add("Colossus Stack", {**amp(5, 800, 90, 500), **fenv(3, 700, 50, 400), "o2_level": 85, "o2_freq": 0.08,
                       "flt_cut": hz(1600), "flt_res": 25, "flt_drive": 35, "lad_env": 30, "voices": "2",
                       "unison": "On", "uni_count": "4", "uni_detune": 40, "spread": 90, "volume": 45,
                       **rows((AT, "Ladder Cutoff", 30), (MW, "Ladder Drive", 30), (VEL, "Ladder Cutoff", 10)),
                       **synth(2, {**PAD, "syn_on": "On", "voices": "4", "volume": 40, "mfx_send2": 30}),
                       **synth(3, {**BASS, "syn_on": "On", "octave_shift": "-1", "volume": 45, "voices": "Mono"}),
                       **delay("1/4.", 25, 12, True), **space(22, 3.0)}, "Performance", "Aggressive", "Layer")
add("Sequenced Trio", {**amp(2, 300, 70, 100), **fenv(1, 240, 10, 120), **TRIG, "seq_pattern": "Bass Line",
                       "seq_length": 16, "seq_output": "1+2+3", "o2_level": 75, "o2_oct": "16'", "o2_freq": 0.06,
                       "flt_cut": hz(300), "flt_res": 40, "flt_drive": 25, "lad_env": 45, "voices": "Mono",
                       "octave_shift": "-1", "volume": 58,
                       **rows((AT, "Ladder Cutoff", 25), (MW, "Ladder Res", 25), (PY, "Send 1 (FX1)", 20)),
                       # Synths 2 and 3 play only what Synth 1's sequence sends them (their key range: the lowest key)
                       **synth(2, {"syn_on": "On", "key_lo": 0, "key_hi": 0, **amp(1, 220, 0, 150),
                                   **fenv(1, 160, 0, 120), "o1_mix": 100, "o1_pw": 35, "o2_level": 50,
                                   "o2_oct": "4'", "flt_cut": hz(1800), "flt_res": 25, "lad_env": 35,
                                   "voices": "6", "octave_shift": "+1", "volume": 45, "pan": -30, "mfx_send1": 35,
                                   **rows((MW, "Ladder Cutoff", 30)), "modfx_on": "On", "modfx_type": "Chorus"}),
                       **synth(3, {"syn_on": "On", "key_lo": 0, "key_hi": 0, **BELL, "octave_shift": "+2",
                                   "volume": 32, "pan": 30, "mfx_send1": 30, "mfx_send2": 30}),
                       **delay("1/8.", 35, 15, True), **space(10, 2.5)}, "Performance", "Bright", "Layer")
add("Left Hand Groove", {**amp(2, 280, 70, 90), **fenv(1, 220, 10, 100), **TRIG, "seq_pattern": "Bass Line",
                         "seq_length": 16, "key_hi": 59, "o2_level": 75, "o2_oct": "16'", "o2_freq": 0.05,
                         "flt_cut": hz(280), "flt_res": 45, "flt_drive": 30, "lad_env": 50, "voices": "Mono",
                         "octave_shift": "-1", "volume": 62,
                         **rows((AT, "Ladder Cutoff", 25), (VEL, "Ladder Cutoff", 10)),
                         **synth(2, {**PAD, "syn_on": "On", "key_lo": 60, "volume": 45, "pan": -15, "mfx_send2": 30}),
                         **synth(3, {**BELL, "syn_on": "On", "key_lo": 60, "volume": 38, "pan": 20, "mfx_send1": 25,
                                     "mfx_send2": 30}),
                         **delay("1/8.", 30, 0, True), **space(0, 3.0)}, "Performance", "Dark", "Split")
add("Orchestra Split", {**STRINGS, "o1_oct": "16'", "o2_oct": "16'", "key_hi": 54, "volume": 60, "mfx_send2": 25,
                        **synth(2, {**STRINGS, "syn_on": "On", "key_lo": 55, "volume": 52, "mfx_send2": 30}),
                        **synth(3, {**BELL, "syn_on": "On", "key_lo": 67, "volume": 32, "pan": 20, "mfx_send2": 35}),
                        **space(0, 3.2, rev_size=70)}, "Performance", "Warm", "Split")
add("Dream Layer", {**PAD, "volume": 48, "mfx_send2": 30, "pan": -15,
                    **synth(2, {**BELL, "syn_on": "On", "volume": 35, "pan": 20, "mfx_send1": 30, "mfx_send2": 35}),
                    **synth(3, {**amp(1500, 2000, 90, 2500), "o1_level": 0, "noise_level": 80,
                                "noise_color_pair": "White+Purple", "flt_cut": hz(5000), "flt_mode": "HP",
                                "voices": "4", "spread": 100, "syn_on": "On", "volume": 22, "mfx_send2": 40,
                                **rows((MW, "Noise Level", 50), (AT, "Ladder Cutoff", -20))}),
                    **delay("1/4.", 35, 0, True), **reverb(0, 5.0, rev_type="Shimmer", rev_size=65)},
    "Performance", "Soft", "Layer")
add("Mono Duo Lead", {**LEAD, "volume": 52, "pan": -25, "mfx_send1": 25, "mfx_send2": 15,
                      **synth(2, {**LEAD, "syn_on": "On", "o1_wave": "Tri", "o1_angle": 30, "octave_shift": "-1",
                                  "o2_freq": 7.0, "flt_cut": hz(1600), "volume": 48, "pan": 25, "mfx_send1": 25,
                                  "mfx_send2": 15}),
                      **delay("1/8.", 35, 0, True), **space(0, 2.2, rev_type="Plate")}, "Performance",
    "Aggressive", "Layer")
add("Keys Over Bass", {**amp(1, 300, 0, 80), **fenv(1, 180, 0, 80), "o2_level": 60, "o2_oct": "16'", "o2_mix": 100,
                       "flt_cut": hz(150), "flt_res": 50, "lad_env": 55, "lad_kbd": 40, "voices": "Mono",
                       "key_hi": 52, "volume": 66, **rows((VEL, "Ladder Cutoff", 15), (AT, "Ladder Cutoff", 20)),
                       **synth(2, {**amp(2, 1600, 30, 600), **fenv(1, 900, 20, 500), "o1_wave": "Tri",
                                   "o1_angle": 50, "o2_level": 50, "o2_oct": "4'", "o2_mix": 100, "flt_cut": hz(2500),
                                   "lad_env": 25, "voices": "8", "syn_on": "On", "key_lo": 53, "volume": 50,
                                   "mfx_send2": 25, **chorus(0.5, 6, 30, 30),
                                   **rows((VEL, "Ladder Cutoff", 20), (MW, "Synth FX Depth", 40))}),
                       **synth(3, {**PAD, "syn_on": "On", "key_lo": 53, **amp(700, 1500, 0, 1800), "voices": "6",
                                   "volume": 50, "mfx_send2": 35,
                                   **rows((AT, "Amp Level", 70, {"xf": "Squared"}), (AT, "Ladder Cutoff", 30))}),
                       **space(0, 2.5)}, "Performance", "Warm", "Split")
add("Arp Over Pad", {**amp(1, 300, 20, 200), **fenv(1, 220, 0, 150), **ARP, "arp_pattern": "Up", "arp_oct": "2",
                     "arp_div": "1/16", "arp_gate": 50, "o2_level": 60, "o2_oct": "4'", "flt_cut": hz(1600),
                     "flt_res": 30, "lad_env": 40, "voices": "6", "key_lo": 60, "volume": 48, "mfx_send1": 30,
                     "mfx_send2": 15, **rows((MW, "Ladder Cutoff", 30), (VEL, "Ladder Cutoff", 10)),
                     **synth(2, {**PAD, "syn_on": "On", "key_lo": 48, "voices": "6", "volume": 40, "mfx_send2": 30}),
                     **synth(3, {**BASS, "syn_on": "On", "key_hi": 59, "volume": 58}),
                     **delay("1/8.", 35, 0, True), **space(0, 3.0)}, "Performance", "Bright", "Multi")
add("Drone And Melody", {**amp(2000, 3000, 100, 4000), "o1_oct": "16'", "o2_level": 80, "o2_freq": 7.0,
                         "o2_oct": "16'", "flt_cut": hz(400), "flt_res": 30, "lad_lfo": 25, "lfo2_rate": lfo(0.07),
                         "voices": "4", "key_hi": 59, "hold": "On", "volume": 50, "mfx_send2": 35, "drift": 60,
                         **rows((AT, "Ladder Cutoff", 25), (MW, "Ladder Res", 25)),
                         **synth(2, {**LEAD, "syn_on": "On", "key_lo": 60, "o1_wave": "Tri", "o1_angle": 30,
                                     "o2_sync": "Off", "o2_freq": 0.08, "volume": 52, "mfx_send1": 30,
                                     "mfx_send2": 25}),
                         **delay("1/4.", 40, 0, True), **space(0, 5.0)}, "Performance", "Evolving", "Split")
add("Brass And Strings", {**amp(30, 600, 75, 300), **fenv(60, 700, 45, 350), "o2_level": 90, "o2_freq": 0.06,
                          "o3_level": 70, "o3_freq": -0.05, "flt_cut": hz(450), "flt_res": 15, "lad_env": 50,
                          "lad_kbd": 50, "voices": "6", "volume": 45, "mfx_send2": 20,
                          **rows((VEL, "Ladder Cutoff", 20), (AT, "Ladder Cutoff", 25), (MW, "Pitch LFO1 Amt", 2)),
                          **synth(2, {**STRINGS, "syn_on": "On", "volume": 42, "mfx_send2": 30}),
                          **space(0, 2.8)}, "Performance", "Bright", "Layer")
add("Stack Of Fifths", {**PAD, "voices": "4", "volume": 40, "pan": -30, "mfx_send2": 25,
                        **synth(2, {**PAD, "syn_on": "On", "transpose": 7, "voices": "4", "volume": 34,
                                    "mfx_send2": 25}),
                        **synth(3, {**PAD, "syn_on": "On", "octave_shift": "+1", "voices": "4", "volume": 30,
                                    "pan": 30, "mfx_send2": 30, "flt_cut": hz(1400)}),
                        **space(0, 4.0)}, "Performance", "Evolving", "Layer")
add("Split Acid Pad", {**amp(1, 250, 60, 60), **fenv(1, 220, 0, 60), "o1_mix": 100, "flt_cut": hz(260),
                       "flt_res": 82, "flt_drive": 55, "lad_env": 55, "lad_kbd": 50, "voices": "Mono", "glide": "On",
                       "glide_type": "LCT", "glide_time": ms(70), "glide_legato": "On", "key_hi": 59, "volume": 55,
                       "mfx_send1": 20, **rows((MW, "Ladder Cutoff", 35), (AT, "Ladder Drive", 25)),
                       **synth(2, {**PAD, "syn_on": "On", "key_lo": 60, "volume": 45, "mfx_send2": 30,
                                   "modfx_type": "6-Stage Phaser"}),
                       **synth(3, {**amp(1, 90, 0, 60), "o1_level": 0, "noise_level": 100, "flt_mode": "HP",
                                   "flt_cut": hz(7000), "flt_res": 25, "voices": "2", "syn_on": "On", "key_lo": 0,
                                   "key_hi": 0, "volume": 30, "arp_on": "Off"}),
                       **delay("1/8.", 35, 0, True), **space(0, 2.5)}, "Performance", "Aggressive", "Split")
add("Wide Screen", {**PAD, "volume": 38, "pan": -70, "flt_cut": hz(600), "mfx_send2": 25,
                    **synth(2, {**PAD, "syn_on": "On", "o1_wave": "Tri", "o1_angle": 40, "volume": 36,
                                "flt_cut": hz(1200), "mfx_send2": 30, "modfx_type": "Ensemble", "modfx_mix": 40}),
                    **synth(3, {**PAD, "syn_on": "On", "volume": 34, "pan": 70, "flt_cut": hz(900),
                                "o2_freq": -0.12, "mfx_send2": 25}),
                    **space(0, 4.5, rev_size=80)}, "Performance", "Warm", "Layer")

# ---------------------------------------------------------------- GENERATIVE (12)
# each plays by itself when loaded (KEYS: FREE) in its KEY and SCALE, unless it says it waits for held keys
MALLET = {**amp(1, 900, 0, 700), **fenv(1, 400, 10, 300), "o1_wave": "Tri", "o1_angle": 50, "o2_level": 40,
          "o2_oct": "4'", "o2_wave": "Tri", "o2_angle": 50, "flt_cut": hz(3500), "lad_env": 20, "voices": "8"}
add("Bouncing Garden", {**MALLET, "key_root": "D", "key_scale": "Dorian", "phys_balls_on": "On", "phys_keys": "Free",
                        "phys_balls": 4, "phys_speed": 35, "phys_gravity": 45, "phys_bounce": 92,
                        "phys_octave": "C3", "phys_range": "3", "phys_max": 6, "phys_len": 40,
                        **rows(("Ball 1 Height", "Ladder Cutoff", 15), (MW, "Amp Env Decay", 30),
                               (AT, "Send 1 (FX1)", 25), (PY, "Send 2 (FX2)", 25)),
                        **delay("1/8.", 35, 22, True), **space(28, 3.0)}, "Generative", "Bright")
add("Particle Drift", {**amp(5, 1500, 0, 1200), **fenv(1, 600, 10, 500), "o1_wave": "Tri", "o1_angle": 40,
                       "o2_level": 50, "o2_freq": 0.08, "flt_cut": hz(2000), "lad_env": 25, "voices": "8",
                       "spread": 80, "key_root": "F", "key_scale": "Lydian", "phys_parts_on": "On",
                       "phys_keys": "Free", "phys_rate": 30, "phys_spread": 45, "phys_life": 60,
                       "phys_trigger": "Crossing Line", "phys_octave": "C3", "phys_range": "3", "phys_max": 5,
                       **rows(("Particle Count", "Ladder Cutoff", 15), (MW, "Ladder Cutoff", 30),
                              (AT, "Send 1 (FX1)", 25), (PY, "Send 2 (FX2)", 25)),
                       **chorus(0.3, 8, 30, 25), **delay("1/4.", 40, 22, True), **space(32, 4.0)},
    "Generative", "Soft")
add("Rain On Tin", {**amp(1, 600, 0, 500), **menv(1, 250, 0, 200), "o1_wave": "Tri", "o1_angle": 50, "o1_level": 0,
                    "o1_oct": "4'", "o1_freq": 5.0, "o2_wave": "Tri", "o2_angle": 50, "o2_level": 100, "fm_amt": 25,
                    "flt_cut": hz(7000), "voices": "8", "key_root": "C", "key_scale": "Pent Major",
                    "rain_on": "On", "rain_keys": "Free", "rain_rate": 62, "rain_echo": 50, "rain_stones": 3,
                    "rain_octave": "C4", "rain_range": "2", "rain_max": 6,
                    **rows(("Mod Env", "FM Amount", 20), ("Ripple Energy", "Send 2 (FX2)", 15),
                           (MW, "FM Amount", 25), (PY, "Send 2 (FX2)", 25)),
                    **delay("1/8.", 30, 15, True), **space(25, 2.8)}, "Generative", "Clean")
add("Markov Daydream", {**amp(2, 1600, 30, 700), **fenv(1, 900, 20, 500), "o1_wave": "Tri", "o1_angle": 50,
                        "o2_level": 45, "o2_oct": "4'", "o2_mix": 100, "flt_cut": hz(2600), "lad_env": 25,
                        "voices": "6", "key_root": "A", "key_scale": "Minor", "mkv_on": "On", "mkv_keys": "Free",
                        "mkv_creat": 40, "mkv_rhythm": "Learned", "mkv_rate": "1/8", "mkv_gate": 75,
                        "mkv_range": "2", "mkv_max": 6,
                        **rows(("Markov Height", "Ladder Cutoff", 15), (MW, "Synth FX Depth", 40),
                               (AT, "Ladder Cutoff", 20), (PY, "Send 2 (FX2)", 25)),
                        **chorus(0.5, 6, 30, 30), **delay("1/4.", 30, 15, True), **space(25, 2.5)},
    "Generative", "Warm")
add("Life Cells", {**amp(1, 500, 0, 400), **fenv(1, 300, 0, 250), "o2_level": 60, "o2_freq": 0.07,
                   "flt_cut": hz(1200), "flt_res": 30, "lad_env": 40, "voices": "8", "key_root": "E",
                   "key_scale": "Phrygian", "life_on": "On", "life_keys": "Free", "life_rate": "1/8",
                   "life_mode": "Scan Column", "life_rule": "Life", "life_density": 30, "life_seed": 7,
                   "life_notes": 2, "life_octave": "C3", "life_range": "2", "life_max": 6,
                   **rows(("Life Population", "Ladder Cutoff", 20), (MW, "Ladder Res", 30), (AT, "Ladder Cutoff", 20),
                          (PY, "Send 2 (FX2)", 25)),
                   **delay("1/8.", 35, 20, True), **space(20, 2.2)}, "Generative", "Dark")
add("Orbit Music", {**MALLET, "key_root": "G", "key_scale": "Mixolydian", "orb_on": "On", "orb_keys": "Free",
                    "orb_planets": 3, "orb_speed": 40, "orb_ecc": 30, "orb_spread": 60, "orb_spokes": 4,
                    "orb_quant": "1/16", "orb_octave": "C3", "orb_range": "3", "orb_max": 6,
                    **rows(("Orbit 1 X", "Pan", 40), (MW, "Ladder Cutoff", 30), (AT, "Send 1 (FX1)", 25),
                           (PY, "Send 2 (FX2)", 25)),
                    **chorus(0.3, 8, 25, 20), **delay("1/8.", 30, 18, True), **space(28, 3.0)},
    "Generative", "Bright")
add("Pendulum Wave", {**amp(1, 2500, 0, 1800), **menv(1, 1000, 0, 700), "o1_wave": "Tri", "o1_angle": 50,
                      "o1_level": 0, "o1_oct": "4'", "o1_freq": 2.0, "o2_wave": "Tri", "o2_angle": 50, "o2_level": 100, "fm_amt": 22,
                      "flt_cut": hz(8000), "voices": "8", "key_root": "C", "key_scale": "Major", "pend_on": "On",
                      "pend_keys": "Free", "pend_count": 7, "pend_cycle": "4 Bars", "pend_speed": 6,
                      "pend_spread": 2, "pend_trigger": "Centre", "pend_octave": "C4", "pend_max": 6,
                      **rows(("Mod Env", "FM Amount", 20), ("Pendulum 1", "Pan", 30), (MW, "FM Amount", 25),
                             (PY, "Send 2 (FX2)", 25)),
                      **space(35, 4.0)}, "Generative", "Clean")
add("Euclid Turing Night", {**amp(1, 700, 0, 500), **fenv(1, 400, 10, 300), "o1_wave": "Tri", "o1_angle": 30,
                            "o2_level": 55, "o2_freq": 0.06, "flt_cut": hz(1500), "flt_res": 30, "lad_env": 35,
                            "voices": "8",
                            # hold a key (latched): a 5-of-12 Euclidean rhythm plays a Turing machine melody in minor
                            "gen_euclid_on": "On", "gen_steps": 12, "gen_hits": 5, "gen_rot": 1, "gen_prob": 85,
                            "gen_vel_rand": 35, "gen_tm_on": "On", "gen_tm_len": 8, "gen_tm_change": 20,
                            "gen_scale": "Minor", "gen_range": "2", "arp_div": "1/16", "arp_gate": 60,
                            "arp_latch": "On",
                            **rows(("Turing Value", "Ladder Cutoff", 20), ("Euclid Gate", "Send 1 (FX1)", 10),
                                   (MW, "Ladder Res", 30), (AT, "Ladder Cutoff", 20), (PY, "Send 2 (FX2)", 25)),
                            **delay("1/8.", 40, 25, True), **space(22, 2.8)}, "Generative", "Dark")
add("Two Worlds", {**MALLET, "key_root": "D", "key_scale": "Pent Minor", "rain_on": "On", "rain_keys": "Free",
                   "rain_rate": 25, "rain_echo": 40, "rain_stones": 2, "rain_octave": "C4", "rain_range": "2",
                   "rain_max": 4, "volume": 55, "pan": -25, "mfx_send1": 25, "mfx_send2": 25,
                   # Synth 2: a soft pad voice whose orbiting planets play low, in the same key
                   **synth(2, {**amp(400, 1500, 70, 1500), **fenv(300, 1200, 40, 1000), "o2_level": 70,
                               "o2_freq": 0.08, "flt_cut": hz(900), "lad_env": 20, "voices": "6", "syn_on": "On",
                               "key_root": "D", "key_scale": "Pent Minor", "orb_on": "On", "orb_keys": "Free",
                               "orb_planets": 2, "orb_speed": 25, "orb_quant": "1/8", "orb_octave": "C2",
                               "orb_range": "2", "orb_max": 3, "orb_len": 80, "volume": 42, "pan": 25,
                               "mfx_send2": 30, **chorus(0.25, 9, 35, 30)}),
                   **delay("1/8.", 35, 0, True), **space(0, 3.5)}, "Generative", "Evolving", "Layer")
add("Three Body Ensemble", {**amp(2, 500, 60, 200), **fenv(1, 300, 20, 150), "o1_oct": "16'", "o2_level": 70,
                            "o2_freq": 0.05, "flt_cut": hz(400), "flt_res": 30, "lad_env": 35, "voices": "4",
                            "key_root": "A", "key_scale": "Dorian", "pend_on": "On", "pend_keys": "Free",
                            "pend_count": 3, "pend_cycle": "8 Bars", "pend_speed": 4, "pend_octave": "C2",
                            "pend_max": 3, "pend_len": 60, "volume": 55,
                            **synth(2, {**MALLET, "syn_on": "On", "key_root": "A", "key_scale": "Dorian",
                                        "phys_balls_on": "On", "phys_keys": "Free", "phys_balls": 3,
                                        "phys_speed": 30, "phys_octave": "C4", "phys_range": "2", "phys_max": 4,
                                        "voices": "6", "volume": 40, "pan": -30, "mfx_send1": 30, "mfx_send2": 25}),
                            **synth(3, {**amp(5, 1200, 50, 600), **fenv(1, 600, 30, 400), "o1_mix": 100,
                                        "o1_pw": 40, "o2_level": 50, "o2_freq": 0.06, "flt_cut": hz(2000),
                                        "lad_env": 25, "voices": "4", "syn_on": "On", "key_root": "A",
                                        "key_scale": "Dorian", "mkv_on": "On", "mkv_keys": "Free", "mkv_creat": 30,
                                        "mkv_rate": "1/8", "mkv_range": "2", "mkv_max": 4,
                                        "volume": 36, "pan": 30, "mfx_send1": 25, "mfx_send2": 25,
                                        **chorus(0.4, 7, 25, 25)}),
                            **delay("1/8.", 35, 0, True), **space(0, 3.0)}, "Generative", "Warm", "Layer")
add("Held Rain", {**amp(1, 1000, 0, 800), **menv(1, 400, 0, 300), "o1_wave": "Tri", "o1_angle": 50, "o1_level": 0,
                  "o1_oct": "4'", "o1_freq": 0.0, "o2_wave": "Tri", "o2_angle": 50, "o2_level": 100, "fm_amt": 15,
                  "flt_cut": hz(6000), "voices": "8", "key_root": "E", "key_scale": "Minor",
                  # hold a chord: the drops play its notes (RAIN KEYS: HELD KEYS); let go and the rain stops
                  "rain_on": "On", "rain_keys": "Held Keys", "rain_rate": 50, "rain_echo": 60, "rain_stones": 4,
                  "rain_range": "2", "rain_max": 8,
                  **rows(("Mod Env", "FM Amount", 15), ("Rain Drop", "Ladder Cutoff", 10), (MW, "FM Amount", 25),
                         (PY, "Send 2 (FX2)", 25)),
                  **delay("1/8.", 30, 18, True), **space(28, 3.2)}, "Generative", "Soft")
add("Seeds Of Life", {**amp(1, 350, 0, 300), **fenv(1, 200, 0, 150), "o1_wave": "Tri", "o1_angle": 30,
                      "o2_level": 50, "o2_oct": "4'", "o2_freq": 0.1, "flt_cut": hz(2200), "flt_res": 25,
                      "lad_env": 35, "voices": "8", "key_root": "B", "key_scale": "Pent Minor", "life_on": "On",
                      "life_keys": "Free", "life_rate": "1/16", "life_mode": "Births", "life_rule": "Seeds",
                      "life_density": 20, "life_seed": 42, "life_notes": 2,
                      "life_octave": "C3", "life_range": "3", "life_max": 6,
                      **rows(("Life Population", "Send 1 (FX1)", 15), (MW, "Ladder Cutoff", 30),
                             (AT, "Ladder Res", 20), (PY, "Send 2 (FX2)", 25)),
                      **delay("1/16", 35, 20, True), **space(20, 2.2, rev_type="Plate")}, "Generative", "Aggressive")


# ---------------------------------------------------------------- loudness trim (dB per sound, bank_b_checks)
TRIM = {
    'Acid Spiral': -5.4,
    'Analog Dusk': -11.5,
    'Arp Over Pad': -4.4,
    'Aurora Bloom': -4.8,
    'Bass Pad Lead Split': -1.5,
    'Bowed Metal': -10.4,
    'Brass And Strings': -5.1,
    'Breathing Room': -6.3,
    'Broken Music Box': -3.3,
    'Bubble Pool': -6.8,
    'Celesta Box': -3.9,
    'Cello Ensemble': -7.6,
    'Chamber Bows': -8.1,
    'Clap Stack': 12.8,
    'Clockwork Bells': -1.5,
    'Cloud Chamber': -3.2,
    'Colossus Stack': -6.9,
    'Crystal Tines': -4.1,
    'Dark Carillon': -3.7,
    'Dark Ensemble': -5.6,
    'Deep Drone Field': -6.6,
    'Distant Signal': -6.6,
    'Dream Layer': -2.3,
    'Dusty Arp': -5.3,
    'Ember Choir': 11.8,
    'Euclid Turing Night': 1.8,
    'Falling Star': -5.3,
    'Floor Tom': -5.5,
    'Frost Strings': -10.3,
    'Frozen Lake': -7.3,
    'Gated Shimmer': 1.3,
    'Glass Bows': -7.7,
    'Glass Harmonica Pad': 13.9,
    'Glass Ladder': -1.7,
    'Golden Hour': -4.1,
    'Granular Mist': -2.4,
    'Grit Strings': -9.0,
    'Half-Time Groove': -5.5,
    'Halo Swell': -7.0,
    'Impact Boom': -1.3,
    'Interval Walk': 1.8,
    'Kalimba Rain': -11.6,
    'Keys Over Bass': -1.2,
    'Laser Grid': 18.6,
    'Left Hand Groove': -4.1,
    'Life Cells': 2.6,
    'Low Cathedral': -7.5,
    'Macro Kick Deep': 1.8,
    'Markov Daydream': 1.1,
    'Metal Scrape': -3.6,
    'Midnight Pulse': -3.8,
    'Modal Chimes': 16.9,
    'Mono Duo Lead': -2.5,
    'Moving Stillness': -6.0,
    'Night Drive Bass': -5.6,
    'Night Glass': -6.9,
    'Octave Bounce': 1.8,
    'Open Hat Sizzle': 1.9,
    'Orbit Music': 3.0,
    'Orbit Strings': 1.3,
    'Orchestra Split': -4.7,
    'Order Of Things': 9.7,
    'Particle Drift': 1.6,
    'Pendulum Keys': -1.8,
    'Pendulum Wave': -4.2,
    'Pentatonic Run': -4.3,
    'Polar Night': -6.9,
    'Pressure Bloom': -3.0,
    'Prism Pad': -4.0,
    'Quartet Air': -7.6,
    'Radio Ghost': 3.7,
    'Rain On Tin': 6.6,
    'Random Fireflies': 2.9,
    'Ratchet Dirt': -2.9,
    'Ratchet Lights': 2.4,
    'Rising Tide': -4.0,
    'Round Kick': -4.0,
    'Rust Belt': -10.0,
    'Seeds Of Life': 9.1,
    'Sequenced Trio': -3.3,
    'Short Loop': -3.8,
    'Signal Flare': -4.0,
    'Silk Section': -6.6,
    'Six-Op Glass': 11.2,
    'Slow Tide': -2.9,
    'Snap Snare': 6.3,
    'Soft Focus': -6.4,
    'Solo Fiddle': -7.9,
    'Space Freeze': -5.1,
    'Split Acid Pad': -1.3,
    'Stab Cascade': -1.7,
    'Staccato Run': 13.0,
    'Stack Of Fifths': -3.6,
    'Static Storm': 3.3,
    'Subterranean': -2.3,
    'Sweep Machine': -11.5,
    'Swell Strings': -3.0,
    'Swing Pluck': 1.9,
    'Sync Haze': -6.0,
    'Tape Memory': -8.9,
    'Three Body Ensemble': -2.1,
    'Tight Hat': 6.3,
    'Tremolo Violas': -6.0,
    'Triple Stack Sequence': -3.1,
    'Triplet Machine': -1.7,
    'Triplet Rain': 2.5,
    'Tubular Hall': -6.6,
    'Two Worlds': 2.4,
    'Undertow': -6.7,
    'Velour': -4.1,
    'Velvet Horizon': -5.3,
    'Wavetable Drift': 7.3,
    'Wide Orchestra': -9.6,
    'Wide Screen': -5.0,
    'Wood And Metal': 16.3,
}


def _trim(values, db):
    if not db:
        return values
    out = dict(values)
    g = 10 ** (db / 20.0)
    for pre in ("", "s2_", "s3_"):
        if pre and out.get(pre + "syn_on") != "On":
            continue
        k = pre + "volume"
        out[k] = round(min(100.0, max(1.0, out.get(k, 70) * g)), 1)
    return out


BANK = [(n, _trim(v, TRIM.get(n, 0)), t) for n, v, t in B]
