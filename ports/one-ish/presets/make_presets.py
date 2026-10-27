#!/usr/bin/env python3
"""Writes ../presets.json from the patches below: every preset sets every parameter (params.json defaults, then the
patch's changes), so loading one never leaves a value from the last. Patches are written in musical units and turned
into each knob's 0..100 position here: envelope times in ms, cutoffs and LFO rates in Hz, tempo divisions by name.
The first preset is the plugin's default sound. Original sounds, not copies of any manufacturer's factory presets.
    python3 ports/one-ish/presets/make_presets.py"""
import json
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))
PARAMS = json.load(open(os.path.join(HERE, "..", "params.json"), encoding="utf-8"))["params"]
DIVS = ["4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32"]   # engine.c SYNC_DIV
LFO_DIVS = ["16 bars", "8 bars", "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64", "1/128",
            "1/256"]   # engine.c LFO_DIV (dotted and triplet timing are lfoN_timing)


def ms(t):        # envelope knobs: 1 ms .. 10 s, log (delay and hold: 0 is off)
    return round(max(0.0, 25 * math.log10(max(t, 1))), 1)


def hz(f):        # cutoff knobs: 20 Hz .. 20 kHz, log
    return round(100 * math.log(f / 20) / math.log(1000), 1)


def lfo(f):       # LFO rate knobs in the default Low range: 0.01 .. 10 Hz, log
    return round(100 * math.log(f / 0.01) / math.log(1000), 1)


def lfo_div(name):   # a synced LFO's rate knob picks a division
    return round(100 * LFO_DIVS.index(name) / (len(LFO_DIVS) - 1), 1)


def div(name):    # a synced delay time knob picks a division
    return round(100 * DIVS.index(name) / (len(DIVS) - 1), 1)


def env_div(name):   # a synced envelope time knob picks a division, longest on the right (engine.c env_div)
    return round(100 * (len(DIVS) - 1 - DIVS.index(name)) / (len(DIVS) - 1), 1)


# the effects (src/fx/common.h): their own 21 tempo divisions and log knobs
FXDIVS = ["1/64T", "1/64", "1/64.", "1/32T", "1/32", "1/32.", "1/16T", "1/16", "1/16.", "1/8T", "1/8", "1/8.", "1/4T",
          "1/4", "1/4.", "1/2T", "1/2", "1/2.", "1", "2", "4"]


def logk(v, lo, hi):
    return round(100 * math.log(v / lo) / math.log(hi / lo), 1)


def fxdiv(name):     # a synced delay time knob picks a division, shortest on the left
    return round(100 * FXDIVS.index(name) / (len(FXDIVS) - 1), 1)


def dms(t):          # delay time knobs: 0.1 ms .. 2 s, log
    return logk(t, 0.1, 2000)


def fxrate(f):       # synth effect rate knobs: 0.01 .. 20 Hz, log
    return logk(f, 0.01, 20)


def rt(sec):         # reverb decay knob: 0.25 .. 10 s at size 100%, log
    return logk(sec, 0.25, 10)


def chorus(rate_hz, time_ms, depth, mix):   # the synth effect as a chorus (time: 0.1 .. 50 ms, log)
    return {"modfx_on": "On", "modfx_type": "Chorus", "modfx_rate": fxrate(rate_hz), "modfx_time": logk(time_ms, 0.1, 50),
            "modfx_depth": depth, "modfx_mix": mix}


def delay(time, fb, send, synced=False):   # the master delay, both sides at one time (ms, or a division name)
    t = fxdiv(time) if synced else dms(time)
    return {"dly_sync": "On" if synced else "Off", "dly_time": t, "dly_time_r": t, "dly_fb": fb, "mfx_send1": send}


def reverb(send, sec=None, **kw):   # the master reverb on send 2
    return dict({"mfx_send2": send, **({"rev_decay": rt(sec)} if sec else {})}, **kw)


def amp(a, d, s, r):
    return {"amp_a": ms(a), "amp_d": ms(d), "amp_s": s, "amp_r": ms(r)}


