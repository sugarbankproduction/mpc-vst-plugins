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


def ms(t):        # envelope knobs: 1 ms .. 10 s, log
    return round(max(0.0, 25 * math.log10(max(t, 1))), 1)


def hz(f):        # cutoff knobs: 20 Hz .. 20 kHz, log
    return round(100 * math.log(f / 20) / math.log(1000), 1)


def lfo(f):       # LFO rate knobs: 0.05 .. 50 Hz, log
    return round(100 * math.log(f / 0.05) / math.log(1000), 1)


def div(name):    # a synced LFO's rate knob picks a division
    return round(100 * DIVS.index(name) / (len(DIVS) - 1), 1)


def amp(a, d, s, r):
    return {"amp_a": ms(a), "amp_d": ms(d), "amp_s": s, "amp_r": ms(r)}


def fenv(a, d, s, r):
    return {"fenv_a": ms(a), "fenv_d": ms(d), "fenv_s": s, "fenv_r": ms(r)}


def menv(a, d, s, r):
    return {"menv_a": ms(a), "menv_d": ms(d), "menv_s": s, "menv_r": ms(r)}


PATCHES = [
    ("Init", {}),
    ("Fat Bass", {**amp(2, 400, 80, 120), **fenv(1, 350, 20, 200),
                  "o2_level": 85, "o2_fine": -7, "o3_level": 70, "o3_oct": "16'", "o3_wave": "Square",
                  "flt_cut": hz(220), "flt_res": 25, "flt_drive": 35, "lad_env": 45, "lad_kbd": 30, "voices": "Mono"}),
    ("Pluck Bass", {**amp(1, 300, 0, 80), **fenv(1, 180, 0, 80),
                    "o2_level": 60, "o2_oct": "16'", "o2_wave": "Square",
                    "flt_cut": hz(140), "flt_res": 55, "lad_env": 60, "lad_kbd": 40, "voices": "Mono"}),
    ("Acid Line", {**amp(1, 250, 60, 60), **fenv(1, 220, 0, 60),
                   "o1_wave": "Square", "flt_cut": hz(260), "flt_res": 82, "flt_drive": 55, "lad_env": 55,
                   "lad_kbd": 50, "flt_slope": "24 dB", "voices": "Mono",
                   "mod2_src": "Velocity", "mod2_dst": "Ladder Cutoff", "mod2_amt": 20}),
    ("Sync Lead", {**amp(3, 500, 85, 250), **menv(1, 900, 0, 300),
                   "o1_level": 0, "o2_level": 100, "o2_sync": "On", "o2_semi": 7, "o3_level": 55, "o3_fine": 6,
                   "o3_oct": "16'", "flt_cut": hz(5000), "flt_res": 20, "mod3_src": "Mod Env", "mod3_dst": "Osc 2 Pitch",
                   "mod3_amt": 40, "mod1_amt": 3, "voices": "Mono"}),
    ("Brass Stack", {**amp(25, 600, 75, 300), **fenv(60, 700, 45, 350),
                     "o2_level": 90, "o2_fine": 6, "o3_level": 70, "o3_fine": -5,
                     "flt_cut": hz(400), "flt_res": 15, "lad_env": 50, "lad_kbd": 50,
                     "mod2_src": "Velocity", "mod2_dst": "Ladder Cutoff", "mod2_amt": 25, "voices": "6"}),
    ("Warm Pad", {**amp(700, 1500, 85, 1800), **fenv(1200, 2500, 50, 2000),
                  "o2_level": 85, "o2_fine": 9, "o3_level": 70, "o3_fine": -8, "o3_oct": "4'",
                  "flt_cut": hz(700), "flt_res": 20, "lad_env": 25, "lad_lfo": 10, "lfo2_rate": lfo(0.25),
                  "lfo2_wave": "Sine", "voices": "8"}),
    ("Glass Keys", {**amp(2, 1600, 0, 900), **fenv(1, 900, 10, 700),
                    "o1_wave": "Square", "o2_level": 60, "o2_oct": "4'", "o2_wave": "Square", "o2_fine": 4,
                    "o1_route": "SVF", "o2_route": "SVF", "svf_mode": "BP", "svf_cut": hz(1800), "svf_res": 45,
                    "svf_env": 35, "svf_kbd": 70, "flt_mix": 0, "voices": "8"}),
    ("Hollow Strings", {**amp(300, 1200, 80, 900),
                        "o2_level": 90, "o2_fine": 12, "o3_level": 80, "o3_fine": -11,
                        "o1_route": "Both", "o2_route": "Both", "o3_route": "Both", "svf_mode": "HP", "svf_cut": hz(300),
                        "flt_route": "Series", "flt_cut": hz(3500), "flt_res": 10, "pitch_lfo": 1, "lfo1_rate": lfo(5.5),
                        "lfo1_wave": "Sine", "voices": "8"}),
    ("Wobble Bass", {**amp(2, 400, 90, 150),
                     "o2_level": 80, "o2_oct": "16'", "o2_wave": "Square", "flt_cut": hz(180), "flt_res": 60,
                     "flt_drive": 45, "lad_lfo": 55, "lfo2_wave": "Sine", "lfo2_sync": "On", "lfo2_rate": div("1/8"),
                     "voices": "Mono"}),
    ("Self-Osc Whistle", {**amp(40, 800, 90, 600),
                          "o1_level": 0, "noise_level": 0, "flt_res": 100, "flt_cut": hz(440), "lad_kbd": 100,
                          "pitch_lfo": 1, "lfo1_rate": lfo(5), "lfo1_wave": "Sine", "voices": "4"}),
    ("Noise Hat", {**amp(1, 90, 0, 60),
                   "o1_level": 0, "noise_level": 100, "flt_mode": "HP", "flt_cut": hz(6000), "flt_res": 30,
                   "voices": "8"}),
    ("Notch Sweep", {**amp(10, 900, 85, 600),
                     "o2_level": 90, "o2_fine": 7, "o1_route": "SVF", "o2_route": "SVF", "svf_mode": "Notch",
                     "svf_cut": hz(800), "svf_res": 40, "svf_lfo": 45, "lfo2_rate": lfo(0.4), "lfo2_wave": "Triangle",
                     "flt_mix": 0, "voices": "6"}),
]


def main():
    by_key = {p["key"]: p for p in PARAMS}
    presets = []
    for name, patch in PATCHES:
        for k in patch:
            if k not in by_key:
                raise SystemExit("%s: %r is not a parameter" % (name, k))
        values = {}
        for p in PARAMS:   # every parameter, in VST order
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
