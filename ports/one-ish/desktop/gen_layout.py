#!/usr/bin/env python3
"""Writes build/desktop/layout.h for the desktop editor (desktop/editor.c): where every control sits on the front panel.

The panel follows the instrument's layout (left to right: LFOs, oscillators, pitch/ring/FM modulation and polyphony;
the centre console with the screen; mixer, the two filters and their cutoff modulation, the three envelopes, effects
and output; along the bottom the left-hand controller and the keyboard). Original artwork: editor.c draws everything.

Controls are placed by parameter KEY, in base units of a BASE_W x BASE_H window (editor.c scales it to fit the screen).
A key that params.json does not have (yet) is skipped, so params other branches add (glide, unison, envelope extras,
the SVF pair) appear by themselves once they exist. The kind of control adapts to the parameter: an option list in a
knob's place becomes a toggle (two options), an LED list (a few) or a button with a popup list (many). Every parameter
the panel does not place, apart from the mod matrix rows, goes to the screen's MORE page, and the build warns about it.
    python3 ports/one-ish/desktop/gen_layout.py <params.json> <out.h>"""
import json
import sys

BASE_W, BASE_H = 2060, 800
LED_ROW, LED_EXTRA = 10, 30   # an LED list: one row per option, plus its red button and label
SIDE = 1                      # a toggle's size: its label beside the button instead of under it

# short option names for LED lists (the panel prints them in small capitals beside each LED)
ABBREV = {"Triangle": "TRI", "Sine": "SINE", "Square": "SQR", "Saw": "SAW", "Ramp": "RAMP", "S&H": "S&H",
          "Parallel": "PAR", "Series": "SER", "Notch": "NOTCH", "Chorus": "CHOR", "Flanger": "FLNG",
          "Phaser": "PHAS", "Mono": "MONO", "Tri": "TRI", "24 dB": "24dB", "18 dB": "18dB", "12 dB": "12dB",
          "6 dB": "6dB"}


class Layout:
    def __init__(self, params):
        self.params = params
        self.index = {p["key"]: i for i, p in enumerate(params)}
        self.panels, self.ctls, self.waves = [], [], []
        self.placed, self.planned = set(), []

    def nopts(self, key):
        return len(self.params[self.index[key]].get("options", []))

    def panel(self, x, y, w, h, title="", style="module", keys=None):
        """style: frame (a silver-edged block), module (title strip), caption (a small row heading only).
        With keys, the panel is drawn only when at least one of them exists."""
        if keys is not None and not any(k in self.index for k in keys):
            return
        self.panels.append((x, y, w, h, title, style))

    def adapt(self, key, kind, h):
        n = self.nopts(key)
        fits = n <= (h - LED_EXTRA) // LED_ROW
        if n == 0:
            return "small" if kind == "toggle" else ("knob" if kind in ("leds", "oct") else kind)
        if kind in ("knob", "small", "big", "toggle"):
            return "toggle" if n == 2 else ("leds" if fits else "button")
        if kind == "oct":
            return "oct" if n > 2 else "leds"
        return "leds" if fits else "button"

    def ctl(self, key, kind, x, y, w, h, label, size=0):
        self.planned.append(key)
        if key not in self.index:
            return False
        wanted, kind = kind, self.adapt(key, kind, h)
        if kind != wanted:
            size = 0   # the size was meant for another kind of control
        size = size or {"knob": 28, "small": 18, "big": 54, "oct": 30}.get(kind, 0)
        self.ctls.append((key, kind, x, y, w, h, size, label))
        self.placed.add(key)
        return True

    def row(self, items, x, y, cw, h, kind="knob", size=0):
        """items: (key, label[, kind]) side by side from x, in cells cw wide; missing keys leave no gap"""
        for it in items:
            if self.ctl(it[0], it[2] if len(it) > 2 else kind, x, y, cw, h, it[1], size):
                x += cw

    def column(self, items, x, y, w, ch, kind="knob", size=0):
        """the same, one under the other, in cells ch high"""
        for it in items:
            if self.ctl(it[0], it[2] if len(it) > 2 else kind, x, y, w, ch, it[1], size):
                y += ch

    def wave(self, n, x, y, w, h):
        keys = ["o%d_%s" % (n, s) for s in ("wave", "angle", "pw", "mix")]
        self.waves.append((x, y, w, h, [self.index.get(k, -1) for k in keys]))


