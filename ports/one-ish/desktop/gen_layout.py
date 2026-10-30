#!/usr/bin/env python3
"""Writes build/desktop/layout.h for the desktop editor (desktop/editor.c): where every control sits on the front panel,
and what each module's MORE page on the centre screen holds.

The panel follows the instrument's layout (left to right: LFOs, oscillators with pitch/ring/waveform/FM modulation
under them, the noise column, and a bottom row of arpeggiator, chord, generative (with the Euclidean pattern as a ring of
dots) and polyphony; the centre console with the screen; mixer (with the macro oscillator's channel once it exists), the
two filters and their cutoff
modulation, the three envelopes, the synth effect, master effects, delay, reverb and output; along the bottom the
left-hand controller, the ribbon and the keyboard). Original artwork: editor_draw.c draws everything.

Controls are placed by parameter KEY, in base units of a BASE_W x BASE_H window (editor.c scales it to fit the screen).
A key that params.json does not have (yet) is skipped, so params other branches add (the ribbon, analog character,
longer option lists) appear by themselves once they exist. The kind of control adapts to the parameter: an option list
in a knob's place becomes a toggle (two options), an LED list (a few) or a button with a popup list (many).

Second-layer controls live on MORE pages, as on the instrument: a module with a small triangle in its top-right corner
has one, and clicking the triangle shows that page on the centre screen (the console's MORE button lists them all).
A page item may also be on the panel (a page shows the whole module); an item can depend on an option (the synth
effect's page shows only the controls of the selected effect type). A page may have a picture under its items (an
envelope, an LFO, the filters, the mixer: more_pics.c). Every parameter that is neither on the panel, on a page nor in
the mod matrix goes to an OTHER page, and the build warns about it. The rate LEDs (rate_leds.c) are placed here too.
    python3 ports/one-ish/desktop/gen_layout.py <params.json> <out.h>"""
import json
import sys

BASE_W, BASE_H = 2162, 856   # (2026-10-30: 56 taller, for the console's TEMPO / GENERATIVE / MASTER row)
LED_ROW, LED_EXTRA = 10, 30   # an LED list: one row per option, plus its red button and label
SIDE = 1                      # a toggle's size: its label beside the button instead of under it
DX = 102                      # the right-hand block sits this much further right than it used to

# short option names for LED lists (the panel prints them in small capitals beside each LED)
ABBREV = {"Triangle": "TRI", "Sine": "SINE", "Square": "SQR", "Saw": "SAW", "Ramp": "RAMP", "S&H": "S&H",
          "Parallel": "PAR", "Series": "SER", "Notch": "NOTCH", "Chorus": "CHOR", "Flanger": "FLNG",
          "Phaser": "PHAS", "Mono": "MONO", "Tri": "TRI", "24 dB": "24dB", "18 dB": "18dB", "12 dB": "12dB",
          "6 dB": "6dB", "Red+White": "R+W", "Red+Purple": "R+P", "White+Purple": "W+P", "Plate": "PLATE",
          "Room": "ROOM", "Hall": "HALL", "Random": "RAND", "Up/Down": "UP-DN", "Down/Up": "DN-UP"}

# the effect types by name (params.json modfx_type, dly_type and rev_type options), and which controls each type uses:
# a page shows a control only while its effect runs a type that reads it (engine.c fx_params, src/fx/*). Types added
# later, and modfx_* keys not listed here, still get a place: see sfx_page().
DELAYS = ["Delay", "BBD Delay", "Tape Delay", "Echo"]
LINES = ["Flanger", "Stereo Flanger", "Chorus", "Dual Chorus", "Ensemble", "Stereo Chorus"]
PHASERS = ["6-Stage Phaser", "12-Stage Phaser", "Time Phaser"]
TWO_TAP = ["Stereo Delay", "Ping Pong"]
VOCODERS = ["10 Band Vocoder", "16 Band Vocoder"]
SFX_COMMON = [("modfx_gain", "Wet Gain"), ("modfx_c1_dst", "CTRL 1 Target"),
              ("modfx_c2_dst", "CTRL 2 Target"), ("modfx_on", "On"), ("modfx_mix", "Mix"), ("modfx_ctrl1", "CTRL 1"),
              ("modfx_ctrl2", "CTRL 2")]
RINGS, CLOUDS = "Modal Resonator", "Granular"   # Mutable Instruments Rings and Clouds (src/fx/mi_effects.*)
AW_TAPE, AW_DENSITY, AW_CONSOLE = "Tape Color", "Density", "Console"   # Airwindows (src/fx/airwindows.h)
SFX_USES = [   # key, label, the types that use it
    ("modfx_model", "Resonator Model", [RINGS]),
    ("modfx_gmode", "Granular Mode", [CLOUDS]),
    ("modfx_quality", "Grain Quality", [CLOUDS]),
    ("modfx_dtime", "Delay Time", [*DELAYS, "Time Phaser", *TWO_TAP]),
    ("modfx_dtime_r", "Delay Time R", TWO_TAP),
    ("modfx_time", "Time", LINES),
    ("modfx_time_r", "Time R", ["Stereo Flanger", "Stereo Chorus"]),
    ("modfx_pitch", "Pitch", [RINGS, CLOUDS]),
    ("modfx_follow", "Key Follow", [RINGS]),
    ("modfx_struct", "Structure", [RINGS]),
    ("modfx_pos", "Position", [RINGS, CLOUDS]),
    ("modfx_size", "Grain Size", [CLOUDS]),
    ("modfx_density", "Density", [CLOUDS]),
    ("modfx_texture", "Texture", [CLOUDS]),
    ("modfx_freeze", "Freeze", [CLOUDS]),
    ("modfx_drive", "Drive", [AW_TAPE, AW_DENSITY, AW_CONSOLE]),
    ("modfx_bump", "Head Bump", [AW_TAPE]),
    ("modfx_lowcut", "Low Cut", [AW_DENSITY]),
    ("modfx_rate", "Rate", ["Echo", *LINES, *PHASERS, "Resonator", "Stereo Delay"]),
    ("modfx_rate_r", "Rate R", ["Stereo Flanger", "Stereo Chorus", "Stereo Delay"]),
    ("modfx_depth", "Depth", ["BBD Delay", "Echo", *LINES, *PHASERS, "Resonator", "Stereo Delay"]),
    ("modfx_depth_r", "Depth R", ["Stereo Delay"]),
    ("modfx_offset", "Offset", ["Ping Pong"]),
    ("modfx_sweep", "Sweep", PHASERS),
    ("modfx_fb", "Feedback", [*DELAYS, *LINES, *PHASERS, CLOUDS, *TWO_TAP]),
    ("modfx_fb_r", "Feedback R", ["Stereo Flanger", "Stereo Chorus"]),
    ("modfx_fbl", "Feedback Left", ["Stereo Delay"]),
    ("modfx_fbr", "Feedback Right", ["Stereo Delay"]),
    ("modfx_dfb", "Delay Feedback", ["Time Phaser"]),
    ("modfx_tone", "Tone", [*DELAYS, *LINES, *PHASERS, RINGS, AW_TAPE, *TWO_TAP, *VOCODERS]),
    ("modfx_spread", "Spread", ["Flanger", "Stereo Flanger", "Chorus", "Ensemble", "Stereo Chorus", *PHASERS, CLOUDS]),
    ("modfx_vrate", "Vibrato Rate", ["Dual Chorus", "Ensemble"]),
    ("modfx_vdepth", "Vibrato Depth", ["Dual Chorus", "Ensemble"]),
    ("modfx_noise", "Noise", ["BBD Delay", *LINES, *PHASERS, *VOCODERS]),
    ("modfx_sibil", "Sibilance", VOCODERS),
    ("modfx_sens", "Sensitivity", VOCODERS),
    ("modfx_wow", "Wow", ["Tape Delay"]),
    ("modfx_flutter", "Flutter", ["Tape Delay", AW_TAPE]),
    ("modfx_bias", "Tape Bias", ["Tape Delay"]),
    ("modfx_room", "Room", ["Echo", CLOUDS]),     # Clouds: its reverb
    ("modfx_decay", "Decay", ["Echo", RINGS]),    # Rings: its damping
    ("modfx_bits", "Bit Depth", ["Bit Crusher"]),
    ("modfx_srate", "Sample Rate", ["Bit Crusher"]),
    ("modfx_xover", "Crossover", ["Bit Crusher"]),
    ("modfx_ftype", "Filter Type", ["Resonator"]),
    ("modfx_reso", "Resonance", ["Resonator"]),
    ("modfx_cut1", "Cutoff Low", ["Resonator"]),
    ("modfx_lvl1", "Level Low", ["Resonator"]),
    ("modfx_cut2", "Cutoff Mid", ["Resonator"]),
    ("modfx_lvl2", "Level Mid", ["Resonator"]),
    ("modfx_cut3", "Cutoff High", ["Resonator"]),
    ("modfx_lvl3", "Level High", ["Resonator"]),
    ("modfx_sync", "Tempo Sync", [*DELAYS, *LINES, *PHASERS, "Resonator", *TWO_TAP]),   # its times and rates
]
# a master effect slot's controls (slot 1's keys "dly_" + the suffix, slot 2's "rev_"): suffix, label, the types that
# read it (engine.c fx_bank_field, src/fx/reverb.h; the synth effect's types as SFX_USES, a slot's TIME being both
# their delay time and their chorus time), then each slot's TYPE, MIX and GAIN for every type
REVERBS = ["Plate", "Room", "Hall", "Space", "Shimmer", "Blackhole"]
CLASSIC = ["Plate", "Room", "Hall", "Shimmer"]   # the ones with pre-delay, damping and diffusion in the loop
MFX_USES = [
    ("time", "Time", [*DELAYS, *LINES, "Time Phaser", *TWO_TAP]),
    ("time_r", "Time R", ["Stereo Flanger", "Stereo Chorus", *TWO_TAP]),
    ("decay", "Decay", ["Plate", "Room", "Hall", "Space", "Shimmer", "Echo"]),
    ("size", "Size", REVERBS),
    ("pre", "Pre-delay", [*CLASSIC, "Blackhole"]),
    ("fb", "Feedback", [*DELAYS, *LINES, *PHASERS, *TWO_TAP, "Blackhole"]),   # (the blackhole's: its decay)
    ("fb_r", "Feedback R", ["Stereo Flanger", "Stereo Chorus"]),
    ("fbl", "Feedback Left", ["Stereo Delay"]),
    ("fbr", "Feedback Right", ["Stereo Delay"]),
    ("dfb", "Delay Feedback", ["Time Phaser"]),
    ("rate", "Rate", ["Echo", *LINES, *PHASERS, "Resonator", "Stereo Delay", "Blackhole"]),
    ("rate_r", "Rate R", ["Stereo Flanger", "Stereo Chorus", "Stereo Delay"]),
    ("depth", "Depth", ["BBD Delay", "Echo", *LINES, *PHASERS, "Resonator", "Stereo Delay"]),
    ("depth_r", "Depth R", ["Stereo Delay"]),
    ("offset", "Offset", ["Ping Pong"]),
    ("tone", "Tone", [*REVERBS, *DELAYS, *LINES, *PHASERS, *TWO_TAP, *VOCODERS]),
    ("damp", "High Damp", CLASSIC),
    ("lowdamp", "Low Damp", CLASSIC),
    ("diff", "Diffusion", CLASSIC),
    ("mod", "Mod Level", REVERBS),
    ("early", "Early / Late", ["Room", "Hall"]),
    ("distance", "Distance", ["Plate"]),
    ("hifreq", "High Freq", ["Room", "Hall", "Shimmer"]),
    ("hlevel", "High Level", ["Room", "Hall", "Blackhole"]),
    ("mlevel", "Mid Level", ["Hall"]),
    ("llevel", "Low Level", ["Room", "Hall", "Blackhole"]),
    ("ldecay", "Low Decay", ["Hall", "Shimmer"]),
    ("hdecay", "High Decay", ["Hall", "Shimmer"]),
    ("pitch", "Pitch A", ["Shimmer"]),
    ("pitch_b", "Pitch B", ["Shimmer"]),
    ("pdecay", "Pitch Decay", ["Shimmer"]),
    ("gravity", "Gravity", ["Blackhole"]),
    ("reso", "Resonance", ["Blackhole", "Resonator"]),
    ("sweep", "Sweep", PHASERS),
    ("spread", "Spread", ["Flanger", "Stereo Flanger", "Chorus", "Ensemble", "Stereo Chorus", *PHASERS]),
    ("vrate", "Vibrato Rate", ["Dual Chorus", "Ensemble"]),
    ("vdepth", "Vibrato Depth", ["Dual Chorus", "Ensemble"]),
    ("noise", "Noise", ["BBD Delay", *LINES, *PHASERS, *VOCODERS]),
    ("sibil", "Sibilance", VOCODERS),
    ("sens", "Sensitivity", VOCODERS),
    ("wow", "Wow", ["Tape Delay"]),
    ("flutter", "Flutter", ["Tape Delay"]),
    ("bias", "Tape Bias", ["Tape Delay"]),
    ("room", "Echo Room", ["Echo"]),
    ("bits", "Bit Depth", ["Bit Crusher"]),
    ("srate", "Sample Rate", ["Bit Crusher"]),
    ("xover", "Crossover", ["Bit Crusher"]),
    ("ftype", "Filter Type", ["Resonator"]),
    ("cut1", "Cutoff Low", ["Resonator"]),
    ("lvl1", "Level Low", ["Resonator"]),
    ("cut2", "Cutoff Mid", ["Resonator"]),
    ("lvl2", "Level Mid", ["Resonator"]),
    ("cut3", "Cutoff High", ["Resonator"]),
    ("lvl3", "Level High", ["Resonator"]),
    ("freeze", "Hold", REVERBS),
    ("sync", "Tempo Sync", [*DELAYS, *LINES, *PHASERS, "Resonator", *TWO_TAP, "Blackhole"]),
]
MFX_COMMON = [("mix", "Mix"), ("gain", "Gain")]
# a MORE page's picture (more_pics.c), drawn over its last PIC_ROWS rows: an envelope, an LFO, the filters, the mixer
PICS = {"ENV": 1, "LFO": 2, "FILTER": 3, "MIXER": 4, "PHYS": 5}   # PHYS: the box (2026-10-29)
PICS.update({"RAIN": 6, "MARKOV": 7})   # (2026-10-30) the pond; the Markov melody's notes and choices
PICS.update({"LIFE": 10, "ORBITS": 11, "PEND": 12})   # (2026-10-30) the grid, the orbits, the pendulums (moving)
PIC_ROWS = 3
# the rate LEDs (rate_leds.c): what each flashes with
RATE_KINDS = {"LFO": 0, "ARP": 1, "SEQ": 2, "TEMPO": 3}