def fenv(a, d, s, r):
    return {"fenv_a": ms(a), "fenv_d": ms(d), "fenv_s": s, "fenv_r": ms(r)}


def menv(a, d, s, r):
    return {"menv_a": ms(a), "menv_d": ms(d), "menv_s": s, "menv_r": ms(r)}


PATCHES = [
    ("Init", {}),
    ("Fat Bass", {**amp(2, 400, 80, 120), **fenv(1, 350, 20, 200), "amp_hold": ms(30),
                  "o2_level": 85, "o2_freq": -0.07, "o3_level": 70, "o3_oct": "16'", "o3_mix": 100,
                  "flt_cut": hz(220), "flt_res": 25, "flt_drive": 35, "lad_env": 45, "lad_kbd": 30, "voices": "Mono"}),
    ("Pluck Bass", {**amp(1, 300, 0, 80), **fenv(1, 180, 0, 80),
                    "o2_level": 60, "o2_oct": "16'", "o2_mix": 100,
                    "flt_cut": hz(140), "flt_res": 55, "lad_env": 60, "lad_kbd": 40, "voices": "Mono"}),
    ("Acid Line", {**amp(1, 250, 60, 60), **fenv(1, 220, 0, 60),
                   "o1_mix": 100, "flt_cut": hz(260), "flt_res": 82, "flt_drive": 55, "lad_env": 55,
                   "lad_kbd": 50, "flt_slope": "24 dB", "voices": "Mono",
                   "mod2_src": "Velocity", "mod2_dst": "Ladder Cutoff", "mod2_amt": 20,
                   "glide": "On", "glide_type": "LCT", "glide_time": ms(70), "glide_legato": "On"}),
    ("Sync Lead", {**amp(3, 500, 85, 250), **menv(1, 900, 0, 300),
                   "o1_level": 0, "o2_level": 100, "o2_sync": "On", "o2_freq": 7, "o3_level": 55, "o3_freq": 0.06,
                   "o3_oct": "16'", "flt_cut": hz(5000), "flt_res": 20, "mod3_src": "Mod Env", "mod3_dst": "Osc 2 Pitch",
                   "mod3_amt": 40, "mod1_amt": 3, "voices": "Mono", "glide": "On", "glide_time": ms(90),
                   **delay("1/8T", 30, 25, True), **reverb(15), "mfx_series": 30}),
    ("Brass Stack", {**amp(25, 600, 75, 300), **fenv(60, 700, 45, 350),
                     "o2_level": 90, "o2_freq": 0.06, "o3_level": 70, "o3_freq": -0.05,
                     "flt_cut": hz(400), "flt_res": 15, "lad_env": 50, "lad_kbd": 50,
                     "mod2_src": "Velocity", "mod2_dst": "Ladder Cutoff", "mod2_amt": 25, "voices": "6",
                     **reverb(20, 1.9)}),
    ("Warm Pad", {**amp(700, 1500, 85, 1800), **fenv(1200, 2500, 50, 2000),
                  "o2_level": 85, "o2_freq": 0.09, "o3_level": 70, "o3_freq": -0.08, "o3_oct": "4'",
                  "flt_cut": hz(700), "flt_res": 20, "lad_env": 25, "lad_lfo": 10, "lfo2_rate": lfo(0.25),
                  "lfo2_wave": "Sine", "voices": "8", "spread": 60, "drift": 50,
                  **chorus(0.25, 8.8, 41, 37), **reverb(35, 3.5)}),
    ("Glass Keys", {**amp(2, 1600, 0, 900), **fenv(1, 900, 10, 700),
                    "o1_mix": 100, "o2_level": 60, "o2_oct": "4'", "o2_mix": 100, "o2_freq": 0.04,
                    "o1_svf": "On", "o1_lad": "Off", "o2_svf": "On", "o2_lad": "Off", "svf_mode": "BP", "svf_cut": hz(1800), "svf_res": 45,
                    "svf_env": 35, "svf_kbd": 70, "flt_mix": 0, "voices": "8",
                    **reverb(30, 2.8, rev_pre=25)}),
    ("Hollow Strings", {**amp(300, 1200, 80, 900),
                        "o2_level": 90, "o2_freq": 0.12, "o3_level": 80, "o3_freq": -0.11,
                        "o1_svf": "On", "o2_svf": "On", "o3_svf": "On", "svf_mode": "HP", "svf_cut": hz(300),
                        "flt_route": "Series", "flt_cut": hz(3500), "flt_res": 10, "pitch_lfo": 1, "lfo1_rate": lfo(5.5),
                        "lfo1_wave": "Sine", "voices": "8",
                        **chorus(0.42, 8.5, 35, 40), **reverb(30)}),
    ("Wobble Bass", {**amp(2, 400, 90, 150),
                     "o2_level": 80, "o2_oct": "16'", "o2_mix": 100, "flt_cut": hz(180), "flt_res": 60,
                     "flt_drive": 45, "lad_lfo": 55, "lfo2_wave": "Sine", "lfo2_sync": "On", "lfo2_rate": lfo_div("1/8"),
                     "voices": "Mono"}),
    ("Self-Osc Whistle", {**amp(40, 800, 90, 600),
                          "o1_level": 0, "noise_level": 0, "flt_res": 100, "flt_cut": hz(440), "lad_kbd": 100,
                          "pitch_lfo": 1, "lfo1_rate": lfo(5), "lfo1_wave": "Sine", "voices": "4"}),
    ("Noise Hat", {**amp(1, 90, 0, 60),
                   "o1_level": 0, "noise_level": 100, "flt_mode": "HP", "flt_cut": hz(6000), "flt_res": 30,
                   "voices": "8"}),
    ("Notch Sweep", {**amp(10, 900, 85, 600),
                     "o2_level": 90, "o2_freq": 0.07, "o1_svf": "On", "o1_lad": "Off", "o2_svf": "On", "o2_lad": "Off", "svf_mode": "Notch",
                     "svf_cut": hz(800), "svf_res": 40, "svf_lfo": 45, "lfo2_rate": lfo(0.4), "lfo2_wave": "Triangle",
                     "flt_mix": 0, "voices": "6",
                     "modfx_on": "On", "modfx_type": "6-Stage Phaser", "modfx_rate": fxrate(0.19), "modfx_depth": 70,
                     "modfx_fb": 40, "modfx_mix": 40}),
    ("FM Bell", {**amp(1, 2500, 0, 1500), **menv(1, 1200, 0, 800),
                 "o1_wave": "Tri", "o1_angle": 50, "o1_level": 0, "o1_oct": "4'", "o1_freq": 2,
                 "o2_wave": "Tri", "o2_angle": 50, "o2_level": 100, "fm_amt": 45, "fm_route": "1 > 2",
                 "mod2_src": "Mod Env", "mod2_dst": "FM Amount", "mod2_amt": 40, "voices": "8",
                 **reverb(30, 3.1)}),
    ("Ring Clang", {**amp(1, 1400, 0, 900), **fenv(1, 600, 20, 500),
                    "o1_level": 0, "o2_freq": 5.5, "o2_oct": "4'", "ring_level": 100, "ring_src": "1 x 2",
                    "flt_cut": hz(2500), "lad_env": 30, "voices": "6",
                    **delay(184, 30, 20)}),
    ("PWM Strings", {**amp(250, 1000, 85, 900),
                     "o1_mix": 100, "o1_pw": 35, "o2_mix": 100, "o2_pw": 65, "o2_level": 90, "o2_freq": 0.1,
                     "mod2_src": "LFO 1", "mod2_dst": "Osc 1 PW", "mod2_amt": 25, "mod3_src": "LFO 2",
                     "mod3_dst": "Osc 2 PW", "mod3_amt": -25, "lfo1_rate": lfo(0.7), "lfo2_rate": lfo(0.45),
                     "lfo1_wave": "Sine", "lfo2_wave": "Sine", "flt_cut": hz(3000), "flt_res": 10, "voices": "8",
                     **chorus(0.25, 8.5, 35, 33), **reverb(30)}),
    ("Angle Lead", {**amp(3, 600, 80, 250), **menv(1, 800, 30, 400),
                    "o1_wave": "Tri", "o1_angle": 30, "o2_wave": "Saw", "o2_angle": 60, "o2_level": 70,
                    "o2_beat": 1.5, "mod2_src": "Mod Env", "mod2_dst": "Osc 1 Angle", "mod2_amt": 40,
                    "flt_cut": hz(3500), "flt_res": 25, "pitch_lfo": 2, "lfo1_rate": lfo(5.5), "lfo1_wave": "Sine",
                    "voices": "Mono", **delay("1/8", 25, 20, True)}),
    ("Unison Lead", {**amp(4, 700, 85, 300), **fenv(2, 600, 40, 300),
                     "o2_level": 80, "o2_freq": 0.05, "o3_level": 60, "o3_oct": "16'", "o3_mix": 100, "o3_pw": 40,
                     "flt_cut": hz(1400), "flt_res": 25, "flt_drive": 30, "lad_env": 30, "lad_kbd": 50,
                     "voices": "Mono", "unison": "On", "uni_count": "6", "uni_detune": 35, "pitch_var": 6,
                     "glide": "On", "glide_type": "EXP", "glide_time": ms(150), "glide_legato": "On",
                     "mod1_amt": 3, **delay("1/8", 30, 20, True), **reverb(20), "mfx_series": 30}),
    ("Vowel Pair", {**amp(80, 1200, 85, 900),
                    "o2_level": 80, "o2_freq": 0.08, "o1_svf": "On", "o1_lad": "Off", "o2_svf": "On", "o2_lad": "Off",
                    "svf_mode": "BP", "svf_pair": "Parallel", "svf_space": 35, "svf_cut": hz(550), "svf_res": 75,
                    "svf_lfo": 25, "lfo2_rate": lfo(0.3), "lfo2_wave": "Sine", "svf_kbd": 50, "flt_mix": 0,
                    "voices": "6", **chorus(0.25, 8.2, 29, 29), **reverb(30, 2.5)}),
    ("FM Growl", {**amp(2, 500, 80, 150), **fenv(1, 400, 30, 200),
                  "o2_level": 0, "o2_oct": "16'", "o2_mix": 100, "o1_svf": "On", "o1_lad": "Off",
                  "svf_mode": "LP", "svf_pair": "Series", "svf_cut": hz(350), "svf_res": 45, "svf_env": 40,
                  "svf_kbd": 60, "svf_fm": 30, "svf_fm_src": "Osc 2", "flt_mix": 0, "voices": "Mono"}),
    ("Wind Spread", {**amp(400, 1500, 90, 1500),
                     "o1_level": 35, "o1_wave": "Tri", "o1_angle": 50, "noise_level": 90, "noise_color_pair": "Red+White",
                     "noise_color_mix": 40, "noise_env": "On", "noise_a": ms(900), "noise_r": ms(1200),
                     "flt_cut": hz(1800), "flt_res": 45, "lad_lfo": 30, "lfo2_rate": lfo(0.15), "lfo2_wave": "Sine",
                     "lad_kbd": 50, "voices": "8", "spread": 100, "drift": 70, **reverb(40, 4.0)}),
    ("Pulse Pad", {"amp_sync": "On", "amp_loop": "On", "amp_a": env_div("1/32"), "amp_d": env_div("1/32"), "amp_s": 60,
                   "amp_r": env_div("1/16"), **fenv(600, 2000, 40, 1500),
                   "o2_level": 85, "o2_freq": 0.08, "o3_level": 60, "o3_oct": "16'", "o3_freq": -0.06,
                   "flt_cut": hz(900), "flt_res": 25, "lad_env": 30, "voices": "8",
                   **chorus(0.25, 8.5, 35, 33), **delay("1/8T", 30, 20, True),
                   **reverb(30, 3.1), "mfx_series": 30}),
    # LFO 3 sweeps the wave angle and pulse width (the hard-wires); LFO 1 is a delayed vibrato that fades in after each
    # note-on; LFO 4 is smooth random wander on the SVF, kept positive by a Limit Low transform; velocity shortens the
    # filter envelope's decay; the mod wheel speeds LFO 3 up; aftertouch opens the ladder, squared for a gentle start.
    ("Morph Motion", {**amp(30, 1200, 85, 900), **fenv(5, 900, 35, 700),
                      "o1_wave": "Tri", "o1_angle": 50, "o1_mix": 40, "o2_level": 80, "o2_freq": 0.07, "o2_mix": 60,
                      "angle_lfo3": 60, "pw_lfo3": 50, "lfo3_rate": lfo(0.35), "lfo3_wave": "Sine",
                      "pitch_lfo": 2, "lfo1_rate": lfo(5.5), "lfo1_wave": "Triangle", "lfo1_var": 70,
                      "lfo1_reset": "On", "lfo1_delay": ms(350), "lfo1_fade_in": ms(800), "mod1_amt": 0,
                      "o1_svf": "On", "o2_svf": "On", "svf_mode": "BP", "svf_cut": hz(900), "svf_res": 35,
                      "flt_mix": 60, "flt_cut": hz(1500), "flt_res": 20, "lad_env": 35, "lad_kbd": 50,
                      "lfo4_wave": "Noise", "lfo4_rate": lfo(0.8), "lfo4_smooth": 30,
                      "mod2_src": "LFO 4", "mod2_dst": "SVF Cutoff", "mod2_amt": 40, "mod2_xf": "Limit Low",
                      "mod2_xf_amt": 50,
                      "mod3_src": "Velocity", "mod3_dst": "Filter Env Decay", "mod3_amt": -20,
                      "mod4_src": "Mod Wheel", "mod4_dst": "LFO 3 Rate (st)", "mod4_amt": 100,
                      "mod5_src": "Aftertouch", "mod5_dst": "Ladder Cutoff", "mod5_amt": 50, "mod5_xf": "Squared",
                      "voices": "6", **chorus(0.25, 8.5, 45, 35), **reverb(30, 2.7)}),
    ("Ensemble Strings", {**amp(350, 1500, 85, 1200), **fenv(500, 1500, 60, 1000),
                          "o1_mix": 100, "o1_pw": 45, "o2_level": 85, "o2_freq": 0.1, "flt_cut": hz(2500), "flt_res": 5,
                          "lad_env": 15, "voices": "8",
                          "modfx_on": "On", "modfx_type": "Ensemble", "modfx_rate": fxrate(0.6), "modfx_time": 45,
                          "modfx_depth": 50, "modfx_vdepth": 25, "modfx_spread": 80, "modfx_mix": 50,
                          **reverb(30, 3.0, rev_type="Hall", rev_size=60, rev_pre=30)}),
    ("Tape Echo Lead", {**amp(3, 600, 80, 300), **fenv(1, 500, 40, 300),
                        "o2_level": 70, "o2_freq": 0.05, "flt_cut": hz(2200), "flt_res": 30, "lad_env": 25,
                        "voices": "Mono", "glide": "On", "glide_time": ms(60), "pitch_lfo": 1, "lfo1_rate": lfo(5),
                        "lfo1_wave": "Sine", "mod1_amt": 3,
                        "modfx_on": "On", "modfx_type": "Tape Delay", "modfx_sync": "On", "modfx_dtime": fxdiv("1/4."),
                        "modfx_fb": 45, "modfx_wow": 35, "modfx_flutter": 25, "modfx_bias": 40, "modfx_tone": 70,
                        "modfx_mix": 30, **reverb(20, 1.2, rev_type="Room")}),
    ("Crushed Keys", {**amp(2, 900, 30, 400), **fenv(1, 500, 20, 300),
                      "o1_wave": "Tri", "o1_angle": 50, "o2_level": 60, "o2_oct": "4'", "o2_mix": 100,
                      "flt_cut": hz(5000), "lad_env": 20, "voices": "6",
                      "modfx_on": "On", "modfx_type": "Bit Crusher", "modfx_bits": 6, "modfx_srate": logk(8000, 128, 44100),
                      "modfx_mix": 60, **delay(330, 25, 15)}),
    # the ANALOG section: every voice card a little different (cutoff, resonance, envelope times, level, tuning), slow
    # drift, and the non-linear ladder, so each note of a chord sounds slightly its own
    ("Vintage Poly Pad", {**amp(450, 1800, 80, 1400), **fenv(700, 2200, 45, 1500),
                          "o1_mix": 30, "o1_pw": 40, "o2_level": 85, "o2_freq": 0.07, "o3_level": 55, "o3_oct": "16'",
                          "flt_cut": hz(900), "flt_res": 35, "flt_drive": 25, "lad_env": 30, "lad_kbd": 40,
                          "voices": "8", "spread": 70,
                          "an_cut": 60, "an_env": 70, "an_lvl": 60, "an_tune": 50, "an_res": 50, "an_ladder": 80,
                          "drift": 60, **chorus(0.3, 9, 30, 30), **reverb(30, 3.2)}),
    # the ARP section: hold a chord; it climbs three octaves and back (pendulum), in 1/16s with a little swing, synced
    ("Arp Cascade", {**amp(1, 260, 25, 140), **fenv(1, 220, 0, 120),
                     "o2_level": 60, "o2_freq": 0.07, "flt_cut": hz(900), "flt_res": 40, "lad_env": 45, "voices": "8",
                     "arp_on": "On", "arp_pattern": "Up", "arp_oct": "3", "arp_dir": "Up/Down", "arp_pendulum": "On",
                     "arp_div": "1/16", "arp_gate": 45, "arp_swing": 56,
                     **delay("1/8.", 35, 20, synced=True), **reverb(20, 2.2)}),
    # the GENERATIVE section: play one key (latched); a Euclidean rhythm (7 of 16) plays a slowly changing Turing machine
    # melody in minor pentatonic over two octaves from it, some steps skipped, velocities varied
    ("Euclid Turing Bells", {**amp(1, 1400, 0, 900), **fenv(1, 600, 10, 500),
                             "o1_wave": "Tri", "o1_angle": 50, "o2_level": 45, "o2_oct": "4'", "o2_wave": "Tri",
                             "o2_angle": 50, "flt_cut": hz(3500), "lad_env": 20, "voices": "8",
                             "gen_euclid_on": "On", "gen_steps": 16, "gen_hits": 7, "gen_rot": 2, "gen_prob": 90,
                             "gen_vel_rand": 30, "gen_tm_on": "On", "gen_tm_len": 8, "gen_tm_change": 12,
                             "gen_scale": "Pent Minor", "gen_range": "2", "arp_gate": 70, "arp_latch": "On",
                             **delay("1/8.", 45, 30, synced=True), **reverb(30, 3.0)}),
    # the MACRO channel (Plaits): a plucked string through the ladder; a wavetable pad whose morph an LFO sweeps; 6-op
    # FM keys with a little of the saw under them
    ("Macro String", {**amp(1, 2500, 0, 1200), "o1_level": 0, "mac_model": "String", "mac_level": 90,
                      "mac_harm": 35, "mac_timbre": 60, "mac_morph": 65, "flt_cut": hz(6000), "voices": "6",
                      **delay(280, 25, 12), **reverb(20, 2.0)}),
    ("Macro Wave Pad", {**amp(600, 1500, 85, 1600), **fenv(800, 2000, 50, 1500),
                        "o1_level": 0, "mac_model": "Wavetable", "mac_level": 85, "mac_harm": 30, "mac_timbre": 45,
                        "mac_morph": 20, "flt_cut": hz(2500), "flt_res": 15, "lad_env": 20, "voices": "8",
                        "lfo2_rate": lfo(0.15), "lfo2_wave": "Sine",
                        "mod2_src": "LFO 2", "mod2_dst": "Macro Morph", "mod2_amt": 35, "spread": 60,
                        **chorus(0.3, 8, 35, 30), **reverb(30, 3.0)}),
    ("Macro FM Keys", {**amp(2, 1800, 40, 700), "o1_level": 25, "o1_oct": "16'", "mac_model": "6-Op FM 1",
                       "mac_level": 85, "mac_harm": 10, "mac_timbre": 55, "mac_morph": 40, "flt_cut": hz(9000),
                       "voices": "8", "mod2_src": "Velocity", "mod2_dst": "Macro Timbre", "mod2_amt": 30,
                       **chorus(0.4, 7, 25, 25), **reverb(20, 1.8)}),
    # the open-source effects: Rings' modal resonator rung by a short pluck (tuned to the newest note), Clouds'
    # granular cloud over a pad, ToTape6-style tape saturation on a bass; the SPACE reverb is Airwindows Galactic
    ("Modal Bells", {**amp(1, 250, 0, 200), **fenv(1, 120, 0, 100),
                     "o1_mix": 100, "o2_level": 0, "flt_cut": hz(3000), "flt_res": 10, "lad_env": 30, "voices": "6",
                     "modfx_on": "On", "modfx_type": "Modal Resonator", "modfx_model": "Modal", "modfx_struct": 30,
                     "modfx_tone": 55, "modfx_decay": 58, "modfx_pos": 30, "modfx_mix": 70, "modfx_gain": 60,
                     **reverb(25, rev_type="Space", rev_decay=60, rev_size=55, rev_tone=60)}),
    ("Grain Cloud", {**amp(900, 2000, 85, 2500), **fenv(1500, 3000, 50, 2500),
                     "o2_level": 80, "o2_freq": 0.08, "o3_level": 60, "o3_oct": "4'", "flt_cut": hz(1800), "flt_res": 15,
                     "lad_env": 20, "voices": "8", "spread": 50,
                     "modfx_on": "On", "modfx_type": "Granular", "modfx_pos": 45, "modfx_size": 60,
                     "modfx_density": 78, "modfx_texture": 65, "modfx_pitch": 12, "modfx_spread": 80, "modfx_fb": 20,
                     "modfx_room": 40, "modfx_mix": 55, "modfx_c2_dst": "Texture",
                     "mod6_src": "Mod Wheel", "mod6_dst": "Synth FX Position", "mod6_amt": 40,
                     **reverb(35, rev_type="Space", rev_decay=75, rev_size=80, rev_tone=45)}),
    ("Tape Bass", {**amp(2, 500, 75, 150), **fenv(1, 300, 25, 200),
                   "o2_level": 80, "o2_oct": "16'", "o2_freq": 0.05, "flt_cut": hz(450), "flt_res": 30, "lad_env": 40,
                   "lad_kbd": 40, "voices": "Mono",
                   "modfx_on": "On", "modfx_type": "Tape Color", "modfx_drive": 72, "modfx_bump": 65,
                   "modfx_flutter": 12, "modfx_tone": 60, "modfx_mix": 100}),
]


