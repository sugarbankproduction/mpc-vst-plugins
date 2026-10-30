#!/usr/bin/env python3
"""Writes the Synth 2 and Synth 3 parameters into ../params.json (multitimbral: docs/MULTITIMBRAL.md).

Every parameter of one Synth (its sound, its arpeggiator and sequencer, its setup) gets a copy for Synth 2 and one for
Synth 3, keyed "s2_<key>" and "s3_<key>" and named "S2 <name>" / "S3 <name>"; the shared ones (GLOBAL below: the master
effects, master swing and tuning, the MIDI and chord-learn settings, master level, PLAY ALL, the generators' FOLLOW
TEMPO) have no copies. The
unprefixed keys are Synth 1's, so every project, patch and preset made before stays Synth 1's sound.
Order (the VST index, which saved projects and automation rely on): Synth 1 and the shared params as written in
params.json, then every s2_ copy, then every s3_ copy. Run again after adding a param to Synth 1: copies already there
keep their places and the new ones go at the very end (s2_ then s3_), so no existing index moves.
Defaults are Synth 1's, except SYNTH_DEFAULTS (Synths 2 and 3 start switched off, on MIDI channels 2 and 3).
src/engine.c knows the same split (engine_global(); test/offline_check.c checks they agree).
    python3 ports/one-ish/presets/make_synths.py   (then make_presets.py, then gen_vst.py --params-h)"""
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
PATH = os.path.join(HERE, "..", "params.json")
PREFIXES = ("s2_", "s3_")
GLOBAL_PREFIX = ("dly_", "rev_", "link_")   # the master effect slots' controls; the window's LINKs (2026-10-30)
GLOBAL = {"mfx_on", "mfx_series", "mfx_width", "mfx_buss", "clock_swing", "master_tune", "fine_tune", "midi_ch",
          "learn_mode", "master_level", "seq_all", "gen_follow_tempo"}
SYNTH_DEFAULTS = {"s2_": {"syn_on": "Off", "syn_ch": "2"}, "s3_": {"syn_on": "Off", "syn_ch": "3"}}


def is_global(key):
    return key in GLOBAL or key.startswith(GLOBAL_PREFIX)


def copy_of(p, pre):
    q = dict(p)
    q["key"] = pre + p["key"]
    q["name"] = pre[:2].upper() + " " + p["name"]
    if p["key"] in SYNTH_DEFAULTS[pre]:
        q["default"] = SYNTH_DEFAULTS[pre][p["key"]]
    return q


def main():
    doc = json.load(open(PATH, encoding="utf-8"))
    params = doc["params"]
    base = [p for p in params if not p["key"].startswith(PREFIXES)]
    copies = [p for p in params if p["key"].startswith(PREFIXES)]
    have = {p["key"] for p in copies}
    want = {pre + p["key"] for p in base if not is_global(p["key"]) for pre in PREFIXES}
    copies = [p for p in copies if p["key"] in want]   # (a copy of a param that became shared, or went: dropped)
    if not copies:   # the first time: every s2_, then every s3_
        new = [copy_of(p, pre) for pre in PREFIXES for p in base if not is_global(p["key"])]
    else:            # later: only the missing ones, at the end
        new = [copy_of(p, pre) for pre in PREFIXES for p in base
               if not is_global(p["key"]) and pre + p["key"] not in have]
    byk = {p["key"]: p for p in base}
    for i, p in enumerate(copies):   # a copy follows its Synth 1 param's range and options
        pre, k = p["key"][:3], p["key"][3:]
        copies[i] = copy_of(byk[k], pre)
    doc["params"] = base + copies + new
    with open(PATH, "w", encoding="utf-8", newline="\n") as f:
        f.write('{\n "name": %s,\n "params": [\n' % json.dumps(doc["name"]))
        f.write(",\n".join("  " + json.dumps(p, ensure_ascii=False) for p in doc["params"]))
        f.write('\n ],\n "sections": [\n')
        f.write(",\n".join("  " + json.dumps(s, ensure_ascii=False) for s in doc["sections"]))
        f.write("\n ]\n}\n")
    print("params.json: %d params (%d shared or Synth 1, %d for Synths 2 and 3)" %
          (len(doc["params"]), len(base), len(copies) + len(new)))


if __name__ == "__main__":
    main()