class Layout:
    def __init__(self, params):
        self.params = params
        self.index = {p["key"]: i for i, p in enumerate(params)}
        self.panels, self.ctls, self.waves = [], [], []
        self.euclid_at = None   # (x, y, w, h): the Euclidean pattern's ring of dots
        self.placed, self.planned = set(), []
        self.pages = []   # (id, title, note, [(key, label, cond_key, types)], (picture, its key prefix) or None)
        self.rates = []   # rate LEDs: (kind, n, x, y, label) with x, y the LED's centre
        self.tris = []    # MORE triangles placed by hand (tri): (x, y, page id)

    def nopts(self, key):
        return len(self.params[self.index[key]].get("options", []))

    def panel(self, x, y, w, h, title="", style="module", keys=None, more=None):
        """style: frame (a silver-edged block), module (title strip), caption (a small row heading only).
        With keys, the panel is drawn only when at least one of them exists. more: the id of its MORE page."""
        if keys is not None and not any(k in self.index for k in keys):
            return
        self.panels.append((x, y, w, h, title, style, more))

    def adapt(self, key, kind, h):
        n = self.nopts(key)
        fits = n <= (h - LED_EXTRA) // LED_ROW
        if n == 0:
            return "small" if kind == "toggle" else ("knob" if kind in ("leds", "oct", "button") else kind)
        if kind in ("knob", "small", "big", "toggle"):
            return "toggle" if n == 2 else ("leds" if fits else "button")
        if kind == "oct":
            return "oct" if n > 2 else "leds"
        if kind == "button":
            return "button"
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

    def cells(self, items, x, y, h, kind="knob", size=0):
        """items: (key, label, width[, kind]) side by side from x; missing keys leave no gap"""
        for it in items:
            if self.ctl(it[0], it[3] if len(it) > 3 else kind, x, y, it[2], h, it[1], size):
                x += it[2]

    def column(self, items, x, y, w, ch, kind="knob", size=0):
        """the same, one under the other, in cells ch high"""
        for it in items:
            if self.ctl(it[0], it[2] if len(it) > 2 else kind, x, y, w, ch, it[1], size):
                y += ch

    def wave(self, n, x, y, w, h):
        keys = ["o%d_%s" % (n, s) for s in ("wave", "angle", "pw", "mix")]
        self.waves.append((x, y, w, h, [self.index.get(k, -1) for k in keys]))

    def page(self, pid, title, note, items, pic=None):
        """a MORE page: items are (key, label[, cond_key, types]); a conditional item shows only while cond_key is set
        to one of the option numbers in types. pic: (a PICS name, the key prefix it reads), drawn under the items"""
        self.pages.append((pid, title, note, [it if len(it) == 4 else (it[0], it[1], None, None) for it in items], pic))

    def tri(self, x, y, pid):
        """a MORE triangle at (x, y) (12 x 12) opening page pid, for a caption whose corner is taken (2026-10-30)"""
        self.tris.append((x, y, pid))

    def rate_led(self, kind, n, x, y, label=""):
        """a rate LED centred on (x, y): it flashes at an LFO's (n: 0-3), the arpeggiator's or the sequencer's rate, or
        on the beat (TEMPO); a label is printed to its right"""
        self.rates.append((RATE_KINDS[kind], n, x, y, label))

    def types(self, cond, names):
        """the option numbers of the effect types named, in cond's option list (names it doesn't have are left out)"""
        opts = self.params[self.index[cond]].get("options", []) if cond in self.index else []
        return [opts.index(n) for n in names if n in opts]


def sfx_page(lay):
    """the synth effect's MORE page items: TYPE, then each control for the types that use it, then the common ones.
    A type this layout doesn't know (added to modfx_type later) shows every control, and a modfx_* key it doesn't know
    shows for those new types (or for every type while there are none), so nothing new lands on the OTHER page."""
    opts = lay.params[lay.index["modfx_type"]].get("options", []) if "modfx_type" in lay.index else []
    known_types = {n for _, _, names in SFX_USES for n in names}
    new = [t for t, n in enumerate(opts) if n not in known_types]
    mask = lambda ts: [t for t in ts if 0 <= t < 32]   # the screen's option mask is 32 bits
    items = [("modfx_type", "Type")]
    items += [(k, lab, "modfx_type", mask(lay.types("modfx_type", names) + new)) for k, lab, names in SFX_USES]
    known = {"modfx_type"} | {k for k, _, _ in SFX_USES} | {k for k, _ in SFX_COMMON}
    for p in lay.params:
        k = p["key"]
        if k.startswith("modfx_") and k not in known:
            name = p.get("name", k)
            for pre in ("Synth FX ", "Synth Fx ", "Mod FX "):
                name = name[len(pre):] if name.startswith(pre) else name
            items.append((k, name, "modfx_type", mask(new or range(len(opts)))))
    return items + SFX_COMMON