def main():
    by_key = {p["key"]: p for p in PARAMS}
    presets = []
    for name, patch in PATCHES:
        for k in patch:
            if k not in by_key:
                raise SystemExit("%s: %r is not a parameter" % (name, k))
        # Link Cutoff ties the two cutoff knobs together as each one is set, and the wrapper sets a preset's values in
        # file order. So it comes first and is always Off in a preset: both cutoffs then land as written, whatever the
        # sound before had. (On, the second cutoff would move the first.)
        if patch.get("flt_link", "Off") != "Off":
            raise SystemExit("%s: presets keep flt_link Off (it would move the cutoffs while they load)" % name)
        values = {}
        for p in sorted(PARAMS, key=lambda p: p["key"] != "flt_link"):   # every parameter, link first, then VST order
            v = patch.get(p["key"], p.get("default", p.get("min", 0)))
            if "options" in p and isinstance(v, (int, float)):
                v = p["options"][int(v)]
            values[p["key"]] = v
        presets.append({"name": name, "values": values})
    out = os.path.join(HERE, "..", "presets.json")
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write('{\n "presets": [\n')
        f.write(",\n".join("  " + json.dumps(pr, ensure_ascii=False) for pr in presets))
        f.write("\n ]\n}\n")
    print("presets.json: %d presets x %d parameters" % (len(presets), len(PARAMS)))


if __name__ == "__main__":
    main()
