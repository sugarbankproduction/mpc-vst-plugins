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
effect's page shows only the controls of the selected effect type). Every parameter that is neither on the panel, on a
page nor in the mod matrix goes to an OTHER page, and the build warns about it.
    python3 ports/one-ish/desktop/gen_layout.py <params.json> <out.h>"""
import json
import sys

BASE_W, BASE_H = 2162, 800
LED_ROW, LED_EXTRA = 10, 30   # an LED list: one row per option, plus its red button and label
SIDE = 1                      # a toggle's size: its label beside the button instead of under it
DX = 102                      # the right-hand block sits this much further right than it used to

# short option names for LED lists (the panel prints them in small capitals beside each LED)
ABBREV = {"Triangle": "TRI", "Sine": "SINE", "Square": "SQR", "Saw": "SAW", "Ramp": "RAMP", "S&H": "S&H",
          "Parallel": "PAR", "Series": "SER", "Notch": "NOTCH", "Chorus": "CHOR", "Flanger": "FLNG",
          "Phaser": "PHAS", "Mono": "MONO", "Tri": "TRI", "24 dB": "24dB", "18 dB": "18dB", "12 dB": "12dB",
          "6 dB": "6dB", "Red+White": "R+W", "Red+Purple": "R+P", "White+Purple": "W+P", "Plate": "PLATE",
          "Room": "ROOM", "Hall": "HALL", "Random": "RAND", "Up/Down": "UP-DN", "Down/Up": "DN-UP"}

# the synth effect's types this layout knows (params.json modfx_type when it was written), and which of its controls
# each type uses (SFX_USES: indexes into SFX_NAMES; matched to the real list by name). Types added later, and modfx_*
# keys not listed here, still get a page: see sfx_page().
SFX_NAMES = ["Delay", "BBD Delay", "Tape Delay", "Echo", "Flanger", "Stereo Flanger", "Chorus", "Dual Chorus",
             "Ensemble", "Stereo Chorus", "6-Stage Phaser", "12-Stage Phaser", "Time Phaser", "Bit Crusher", "Resonator"]
SFX_COMMON = [("modfx_sync", "Tempo Sync"), ("modfx_gain", "Wet Gain"), ("modfx_c1_dst", "CTRL 1 Target"),
              ("modfx_c2_dst", "CTRL 2 Target"), ("modfx_on", "On"), ("modfx_mix", "Mix"), ("modfx_ctrl1", "CTRL 1"),
              ("modfx_ctrl2", "CTRL 2")]
DELAYS, LINES = range(0, 4), range(4, 10)
PHASERS = range(10, 13)
SFX_USES = [   # key, label, types
    ("modfx_dtime", "Delay Time", [0, 1, 2, 3, 12]),
    ("modfx_time", "Time", LINES),
    ("modfx_time_r", "Time R", [5, 9]),
    ("modfx_rate", "Rate", [3, *LINES, *PHASERS, 14]),
    ("modfx_rate_r", "Rate R", [5, 9]),
    ("modfx_depth", "Depth", [1, 3, *LINES, *PHASERS, 14]),
    ("modfx_sweep", "Sweep", PHASERS),
    ("modfx_fb", "Feedback", [*DELAYS, *LINES, *PHASERS]),
    ("modfx_fb_r", "Feedback R", [5, 9]),
    ("modfx_dfb", "Delay Feedback", [12]),
    ("modfx_tone", "Tone", [*DELAYS, *LINES, *PHASERS]),
    ("modfx_spread", "Spread", [4, 5, 6, 8, 9, *PHASERS]),
    ("modfx_vrate", "Vibrato Rate", [7, 8]),
    ("modfx_vdepth", "Vibrato Depth", [7, 8]),
    ("modfx_noise", "Noise", [1, *LINES, *PHASERS]),
    ("modfx_wow", "Wow", [2]),
    ("modfx_flutter", "Flutter", [2]),
    ("modfx_bias", "Tape Bias", [2]),
    ("modfx_room", "Echo Room", [3]),
    ("modfx_decay", "Echo Decay", [3]),
    ("modfx_bits", "Bit Depth", [13]),
    ("modfx_srate", "Sample Rate", [13]),
    ("modfx_ftype", "Filter Type", [14]),
    ("modfx_reso", "Resonance", [14]),
    ("modfx_cut1", "Cutoff Low", [14]),
    ("modfx_lvl1", "Level Low", [14]),
    ("modfx_cut2", "Cutoff Mid", [14]),
    ("modfx_lvl2", "Level Mid", [14]),
    ("modfx_cut3", "Cutoff High", [14]),
    ("modfx_lvl3", "Level High", [14]),
]


class Layout:
    def __init__(self, params):
        self.params = params
        self.index = {p["key"]: i for i, p in enumerate(params)}
        self.panels, self.ctls, self.waves = [], [], []
        self.euclid_at = None   # (x, y, w, h): the Euclidean pattern's ring of dots
        self.placed, self.planned = set(), []
        self.pages = []   # (id, title, note, [(key, label, cond_key, types)])

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

    def page(self, pid, title, note, items):
        """a MORE page: items are (key, label[, cond_key, types]); a conditional item shows only while cond_key is set
        to one of the option numbers in types"""
        self.pages.append((pid, title, note, [it if len(it) == 4 else (it[0], it[1], None, None) for it in items]))


def sfx_page(lay):
    """the synth effect's MORE page items: TYPE, then each control for the types that use it, then the common ones.
    A type this layout doesn't know (added to modfx_type later) shows every control, and a modfx_* key it doesn't know
    shows for those new types (or for every type while there are none), so nothing new lands on the OTHER page."""
    opts = lay.params[lay.index["modfx_type"]].get("options", []) if "modfx_type" in lay.index else SFX_NAMES
    real = [opts.index(n) if n in opts else -1 for n in SFX_NAMES]
    new = [t for t, n in enumerate(opts) if n not in SFX_NAMES]
    mask = lambda ts: [t for t in ts if 0 <= t < 32]   # the screen's option mask is 32 bits
    items = [("modfx_type", "Type")]
    items += [(k, lab, "modfx_type", mask([real[t] for t in types] + new)) for k, lab, types in SFX_USES]
    known = {"modfx_type"} | {k for k, _, _ in SFX_USES} | {k for k, _ in SFX_COMMON}
    for p in lay.params:
        k = p["key"]
        if k.startswith("modfx_") and k not in known:
            name = p.get("name", k)
            for pre in ("Synth FX ", "Synth Fx ", "Mod FX "):
                name = name[len(pre):] if name.startswith(pre) else name
            items.append((k, name, "modfx_type", mask(new or range(len(opts)))))
    return items + SFX_COMMON


def build(lay):
    L = lay
    # ---- the three panel blocks, the controller box and the keyboard ----
    L.panel(34, 10, 716, 536, style="frame")
    L.panel(758, 10, 424, 536, style="frame")
    L.panel(1182, 10, 946, 536, style="frame")
    L.panel(34, 560, 340, 230, style="frame", more="CTRL")

    # ---- LFOs: a column on the far left ----
    for n in range(1, 5):
        y = 18 + 130 * (n - 1)
        k = "lfo%d_" % n
        L.panel(42, y, 128, 126, "LFO %d" % n, keys=[k + "rate", k + "wave"], more="LFO%d" % n)
        L.ctl(k + "rate", "knob", 44, y + 16, 60, 64, "RATE", 30)
        L.ctl(k + "wave", "leds", 106, y + 16, 62, 104, "WAVE")
        L.ctl(k + "sync", "toggle", 44, y + 82, 60, 40, "SYNC")
        L.page("LFO%d" % n, "LFO %d" % n, "Rate, wave and sync are on the panel too.", [
            (k + "rate", "Rate"), (k + "wave", "Wave"), (k + "sync", "Sync"), (k + "timing", "Sync Timing"),
            (k + "range", "Range"), (k + "polarity", "Polarity"), (k + "var", "Variation"), (k + "smooth", "Smoothing"),
            (k + "phase", "Start Phase"), (k + "reset", "Note Reset"), (k + "delay", "Delay"),
            (k + "fade_in", "Fade In"), (k + "fade_out", "Fade Out"), (k + "repeats", "Repeats")])

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
    L.panel(176, 314, 142, 88, "PITCH MODULATION")
    L.ctl("pitch_lfo", "knob", 180, 328, 68, 72, "LFO 1 AMT")
    L.ctl("pitch_env", "knob", 248, 328, 68, 72, "MOD EG AMT")
    L.panel(322, 314, 76, 88, "RING MOD")
    L.ctl("ring_src", "leds", 326, 328, 68, 72, "SOURCES")
    L.panel(402, 314, 142, 88, "WAVEFORM MOD", keys=["angle_lfo3", "pw_lfo3"])
    L.ctl("angle_lfo3", "knob", 406, 328, 68, 72, "LFO3>ANGLE")
    L.ctl("pw_lfo3", "knob", 474, 328, 68, 72, "LFO3>PW")
    L.panel(548, 314, 92, 88, "FREQ MOD")
    L.ctl("fm_amt", "knob", 550, 328, 48, 72, "FM AMT", 26)
    L.ctl("fm_route", "leds", 596, 328, 42, 72, "ROUTE")

    # ---- noise: a column right of the oscillators ----
    L.panel(646, 18, 96, 384, "NOISE")
    L.ctl("noise_a", "knob", 650, 34, 88, 68, "ATTACK")
    L.ctl("noise_r", "knob", 650, 104, 88, 68, "RELEASE")
    L.ctl("noise_sus", "toggle", 650, 176, 44, 42, "SUSTAIN")
    L.ctl("noise_env", "toggle", 694, 176, 44, 42, "ENV")
    L.ctl("noise_color_mix", "knob", 650, 222, 88, 70, "COLOR MIX")
    L.ctl("noise_color_pair", "leds", 650, 296, 88, 102, "COLOR")

    # ---- the bottom row, as on the instrument: arpeggiator, chord, generative, polyphony. Each module: a line of
    # buttons (labels beside them) under its title, left of its MORE triangle, then its knobs and LED lists ----
    YB, HB = 408, 132          # the row
    YA, YL, HL = 424, 442, 96  # the line of buttons; the line of controls under it and its height
    L.panel(176, YB, 232, HB, "ARPEGGIATOR", more="ARP", keys=["arp_on"])
    L.row([("arp_on", "ON"), ("arp_latch", "LATCH"), ("arp_sync", "SYNC")], 180, YA, 64, 14, "toggle", SIDE)
    L.cells([("arp_pattern", "PATTERN", 54, "leds"), ("arp_oct", "OCT", 42, "leds"),
             ("arp_dir", "DIRECTION", 54, "leds"), ("arp_div", "RATE", 44, "button")], 178, YL, HL)
    L.column([("arp_gate", "GATE"), ("arp_swing", "SWING")], 372, YL, 36, HL // 2, "small")
    L.page("ARP", "ARPEGGIATOR", "Plays the held keys in turn; RATE is each step's note value.", [
        ("arp_on", "On"), ("arp_latch", "Latch"), ("arp_pattern", "Pattern"), ("arp_oct", "Octaves"),
        ("arp_dir", "Octave Direction"), ("arp_oct_order", "Octave Order"), ("arp_pendulum", "Pendulum"),
        ("arp_pend_mode", "Pendulum Ends"), ("arp_rnd_mode", "Random Mode"), ("arp_sync", "Tempo Sync"),
        ("arp_div", "Rate (Synced)"), ("arp_timing", "Timing"), ("arp_rate", "Free Rate"), ("arp_gate", "Gate Length"),
        ("arp_swing", "Swing"), ("arp_reset", "Note Reset")])

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
        ("gen_scale", "Turing Scale"), ("gen_range", "Turing Octaves")])

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
             "FOCUS1": (769, 432, 56, 42), "FOCUS2": (825, 432, 56, 42), "FOCUS3": (881, 432, 56, 42),
             "TAB_SETUP": (945, 432, 58, 42)}
    # Synth 2 and 3's copies are reached through the Panel Focus; the setup keys live on the SETUP page
    L.placed.update(k for k in L.index if k.startswith(("s2_", "s3_")))
    L.placed.update(k for k in ("syn_on", "syn_ch", "key_lo", "key_hi", "master_level", "seq_all", "midi_ch",
                                "learn_mode") if k in L.index)
    # the step sequencer: ON, PLAY, REC, NOTE TRIG, TRANSPOSE, STEP MOD; the page LEDs (the step editor's pages, the
    # playing one lit); LENGTH and RATE. Its MORE triangle: the settings; the SEQ button: the step editor
    L.panel(764, 476, 412, 62, "SEQUENCER", more="SEQ", keys=["seq_on"])
    L.column([("seq_on", "ON"), ("seq_note_trig", "TRIG")], 768, 492, 66, 22, "toggle", SIDE)
    L.column([("seq_play", "PLAY"), ("seq_transpose", "TRANSP")], 834, 492, 74, 22, "toggle", SIDE)
    L.column([("seq_rec", "REC"), ("seq_step_mod", "STEP MOD")], 908, 492, 70, 22, "toggle", SIDE)
    if "seq_on" in L.index:
        zones["SEQ_LEDS"] = (976, 490, 82, 46)
    L.ctl("seq_length", "small", 1058, 488, 52, 50, "LENGTH")
    L.ctl("seq_div", "button", 1108, 491, 50, 46, "RATE")
    L.placed.add("seq_pos")   # the readout: the step editor and the page LEDs show it
    L.page("SEQ", "SEQUENCER", "Settings; the steps: the SEQ button. Synced: RATE is each step's note value.", [
        ("seq_on", "On"), ("seq_play", "Play"), ("seq_rec", "Record"), ("seq_rec_mode", "Record Mode"),
        ("seq_step_mod", "Step Mod"), ("seq_length", "Length"), ("seq_sync", "Sync"), ("seq_div", "Rate (Synced)"),
        ("seq_timing", "Timing"), ("seq_rate", "Free Rate"), ("seq_follow_swing", "Follow Swing"),
        ("clock_swing", "Master Swing"), ("seq_note_trig", "Note Trig"), ("seq_note_reset", "Note Reset"),
        ("seq_restart_trig", "Restart On Trig"), ("seq_restart_hold", "Restart On Hold"),
        ("seq_transpose", "Transpose"), ("seq_tr_mode", "Transpose Mode"), ("seq_one_shot", "One Shot"),
        ("seq_pattern", "Factory Sequence")])

    # ---- mixer ----
    X = DX
    L.panel(X + 1086, 18, 138, 522, "MIXER")
    chans = [("o1", "OSC 1"), ("o2", "OSC 2"), ("o3", "OSC 3"), ("mac", "MACRO"), ("ring", "RING MOD"), ("noise", "NOISE")]
    L.planned += [c + s for c, _ in chans if c + "_level" not in L.index for s in ("_level", "_svf", "_lad")]
    chans = [c for c in chans if c[0] + "_level" in L.index]
    step = 100 if len(chans) <= 5 else 84   # the macro oscillator's channel makes six
    for k, (src, label) in enumerate(chans):
        y = 38 + step * k
        L.ctl(src + "_level", "knob", X + 1088, y, 62, 70, label, 30)
        L.ctl(src + "_svf", "toggle", X + 1150, y + 10, 32, 50, "SVF")
        L.ctl(src + "_lad", "toggle", X + 1184, y + 10, 38, 50, "LADDER")
        if src == "mac":   # its MORE triangle: the macro oscillator's own page
            L.panel(X + 1088, y - 8, 134, 14, "", style="caption", more="MACRO")

    # ---- filters: SVF and ladder side by side, link / mix / route between them ----
    L.panel(X + 1230, 18, 306, 290, "STATE VARIABLE FILTER|LADDER FILTER")
    L.ctl("svf_cut", "big", X + 1256, 36, 100, 100, "CUTOFF")
    L.ctl("flt_cut", "big", X + 1410, 36, 100, 100, "CUTOFF")
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
    L.panel(X + 1230, 314, 306, 226, "CUTOFF MODULATION")
    for y, f, cap in ((330, "svf", "SVF"), (434, "lad", "LADDER")):
        L.panel(X + 1238, y, 120, 14, cap, style="caption")
        L.row([(f + "_env", "EG AMT"), (f + "_lfo", "LFO 2 AMT"), (f + "_kbd", "KEY TRK"), (f + "_fm", "FM AMT"),
               (f + "_fm_src", "FM SRC", "leds")], X + 1234, y + 12, 60, 90)

    # ---- envelopes: a row each, buttons over the time knobs, curve / scaling knobs at the right ----
    for k, (p, title) in enumerate([("fenv", "FILTER ENVELOPE"), ("amp", "AMPLIFIER ENVELOPE"),
                                    ("menv", "MODULATION ENVELOPE")]):
        yt = 18 + 104 * k
        L.panel(X + 1542, yt, 478, 100, title)
        L.row([(p + "_multi", "MULTI"), (p + "_sync", "SYNC"), (p + "_loop", "LOOP"), (p + "_latch", "LATCH"),
               (p + "_reset", "RESET")], X + 1550, yt + 16, 62, 14, "toggle", SIDE)
        L.row([(p + "_delay", "DELAY"), (p + "_a", "ATTACK"), (p + "_hold", "HOLD"), (p + "_d", "DECAY"),
               (p + "_s", "SUSTAIN"), (p + "_r", "RELEASE")], X + 1546, yt + 32, 52, 66, "knob", 26)
        L.row([(p + "_acurve", "A CURVE"), (p + "_dcurve", "D CURVE"), (p + "_rcurve", "R CURVE")],
              X + 1862, yt + 14, 52, 42, "small")
        L.row([(p + "_scale", "KEY SCL"), (p + "_vel", "VEL AMT")], X + 1888, yt + 56, 52, 42, "small")

    # ---- synth effect and master effects (send levels); delay, reverb and output under them ----
    L.panel(X + 1542, 334, 238, 102, "SYNTH EFFECT", more="SFX")
    L.cells([("modfx_on", "ON", 40, "toggle"), ("modfx_type", "TYPE", 66, "button"), ("modfx_ctrl1", "CTRL 1", 44),
             ("modfx_ctrl2", "CTRL 2", 44), ("modfx_mix", "MIX", 42)], X + 1544, 354, 64)
    L.panel(X + 1784, 334, 236, 102, "MASTER EFFECTS - SHARED", more="MFX")
    L.cells([("mfx_on", "ON", 40, "toggle"), ("mfx_send1", "SEND 1", 46), ("mfx_send2", "SEND 2", 46),
             ("mfx_series", "SERIES", 50), ("mfx_width", "WIDTH", 50)], X + 1786, 354, 64)
    L.panel(X + 1542, 440, 162, 100, "DELAY - SHARED", more="DELAY")
    L.cells([("dly_time", "TIME", 52), ("dly_fb", "FDBK", 52), ("dly_sync", "SYNC", 54, "toggle")], X + 1544, 458, 72)
    L.panel(X + 1708, 440, 162, 100, "REVERB - SHARED", more="REVERB")
    L.cells([("rev_type", "TYPE", 50, "leds"), ("rev_decay", "DECAY", 54), ("rev_size", "SIZE", 54)], X + 1710, 452,
            84)
    L.panel(X + 1874, 440, 146, 100, "SYNTH OUTPUT")
    L.ctl("volume", "knob", X + 1876, 456, 54, 78, "VOLUME", 34)
    L.cells([("pan", "PAN", 46), ("spread", "SPREAD", 44)], X + 1930, 456, 78)
    L.page("SFX", "SYNTH EFFECT", "The controls of the selected type (TYPE changes the list).", sfx_page(L))
    mac = [("mac_model", "Model"), ("mac_harm", "Harmonics"), ("mac_timbre", "Timbre"), ("mac_morph", "Morph"),
           ("mac_oct", "Octave"), ("mac_freq", "Frequency"), ("mac_aux", "Aux Mix"), ("mac_level", "Level"),
           ("mac_svf", "To SVF"), ("mac_lad", "To Ladder")]
    mac += [(p["key"], p.get("name", p["key"])) for p in L.params   # any other macro key: here, not on OTHER
            if p["key"].startswith("mac_") and p["key"] not in dict(mac)]
    L.page("MACRO", "MACRO OSCILLATOR", "A fourth sound source with many models (level: the mixer).",
           mac)
    L.page("MFX", "MASTER EFFECTS", "Delay and reverb on two sends after the synth effect.", [
        ("mfx_on", "On"), ("mfx_send1", "Send 1 (Delay)"), ("mfx_send2", "Send 2 (Reverb)"),
        ("mfx_series", "Delay > Reverb"), ("mfx_width", "Width")])
    L.page("DELAY", "DELAY", "The master delay (send 1).", [
        ("mfx_send1", "Send 1"), ("dly_time", "Time L"), ("dly_time_r", "Time R"), ("dly_sync", "Tempo Sync"),
        ("dly_fb", "Feedback"), ("dly_tone", "Tone"), ("dly_pp", "Ping-Pong"), ("dly_offset", "Ping-Pong Offset")])
    L.page("REVERB", "REVERB", "The master reverb (send 2).", [
        ("mfx_send2", "Send 2"), ("rev_type", "Type"), ("rev_decay", "Decay"), ("rev_size", "Size"),
        ("rev_pre", "Pre-delay"), ("rev_damp", "High Damp"), ("rev_lowdamp", "Low Damp"), ("rev_tone", "Tone"),
        ("rev_diff", "Diffusion"), ("rev_early", "Early / Late"), ("rev_mod", "Modulation"), ("rev_freeze", "Freeze")])

    # ---- left-hand controller: octave, glide, wheels, X/Y pad, bend range; the ribbon over the keys ----
    zones.update({"OCT_LEDS": (50, 576, 116, 26), "OCT_DOWN": (64, 606, 36, 16), "OCT_UP": (118, 606, 36, 16),
                  "PITCH": (56, 650, 36, 112), "MODW": (110, 650, 36, 112), "PAD": (166, 650, 126, 112),
                  "KEYS": (384, 584, 1744, 200), "RIBBON": (384, 556, 1744, 20)})   # shorter with a MORE page
    L.ctl("glide_time", "knob", 172, 570, 64, 70, "GLIDE RATE")
    L.ctl("glide", "toggle", 236, 576, 42, 56, "ON")
    L.ctl("glide_type", "leds", 278, 568, 52, 76, "TYPE")
    L.column([("glide_legato", "LEGATO"), ("glide_gated", "GATED"), ("glissando", "GLISS")], 298, 650, 72, 22,
             "toggle", SIDE)   # the other glide options, beside the pad
    L.panel(298, 714, 72, 12, "BEND RANGE", style="caption", keys=["bend_up", "bend_down"])
    L.cells([("bend_up", "UP", 36, "small"), ("bend_down", "DOWN", 36, "small")], 298, 724, 44, "small", 18)
    L.page("CTRL", "CONTROLLERS AND TUNING", "Keyboard, wheels, glide and tuning.", [
        ("bend_up", "Bend Up"), ("bend_down", "Bend Down"), ("vel_curve", "Velocity Curve"),
        ("transpose", "Transpose"), ("octave_shift", "Octave Shift"), ("key_center", "Key Track Centre"),
        ("master_tune", "Master Tune"), ("fine_tune", "Fine Tune"), ("glide", "Glide"), ("glide_time", "Glide Rate"),
        ("glide_type", "Glide Type"), ("glide_legato", "Legato Glide"), ("glide_gated", "Gated Glide"),
        ("glissando", "Glissando")])
    L.page("RIBBON", "RIBBON", "The touch strip over the keys (MIDI CC 16 position, CC 17 touch).", [
        ("ribbon_dst", "Destination"), ("ribbon_amt", "Amount"), ("ribbon_mode", "Mode"), ("ribbon_origin", "Origin")])
    L.page("ANALOG", "ANALOG CHARACTER", "Per-voice differences, as between real analog voices. All 0 = exact.", [
        ("drift", "Drift"), ("an_cut", "Cutoff Spread"), ("an_res", "Resonance Spread"), ("an_env", "Envelope Spread"),
        ("an_lvl", "Level Spread"), ("an_tune", "Tuning Spread"), ("an_ladder", "Ladder Character")])
    return zones


MATRIX_COLS = ("src", "dst", "amt", "via", "via_amt", "xf", "xf_amt")


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
    matrix = []
    for n in range(1, 33):
        keys = ["mod%d_%s" % (n, s) for s in MATRIX_COLS]
        if all(k in lay.index for k in keys[:4]):
            matrix.append("{%s}" % ", ".join(str(lay.index.get(k, -1)) for k in keys))
            lay.placed.update(k for k in keys if k in lay.index)
    on_pages = set()
    pages = []
    for pid, title, note, items in lay.pages:
        got = [it for it in items if it[0] in lay.index and (it[2] is None or it[2] in lay.index)]
        if got:
            pages.append((pid, title, note, got))
            on_pages.update(it[0] for it in got)
    other = [p["key"] for p in P if p["key"] not in lay.placed and p["key"] not in on_pages]
    if other:
        pages.append(("OTHER", "OTHER", "Parameters that have no home yet.", [(k, P[lay.index[k]]["name"], None, None)
                                                                             for k in other]))
    page_ix = {pid: i for i, (pid, _, _, _) in enumerate(pages)}

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
    sp = [lay.index.get(k, -1) for k in ("seq_on", "seq_play", "seq_rec", "seq_length", "seq_pos", "seq_step_mod",
                                         "seq_div", "seq_timing")]
    lines.append("LAY short SEQ_P[8] = {%s};   /* on, play, rec, length, readout, step mod, div, timing (-1: absent) */"
                 % ", ".join(map(str, sp)))
    lines.append("LAY short EUCLID_P[4] = {%s};   /* on, steps, hits, rotation */" % ", ".join(map(str, eu)))

    # the mod matrix: per row source, destination, amount, via, via amount, transform, transform amount (-1: absent)
    lines.append("enum { MX_SRC, MX_DST, MX_AMT, MX_VIA, MX_VIA_AMT, MX_XF, MX_XF_AMT, MX_COLS };")
    lines.append("LAY short MATRIX[][MX_COLS] = {%s};" % ", ".join(matrix or ["{-1, -1, -1, -1, -1, -1, -1}"]))
    lines.append("#define NMATRIX %d" % len(matrix))

    # the MORE pages: their items (a param, its label, and the option of another param it depends on)
    lines.append("typedef struct { short param, cond; unsigned mask; const char *label; } lay_item_t;   /* cond: "
                 "shown while option (mask bit) of param cond is set; -1 always */")
    lines.append("typedef struct { const char *title, *note; short first, count; } lay_page_t;")
    items, prow = [], []
    for pid, title, note, got in pages:
        prow.append("{%s, %s, %d, %d}" % (c_str(title), c_str(note), len(items), len(got)))
        for key, label, cond, types in got:
            mask = sum(1 << t for t in types) if types is not None else 0xFFFFFFFF
            items.append("{%d, %d, 0x%Xu, %s}" % (lay.index[key], lay.index[cond] if cond else -1, mask, c_str(label)))
    lines.append("LAY lay_item_t ITEMS[] = {%s};" % ",\n    ".join(items or ['{-1, -1, 0, ""}']))
    lines.append("LAY lay_page_t PAGES[] = {%s};" % ",\n    ".join(prow or ['{"", "", 0, 0}']))
    lines.append("#define NPAGES %d" % len(pages))
    for pid in ("LFO1", "LFO2", "LFO3", "LFO4", "OSC1", "OSC2", "OSC3", "POLY", "ARP", "CHORD", "GEN", "SFX", "MFX",
                "DELAY", "REVERB", "MACRO", "CTRL", "RIBBON", "ANALOG", "SEQ", "OTHER"):
        lines.append("#define PG_%s %d" % (pid, page_ix.get(pid, -1)))
    # the triangles that open a module's page: the panel's top-right corner under its title strip; the ribbon's end
    tris = []
    for x, y, w, h, t, s, m in lay.panels:
        if m in page_ix:
            tris.append((x + w - 16, y + (16 if s == "module" else 10), 12, 12, page_ix[m]))
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
                   {it[0] for _, _, _, items in lay.pages for it in items if it[0] not in lay.index})
    print("layout.h: %d controls on the panel, %d MORE pages (%d items), %d unplaced; %d planned keys not in params "
          "yet%s" % (len(lay.ctls), len(pages), sum(len(p[3]) for p in pages), len(other), len(later),
                     (": " + " ".join(later)) if later else ""))


if __name__ == "__main__":
    main()