def mfx_page(lay, pre):
    """a master effect slot's MORE page items (pre: "dly_" slot 1, "rev_" slot 2): TYPE, each control while its type
    uses it, MIX and GAIN. A type this layout doesn't know shows every control, and a key of the slot it doesn't
    know shows for those types (or for every type while there are none): nothing new lands on the OTHER page."""
    cond = pre + "type"
    opts = lay.params[lay.index[cond]].get("options", []) if cond in lay.index else []
    known_types = {n for _, _, names in MFX_USES for n in names}
    new = [t for t, n in enumerate(opts) if n not in known_types]
    mask = lambda ts: [t for t in ts if 0 <= t < 32]
    items = [(cond, "Type")]
    items += [(pre + k, lab, cond, mask(lay.types(cond, names) + new)) for k, lab, names in MFX_USES]
    known = {cond} | {pre + k for k, _, _ in MFX_USES} | {pre + k for k, _ in MFX_COMMON}
    for p in lay.params:
        if p["key"].startswith(pre) and p["key"] not in known:
            items.append((p["key"], p.get("name", p["key"]), cond, mask(new or range(len(opts)))))
    return items + [(pre + k, lab) for k, lab in MFX_COMMON]


def build(lay):
    L = lay
    # ---- the three panel blocks, the controller box and the keyboard ----
    L.panel(34, 10, 716, 592, style="frame")
    L.panel(758, 10, 424, 592, style="frame")
    L.panel(1182, 10, 946, 592, style="frame")
    L.panel(34, 616, 340, 230, style="frame", more="CTRL")

    # ---- LFOs: a column on the far left ----
    for n in range(1, 5):
        y = 18 + 144 * (n - 1)
        k = "lfo%d_" % n
        L.panel(42, y, 128, 140, "LFO %d" % n, keys=[k + "rate", k + "wave"], more="LFO%d" % n)
        L.ctl(k + "rate", "knob", 44, y + 16, 60, 64, "RATE", 30)
        L.ctl(k + "wave", "leds", 106, y + 18, 62, 108, "WAVE")
        L.ctl(k + "sync", "toggle", 44, y + 82, 60, 40, "SYNC")
        L.ctl("link_lfo%d" % n, "toggle", 50, y + 122, 54, 14, "LINK", SIDE)   # RATE the same in all 3 Synths
        if k + "rate" in L.index:
            L.rate_led("LFO", n - 1, 50, y + 22)   # top left of the RATE knob, clear of its scale
        L.page("LFO%d" % n, "LFO %d" % n, "Rate, wave and sync are on the panel too.", [
            (k + "rate", "Rate"), (k + "wave", "Wave"), (k + "sync", "Sync"), (k + "timing", "Sync Timing"),
            (k + "range", "Range"), (k + "polarity", "Polarity"), (k + "var", "Variation"), (k + "smooth", "Smoothing"),
            (k + "phase", "Start Phase"), (k + "reset", "Note Reset"), (k + "delay", "Delay"),
            (k + "fade_in", "Fade In"), (k + "fade_out", "Fade Out"), (k + "repeats", "Repeats"),
            ("link_lfo%d" % n, "Link Rate (3 Synths)")], pic=("LFO", k))

    # ---- oscillators 1-3 ----
    for n in range(1, 4):
        x0 = 176 + 156 * (n - 1)
        o = "o%d_" % n
        c1, c2, c3 = x0 + 34, x0 + 76, x0 + 118
        L.panel(x0, 18, 152, 290, "OSCILLATOR %d" % n, more="OSC%d" % n)
        L.ctl(o + "oct", "oct", c2 - 40, 32, 80, 72, "OCTAVE", 30)
        L.ctl(o + "freq", "knob", c1 - 28, 98, 56, 64, "FREQUENCY")
        L.ctl(o + "beat", "knob", c3 - 28, 98, 56, 64, "BEAT FREQ")
        L.wave(n, c2 - 17, 110, 34, 28)
        L.ctl(o + "angle", "knob", c1 - 28, 166, 56, 64, "WAVE ANGLE")
        L.ctl(o + "pw", "knob", c3 - 28, 166, 56, 64, "PULSE WIDTH")
        L.ctl(o + "wave", "leds", c1 - 28, 234, 56, 72, "WAVE")
        L.ctl(o + "mix", "knob", c2 - 24, 238, 48, 66, "MIX")
        if n > 1:   # oscillators 2 and 3 sync to another
            L.ctl(o + "sync", "toggle", c3 - 28, 246, 56, 50, "HARD SYNC")
        L.page("OSC%d" % n, "OSCILLATOR %d" % n, "Tracking, reset and bend offsets of this oscillator.", [
            (o + "track", "Pitch Tracking"), (o + "reset", "Note-On Reset"), (o + "bend_up_off", "Bend Up Offset"),
            (o + "bend_dn_off", "Bend Down Offset")] +
               ([(o + "sync", "Hard Sync"), (o + "sync_src", "Sync Source")] if n > 1 else []))

    # ---- modulation row under the oscillators (a little lower than it was, to make room for the bottom row) ----
    L.panel(176, 314, 142, 88, "PITCH MODULATION", more="PITCHMOD")
    L.ctl("pitch_lfo", "knob", 180, 328, 68, 72, "LFO 1 AMT")
    L.ctl("pitch_env", "knob", 248, 328, 68, 72, "MOD EG AMT")
    L.panel(322, 314, 76, 88, "RING MOD", more="RINGMOD")
    L.ctl("ring_src", "leds", 326, 328, 68, 72, "SOURCES")
    L.panel(402, 314, 142, 88, "WAVEFORM MOD", keys=["angle_lfo3", "pw_lfo3"], more="WAVEMOD")
    L.ctl("angle_lfo3", "knob", 406, 328, 68, 72, "LFO3>ANGLE")
    L.ctl("pw_lfo3", "knob", 474, 328, 68, 72, "LFO3>PW")
    L.panel(548, 314, 92, 88, "FREQ MOD", more="FMMOD")
    L.ctl("fm_amt", "knob", 550, 328, 48, 72, "FM AMT", 26)
    L.ctl("fm_route", "leds", 596, 328, 42, 72, "ROUTE")

    # ---- noise: a column right of the oscillators ----
    L.panel(646, 18, 96, 384, "NOISE", more="NOISE")
    L.ctl("noise_a", "knob", 650, 34, 88, 68, "ATTACK")
    L.ctl("noise_r", "knob", 650, 104, 88, 68, "RELEASE")
    L.ctl("noise_sus", "toggle", 650, 176, 44, 42, "SUSTAIN")
    L.ctl("noise_env", "toggle", 694, 176, 44, 42, "ENV")
    L.ctl("noise_color_mix", "knob", 650, 222, 88, 70, "COLOR MIX")
    L.ctl("noise_color_pair", "leds", 650, 296, 88, 102, "COLOR")
    # (2026-10-30) the modulation row's and the noise column's MORE pages: each section's params, and what drives them
    L.page("PITCHMOD", "PITCH MODULATION", "Vibrato from LFO 1 and pitch sweeps from the mod envelope.", [
        ("pitch_lfo", "LFO 1 Amount"), ("pitch_env", "Mod EG Amount"), ("pitch_var", "Pitch Variance"),
        ("lfo1_rate", "LFO 1 Rate"), ("lfo1_wave", "LFO 1 Wave"), ("lfo1_sync", "LFO 1 Sync"),
        ("menv_a", "Mod EG Attack"), ("menv_d", "Mod EG Decay")])
    L.page("RINGMOD", "RING MOD", "Two oscillators multiplied: bell and metal tones. Its level: the mixer.", [
        ("ring_src", "Sources"), ("ring_level", "Level (Mixer)"), ("ring_svf", "To SVF"), ("ring_lad", "To Ladder")])
    L.page("WAVEMOD", "WAVEFORM MOD", "LFO 3 moves the oscillators' wave angle and pulse width.", [
        ("angle_lfo3", "LFO 3 > Angle"), ("pw_lfo3", "LFO 3 > Pulse Width"), ("lfo3_rate", "LFO 3 Rate"),
        ("lfo3_wave", "LFO 3 Wave"), ("lfo3_sync", "LFO 3 Sync"), ("lfo3_range", "LFO 3 Range")])
    L.page("FMMOD", "FREQ MOD", "One oscillator moves another's frequency (ROUTE); the filters' FM below.", [
        ("fm_amt", "FM Amount"), ("fm_route", "Route"), ("svf_fm", "SVF FM Amount"), ("svf_fm_src", "SVF FM Source"),
        ("lad_fm", "Ladder FM Amount"), ("lad_fm_src", "Ladder FM Source")])
    L.page("NOISE", "NOISE", "The noise source: its colours, its own envelope, and where it goes.", [
        ("noise_a", "Env Attack"), ("noise_r", "Env Release"), ("noise_sus", "Env Sustain"), ("noise_env", "Env On"),
        ("noise_color_mix", "Color Mix"), ("noise_color_pair", "Color Pair"), ("noise_level", "Level (Mixer)"),
        ("noise_svf", "To SVF"), ("noise_lad", "To Ladder")])

    # ---- the bottom row, as on the instrument: arpeggiator, chord, generative, polyphony. Each module: a line of
    # buttons (labels beside them) under its title, left of its MORE triangle, then its knobs and LED lists ----
    YB, HB = 408, 188          # the row (2026-10-30: 56 taller, a third line under the controls)
    YA, YL, HL = 424, 442, 96  # the line of buttons; the line of controls under it and its height
    L.panel(176, YB, 232, HB, "ARPEGGIATOR", more="ARP", keys=["arp_on"])
    L.cells([("arp_on", "ON", 44), ("arp_latch", "LATCH", 59), ("arp_sync", "SYNC", 56)], 180, YA, 14, "toggle", SIDE)
    if "arp_on" in L.index:   # the master clock's TEMPO LED (on the beat) after the buttons; RATE's over its window
        L.rate_led("TEMPO", 0, 341, YA + 7, "TEMPO")
        L.rate_led("ARP", 0, 350, YL + 13)
    L.cells([("arp_pattern", "PATTERN", 54, "leds"), ("arp_oct", "OCT", 42, "leds"),
             ("arp_dir", "DIRECTION", 54, "leds"), ("arp_div", "RATE", 44, "button")], 178, YL, HL)
    L.column([("arp_gate", "GATE"), ("arp_swing", "SWING")], 372, YL, 36, HL // 2, "small")
    YX = YL + HL + 8   # (2026-10-30) the third line: LINK under RATE; BALLS and PARTICLES
    L.ctl("link_arp", "toggle", 340, YX, 50, 14, "LINK", SIDE)   # the arp's RATE the same in all 3 Synths
    L.page("ARP", "ARPEGGIATOR", "Plays the held keys in turn; RATE is each step's note value.", [
        ("arp_on", "On"), ("arp_latch", "Latch"), ("arp_pattern", "Pattern"), ("arp_oct", "Octaves"),
        ("arp_dir", "Octave Direction"), ("arp_oct_order", "Octave Order"), ("arp_pendulum", "Pendulum"),
        ("arp_pend_mode", "Pendulum Ends"), ("arp_rnd_mode", "Random Mode"), ("arp_sync", "Tempo Sync"),
        ("arp_div", "Rate (Synced)"), ("arp_timing", "Timing"), ("arp_rate", "Free Rate"), ("arp_gate", "Gate Length"),
        ("arp_swing", "Swing"), ("arp_follow_swing", "Follow Swing"), ("arp_reset", "Note Reset"),
        ("link_arp", "Link Rate (3 Synths)")])

    L.panel(412, YB, 58, HB, "CHORD", more="CHORD", keys=["chord_on"])
    L.ctl("chord_on", "toggle", 416, YA, 36, 14, "ON", SIDE)
    L.ctl("chord_learn", "toggle", 413, YL, 56, 36, "LEARN")
    L.ctl("chord_shape", "button", 413, YL + 36, 56, HL - 36, "SHAPE")
    L.page("CHORD", "CHORD MEMORY", "Each key plays a chord. LEARN on, play a chord, LEARN off: it is kept.", [
        ("chord_on", "On"), ("chord_learn", "Learn"), ("chord_shape", "Shape"), ("chord_iv1", "Note 2"),
        ("chord_iv2", "Note 3"), ("chord_iv3", "Note 4"), ("chord_iv4", "Note 5"), ("chord_iv5", "Note 6")])

    L.panel(474, YB, 144, HB, "GENERATIVE", more="GEN", keys=["gen_euclid_on", "gen_tm_on"])
    L.row([("gen_euclid_on", "EUCLID"), ("gen_tm_on", "TURING")], 478, YA, 60, 14, "toggle", SIDE)
    if "gen_steps" in L.index:
        L.euclid_at = (476, YL, 58, HL)
    L.column([("gen_steps", "STEPS"), ("gen_hits", "HITS")], 534, YL, 41, HL // 2, "small")
    L.column([("gen_rot", "ROTATE"), ("gen_prob", "CHANCE")], 575, YL, 41, HL // 2, "small")
    L.page("GEN", "GENERATIVE", "Euclidean rhythm and Turing machine melodies from the held keys.", [
        ("gen_euclid_on", "Euclid On"), ("gen_steps", "Euclid Steps"), ("gen_hits", "Euclid Hits"),
        ("gen_rot", "Euclid Rotation"), ("gen_prob", "Step Probability"), ("gen_vel_rand", "Velocity Random"),
        ("gen_tm_on", "Turing On"), ("gen_tm_len", "Turing Length"), ("gen_tm_change", "Turing Change"),
        ("gen_scale", "Turing Scale"), ("gen_range", "Turing Octaves"), ("key_root", "Key"), ("link_key", "Link Key"),
        ("key_scale", "Scale"), ("link_scale", "Link Scale")])
    # (2026-10-30) BALLS and PARTICLES on the third line, each its ON and its MORE triangle
    for k, (key, label, pid) in enumerate((("phys_balls_on", "BALLS", "BALLS"), ("phys_parts_on", "PARTICLES", "PARTICLES"))):
        y0 = YX - 2 + 22 * k
        L.ctl(key, "toggle", 480, y0 + 2, 110, 14, label, SIDE)
        L.panel(476, y0 - 7, 140, 14, "", style="caption", keys=[key], more=pid)
    # the bouncing balls and particles (2026-10-29): two pages (the MORE index lists them), each with the box moving
    shared = [("phys_max", "Max Notes/s"), ("phys_dyn", "Dynamics"), ("phys_len", "Note Length"), ("phys_sync", "Sync"),
              ("phys_keys", "Keys"), ("phys_restart", "Key Restarts"), ("phys_octave", "Low Octave"),
              ("phys_range", "Octaves"), ("key_root", "Key"), ("key_scale", "Scale")]
    world = [("phys_speed", "Speed"), ("phys_gravity", "Gravity"), ("phys_size", "Box Size"), ("phys_bounce", "Bounce")]
    link = [("link_phys", "Link (3 Synths)")]   # (2026-10-30) every phys_* setting the same in all three
    # (2026-10-30) each wall PLAYS, SILENT or OPEN; FOLLOW OCTAVE (the keyboard's octave buttons move LOW OCTAVE)
    world += [(k, lab) for k, lab in (("phys_floor", "Floor"), ("phys_ceil", "Ceiling"), ("phys_left", "Left Wall"),
                                      ("phys_right", "Right Wall")) if k in L.index]
    shared += [("phys_follow_oct", "Follow Octave")] if "phys_follow_oct" in L.index else []
    L.page("BALLS", "BOUNCING BALLS", "Balls bounce in a box: each wall they hit plays a note in the KEY.",
           [("phys_balls_on", "Balls On"), ("phys_balls", "Balls")] + link + world + shared, pic=("PHYS", "phys_"))
    L.page("PARTICLES", "PARTICLES", "Particles fall from the top: the LINE, a collision or their end plays a note.",
           [("phys_parts_on", "Particles On"), ("phys_rate", "Rate"), ("phys_spread", "Spread"), ("phys_life", "Life"),
            ("phys_attract", "Attraction"), ("phys_trigger", "Plays On")] + link + world + shared, pic=("PHYS", "phys_"))
    # (2026-10-30) rain and ripples, the Markov melody: a page each (in the MORE index), its picture moving
    L.page("RAIN", "RAIN AND RIPPLES", "Drops fall on a pond: each plays a note in the KEY; meeting rings echo softly.", [
        ("rain_on", "Rain On"), ("rain_rate", "Rate"), ("rain_sync", "Sync"), ("rain_spread", "Drop Size Spread"),
        ("rain_speed", "Ripple Speed"), ("rain_life", "Ripple Life"), ("rain_echo", "Echo"), ("rain_stones", "Stones"),
        ("rain_max", "Max Notes/s"), ("rain_len", "Note Length"), ("rain_keys", "Keys"), ("rain_octave", "Low Octave"),
        ("rain_range", "Octaves"), ("key_root", "Key"), ("key_scale", "Scale")], pic=("RAIN", "rain_"))
    L.page("MARKOV", "MARKOV MELODY", "Learns from what you play into this Synth, then improvises in that style.", [
        ("mkv_on", "Markov On"), ("mkv_learn", "Learn"), ("mkv_forget", "Forget (Clear)"), ("mkv_order", "Order"),
        ("mkv_creat", "Creativity"), ("mkv_rhythm", "Rhythm"), ("mkv_rate", "Rate"), ("mkv_gate", "Note Length"),
        ("mkv_quant", "In Key"), ("mkv_range", "Octaves"), ("mkv_max", "Max Notes/s"), ("mkv_keys", "Keys"),
        ("key_root", "Key"), ("key_scale", "Scale")], pic=("MARKOV", "mkv_"))
    # (2026-10-30) GAME OF LIFE, GRAVITY ORBITS, PENDULUMS: a page each (the MORE index lists them), each picture moving
    scale = [("key_root", "Key"), ("key_scale", "Scale")]
    L.page("LIFE", "GAME OF LIFE", "Cells live and die by the RULE; births or a scanning playhead play notes in the KEY.",
           [("life_on", "Life On"), ("life_mode", "Plays"), ("life_rate", "Rate"), ("life_speed", "Free Rate", "life_rate", [6]),
            ("life_reseed", "Reseed"), ("life_rule", "Rule"), ("life_density", "Density"), ("life_seed", "Seed"),
            ("life_wrap", "Wrap Edges"),
            ("life_notes", "Notes per Step"), ("life_max", "Max Notes/s"), ("life_len", "Note Length"), ("life_keys", "Keys"),
            ("life_octave", "Low Octave"), ("life_range", "Octaves")] + scale, pic=("LIFE", "life_"))
    L.page("ORBITS", "GRAVITY ORBITS", "Planets circle a sun: crossing a spoke plays a note in the KEY.",
           [("orb_on", "Orbits On"), ("orb_planets", "Planets"), ("orb_speed", "Speed"), ("orb_gravity", "Gravity"),
            ("orb_ecc", "Eccentricity"), ("orb_spread", "Spread"), ("orb_spokes", "Spokes"), ("orb_planet_to", "Planets As"),
            ("orb_quant", "Sync"), ("orb_max", "Max Notes/s"), ("orb_len", "Note Length"), ("orb_keys", "Keys"),
            ("orb_octave", "Low Octave"), ("orb_range", "Octaves")] + scale, pic=("ORBITS", "orb_"))
    L.page("PEND", "PENDULUMS", "Pendulums drift out of step and meet again each CYCLE; each plays its note as it swings.",
           [("pend_on", "Pendulums On"), ("pend_count", "Pendulums"), ("pend_cycle", "Cycle"), ("pend_speed", "Swings"),
            ("pend_spread", "Spread"), ("pend_damp", "Damping"), ("pend_trigger", "Plays At"), ("pend_restart", "Key Restarts"),
            ("pend_max", "Max Notes/s"), ("pend_dyn", "Dynamics"), ("pend_len", "Note Length"), ("pend_keys", "Keys"),
            ("pend_octave", "Low Octave")] + scale, pic=("PEND", "pend_"))

    L.panel(622, YB, 120, HB, "POLYPHONY", more="POLY")
    L.ctl("unison", "toggle", 626, YA, 64, 14, "UNISON", SIDE)
    L.cells([("voices", "VOICES", 40, "button"), ("uni_detune", "DETUNE", 40, "knob"), ("hold", "HOLD", 40, "toggle")],
            622, YL, HL, size=24)
    L.page("POLY", "POLYPHONY", "How notes share the voices.", [
        ("voices", "Voices"), ("note_priority", "Mono Priority"), ("steal_mode", "Voice Stealing"), ("hold", "Hold"),
        ("unison", "Unison"), ("uni_count", "Unison Voices"), ("uni_detune", "Unison Detune"),
        ("pitch_var", "Pitch Variance")])

    # ---- centre console: the screen (presets, mod matrix, MORE pages, the step editor, help), its buttons, and under
    # them the SEQUENCER module ----
    zones = {"SCREEN": (770, 22, 400, 362), "TAB_MATRIX": (769, 390, 48, 40), "TAB_MORE": (817, 390, 48, 40),
             "TAB_ANALOG": (865, 390, 48, 40), "TAB_RANDOM": (913, 390, 48, 40), "TAB_SEQ": (961, 390, 44, 40),
             "ENCODER": (1008, 384, 64, 92), "PREV": (1078, 390, 44, 40), "NEXT": (1124, 390, 44, 40),
             "BRAND": (1076, 438, 94, 28),
             # PANEL FOCUS: which Synth the panel shows and edits (multitimbral), and the SETUP page of all three
             # (on the row above's grid, so the SETTINGS page's button fits at the end: desktop/settings.c)
             "FOCUS1": (769, 432, 48, 42), "FOCUS2": (817, 432, 48, 42), "FOCUS3": (865, 432, 48, 42),
             "TAB_SETUP": (913, 432, 48, 42), "TAB_SETTINGS": (961, 432, 44, 42),
             # (2026-10-30) the third row: TOGETHER (lit: the SYNTH buttons add and remove Synths edited together,
             # hub.c), GENERATIVE (the generators' hub page) and GEN OFF (every generator off in all three Synths;
             # again: back on); the global TEMPO readout with HOST / OWN (the SETTINGS page's RECEIVE MIDI CLOCK and
             # TEMPO); UNDO and REDO (library.c's undo, as Ctrl+Z / Ctrl+Y). Spaced for their labels, not the grid.
             "TOGETHER": (769, 478, 54, 42), "TAB_GEN": (827, 478, 62, 42), "GEN_OFF": (893, 478, 48, 42),
             "TEMPO": (946, 482, 80, 24), "TEMPO_HOST": (1032, 481, 54, 15), "TEMPO_OWN": (1032, 498, 54, 15),
             "UNDO": (1102, 478, 34, 42), "REDO": (1136, 478, 34, 42)}
    # Synth 2 and 3's copies are reached through the Panel Focus; the setup keys live on the SETUP page
    L.placed.update(k for k in L.index if k.startswith(("s2_", "s3_")))
    # (syn_solo: the S half of each SYNTH button, editor.c; never saved with the sound, so no MORE page row)
    L.placed.update(k for k in ("syn_on", "syn_ch", "key_lo", "key_hi", "seq_all", "midi_ch",
                                "learn_mode", "syn_solo") if k in L.index)
    if "arp_on" in L.index:
        L.rate_led("TEMPO", 0, 1094, 491)   # (2026-10-30) the beat, beside the TEMPO readout
    # the step sequencer: ON, PLAY, REC, NOTE TRIG, TRANSPOSE, STEP MOD; the page LEDs (the step editor's pages, the
    # playing one lit); LENGTH and RATE. Its MORE triangle: the settings; the SEQ button: the step editor
    L.panel(764, 532, 412, 62, "SEQUENCER", more="SEQ", keys=["seq_on"])
    L.column([("seq_on", "ON"), ("seq_note_trig", "TRIG")], 768, 548, 62, 22, "toggle", SIDE)
    L.column([("seq_play", "PLAY"), ("seq_transpose", "TRANSP")], 830, 548, 72, 22, "toggle", SIDE)
    L.column([("seq_rec", "REC"), ("seq_step_mod", "STEP MOD")], 902, 548, 70, 22, "toggle", SIDE)
    if "seq_on" in L.index:
        zones["SEQ_LEDS"] = (970, 546, 80, 46)
    L.ctl("seq_length", "small", 1050, 544, 48, 50, "LENGTH")
    L.ctl("seq_div", "button", 1098, 547, 46, 46, "RATE")
    if "seq_div" in L.index:
        L.rate_led("SEQ", 0, 1141, 572)   # right of RATE's red button
    L.ctl("link_seq", "toggle", 1146, 547, 30, 46, "LINK")   # (2026-10-30) RATE the same in all 3 Synths
    L.placed.add("seq_pos")   # the readout: the step editor and the page LEDs show it
    L.page("SEQ", "SEQUENCER", "Settings; the steps: the SEQ button. Synced: RATE is each step's note value.", [
        ("seq_on", "On"), ("seq_play", "Play"), ("seq_rec", "Record"), ("seq_rec_mode", "Record Mode"),
        ("seq_step_mod", "Step Mod"), ("seq_length", "Length"), ("seq_sync", "Sync"), ("seq_div", "Rate (Synced)"),
        ("seq_timing", "Timing"), ("seq_rate", "Free Rate"), ("seq_follow_swing", "Follow Swing"),
        ("clock_swing", "Master Swing"), ("seq_note_trig", "Note Trig"), ("seq_note_reset", "Note Reset"),
        ("seq_restart_trig", "Restart On Trig"), ("seq_restart_hold", "Restart On Hold"),
        ("seq_transpose", "Transpose"), ("seq_tr_mode", "Transpose Mode"), ("seq_one_shot", "One Shot"),
        ("seq_pattern", "Factory Sequence"), ("seq_host_ss", "Host Start/Stop"), ("link_seq", "Link Rate (3 Synths)")])

    # ---- mixer ----
    X = DX
    L.panel(X + 1086, 18, 138, 578, "MIXER", more="MIXER")
    L.page("MIXER", "MIXER: EXTERNAL INPUT", "The plugin's audio input (connect it in the host) into every "
           "sounding voice's filters.", [("ext_level", "Ext In Level"), ("ext_route", "Ext In Route")])
    chans = [("o1", "OSC 1"), ("o2", "OSC 2"), ("o3", "OSC 3"), ("mac", "MACRO"), ("ring", "RING MOD"), ("noise", "NOISE")]
    L.planned += [c + s for c, _ in chans if c + "_level" not in L.index for s in ("_level", "_svf", "_lad")]
    chans = [c for c in chans if c[0] + "_level" in L.index]
    L.page("MIXER", "MIXER", "Each source's level, and the filters it goes to (the bars below).",
           [it for src, label in chans for it in ((src + "_level", label.title().replace("Osc", "OSC") + " Level"),
                                                   (src + "_svf", label.title().replace("Osc", "OSC") + " > SVF"),
                                                   (src + "_lad", label.title().replace("Osc", "OSC") + " > Ladder"))],
           pic=("MIXER", ""))
    step = 111 if len(chans) <= 5 else 94   # the macro oscillator's channel makes six
    for k, (src, label) in enumerate(chans):
        y = 38 + step * k
        L.ctl(src + "_level", "knob", X + 1088, y, 62, 70, label, 30)
        L.ctl(src + "_svf", "toggle", X + 1150, y + 10, 32, 50, "SVF")
        L.ctl(src + "_lad", "toggle", X + 1184, y + 10, 38, 50, "LADDER")
        if src == "mac":   # its MORE triangle: the macro oscillator's own page
            L.panel(X + 1088, y - 8, 134, 14, "", style="caption", more="MACRO")

    # ---- filters: SVF and ladder side by side, link / mix / route between them ----
    L.panel(X + 1230, 18, 306, 290, "STATE VARIABLE FILTER|LADDER FILTER", more="FILTERS")
    L.page("FILTERS", "FILTERS", "The two filters, how they connect, and their curves (below).", [
        ("svf_cut", "SVF Cutoff"), ("svf_res", "SVF Resonance"), ("svf_mode", "SVF Mode"), ("svf_pair", "SVF Pair"),
        ("svf_space", "SVF Spacing"), ("flt_cut", "Ladder Cutoff"), ("flt_res", "Ladder Resonance"),
        ("flt_mode", "Ladder Mode"), ("flt_slope", "Ladder Slope"), ("flt_drive", "Ladder Drive"),
        ("flt_route", "Route"), ("flt_mix", "Mix (SVF > Ladder)"), ("flt_link", "Link Cutoff"),
        ("link_cut", "Cutoffs: All 3 Synths")], pic=("FILTER", ""))
    L.ctl("svf_cut", "big", X + 1256, 36, 100, 100, "CUTOFF")
    L.ctl("flt_cut", "big", X + 1410, 36, 100, 100, "CUTOFF")
    L.ctl("link_cut", "toggle", X + 1356, 60, 54, 40, "ALL 3")   # (2026-10-30) both cutoffs in all 3 Synths, relative
    L.ctl("flt_link", "toggle", X + 1356, 104, 54, 44, "LINK CUTOFF")
    L.ctl("svf_res", "knob", X + 1240, 148, 58, 66, "RESONANCE")
    L.ctl("svf_space", "knob", X + 1298, 148, 56, 66, "SPACING")
    L.ctl("flt_mix", "knob", X + 1356, 148, 54, 66, "MIX")
    L.ctl("flt_res", "knob", X + 1410, 148, 58, 66, "RESONANCE")
    L.ctl("flt_drive", "knob", X + 1470, 148, 58, 66, "DRIVE")
    L.ctl("svf_mode", "leds", X + 1236, 216, 54, 88, "MODE")
    L.ctl("svf_pair", "leds", X + 1292, 216, 60, 88, "PAIR")
    L.ctl("flt_route", "leds", X + 1356, 216, 54, 88, "ROUTE")
    L.ctl("flt_mode", "leds", X + 1414, 216, 54, 88, "MODE")
    L.ctl("flt_slope", "leds", X + 1474, 216, 58, 88, "SLOPE")
    L.panel(X + 1230, 314, 306, 282, "CUTOFF MODULATION", more="CUTMOD")
    L.page("CUTMOD", "CUTOFF MODULATION", "What moves each filter's cutoff: envelope, LFO 2, the keys, an oscillator.", [
        ("svf_env", "SVF EG Amount"), ("svf_lfo", "SVF LFO 2 Amount"), ("svf_kbd", "SVF Key Track"),
        ("svf_fm", "SVF FM Amount"), ("svf_fm_src", "SVF FM Source"), ("key_center", "Key Track Centre"),
        ("lad_env", "Ladder EG Amount"), ("lad_lfo", "Ladder LFO 2 Amount"), ("lad_kbd", "Ladder Key Track"),
        ("lad_fm", "Ladder FM Amount"), ("lad_fm_src", "Ladder FM Source")])
    for y, f, cap in ((330, "svf", "SVF"), (462, "lad", "LADDER")):
        L.panel(X + 1238, y, 120, 14, cap, style="caption")
        L.row([(f + "_env", "EG AMT"), (f + "_lfo", "LFO 2 AMT"), (f + "_kbd", "KEY TRK"), (f + "_fm", "FM AMT"),
               (f + "_fm_src", "FM SRC", "leds")], X + 1234, y + 12, 60, 90)

    # ---- envelopes: a row each, buttons over the time knobs, curve / scaling knobs at the right ----
    for k, (p, title) in enumerate([("fenv", "FILTER ENVELOPE"), ("amp", "AMPLIFIER ENVELOPE"),
                                    ("menv", "MODULATION ENVELOPE")]):
        yt = 18 + 112 * k
        L.panel(X + 1542, yt, 478, 108, title, more="ENV%d" % (k + 1))
        L.page("ENV%d" % (k + 1), title, "Its shape below: stage times as their knobs, curves, sustain.", [
            (p + "_delay", "Delay"), (p + "_a", "Attack"), (p + "_hold", "Hold"), (p + "_d", "Decay"),
            (p + "_s", "Sustain"), (p + "_r", "Release"), (p + "_acurve", "Attack Curve"),
            (p + "_dcurve", "Decay Curve"), (p + "_rcurve", "Release Curve"), (p + "_scale", "Time Scale"),
            (p + "_vel", "Velocity Amount"), (p + "_multi", "Multi Trig"), (p + "_sync", "Tempo Sync"),
            (p + "_loop", "Loop"), (p + "_latch", "Latch"), (p + "_reset", "Reset")], pic=("ENV", p + "_"))
        L.row([(p + "_multi", "MULTI"), (p + "_sync", "SYNC"), (p + "_loop", "LOOP"), (p + "_latch", "LATCH"),
               (p + "_reset", "RESET")], X + 1550, yt + 16, 62, 14, "toggle", SIDE)
        L.row([(p + "_delay", "DELAY"), (p + "_a", "ATTACK"), (p + "_hold", "HOLD"), (p + "_d", "DECAY"),
               (p + "_s", "SUSTAIN"), (p + "_r", "RELEASE")], X + 1546, yt + 32, 52, 66, "knob", 26)
        L.row([(p + "_acurve", "A CURVE"), (p + "_dcurve", "D CURVE"), (p + "_rcurve", "R CURVE")],
              X + 1856, yt + 14, 52, 42, "small")
        L.row([(p + "_scale", "KEY SCL"), (p + "_vel", "VEL AMT")], X + 1882, yt + 56, 52, 42, "small")

    # ---- synth effect and master effects (send levels); delay, reverb and output under them ----
    L.panel(X + 1542, 354, 238, 110, "SYNTH EFFECT", more="SFX")
    L.cells([("modfx_on", "ON", 40, "toggle"), ("modfx_type", "TYPE", 66, "button"), ("modfx_ctrl1", "CTRL 1", 44),
             ("modfx_ctrl2", "CTRL 2", 44), ("modfx_mix", "MIX", 42)], X + 1544, 378, 64)
    L.panel(X + 1784, 354, 236, 110, "MASTER EFFECTS - SHARED", more="MFX")
    L.cells([("mfx_on", "ON", 40, "toggle"), ("mfx_send1", "SEND 1", 46), ("mfx_send2", "SEND 2", 46),
             ("mfx_series", "SERIES", 50), ("mfx_width", "WIDTH", 50)], X + 1786, 378, 64)
    L.ctl("link_send", "toggle", X + 1832, 446, 90, 14, "LINK SENDS", SIDE)   # (2026-10-30) the sends alike
    # the two master effect slots, any type each (by default a stereo delay and a plate): TYPE and the two knobs their
    # default types are played by most; every control of the chosen type is on the slot's MORE page
    L.panel(X + 1542, 468, 162, 128, "MASTER FX 1 - SHARED", more="DELAY")
    L.cells([("dly_type", "TYPE", 60, "button"), ("dly_time", "TIME", 50), ("dly_fb", "FDBK", 50)], X + 1544, 488, 84)
    L.panel(X + 1708, 468, 162, 128, "MASTER FX 2 - SHARED", more="REVERB")
    L.cells([("rev_type", "TYPE", 60, "button"), ("rev_decay", "DECAY", 50), ("rev_size", "SIZE", 50)], X + 1710, 488,
            84)
    L.panel(X + 1874, 468, 146, 128, "SYNTH OUTPUT", more="OUTPUT")
    L.ctl("volume", "knob", X + 1876, 490, 54, 78, "VOLUME", 34)
    L.cells([("pan", "PAN", 46), ("spread", "SPREAD", 44)], X + 1930, 490, 78)
    if "master_level" in L.index:   # (2026-10-30) lit: VOLUME is MASTER (master_level, all three Synths; hub.c)
        zones["VOL_MASTER"] = (X + 1880, 572, 60, 14)
    L.page("OUTPUT", "SYNTH OUTPUT", "This Synth's level, place and sends; MASTER: all three Synths' level.", [
        ("volume", "Volume"), ("pan", "Pan"), ("spread", "Spread"), ("mfx_send1", "Send 1 (FX 1)"),
        ("mfx_send2", "Send 2 (FX 2)"), ("master_level", "Master (All Synths)")])
    L.page("SFX", "SYNTH EFFECT", "The controls of the selected type (TYPE changes the list).", sfx_page(L))
    mac = [("mac_model", "Model"), ("mac_harm", "Harmonics"), ("mac_timbre", "Timbre"), ("mac_morph", "Morph"),
           ("mac_oct", "Octave"), ("mac_freq", "Frequency"), ("mac_aux", "Aux Mix"), ("mac_level", "Level"),
           ("mac_svf", "To SVF"), ("mac_lad", "To Ladder")]
    mac += [(p["key"], p.get("name", p["key"])) for p in L.params   # any other macro key: here, not on OTHER
            if p["key"].startswith("mac_") and p["key"] not in dict(mac)]
    L.page("MACRO", "MACRO OSCILLATOR", "A fourth sound source with many models (level: the mixer).",
           mac)
    L.page("MFX", "MASTER EFFECTS", "Two effects on two sends after the synth effects, shared by the Synths.", [
        ("mfx_on", "On"), ("mfx_buss", "Buss Mode"), ("mfx_send1", "Send 1 (FX 1)"), ("mfx_send2", "Send 2 (FX 2)"),
        ("mfx_series", "FX 1 > FX 2"), ("mfx_width", "Width"), ("link_send", "Link Sends (3 Synths)")])
    L.page("DELAY", "MASTER FX 1", "Its type's controls (TYPE changes the list). Fed by SEND 1.", mfx_page(L, "dly_"))
    L.page("REVERB", "MASTER FX 2", "Its type's controls (TYPE changes the list). Fed by SEND 2.", mfx_page(L, "rev_"))

    # ---- left-hand controller: octave, glide, wheels, X/Y pad, bend range; the ribbon over the keys ----
    zones.update({"OCT_LEDS": (50, 632, 116, 26), "OCT_DOWN": (64, 662, 36, 16), "OCT_UP": (118, 662, 36, 16),
                  "PITCH": (56, 706, 36, 112), "MODW": (110, 706, 36, 112), "PAD": (166, 706, 126, 112),
                  "KEYS": (384, 640, 1744, 200), "RIBBON": (384, 612, 1744, 20),   # shorter with a MORE page
                  # (2026-10-30) the controller's DEST button, top left: modulate from the wheels, the X/Y pad,
                  # the ribbon or the keyboard (quickmod.c); the computer keyboard's hint on the front rail
                  "QM_HANDS": (43, 623, 26, 9), "KB_HINT": (400, 841, 900, 14),
                  "PANIC": (236, 691, 54, 13)})   # (2026-10-30) every note off, under glide's ON
    L.ctl("glide_time", "knob", 172, 626, 64, 70, "GLIDE RATE")
    L.ctl("glide", "toggle", 236, 632, 42, 56, "ON")
    L.ctl("glide_type", "leds", 278, 624, 52, 76, "TYPE")
    L.column([("glide_legato", "LEGATO"), ("glide_gated", "GATED"), ("glissando", "GLISS")], 298, 706, 72, 22,
             "toggle", SIDE)   # the other glide options, beside the pad
    L.panel(298, 770, 72, 12, "BEND RANGE", style="caption", keys=["bend_up", "bend_down"])
    if "bend_up" in L.index:
        L.tri(358, 770, "BEND")   # (2026-10-30) after its caption (the knobs fill the corner)
    L.cells([("bend_up", "UP", 36, "small"), ("bend_down", "DOWN", 36, "small")], 298, 780, 44, "small", 18)
    L.page("BEND", "BEND RANGE", "How far the pitch wheel bends, up and down, and each oscillator's own offset.", [
        ("bend_up", "Bend Up"), ("bend_down", "Bend Down"), ("o1_bend_up_off", "OSC 1 Up Offset"),
        ("o1_bend_dn_off", "OSC 1 Down Offset"), ("o2_bend_up_off", "OSC 2 Up Offset"),
        ("o2_bend_dn_off", "OSC 2 Down Offset"), ("o3_bend_up_off", "OSC 3 Up Offset"),
        ("o3_bend_dn_off", "OSC 3 Down Offset")])
    L.page("CTRL", "CONTROLLERS AND TUNING", "Keyboard, wheels, glide and tuning.", [
        ("bend_up", "Bend Up"), ("bend_down", "Bend Down"), ("vel_curve", "Velocity Curve"),
        ("transpose", "Transpose"), ("octave_shift", "Octave Shift"), ("key_center", "Key Track Centre"),
        ("master_tune", "Master Tune"), ("fine_tune", "Fine Tune"), ("glide", "Glide"), ("glide_time", "Glide Rate"),
        ("glide_type", "Glide Type"), ("glide_legato", "Legato Glide"), ("glide_gated", "Gated Glide"),
        ("glissando", "Glissando"),
        # the X/Y pad and the saved wheel and pad positions (2026-10-29)
        ("pad_polarity", "Pad Polarity"), ("pad_hold", "Pad X/Y Hold"), ("load_pos", "Load Saved Pos"),
        ("wheel_pos", "Saved Mod Wheel"), ("pad_x_pos", "Saved Pad X"), ("pad_y_pos", "Saved Pad Y"),
        ("eg_times", "EG Times (MIN, Syncs)")])   # the envelopes' MIN and 13 synced values (2026-10-29)
    L.page("RIBBON", "RIBBON", "The touch strip over the keys (MIDI CC 16 position, CC 17 touch).", [
        ("ribbon_dst", "Destination"), ("ribbon_amt", "Amount"), ("ribbon_mode", "Mode"), ("ribbon_origin", "Origin")])
    L.page("ANALOG", "ANALOG CHARACTER", "Per-voice differences, as between real analog voices. All 0 = exact.", [
        ("drift", "Drift"), ("an_cut", "Cutoff Spread"), ("an_res", "Resonance Spread"), ("an_env", "Envelope Spread"),
        ("an_lvl", "Level Spread"), ("an_tune", "Tuning Spread"), ("an_ladder", "Ladder Character")])
    return zones


MATRIX_COLS = ("src", "dst", "amt", "via", "via_amt", "xf", "xf_amt")

# (2026-10-30) the GENERATIVE hub page (desktop/hub.c): one row per generator, its ON and the MORE page it opens. The
# pages other branches add (ids below) join by themselves: a row per two-option "..._on" item on the page.
HUB_FIXED = [("EUCLID", "GEN", "gen_euclid_on"), ("TURING", "GEN", "gen_tm_on"),
             ("BOUNCING BALLS", "BALLS", "phys_balls_on"), ("PARTICLES", "PARTICLES", "phys_parts_on")]
HUB_LATER = ("LIFE", "ORBITS", "PEND", "RAIN", "MARKOV")
# the LINKs (shared params): which settings each one keeps the same in all three Synths
LINK_GROUPS = [("link_key", ("key_root",)), ("link_scale", ("key_scale",)),
               ("link_arp", ("arp_div", "arp_timing", "arp_sync", "arp_rate")),
               ("link_seq", ("seq_div", "seq_timing", "seq_sync", "seq_rate")),
               ("link_send", ("mfx_send1", "mfx_send2")),
               ("link_cut", ("svf_cut", "flt_cut"))] + \
              [("link_lfo%d" % n, tuple("lfo%d_%s" % (n, s) for s in ("rate", "sync", "timing"))) for n in range(1, 5)]


def hub_and_links(lay, pages, page_ix, lines):
    """HUB[]: the generators (label, page, ON param); LINK_OF[]: {param, its LINK param}"""
    rows = []
    for label, pid, key in HUB_FIXED:
        if pid in page_ix and key in lay.index:
            rows.append((label, page_ix[pid], lay.index[key]))
    for pid in HUB_LATER:
        if pid not in page_ix:
            continue
        _, title, _, got, _ = pages[page_ix[pid]]
        ons = [it for it in got if it[0].endswith("_on") and lay.nopts(it[0]) == 2]
        for it in ons or [(None, title)]:
            rows.append((title if len(ons) <= 1 else it[1].upper(), page_ix[pid], lay.index[it[0]] if it[0] else -1))
    lines.append("typedef struct { const char *label; short page, on; } lay_hub_t;   /* a generator: its ON (-1: none) */")
    lines.append("LAY lay_hub_t HUB[] = {%s};" % ", ".join("{%s, %d, %d}" % (c_str(a), b, c) for a, b, c in rows or
                                                          [("", -1, -1)]))
    lines.append("#define NHUB %d" % len(rows))
    groups = LINK_GROUPS + [("link_phys", tuple(k for k in lay.index if k.startswith("phys_")))]
    pairs = [(lay.index[k], lay.index[g]) for g, keys in groups if g in lay.index for k in keys if k in lay.index]
    lines.append("LAY short LINK_OF[][2] = {%s};   /* a setting, and the LINK that keeps it the same in all 3 Synths */"
                 % ", ".join("{%d, %d}" % pr for pr in pairs or [(-1, -1)]))
    lines.append("#define NLINKOF %d" % len(pairs))


# (2026-10-30) the generator tabs (desktop/nav.c): one per generator page in HUB order, a short name each (EUCLID and
# TURING share the GEN page); a page added to HUB_LATER joins with the first five letters of its id
HUB_TAB_NAMES = {"GEN": "EUC", "BALLS": "BALLS", "PARTICLES": "PART", "LIFE": "LIFE", "ORBITS": "ORB", "PEND": "PEND",
                 "RAIN": "RAIN", "MARKOV": "MKV"}


def hub_tabs(lay, page_ix, lines):
    """HUB_TAB[]: the generator pages, in the hub's order, with their tab names"""
    pids = [pid for _, pid, key in HUB_FIXED if pid in page_ix and key in lay.index] +            [pid for pid in HUB_LATER if pid in page_ix]
    tabs = []
    for pid in pids:
        if pid not in [t[0] for t in tabs]:
            tabs.append((pid, HUB_TAB_NAMES.get(pid, pid[:5])))
    lines.append("typedef struct { const char *label; short page; } lay_hub_tab_t;   /* a generator page's tab */")
    lines.append("LAY lay_hub_tab_t HUB_TAB[] = {%s};" % ", ".join("{%s, %d}" % (c_str(n), page_ix[pid]) for pid, n in tabs
                                                                   or [("", -1)]))
    lines.append("#define NHUB_TAB %d" % len(tabs))


def c_str(s):
    return '"%s"' % s.replace("\\", "\\\\").replace('"', '\\"')


def emit(lay, zones, out):
    P = lay.params
    kinds = ["knob", "small", "big", "oct", "leds", "toggle", "button"]
    lines = ["/* generated by ports/one-ish/desktop/gen_layout.py from params.json: do not edit */", "#pragma once",
             "#define BASE_W %d" % BASE_W, "#define BASE_H %d" % BASE_H, "#define LAY __attribute__((unused)) static const",
             "enum { " + ", ".join("K_" + k.upper() for k in kinds) + ", K_AMOUNT, K_MENU };",
             "typedef struct { short x, y, w, h; const char *title; short style, more; } lay_panel_t;   /* style: 0 "
             "frame, 1 module, 2 caption; more: its MORE page or -1 */",
             "typedef struct { short param, kind, x, y, w, h, size; const char *label; const char *const *leds; } lay_ctl_t;"]

    # MORE pages: the items whose keys exist, the OTHER page for whatever is left, then the panel triangles
    matrix, matrix_on = [], []
    for n in range(1, 33):
        keys = ["mod%d_%s" % (n, s) for s in MATRIX_COLS]
        if all(k in lay.index for k in keys[:4]):
            matrix.append("{%s}" % ", ".join(str(lay.index.get(k, -1)) for k in keys))
            lay.placed.update(k for k in keys if k in lay.index)
            matrix_on.append(str(lay.index.get("mod%d_on" % n, -1)))   # the row's ENABLE: its number (2026-10-29)
            lay.placed.add("mod%d_on" % n)
    on_pages = set()
    pages = []
    for pid, title, note, items, pic in lay.pages:
        got = [it for it in items if it[0] in lay.index and (it[2] is None or it[2] in lay.index)]
        if got:
            pages.append((pid, title, note, got, pic))
            on_pages.update(it[0] for it in got)
    other = [p["key"] for p in P if p["key"] not in lay.placed and p["key"] not in on_pages]
    if other:
        pages.append(("OTHER", "OTHER", "Parameters that have no home yet.", [(k, P[lay.index[k]]["name"], None, None)
                                                                             for k in other], None))
    page_ix = {pg[0]: i for i, pg in enumerate(pages)}

    styles = {"frame": 0, "module": 1, "caption": 2}
    rows = ["{%d, %d, %d, %d, %s, %d, %d}" % (x, y, w, h, c_str(t), styles[s], page_ix.get(m, -1))
            for x, y, w, h, t, s, m in lay.panels]
    lines.append("LAY lay_panel_t PANELS[] = {%s};" % ",\n    ".join(rows))
    lines.append("#define NPANELS %d" % len(rows))
    rows = []
    for n, (key, kind, x, y, w, h, size, label) in enumerate(lay.ctls):
        opts = P[lay.index[key]].get("options", [])
        leds = "0"
        if kind == "leds":
            lines.append("LAY char *const LEDS_%d[] = {%s};" % (
                n, ", ".join(c_str(ABBREV.get(o, o.upper()[:5])) for o in opts)))
            leds = "LEDS_%d" % n
        rows.append("{%d, K_%s, %d, %d, %d, %d, %d, %s, %s}" % (lay.index[key], kind.upper(), x, y, w, h, size,
                                                                c_str(label), leds))
    lines.append("LAY lay_ctl_t CTLS[] = {%s};" % ",\n    ".join(rows))
    lines.append("#define NCTLS %d" % len(rows))
    rows = ["{%d, %d, %d, %d, {%s}}" % (x, y, w, h, ", ".join(map(str, ix))) for x, y, w, h, ix in lay.waves]
    lines.append("typedef struct { short x, y, w, h; short param[4]; } lay_wave_t;   /* wave, angle, pw, mix */")
    lines.append("LAY lay_wave_t WAVES[] = {%s};" % ", ".join(rows))
    lines.append("#define NWAVES %d" % len(rows))
    # the Euclidean pattern's ring (GENERATIVE): where, and its params on, steps, hits, rotation (-1: absent)
    eu = [lay.index.get(k, -1) for k in ("gen_euclid_on", "gen_steps", "gen_hits", "gen_rot")]
    if lay.euclid_at is None or min(eu[1:]) < 0:
        eu = [-1] * 4
    zones["EUCLID"] = lay.euclid_at or (0, 0, 0, 0)
    zones.setdefault("SEQ_LEDS", (0, 0, 0, 0))   # (an empty zone without a sequencer)
    zones.setdefault("VOL_MASTER", (0, 0, 0, 0))
    sp = [lay.index.get(k, -1) for k in ("seq_on", "seq_play", "seq_rec", "seq_length", "seq_pos", "seq_step_mod",
                                         "seq_div", "seq_timing")]
    lines.append("LAY short SEQ_P[8] = {%s};   /* on, play, rec, length, readout, step mod, div, timing (-1: absent) */"
                 % ", ".join(map(str, sp)))
    lines.append("LAY short EUCLID_P[4] = {%s};   /* on, steps, hits, rotation */" % ", ".join(map(str, eu)))

    # the mod matrix: per row source, destination, amount, via, via amount, transform, transform amount (-1: absent)
    lines.append("enum { MX_SRC, MX_DST, MX_AMT, MX_VIA, MX_VIA_AMT, MX_XF, MX_XF_AMT, MX_COLS };")
    lines.append("LAY short MATRIX[][MX_COLS] = {%s};" % ", ".join(matrix or ["{-1, -1, -1, -1, -1, -1, -1}"]))
    lines.append("#define NMATRIX %d" % len(matrix))
    lines.append("LAY short MATRIX_ON[] = {%s};   /* each row's ENABLE (-1: none) */" % ", ".join(matrix_on or ["-1"]))

    # the MORE pages: their items (a param, its label, and the option of another param it depends on)
    lines.append("typedef struct { short param, cond; unsigned mask; const char *label; } lay_item_t;   /* cond: "
                 "shown while option (mask bit) of param cond is set; -1 always */")
    lines.append("enum { PIC_NONE, %s };   /* a MORE page's picture (more_pics.c) */"
                 % ", ".join("PIC_%s = %d" % (n, v) for n, v in PICS.items()))
    lines.append("#define PIC_ROWS %d   /* the screen rows a picture takes, at the bottom of its page */" % PIC_ROWS)
    lines.append("typedef struct { const char *title, *note; short first, count, pic; const char *pic_key; } lay_page_t;"
                 "   /* pic_key: the key prefix of what the picture shows (an envelope's \"fenv_\") */")
    items, prow = [], []
    for pid, title, note, got, pic in pages:
        prow.append("{%s, %s, %d, %d, %d, %s}" % (c_str(title), c_str(note), len(items), len(got),
                                                  PICS[pic[0]] if pic else 0, c_str(pic[1] if pic else "")))
        for key, label, cond, types in got:
            mask = sum(1 << t for t in types) if types is not None else 0xFFFFFFFF
            items.append("{%d, %d, 0x%Xu, %s}" % (lay.index[key], lay.index[cond] if cond else -1, mask, c_str(label)))
    lines.append("LAY lay_item_t ITEMS[] = {%s};" % ",\n    ".join(items or ['{-1, -1, 0, ""}']))
    lines.append("LAY lay_page_t PAGES[] = {%s};" % ",\n    ".join(prow or ['{"", "", 0, 0, 0, ""}']))
    lines.append("#define NPAGES %d" % len(pages))
    for pid in ("LFO1", "LFO2", "LFO3", "LFO4", "OSC1", "OSC2", "OSC3", "POLY", "ARP", "CHORD", "GEN", "SFX", "MFX",
                "DELAY", "REVERB", "MACRO", "CTRL", "RIBBON", "ANALOG", "SEQ", "ENV1", "ENV2", "ENV3", "MIXER",
                "FILTERS", "OTHER", "BALLS", "PARTICLES", "PITCHMOD", "RINGMOD", "WAVEMOD", "FMMOD", "NOISE", "CUTMOD",
                "OUTPUT", "BEND") + HUB_LATER:
        lines.append("#define PG_%s %d" % (pid, page_ix.get(pid, -1)))
    for pid in ("LIFE", "ORBITS", "PEND"):   # (2026-10-30) GAME OF LIFE, GRAVITY ORBITS, PENDULUMS
        lines.append("#define PG_%s %d" % (pid, page_ix.get(pid, -1)))
    # the rate LEDs: what each flashes with (RL_*: an LFO's rate (n 0-3), the arpeggiator's, the sequencer's, the beat)
    lines.append("enum { %s };" % ", ".join("RL_%s = %d" % (k, v) for k, v in RATE_KINDS.items()))
    lines.append("typedef struct { short kind, n, x, y; const char *label; } lay_rate_t;   /* x, y: the LED's centre */")
    lines.append("LAY lay_rate_t RATES[] = {%s};" % ", ".join("{%d, %d, %d, %d, %s}" % (k, n, x, y, c_str(t))
                                                           for k, n, x, y, t in lay.rates or [(0, -1, 0, 0, "")]))
    lines.append("#define NRATES %d" % len(lay.rates))
    # the triangles that open a module's page: the panel's top-right corner under its title strip; the ribbon's end
    tris = []
    for x, y, w, h, t, s, m in lay.panels:
        if m in page_ix:
            tris.append((x + w - 16, y + (16 if s == "module" else 10), 12, 12, page_ix[m]))
    tris += [(x, y, 12, 12, page_ix[pid]) for x, y, pid in lay.tris if pid in page_ix]
    if "RIBBON" in page_ix:   # the ribbon makes room for its triangle at its right end
        rx, ry, rw_, rh_ = zones["RIBBON"]
        zones["RIBBON"] = (rx, ry, rw_ - 26, rh_)
        tris.append((rx + rw_ - 18, ry + 2, 16, rh_ - 4, page_ix["RIBBON"]))
    for name, (x, y, w, h) in zones.items():
        lines.append("LAY short ZONE_%s[4] = {%d, %d, %d, %d};" % (name, x, y, w, h))
    lines.append("typedef struct { short x, y, w, h, page; } lay_more_t;")
    lines.append("LAY lay_more_t MORES[] = {%s};" % ", ".join("{%d, %d, %d, %d, %d}" % t for t in tris or
                                                              [(0, 0, 0, 0, -1)]))
    lines.append("#define NMORES %d" % len(tris))
    hub_and_links(lay, pages, page_ix, lines)
    hub_tabs(lay, page_ix, lines)
    open(out, "w", encoding="utf-8", newline="\n").write("\n".join(lines) + "\n")
    return other, pages


def main():
    src, out = sys.argv[1], sys.argv[2]
    lay = Layout(json.load(open(src, encoding="utf-8"))["params"])
    zones = build(lay)
    dup = sorted({k for k in lay.planned if lay.planned.count(k) > 1})
    if dup:
        raise SystemExit("gen_layout.py: keys placed twice on the panel: %s" % dup)
    other, pages = emit(lay, zones, out)
    for k in other:
        print("warning: gen_layout.py: %s has no place on the panel or a MORE page; it is on the OTHER page" % k,
              file=sys.stderr)
    later = sorted({k for k in lay.planned if k not in lay.index} |
                   {it[0] for pg in lay.pages for it in pg[3] if it[0] not in lay.index})
    print("layout.h: %d controls on the panel, %d MORE pages (%d items), %d unplaced; %d planned keys not in params "
          "yet%s" % (len(lay.ctls), len(pages), sum(len(p[3]) for p in pages), len(other), len(later),
                     (": " + " ".join(later)) if later else ""))


if __name__ == "__main__":
    main()