def build(lay):
    L = lay
    # ---- the three panel blocks, the controller box and the keyboard ----
    L.panel(34, 10, 666, 536, style="frame")
    L.panel(708, 10, 364, 536, style="frame")
    L.panel(1080, 10, 946, 536, style="frame")
    L.panel(34, 560, 340, 230, style="frame")

    # ---- LFOs: a column on the far left (LFO 3 and 4 appear when their params exist) ----
    for n in range(1, 5):
        y = 18 + 130 * (n - 1)
        k = "lfo%d_" % n
        L.panel(42, y, 128, 126, "LFO %d" % n, keys=[k + "rate", k + "wave"])
        L.ctl(k + "rate", "knob", 44, y + 16, 60, 64, "RATE", 30)
        L.ctl(k + "wave", "leds", 106, y + 16, 62, 104, "WAVE")
        L.ctl(k + "sync", "toggle", 44, y + 82, 60, 40, "SYNC")

    # ---- oscillators 1-3 ----
    for n in range(1, 4):
        x0 = 176 + 174 * (n - 1)
        o = "o%d_" % n
        c1, c2, c3 = x0 + 35, x0 + 85, x0 + 135
        L.panel(x0, 18, 170, 290, "OSCILLATOR %d" % n)
        L.ctl(o + "oct", "oct", c2 - 40, 32, 80, 72, "OCTAVE", 30)
        L.ctl(o + "freq", "knob", c1 - 29, 98, 58, 64, "FREQUENCY")
        L.ctl(o + "beat", "knob", c3 - 29, 98, 58, 64, "BEAT FREQ")
        L.wave(n, c2 - 21, 108, 42, 32)
        L.ctl(o + "angle", "knob", c1 - 29, 166, 58, 64, "WAVE ANGLE")
        L.ctl(o + "pw", "knob", c3 - 29, 166, 58, 64, "PULSE WIDTH")
        L.ctl(o + "wave", "leds", c1 - 29, 234, 58, 72, "WAVE")
        L.ctl(o + "mix", "knob", c2 - 27, 238, 54, 66, "MIX")
        L.ctl(o + "sync", "toggle", c3 - 29, 246, 58, 50, "HARD SYNC")

    # ---- modulation row under the oscillators ----
    L.panel(176, 314, 170, 102, "PITCH MODULATION")
    L.ctl("pitch_lfo", "knob", 186, 332, 70, 76, "LFO 1 AMT")
    L.ctl("pitch_env", "knob", 266, 332, 70, 76, "MOD EG AMT")
    L.panel(350, 314, 82, 102, "RING MOD")
    L.ctl("ring_src", "leds", 356, 330, 70, 84, "SOURCES")
    L.panel(436, 314, 256, 102, "FREQUENCY MODULATION")
    L.ctl("fm_amt", "knob", 480, 332, 70, 76, "FM AMT")
    L.ctl("fm_route", "leds", 590, 330, 70, 84, "ROUTE")

    # ---- polyphony ----
    L.panel(176, 422, 516, 118, "POLYPHONY")
    L.row([("voices", "VOICES", "leds"), ("unison", "UNISON", "toggle"), ("uni_count", "UNI VOICES", "oct"),
           ("uni_detune", "DETUNE"), ("pitch_var", "PITCH VAR")], 200, 438, 92, 100)

    # ---- centre console: the screen (presets, mod matrix, MORE page, help) and its buttons ----
    screen = (720, 22, 340, 362)
    zones = {"SCREEN": screen, "TAB_MATRIX": (730, 408, 52, 46), "TAB_MORE": (788, 408, 52, 46),
             "ENCODER": (850, 398, 84, 104), "PREV": (944, 408, 52, 46), "NEXT": (1002, 408, 52, 46),
             "BRAND": (720, 506, 340, 26)}

    # ---- mixer ----
    L.panel(1086, 18, 138, 522, "MIXER")
    for k, (src, label) in enumerate([("o1", "OSC 1"), ("o2", "OSC 2"), ("o3", "OSC 3"), ("ring", "RING MOD"),
                                      ("noise", "NOISE")]):
        y = 38 + 100 * k
        L.ctl(src + "_level", "knob", 1088, y, 62, 70, label, 30)
        L.ctl(src + "_svf", "toggle", 1150, y + 10, 32, 50, "SVF")
        L.ctl(src + "_lad", "toggle", 1184, y + 10, 38, 50, "LADDER")

    # ---- filters: SVF and ladder side by side, link / mix / route between them ----
    L.panel(1230, 18, 306, 290, "STATE VARIABLE FILTER|LADDER FILTER")
    L.ctl("svf_cut", "big", 1256, 36, 100, 100, "CUTOFF")
    L.ctl("flt_cut", "big", 1410, 36, 100, 100, "CUTOFF")
    L.ctl("flt_link", "toggle", 1356, 104, 54, 44, "LINK CUTOFF")
    L.ctl("svf_res", "knob", 1240, 148, 58, 66, "RESONANCE")
    L.ctl("svf_space", "knob", 1298, 148, 56, 66, "SPACING")
    L.ctl("flt_mix", "knob", 1356, 148, 54, 66, "MIX")
    L.ctl("flt_res", "knob", 1410, 148, 58, 66, "RESONANCE")
    L.ctl("flt_drive", "knob", 1470, 148, 58, 66, "DRIVE")
    L.ctl("svf_mode", "leds", 1236, 216, 54, 88, "MODE")
    L.ctl("svf_pair", "leds", 1292, 216, 60, 88, "PAIR")
    L.ctl("flt_route", "leds", 1356, 216, 54, 88, "ROUTE")
    L.ctl("flt_mode", "leds", 1414, 216, 54, 88, "MODE")
    L.ctl("flt_slope", "leds", 1474, 216, 58, 88, "SLOPE")
    L.panel(1230, 314, 306, 226, "CUTOFF MODULATION")
    for y, f, cap in ((330, "svf", "SVF"), (434, "lad", "LADDER")):
        L.panel(1238, y, 120, 14, cap, style="caption")
        L.row([(f + "_env", "EG AMT"), (f + "_lfo", "LFO 2 AMT"), (f + "_kbd", "KEY TRK"), (f + "_fm", "FM AMT"),
               (f + "_fm_src", "FM SRC", "leds")], 1234, y + 12, 60, 90)

    # ---- envelopes: a row each, buttons over the time knobs, curve / scaling knobs at the right ----
    for k, (p, title) in enumerate([("fenv", "FILTER ENVELOPE"), ("amp", "AMPLIFIER ENVELOPE"),
                                    ("menv", "MODULATION ENVELOPE")]):
        yt = 18 + 104 * k
        L.panel(1542, yt, 478, 100, title)
        L.row([(p + "_multi", "MULTI"), (p + "_sync", "SYNC"), (p + "_loop", "LOOP"), (p + "_latch", "LATCH"),
               (p + "_reset", "RESET")], 1550, yt + 16, 62, 14, "toggle", SIDE)
        L.row([(p + "_delay", "DELAY"), (p + "_a", "ATTACK"), (p + "_hold", "HOLD"), (p + "_d", "DECAY"),
               (p + "_s", "SUSTAIN"), (p + "_r", "RELEASE")], 1546, yt + 32, 52, 66, "knob", 26)
        L.row([(p + "_acurve", "A CURVE"), (p + "_dcurve", "D CURVE"), (p + "_rcurve", "R CURVE")],
              1862, yt + 14, 52, 42, "small")
        L.row([(p + "_scale", "KEY SCL"), (p + "_vel", "VEL AMT")], 1888, yt + 56, 52, 42, "small")

    # ---- effects and output ----
    L.panel(1542, 334, 262, 102, "MOD EFFECT")
    L.ctl("modfx_type", "leds", 1546, 350, 56, 84, "TYPE")
    L.row([("modfx_rate", "RATE"), ("modfx_depth", "DEPTH"), ("modfx_fb", "FDBK"), ("modfx_mix", "MIX")],
          1604, 354, 50, 64)
    L.panel(1808, 334, 212, 102, "REVERB")
    L.row([("rev_decay", "DECAY"), ("rev_damp", "DAMP"), ("rev_pre", "PRE-DLY"), ("rev_mix", "MIX")],
          1810, 354, 52, 64)
    L.panel(1542, 440, 330, 100, "DELAY")
    L.row([("dly_time", "TIME"), ("dly_sync", "SYNC", "toggle"), ("dly_fb", "FDBK"), ("dly_tone", "TONE"),
           ("dly_pp", "PING-PONG", "toggle"), ("dly_mix", "MIX")], 1546, 458, 54, 72)
    L.panel(1876, 440, 144, 100, "OUTPUT")
    L.ctl("volume", "knob", 1900, 456, 96, 78, "VOLUME", 36)

    # ---- left-hand controller: octave, glide, bend range, wheels, X/Y pad ----
    zones.update({"OCT_LEDS": (50, 576, 116, 26), "OCT_DOWN": (64, 606, 36, 16), "OCT_UP": (118, 606, 36, 16),
                  "PITCH": (56, 650, 36, 112), "MODW": (110, 650, 36, 112), "PAD": (166, 650, 126, 112),
                  "KEYS": (384, 566, 1642, 218)})
    L.ctl("glide_time", "knob", 172, 570, 64, 70, "GLIDE RATE")
    L.ctl("glide", "toggle", 236, 576, 42, 56, "ON")
    L.ctl("glide_type", "leds", 278, 568, 52, 76, "TYPE")
    L.ctl("bend_range", "small", 328, 570, 44, 70, "BEND", 22)
    L.column([("glide_legato", "LEGATO"), ("glide_gated", "GATED"), ("glissando", "GLISS")], 298, 652, 72, 22,
             "toggle", SIDE)   # the other glide options, beside the pad
    return zones


