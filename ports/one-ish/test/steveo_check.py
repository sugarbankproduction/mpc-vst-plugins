#!/usr/bin/env python3
"""Writes the list test/steveo_check.c plays: each libraries/<BANK>/*.oneish patch as name TAB state."""
import glob
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
META = {"name", "type", "category", "mood", "group", "notes"}
with open(sys.argv[1], "w", encoding="utf-8", newline="\n") as out:
    for f in sorted(glob.glob(os.path.join(HERE, "..", "libraries", "*", "*.oneish"))):
        with open(f, encoding="utf-8", errors="replace", newline="") as fh:
            lines = fh.read().split("\n")[1:]
        pairs = [ln for ln in lines if "=" in ln and not ln.startswith("#")]
        name = next(ln[5:] for ln in pairs if ln.startswith("name="))[:24].rstrip()
        # (a preset keeps flt_link Off: it only ties the two knobs while they're moved, the samples are the same)
        state = ["v=1"] + [ln for ln in pairs if ln.split("=", 1)[0] not in META and not ln.split("=", 1)[0].endswith("flt_link")]
        out.write("%s\t%s\n" % (name, ";".join(state)))