def emit(lay, zones, out):
    P = lay.params
    kinds = ["knob", "small", "big", "oct", "leds", "toggle", "button"]
    lines = ["/* generated by ports/one-ish/desktop/gen_layout.py from params.json: do not edit */", "#pragma once",
             "#define BASE_W %d" % BASE_W, "#define BASE_H %d" % BASE_H, "#define LAY __attribute__((unused)) static const",
             "enum { " + ", ".join("K_" + k.upper() for k in kinds) + ", K_AMOUNT, K_MENU };",
             "typedef struct { short x, y, w, h; const char *title; short style; } lay_panel_t;   /* style: 0 frame, "
             "1 module, 2 caption */",
             "typedef struct { short param, kind, x, y, w, h, size; const char *label; const char *const *leds; } lay_ctl_t;"]
    styles = {"frame": 0, "module": 1, "caption": 2}
    rows = ["{%d, %d, %d, %d, \"%s\", %d}" % (x, y, w, h, t, styles[s]) for x, y, w, h, t, s in lay.panels]
    lines.append("LAY lay_panel_t PANELS[] = {%s};" % ", ".join(rows))
    lines.append("#define NPANELS %d" % len(rows))
    rows = []
    for n, (key, kind, x, y, w, h, size, label) in enumerate(lay.ctls):
        opts = P[lay.index[key]].get("options", [])
        leds = "0"
        if kind == "leds":
            lines.append("LAY char *const LEDS_%d[] = {%s};" % (
                n, ", ".join('"%s"' % ABBREV.get(o, o.upper()[:5]) for o in opts)))
            leds = "LEDS_%d" % n
        rows.append("{%d, K_%s, %d, %d, %d, %d, %d, \"%s\", %s}" % (lay.index[key], kind.upper(), x, y, w, h, size,
                                                                   label, leds))
    lines.append("LAY lay_ctl_t CTLS[] = {%s};" % ",\n    ".join(rows))
    lines.append("#define NCTLS %d" % len(rows))
    rows = ["{%d, %d, %d, %d, {%s}}" % (x, y, w, h, ", ".join(map(str, ix))) for x, y, w, h, ix in lay.waves]
    lines.append("typedef struct { short x, y, w, h; short param[4]; } lay_wave_t;   /* wave, angle, pw, mix */")
    lines.append("LAY lay_wave_t WAVES[] = {%s};" % ", ".join(rows))
    lines.append("#define NWAVES %d" % len(rows))
    for name, (x, y, w, h) in zones.items():
        lines.append("LAY short ZONE_%s[4] = {%d, %d, %d, %d};" % (name, x, y, w, h))
    # the mod matrix: rows of source, destination, amount, via, in the screen
    matrix = []
    for n in range(1, 33):
        keys = ["mod%d_%s" % (n, s) for s in ("src", "dst", "amt", "via")]
        if all(k in lay.index for k in keys):
            matrix.append("{%s}" % ", ".join(str(lay.index[k]) for k in keys))
            lay.placed.update(keys)
    lines.append("LAY short MATRIX[][4] = {%s};" % ", ".join(matrix or ["{-1, -1, -1, -1}"]))
    lines.append("#define NMATRIX %d" % len(matrix))
    other = [i for i, p in enumerate(P) if p["key"] not in lay.placed]
    lines.append("LAY short OTHER[] = {%s};" % ", ".join(map(str, other or [-1])))
    lines.append("#define NOTHER %d" % len(other))
    open(out, "w", encoding="utf-8", newline="\n").write("\n".join(lines) + "\n")
    return other


def main():
    src, out = sys.argv[1], sys.argv[2]
    lay = Layout(json.load(open(src, encoding="utf-8"))["params"])
    zones = build(lay)
    dup = sorted({k for k in lay.planned if lay.planned.count(k) > 1})
    if dup:
        raise SystemExit("gen_layout.py: keys placed twice: %s" % dup)
    other = emit(lay, zones, out)
    for i in other:
        print("warning: gen_layout.py: %s has no place on the panel; it is on the screen's MORE page"
              % lay.params[i]["key"], file=sys.stderr)
    later = [k for k in lay.planned if k not in lay.index]
    print("layout.h: %d controls on the panel, %d matrix params, %d on the MORE page; %d planned keys not in params "
          "yet" % (len(lay.ctls), len(lay.placed) - len(lay.ctls), len(other), len(later)))


if __name__ == "__main__":
    main()
