"""Bank A (2026-10-30): 128 factory presets for playing: BASS, LEAD, KEYS, BRASS, PLUCK and ORGAN (ORGAN is a category
this bank adds to the browser's list). Each is designed for this synth, none copies a manufacturer's preset or name.
Written like make_presets.py's PATCHES (musical units through its helpers); BANK = [(name, values, tags)].

Played on a 37-key controller with aftertouch, mod wheel, pitch wheel, the X/Y pad and the ribbon, the bank follows one
plan so the hands learn it once:
  MOD WHEEL   basses: opens the sound up (cutoff, growl, a second oscillator); leads and brass: vibrato (plus a little
              brightness); keys: tremolo or movement; organs: the rotary speed.
  AFTERTOUCH  pressing harder opens the filter, adds vibrato or swells the level, as suits the sound.
  VELOCITY    nearly everything: harder is brighter (filter envelope, FM depth), not only louder.
  PAD X / Y   X: timbre (cutoff, wave shape, FM); Y: space or dirt (delay/reverb sends, drive, resonance).
  RIBBON      leads: pitch (a bend of about 2 semitones, or a wide slide on the theremin-like ones); basses: cutoff.
Single presets switch Synths 2 and 3 off themselves. LEVEL (at the end) trims each preset's volume so the bank plays
at one loudness: test/bank_check.c measures it (offline_check banka)."""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from make_presets import (ms, hz, lfo, lfo_div, fxdiv, fxrate, logk, chorus, delay, reverb, amp, fenv, menv,   # noqa: E402
                          synth)

VEL, MW, AT, KEY, RIB = "Velocity", "Mod Wheel", "Aftertouch", "Keyboard", "Ribbon"
PX, PY = "Pad X (CC 2)", "Pad Y (CC 3)"


def mx(*rows):   # matrix rows from slot 1: (source, destination, amount[, controller[, transform, its amount]])
    d = {}
    for i, r in enumerate(rows, 1):
        d["mod%d_src" % i], d["mod%d_dst" % i], d["mod%d_amt" % i] = r[0], r[1], r[2]
        d["mod%d_via" % i] = r[3] if len(r) > 3 and r[3] else "None"   # (slot 1's default controller is the wheel)
        if len(r) > 4:
            d["mod%d_xf" % i], d["mod%d_xf_amt" % i] = r[4], r[5] if len(r) > 5 else 0
    if not rows:
        d.update({"mod1_src": "Off", "mod1_dst": "Off", "mod1_via": "None"})
    return d


def vib(amt=3, via=MW):   # LFO 1 into pitch, brought in by a controller
    return ("LFO 1", "Pitch", amt, via)


def lfo1(rate=5.5, wave="Sine", delay_ms=0, fade_ms=0):   # the vibrato LFO, optionally delayed and faded in per note
    d = {"lfo1_rate": lfo(rate), "lfo1_wave": wave}
    if delay_ms:
        d["lfo1_delay"] = ms(delay_ms)
    if fade_ms:
        d["lfo1_fade_in"] = ms(fade_ms)
    return d


def rib(dst, amt, **kw):   # the ribbon's quick assignment
    return dict({"ribbon_dst": dst, "ribbon_amt": amt}, **kw)


def mono(glide_ms=0, legato=True, kind="LCT", retrig=True):   # one voice, last-note priority, optional glide
    d = {"voices": "Mono", "note_priority": "Last"}
    if glide_ms:
        d.update({"glide": "On", "glide_time": ms(glide_ms), "glide_type": kind, "glide_legato": "On" if legato else "Off"})
    if not retrig:   # legato notes keep the envelopes going (an acid slide)
        d.update({"amp_multi": "Off", "fenv_multi": "Off", "menv_multi": "Off"})
    return d


def uni(count, detune, var=0):
    return {"unison": "On", "uni_count": str(count), "uni_detune": detune, "pitch_var": var}


TRI = {"o1_wave": "Tri", "o1_angle": 50}   # osc 1 as a plain triangle


def tri(n, level=None, oct=None, freq=None):   # osc n as a triangle (n 2 or 3: at a level)
    d = {"o%d_wave" % n: "Tri", "o%d_angle" % n: 50}
    if level is not None:
        d["o%d_level" % n] = level
    if oct:
        d["o%d_oct" % n] = oct
    if freq is not None:
        d["o%d_freq" % n] = freq
    return d


def svf_only(*chs):   # these channels into the SVF only (else they go to the ladder, the default)
    d = {}
    for c in chs:
        d[c + "_svf"], d[c + "_lad"] = "On", "Off"
    return d


def both(*chs):   # these channels into both filters
    return {k: "On" for c in chs for k in (c + "_svf", c + "_lad")}


BANK = []
ABOUT = {}   # one line per preset: what it is and what the controls do (the report and the browser's notes)


def add(name, cat, mood, about, values, kind="Single"):
    if kind == "Single":
        values = {**values, "s2_syn_on": "Off", "s3_syn_on": "Off"}
    BANK.append((name, values, {"type": kind, "category": cat, "mood": mood}))
    ABOUT[name] = about


def multi(s1, s2, s3=None, **shared):   # Synth 1's sound with Synth 2 (and 3) on: a layer or a split
    v = {**s1, **synth(2, {**s2, "syn_on": "On"}), **shared}
    v.update(synth(3, {**s3, "syn_on": "On"}) if s3 else {"s3_syn_on": "Off"})
    return v


# ======================================================================================================== BASS (32)
add("Floor Sub", "Bass", "Clean", "A round triangle sub; the wheel blends in a saw an octave up, aftertouch opens it, "
    "pad X sharpens the wave", {
        **TRI, "o1_oct": "8'", "o2_level": 0, "o2_oct": "4'", "flt_cut": hz(260), "lad_kbd": 60, **amp(3, 300, 100, 90),
        **mono(35), **mx((MW, "Osc 2 Level", 40), (AT, "Ladder Cutoff", 25), (PX, "Osc 1 Angle", -45),
                         (VEL, "Amp Level", 15)),
        **rib("Pitch", 8)})
add("Round Ladder Bass", "Bass", "Warm", "Two saws and a square an octave down into the ladder with a soft envelope; "
    "velocity and aftertouch open the filter, the wheel adds drive", {
        "o2_level": 80, "o2_freq": 0.06, "o3_level": 70, "o3_oct": "16'", "o3_mix": 100,
        "flt_cut": hz(240), "flt_res": 20, "flt_drive": 25, "lad_env": 40, "lad_kbd": 40, **fenv(2, 420, 25, 200),
        "fenv_vel": 40, **amp(2, 500, 85, 120), **mono(25),
        **mx((AT, "Ladder Cutoff", 20), (MW, "Ladder Drive", 45), (MW, "Ladder Cutoff", 12), (PX, "Ladder Res", 40)),
        **rib("Ladder Cutoff", 30)})
add("Velvet Thump", "Bass", "Soft", "A soft round pluck with a short filter bump; velocity sets the knock, the wheel "
    "lengthens it", {
        "o1_mix": 40, "o1_pw": 40, **tri(2, 70, "16'"), "flt_cut": hz(150), "flt_res": 30, "lad_env": 45, "lad_kbd": 50,
        **fenv(1, 160, 0, 120), "fenv_vel": 50, **amp(2, 700, 40, 150), **mono(),
        **mx((MW, "Filter Env Decay", 25), (AT, "Ladder Cutoff", 18), (PX, "Osc 1 Wave Mix", 50)),
        **rib("Ladder Cutoff", 25)})
add("Squelch Spiral", "Bass", "Aggressive", "Acid line: a resonant saw with legato slides (no retrigger) and accents "
    "from velocity; the wheel pushes the resonance, aftertouch the drive, pad X the cutoff", {
        "o1_mix": 0, "flt_cut": hz(300), "flt_res": 78, "flt_drive": 50, "lad_env": 58, "lad_kbd": 45,
        **fenv(1, 230, 0, 60), "fenv_vel": 60, **amp(1, 260, 70, 50), **mono(65, kind="LCT", retrig=False),
        **mx((VEL, "Ladder Res", 12), (MW, "Ladder Res", 18), (AT, "Ladder Drive", 35), (PX, "Ladder Cutoff", 30),
             (PY, "Send 1 (FX1)", 30)),
        **delay("1/8.", 30, 0, True), **rib("Ladder Cutoff", 35)})
add("Rubber Square Acid", "Bass", "Dirty", "A square-wave acid voice through the 18 dB ladder, slid legato, with tape "
    "grit; the wheel sweeps the envelope amount", {
        "o1_mix": 100, "o1_pw": 50, "flt_cut": hz(260), "flt_res": 72, "flt_drive": 40, "flt_slope": "18 dB",
        "lad_env": 50, "lad_kbd": 40, **fenv(1, 300, 0, 80), "fenv_vel": 55, **amp(1, 300, 75, 60),
        **mono(55, retrig=False),
        **mx((MW, "Ladder EG Amt", 30), (AT, "Ladder Cutoff", 20), (PX, "Osc 1 PW", -35), (PY, "Ladder Drive", 40)),
        "modfx_on": "On", "modfx_type": "Tape Color", "modfx_drive": 55, "modfx_bump": 50, "modfx_tone": 60,
        "modfx_mix": 100})
add("Bubble Acid", "Bass", "Bright", "Acid through the SVF band-pass and the ladder at once: a vocal, bubbling "
    "squelch; the wheel moves the band-pass, aftertouch the resonance", {
        "o1_mix": 30, "o2_level": 70, "o2_freq": 0.05, **both("o1", "o2"), "svf_mode": "BP", "svf_cut": hz(800), "svf_res": 55, "svf_env": 45, "svf_kbd": 50,
        "flt_cut": hz(350), "flt_res": 65, "lad_env": 50, "lad_kbd": 40, "flt_mix": 55, **fenv(1, 200, 0, 60),
        "fenv_vel": 50, **amp(1, 240, 65, 50), **mono(60, retrig=False),
        **mx((MW, "SVF Cutoff", 30), (AT, "Ladder Res", 20), (VEL, "SVF Res", 15), (PX, "Filter Mix", -40)),
        **delay("1/16", 25, 12, True)})
add("Knock FM Bass", "Bass", "Clean", "Two-operator FM: a quick FM burst on each note gives a woody knock over a clean "
    "low tone; velocity sets the knock, the wheel holds the FM open", {
        **TRI, "o1_level": 0, "o1_oct": "8'", **tri(2, 100, "8'"), "fm_route": "1 > 2", "fm_amt": 8,
        **menv(1, 140, 0, 80), "flt_cut": hz(3000), "lad_kbd": 30, **amp(1, 600, 70, 100), **mono(),
        **mx(("Mod Env", "FM Amount", 45), (VEL, "FM Amount", 15), (MW, "FM Amount", 25), (AT, "Ladder Cutoff", -20),
             (PX, "Osc 1 Pitch", 50)),
        **rib("FM Amount", 30)})
add("Steel Wire Bass", "Bass", "Bright", "FM at a 2:1 ratio with a touch of ring modulation: a metallic, wiry bass "
    "that cuts; aftertouch thickens the FM, pad Y the ring", {
        **TRI, "o1_level": 0, "o1_oct": "4'", "o2_level": 100, "o2_oct": "8'", "fm_route": "1 > 2", "fm_amt": 18,
        "ring_level": 15, **menv(1, 350, 15, 120), "flt_cut": hz(1800), "flt_res": 15, "lad_env": 30, "lad_kbd": 50,
        **fenv(1, 300, 20, 100), **amp(1, 500, 75, 90), **mono(20),
        **mx(("Mod Env", "FM Amount", 30), (AT, "FM Amount", 20), (MW, "Ladder Cutoff", 25), (PY, "Ring Level", 40),
             (VEL, "Ladder EG Amt", 15))})
add("Rubber Filter FM", "Bass", "Dirty", "Osc 3 modulates the SVF cutoff at audio rate for a rubbery, buzzing growl; "
    "the wheel turns the filter FM up, aftertouch opens the ladder", {
        "o1_mix": 0, **tri(3, 0, "8'", 7), **both("o1"), "svf_mode": "LP", "svf_cut": hz(400), "svf_res": 40,
        "svf_fm": 20, "svf_fm_src": "Osc 3", "svf_kbd": 60, "flt_cut": hz(900), "flt_res": 10, "lad_kbd": 40,
        "flt_mix": 30, **fenv(1, 350, 30, 100), "svf_env": 30, **amp(2, 400, 85, 100), **mono(30),
        **mx((MW, "SVF FM Amt", 35), (AT, "Ladder Cutoff", 25), (PX, "Osc 3 Pitch", 20), (VEL, "SVF EG Amt", 15))})
add("Reese Tunnel", "Bass", "Dark", "A dark detuned unison saw stack under a low ladder, slowly breathing; the wheel "
    "widens the detune, aftertouch lifts the cutoff", {
        "o2_level": 85, "o2_freq": 0.1, "flt_cut": hz(380), "flt_res": 20, "lad_kbd": 40, "lad_lfo": 12,
        "lfo2_rate": lfo(0.12), "lfo2_wave": "Sine", **amp(4, 600, 95, 200), **mono(40), **uni(4, 30),
        **mx((MW, "Osc 2 Beat", 12), (AT, "Ladder Cutoff", 25), (PX, "Ladder Res", 40), (PY, "Spread", 50)),
        "spread": 40, **rib("Ladder Cutoff", 30)})
add("Reese Grind", "Bass", "Aggressive", "Two saws a fraction of a hertz apart through a driven ladder and a "
    "saturator: a churning, phasing bass; pad X drives it harder", {
        "o2_level": 95, "o2_beat": 0.7, "o3_level": 50, "o3_oct": "16'", "o3_mix": 100,
        "flt_cut": hz(700), "flt_res": 25, "flt_drive": 55, "lad_kbd": 30, **amp(3, 600, 90, 150), **mono(30),
        **mx((MW, "Osc 2 Beat", 25), (AT, "Ladder Cutoff", 22), (PX, "Synth FX Drive", 35), (PY, "Ladder Res", 35)),
        "modfx_on": "On", "modfx_type": "Density", "modfx_drive": 45, "modfx_mix": 100})
add("Notch Wobble", "Bass", "Aggressive", "A tempo-synced wobble: LFO 2 at 1/8 sweeps the ladder and a notch; the "
    "wheel doubles the wobble rate, aftertouch deepens it", {
        "o2_level": 80, "o2_oct": "16'", "o2_mix": 100, **both("o1", "o2"), "svf_mode": "Notch", "svf_cut": hz(700),
        "svf_res": 30, "svf_lfo": 30, "flt_route": "Series", "flt_cut": hz(200), "flt_res": 55, "flt_drive": 45,
        "lad_lfo": 45, "lfo2_wave": "Sine", "lfo2_sync": "On", "lfo2_rate": lfo_div("1/8"), "lfo2_reset": "On",
        **amp(2, 400, 90, 120), **mono(20),
        **mx((MW, "LFO 2 Rate (st)", 50), (AT, "Ladder LFO2 Amt", 30), (PX, "Ladder Res", 30), (PY, "Ladder Drive", 40))})
add("Ember Pluck Bass", "Bass", "Warm", "A warm plucked bass that dies away like a string; velocity brightens the pick, "
    "the wheel lets it ring longer", {
        "o2_level": 60, "o2_freq": -0.05, "o2_mix": 100, "o2_pw": 35, "flt_cut": hz(170), "flt_res": 35, "lad_env": 55,
        "lad_kbd": 50, **fenv(1, 220, 0, 150), "fenv_vel": 45, **amp(1, 1100, 0, 200), **mono(),
        **mx((MW, "Amp Env Decay", 20), (MW, "Filter Env Decay", 15), (AT, "Ladder Cutoff", 15), (PX, "Osc 2 PW", 40)),
        **reverb(8, 1.0, rev_type="Room")})
add("Muted Pick Bass", "Bass", "Clean", "A palm-muted pick: a tiny noise click and a short, round body; velocity "
    "adds the click, aftertouch opens the mute", {
        "o1_mix": 30, **tri(2, 60), "noise_level": 30, "noise_env": "On", "noise_a": ms(1), "noise_r": ms(25),
        "noise_sus": "Off", "flt_cut": hz(220), "flt_res": 15, "lad_env": 40, "lad_kbd": 60, **fenv(1, 120, 10, 80),
        "fenv_vel": 35, **amp(1, 450, 30, 90), **mono(),
        **mx((VEL, "Noise Level", 30), (AT, "Ladder Cutoff", 25), (MW, "Filter Env Decay", 30), (PX, "Osc 1 Wave Mix", 50))})
add("Sync Bite Bass", "Bass", "Aggressive", "Osc 2 hard-synced to osc 1, its pitch swept down by the mod envelope on "
    "each note: a biting, vocal attack; aftertouch adds sync sweep", {
        "o1_level": 30, "o2_level": 100, "o2_sync": "On", "o2_freq": 5, "o3_level": 70, "o3_oct": "16'", "o3_mix": 100,
        **menv(1, 260, 0, 100), "flt_cut": hz(1400), "flt_res": 20, "lad_env": 25, "lad_kbd": 40,
        **fenv(1, 300, 30, 100), **amp(1, 450, 80, 90), **mono(15),
        **mx(("Mod Env", "Osc 2 Pitch", 40), (AT, "Osc 2 Pitch", 20), (VEL, "Osc 2 Pitch", 10), (MW, "Ladder Cutoff", 25),
             (PX, "Osc 2 Pitch", 25)),
        **rib("Osc 2 Pitch", 30)})
add("Sync Growler", "Bass", "Dirty", "A synced bass whose slave pitch is moved by a slow LFO: it growls and talks; "
    "the wheel speeds the growl, aftertouch deepens it", {
        "o1_level": 40, "o2_level": 100, "o2_sync": "On", "o2_freq": 3, "flt_cut": hz(1000), "flt_res": 30,
        "flt_drive": 45, "lad_kbd": 40, "lfo4_wave": "Triangle", "lfo4_rate": lfo(1.2), **amp(2, 500, 90, 120),
        **mono(25),
        **mx(("LFO 4", "Osc 2 Pitch", 12), (MW, "LFO 4 Rate (st)", 60), (AT, "Osc 2 Pitch", 18), (PX, "Ladder Cutoff", 30),
             (PY, "Ladder Drive", 30)),
        "modfx_on": "On", "modfx_type": "Tape Color", "modfx_drive": 60, "modfx_bump": 55, "modfx_mix": 100})
add("Ratchet Drive Seq", "Bass", "Evolving", "Hold a key: the step sequencer plays a ratcheting pentatonic run from "
    "it (transposed by the key); the wheel opens the filter, aftertouch adds drive", {
        "o2_level": 70, "o2_oct": "16'", "o2_mix": 100, "flt_cut": hz(300), "flt_res": 45, "flt_drive": 30,
        "lad_env": 50, "lad_kbd": 50, **fenv(1, 180, 10, 80), "fenv_vel": 40, **amp(1, 250, 60, 70), **mono(),
        "seq_on": "On", "seq_pattern": "Ratchet Run", "seq_length": 16, "seq_note_trig": "On", "seq_transpose": "On",
        **mx((MW, "Ladder Cutoff", 30), (AT, "Ladder Drive", 35), (PX, "Ladder Res", 30), (PY, "Send 1 (FX1)", 35)),
        **delay("1/8.", 30, 10, True)})
add("Octave Runner", "Bass", "Evolving", "The arpeggiator bounces held notes over two octaves in 1/16s: a driving, "
    "rolling bass; the wheel opens the cutoff, pad X sets the gate length", {
        "o1_mix": 50, "o1_pw": 40, "o2_level": 50, "o2_freq": 0.05, "flt_cut": hz(400), "flt_res": 40, "lad_env": 45,
        "lad_kbd": 50, **fenv(1, 150, 10, 60), **amp(1, 200, 60, 60), "voices": "4",
        "arp_on": "On", "arp_pattern": "Up", "arp_oct": "2", "arp_dir": "Up/Down", "arp_div": "1/16", "arp_gate": 45,
        **mx((MW, "Ladder Cutoff", 30), (AT, "Ladder Res", 20), (VEL, "Ladder EG Amt", 15), (PX, "Filter Env Decay", 30)),
        **delay("1/8.", 25, 10, True)})
add("Hollow Square Bass", "Bass", "Warm", "A hollow square bass through the gentle 12 dB ladder: woody and "
    "round; the wheel narrows the pulse, aftertouch opens it", {
        "o1_mix": 100, "o1_pw": 50, "o2_level": 40, "o2_oct": "16'", **tri(2), "flt_cut": hz(500), "flt_res": 15,
        "flt_slope": "12 dB", "lad_env": 25, "lad_kbd": 50, **fenv(1, 400, 30, 150), **amp(2, 500, 85, 120),
        **mono(20), **mx((MW, "Osc 1 PW", 35), (AT, "Ladder Cutoff", 22), (VEL, "Ladder EG Amt", 15),
                         (PX, "Ladder Res", 40))})
add("Fuzz Pedal Bass", "Bass", "Dirty", "A saw driven hard into the ladder and through tape: a fuzzed, compressed "
    "bass; pad X and aftertouch push the fuzz", {
        "o2_level": 70, "o2_freq": -0.08, "flt_cut": hz(800), "flt_res": 20, "flt_drive": 80, "lad_kbd": 30,
        "lad_env": 15, **fenv(1, 300, 50, 100), **amp(1, 400, 90, 100), **mono(20),
        **mx((AT, "Ladder Drive", 20), (MW, "Ladder Cutoff", 25), (PX, "Synth FX Drive", 30), (PY, "Ladder Res", 30)),
        "modfx_on": "On", "modfx_type": "Tape Color", "modfx_drive": 75, "modfx_bump": 70, "modfx_tone": 50,
        "modfx_mix": 100})
add("Deep Notch Bass", "Bass", "Dark", "Saws through a notch that follows the keyboard, then a low ladder: a dark, "
    "hollow bass; the wheel moves the notch, aftertouch lifts the cutoff", {
        "o2_level": 70, "o2_freq": 0.04, **both("o1", "o2"), "svf_mode": "Notch", "svf_cut": hz(250), "svf_res": 35,
        "svf_kbd": 100, "flt_route": "Series", "flt_cut": hz(450), "flt_res": 15, "lad_kbd": 40,
        **amp(3, 500, 90, 150), **mono(30),
        **mx((MW, "SVF Cutoff", 30), (AT, "Ladder Cutoff", 20), (PX, "SVF Res", 40), (VEL, "Amp Level", 10))})
add("Slap Pop Bass", "Bass", "Bright", "A bright slap: velocity throws a quick pitch blip and a filter snap; "
    "aftertouch adds the snap, the wheel lengthens the tail", {
        "o1_mix": 40, "o1_pw": 30, "o2_level": 60, "o2_oct": "4'", **tri(2), "flt_cut": hz(400), "flt_res": 30,
        "lad_env": 55, "lad_kbd": 60, **fenv(1, 140, 15, 100), "fenv_vel": 60, **menv(1, 40, 0, 30), "pitch_env": 8,
        **amp(1, 500, 45, 120), **mono(),
        **mx((VEL, "Pitch EG Amt", 8), (AT, "Ladder Cutoff", 20), (MW, "Amp Env Decay", 20), (PX, "Osc 2 Level", 40))})
add("Fifth Glider", "Bass", "Warm", "A saw with a fifth above on osc 2, gliding exponentially between notes: a "
    "sliding power bass; aftertouch opens it, the wheel brings in vibrato", {
        "o2_level": 60, "o2_freq": 7, "o3_level": 50, "o3_oct": "16'", **tri(3), "flt_cut": hz(500), "flt_res": 25,
        "lad_kbd": 50, "lad_env": 20, **fenv(2, 500, 40, 200), **amp(3, 500, 90, 150), **mono(140, kind="EXP"),
        **lfo1(5.0), **mx(vib(2), (AT, "Ladder Cutoff", 25), (PX, "Osc 2 Level", -60), (PY, "Ladder Res", 35))})
add("Ring Rumble", "Bass", "Dirty", "Ring modulation of two slightly detuned saws under a low ladder: a rumbling, "
    "clangy sub; pad X detunes the ring, aftertouch opens it", {
        "o1_level": 60, "o2_freq": 0.2, "ring_level": 70, "flt_cut": hz(400), "flt_res": 25, "lad_kbd": 40,
        "lad_env": 25, **fenv(1, 350, 30, 120), **amp(2, 500, 90, 120), **mono(20),
        **mx((PX, "Osc 2 Pitch", 30), (AT, "Ladder Cutoff", 22), (MW, "Ring Level", 30), (PY, "Ladder Drive", 50))})
add("Wooden Upright", "Bass", "Soft", "An upright-style bass: a triangle with a breathy thump through a band-pass "
    "body, decaying naturally; velocity sets the thump, aftertouch the body", {
        **TRI, "o2_level": 25, "o2_mix": 100, "o2_pw": 30, "noise_level": 25, "noise_color_pair": "Red+White",
        "noise_color_mix": 30, "noise_env": "On", "noise_a": ms(1), "noise_r": ms(60), "noise_sus": "Off",
        **svf_only("noise"), "svf_mode": "BP", "svf_cut": hz(300), "svf_res": 30, "svf_kbd": 50, "flt_mix": 80,
        "flt_cut": hz(600), "lad_kbd": 50, "lad_env": 20, **fenv(1, 200, 20, 120), **amp(4, 900, 25, 180),
        **mono(), **mx((VEL, "Noise Level", 25), (AT, "Ladder Cutoff", 20), (MW, "Amp Env Decay", 25),
                       (PX, "SVF Cutoff", 30)),
        **reverb(10, 1.0, rev_type="Room")})
add("Vowel Bass", "Bass", "Evolving", "Two band-pass formants in parallel make the bass talk; the wheel and a slow "
    "LFO move the vowel, aftertouch opens it, pad X spreads the formants", {
        "o2_level": 80, "o2_oct": "16'", **svf_only("o1", "o2"), "svf_mode": "BP", "svf_pair": "Parallel",
        "svf_space": 40, "svf_cut": hz(500), "svf_res": 65, "svf_kbd": 30, "svf_lfo": 15, "lfo2_rate": lfo(0.3),
        "lfo2_wave": "Triangle", "flt_mix": 0, "svf_env": 15, **fenv(5, 500, 40, 200), **amp(3, 500, 90, 150),
        **mono(30), **mx((MW, "SVF Cutoff", 30), (AT, "SVF EG Amt", 25), (PX, "SVF Spacing", 35), (PY, "SVF Res", 25))})
add("Stack Mono Brute", "Bass", "Aggressive", "Four unison voices of saw and sub into a driven ladder: huge and "
    "forward; aftertouch opens it, the wheel widens the stack", {
        "o2_level": 60, "o2_oct": "16'", "o2_mix": 100, "flt_cut": hz(450), "flt_res": 30, "flt_drive": 45,
        "lad_env": 35, "lad_kbd": 40, **fenv(1, 450, 35, 150), "fenv_vel": 30, **amp(2, 500, 90, 120), **mono(30),
        **uni(4, 22), "spread": 30,
        **mx((AT, "Ladder Cutoff", 22), (MW, "Spread", 60), (PX, "Ladder Res", 35), (PY, "Ladder Drive", 40))})
add("Crushed Sub", "Bass", "Dirty", "A sine-like sub through the bit crusher: the wheel brings the crushing in, pad "
    "X lowers the sample rate", {
        **TRI, "o2_level": 30, "o2_oct": "4'", "o2_mix": 100, "flt_cut": hz(1200), "lad_kbd": 40,
        **amp(2, 400, 95, 100), **mono(20),
        "modfx_on": "On", "modfx_type": "Bit Crusher", "modfx_bits": 8, "modfx_srate": logk(11000, 128, 44100),
        "modfx_mix": 15,
        **mx((MW, "Synth FX Mix", 60), (PX, "Synth FX Sample Rate", -25), (AT, "Ladder Cutoff", 25),
             (PY, "Osc 2 Level", 40))})
add("Triplet Roller", "Bass", "Evolving", "LFO 2 in triplet eighths chops the ladder: a rolling, galloping bass from "
    "one held note; the wheel deepens the roll, aftertouch adds resonance", {
        "o1_mix": 40, "o2_level": 70, "o2_freq": 0.06, "flt_cut": hz(220), "flt_res": 40, "lad_kbd": 40, "lad_lfo": 50,
        "lfo2_wave": "Ramp", "lfo2_sync": "On", "lfo2_rate": lfo_div("1/8"), "lfo2_timing": "Triplet", "lfo2_reset": "On",
        **amp(2, 500, 90, 120), **mono(20),
        **mx((MW, "Ladder LFO2 Amt", 25), (AT, "Ladder Res", 25), (PX, "Ladder Cutoff", 25), (PY, "Send 1 (FX1)", 30)),
        **delay("1/8T", 30, 8, True)})
add("Bass Under Tines", "Bass", "Warm", "Split: a round ladder bass below E3, bell-like FM tines above (each Synth "
    "keeps its own wheel and aftertouch moves)", multi({
        "o2_level": 70, "o2_freq": 0.05, "o3_level": 50, "o3_oct": "16'", "o3_mix": 100, "flt_cut": hz(280),
        "flt_res": 20, "lad_env": 35, "lad_kbd": 40, **fenv(2, 400, 30, 150), **amp(2, 500, 85, 120), **mono(20),
        "key_hi": 51, **mx((AT, "Ladder Cutoff", 20), (MW, "Ladder Cutoff", 20))}, {
        **TRI, "o1_level": 0, **tri(2, 100), **tri(3, 0, "2'"), "fm_route": "1 > 2", "fm_amt": 10,
        **menv(1, 900, 10, 400), **fenv(1, 140, 0, 80), "flt_cut": hz(6000), "lad_kbd": 30,
        **amp(2, 2400, 25, 450), "voices": "8", "key_lo": 52, "lfo4_rate": lfo(4.5), "lfo4_wave": "Sine",
        **mx(("Mod Env", "FM Amount", 35), (VEL, "FM Amount", 20), ("Filter Env", "Osc 3 Level", 30),
             ("LFO 4", "Pan", 50, MW), (AT, "FM Amount", 12)),
        **chorus(0.5, 6, 30, 25), "mfx_send2": 18}, **reverb(0, 1.6)), "Split")
add("Sub And Snarl", "Bass", "Aggressive", "Layer: a clean triangle sub (Synth 1) under a filtered, LFO-snarling saw "
    "(Synth 2); the wheel and aftertouch work the snarl only", multi({
        **TRI, "flt_cut": hz(200), "lad_kbd": 50, **amp(2, 400, 100, 100), **mono(20)}, {
        "o2_level": 80, "o2_freq": 0.12, "flt_cut": hz(600), "flt_res": 45, "flt_drive": 50, "flt_mode": "LP",
        "lad_lfo": 25, "lfo2_rate": lfo(3), "lfo2_wave": "Triangle", "lad_kbd": 40, **amp(5, 500, 85, 120), **mono(20),
        "svf_mode": "HP", "svf_cut": hz(150), **both("o1", "o2"), "flt_route": "Series",
        **mx((MW, "Ladder LFO2 Amt", 30), (AT, "Ladder Cutoff", 25), (PX, "LFO 2 Rate (st)", 40),
             (PY, "Ladder Drive", 40))}), "Layer")
# Bass, more colours
add("Bounce Ladder Bass", "Bass", "Bright", "A punchy saw-and-pulse bass with a springy filter envelope; velocity "
    "sets the bounce, the wheel adds resonance", {
        "o1_mix": 30, "o1_pw": 35, "o2_level": 70, "o2_freq": 0.07, "flt_cut": hz(300), "flt_res": 40, "lad_env": 55,
        "lad_kbd": 50, **fenv(1, 250, 20, 100), "fenv_vel": 50, "fenv_dcurve": 80, **amp(1, 400, 80, 100), **mono(),
        **mx((MW, "Ladder Res", 25), (AT, "Ladder Cutoff", 20), (PX, "Osc 1 PW", 30), (PY, "Send 1 (FX1)", 30)),
        **delay("1/16", 20, 0, True)})

# ======================================================================================================== LEAD (32)
add("Silk Glide Lead", "Lead", "Warm", "A smooth saw-and-square lead with legato glide; the wheel brings vibrato, "
    "aftertouch opens the filter and adds vibrato, the ribbon bends", {
        "o2_level": 70, "o2_mix": 100, "o2_freq": 0.05, "flt_cut": hz(1500), "flt_res": 25, "lad_env": 25, "lad_kbd": 60,
        **fenv(5, 600, 50, 300), **amp(4, 600, 85, 280), **mono(110), **lfo1(5.5),
        **mx(vib(3), vib(2, AT), (AT, "Ladder Cutoff", 20), (PX, "Ladder Res", 35), (PY, "Send 1 (FX1)", 30)),
        **rib("Pitch", 8), **delay("1/8.", 30, 18, True), **reverb(15, 1.8)})
add("Ladder Cry Solo", "Lead", "Bright", "A singing resonant ladder lead: the envelope opens it on each note; "
    "aftertouch makes it cry, the wheel adds delayed vibrato", {
        "o2_level": 80, "o2_freq": 0.08, "flt_cut": hz(900), "flt_res": 60, "lad_env": 40, "lad_kbd": 70,
        **fenv(10, 500, 40, 300), "fenv_vel": 30, **amp(3, 600, 85, 300), **mono(60), **lfo1(5.8, delay_ms=250, fade_ms=500),
        **mx(vib(3), (AT, "Ladder Cutoff", 25), (AT, "Ladder Res", 10), (PX, "Ladder Drive", 40), (PY, "Send 1 (FX1)", 30)),
        **rib("Pitch", 8), **delay("1/4", 30, 15, True), **reverb(15, 2.0)})
add("Sync Lightning", "Lead", "Aggressive", "A screaming hard-sync lead: the mod envelope sweeps the synced oscillator "
    "down on every note; aftertouch and pad X push the sweep, the wheel adds vibrato", {
        "o1_level": 0, "o2_level": 100, "o2_sync": "On", "o2_freq": 2, "o3_level": 50, "o3_oct": "16'", "o3_freq": 0.05,
        **menv(1, 600, 20, 300), "flt_cut": hz(4000), "flt_res": 15, "lad_kbd": 30, **amp(2, 500, 90, 250),
        **mono(50), **lfo1(6),
        **mx(vib(3), ("Mod Env", "Osc 2 Pitch", 35), (AT, "Osc 2 Pitch", 25), (PX, "Osc 2 Pitch", 30),
             (PY, "Ladder Drive", 40)),
        **rib("Osc 2 Pitch", 40), **delay("1/8.", 30, 15, True), **reverb(12)})
add("Sync Whine", "Lead", "Bright", "Sync under your fingers: the ribbon and aftertouch move the synced pitch for "
    "whining, talking lines; the wheel gives vibrato", {
        "o1_level": 30, "o2_level": 90, "o2_sync": "On", "o2_freq": 4, "flt_cut": hz(3000), "flt_res": 30,
        "lad_kbd": 50, "lad_env": 15, **fenv(5, 600, 60, 300), **amp(3, 500, 90, 250), **mono(80, kind="EXP"),
        **lfo1(5.5), **mx(vib(3), (AT, "Osc 2 Pitch", 30), (PX, "Ladder Cutoff", 20), (PY, "Send 1 (FX1)", 35)),
        **rib("Osc 2 Pitch", 60), **delay("1/8", 30, 15, True), **reverb(15)})
add("Seven Saw Choir", "Lead", "Bright", "Seven detuned unison saws, wide and shining; the wheel widens the detune, "
    "aftertouch opens the filter, pad Y adds space", {
        "o2_level": 60, "o2_oct": "4'", "o2_freq": 0.05, "flt_cut": hz(2500), "flt_res": 15, "lad_kbd": 50,
        **amp(5, 600, 90, 350), **mono(80), **uni(7, 40), "spread": 80, **lfo1(5.5),
        **mx((MW, "Spread", 20), vib(2, AT), (AT, "Ladder Cutoff", 20), (PY, "Send 2 (FX2)", 30), (PX, "Osc 2 Level", 40)),
        **rib("Pitch", 8), **delay("1/8.", 30, 15, True), **reverb(20, 2.2), "mfx_series": 20})
add("Stacked Horizon", "Lead", "Warm", "A soft-edged four-voice unison lead with chorus and echoes: big but warm; "
    "aftertouch brightens, the wheel adds vibrato", {
        "o2_level": 70, "o2_mix": 100, "o2_pw": 40, "flt_cut": hz(1300), "flt_res": 20, "lad_env": 20, "lad_kbd": 60,
        **fenv(20, 800, 50, 400), **amp(10, 700, 90, 400), **mono(90), **uni(4, 25), "spread": 60, **lfo1(5.2),
        **mx(vib(3), (AT, "Ladder Cutoff", 22), (PX, "Osc 2 PW", 30), (PY, "Send 1 (FX1)", 35)),
        **chorus(0.3, 8, 30, 30), **rib("Pitch", 8), **delay("1/4.", 35, 18, True), **reverb(18, 2.4)})
add("Reed PWM Lead", "Lead", "Soft", "A reedy pulse lead whose width breathes with LFO 3; aftertouch swells it like "
    "breath, the wheel adds vibrato", {
        "o1_mix": 100, "o1_pw": 30, "pw_lfo3": 30, "lfo3_rate": lfo(0.9), "lfo3_wave": "Triangle", "flt_cut": hz(1800),
        "flt_res": 10, "lad_kbd": 60, **amp(25, 500, 85, 200), **mono(40), **lfo1(5.0, delay_ms=200, fade_ms=400),
        **mx(vib(3), (AT, "Amp Level", 20), (AT, "Ladder Cutoff", 15), (PX, "PW LFO3 Amt", 40), (PY, "Send 2 (FX2)", 30)),
        **rib("Pitch", 8), **reverb(18, 1.8)})
add("Twin Pulse Lead", "Lead", "Warm", "Two pulses modulated against each other by two LFOs: thick, moving and "
    "warm; the wheel speeds the motion, aftertouch opens it", {
        "o1_mix": 100, "o1_pw": 40, "o2_mix": 100, "o2_pw": 60, "o2_level": 85, "o2_freq": 0.06,
        "lfo3_rate": lfo(0.6), "lfo4_rate": lfo(0.45), "lfo4_wave": "Sine", "flt_cut": hz(1600), "flt_res": 20,
        "lad_kbd": 60, "lad_env": 20, **fenv(5, 600, 50, 300), **amp(4, 600, 90, 300), **mono(60), **lfo1(5.4),
        **mx(("LFO 3", "Osc 1 PW", 20), ("LFO 4", "Osc 2 PW", -20), (MW, "LFO 3 Rate (st)", 40), vib(2, AT),
             (AT, "Ladder Cutoff", 20), (PY, "Send 1 (FX1)", 30)),
        **rib("Pitch", 8), **delay("1/8.", 25, 15, True), **reverb(15)})
add("Filter FM Howl", "Lead", "Dirty", "Osc 3 modulates the ladder's cutoff at audio rate: a howling, ripping lead; "
    "aftertouch turns the filter FM up, the wheel adds vibrato", {
        "o2_level": 70, "o2_freq": 0.07, **tri(3, 0, "8'", 0.03), "lad_fm": 15, "lad_fm_src": "Osc 3",
        "flt_cut": hz(1200), "flt_res": 45, "lad_kbd": 60, "lad_env": 25, **fenv(5, 500, 40, 300),
        **amp(3, 500, 90, 250), **mono(60), **lfo1(5.6),
        **mx(vib(3), (AT, "Ladder FM Amt", 30), (PX, "Osc 3 Pitch", 25), (PY, "Ladder Res", 30)),
        **rib("Pitch", 8), **delay("1/8.", 30, 15, True)})
add("SVF FM Scream", "Lead", "Aggressive", "A resonant SVF low-pass whose cutoff osc 2 shakes at audio rate: "
    "an acid-tongued scream; aftertouch adds the shake, pad X the resonance", {
        "o2_level": 0, "o2_oct": "4'", **svf_only("o1"), "svf_mode": "LP", "svf_cut": hz(1500), "svf_res": 60,
        "svf_fm": 15, "svf_fm_src": "Osc 2", "svf_kbd": 60, "svf_env": 25, "flt_mix": 0, **fenv(2, 400, 40, 300),
        **amp(2, 500, 90, 250), **mono(50), **lfo1(6),
        **mx(vib(3), (AT, "SVF FM Amt", 30), (PX, "SVF Res", 30), (PY, "Send 1 (FX1)", 30), (VEL, "SVF EG Amt", 15)),
        **rib("Pitch", 8), **delay("1/8", 25, 12, True), **reverb(10)})
add("Breath Flute", "Lead", "Soft", "A breathy flute: a triangle with filtered noise, vibrato fading in after the "
    "attack; aftertouch blows harder (louder, more breath)", {
        **TRI, "noise_level": 22, "noise_color_pair": "Red+White", "noise_color_mix": 60, **both("noise"),
        "svf_mode": "BP", "svf_cut": hz(2000), "svf_res": 30, "svf_kbd": 80, "flt_mix": 70, "flt_cut": hz(2500),
        "lad_kbd": 60, **amp(60, 500, 85, 250), **mono(30), **lfo1(5.2, delay_ms=300, fade_ms=700), "pitch_lfo": 1,
        **mx(vib(3), (AT, "Amp Level", 25), (AT, "Noise Level", 20), (PX, "Noise Level", 30), (PY, "Send 2 (FX2)", 30)),
        **rib("Pitch", 8), **reverb(25, 2.4, rev_type="Hall", rev_size=55)})
add("Ribbon Theremin", "Lead", "Soft", "A pure, slightly hollow tone with long exponential glide: slide the ribbon "
    "over an octave each way; aftertouch adds a wide vibrato", {
        **TRI, "o2_level": 15, "o2_oct": "4'", **tri(2), "flt_cut": hz(3000), "lad_kbd": 50, **amp(40, 500, 95, 400),
        **mono(250, kind="EXP", legato=False), **lfo1(6.2),
        **mx(vib(3), vib(5, AT), (PX, "Osc 2 Level", 40), (PY, "Send 2 (FX2)", 35)),
        **rib("Pitch", 50, ribbon_mode="Hold"), **reverb(25, 2.8, rev_type="Hall", rev_size=60),
        **delay("1/4", 30, 12, True)})
add("Pressure Horn Lead", "Lead", "Warm", "A brassy lead that answers pressure: aftertouch opens the filter and "
    "brings in vibrato, velocity sets the bite", {
        "o2_level": 80, "o2_freq": 0.07, "flt_cut": hz(700), "flt_res": 15, "lad_env": 35, "lad_kbd": 60,
        **fenv(40, 500, 50, 300), "fenv_vel": 40, **amp(20, 500, 90, 250), **mono(50), **lfo1(5.4),
        **mx(vib(4, AT), vib(3), (AT, "Ladder Cutoff", 30), (PX, "Ladder Res", 30), (PY, "Send 2 (FX2)", 30)),
        **rib("Pitch", 8), **reverb(18, 1.8)})
add("Bent Wire Lead", "Lead", "Bright", "A bright, thin lead built for bending: the pitch wheel goes up a fifth, "
    "the ribbon moves osc 2 alone against osc 1", {
        "o1_mix": 50, "o1_pw": 25, "o2_level": 70, "o2_freq": 0.04, "flt_cut": hz(3500), "flt_res": 20, "lad_kbd": 50,
        **amp(2, 500, 90, 250), **mono(40), "bend_up": 7, "bend_down": -2, **lfo1(5.8),
        **mx(vib(3), (AT, "Ladder Cutoff", 15), vib(2, AT), (PX, "Osc 1 PW", 30), (PY, "Send 1 (FX1)", 30)),
        **rib("Osc 2 Pitch", 8), **delay("1/8.", 30, 18, True), **reverb(12)})
add("Glass Sine Lead", "Lead", "Clean", "A clean sine-like lead; velocity adds a glassy FM edge, aftertouch more, "
    "the wheel vibrato", {
        **TRI, "o1_level": 0, "o1_oct": "4'", **tri(2, 100), "fm_route": "1 > 2", "fm_amt": 4, "flt_cut": hz(6000),
        **amp(4, 500, 90, 300), **mono(50), **lfo1(5.5),
        **mx(vib(3), (VEL, "FM Amount", 12), (AT, "FM Amount", 18), (PX, "Osc 1 Pitch", 15), (PY, "Send 1 (FX1)", 35)),
        **rib("Pitch", 8), **delay("1/4", 35, 15, True), **reverb(18, 2.2)})
add("Ring Bell Lead", "Lead", "Bright", "Ring modulation at an odd ratio: a bell-edged lead that rings over the mix; "
    "pad X changes the ratio, aftertouch adds vibrato", {
        "o1_level": 40, **tri(2, 0, "4'", 3.5), "ring_level": 70, "flt_cut": hz(4000), "lad_kbd": 40,
        **amp(2, 700, 75, 350), **mono(30), **lfo1(5.6),
        **mx(vib(3), vib(2, AT), (PX, "Osc 2 Pitch", 15), (AT, "Ring Level", 20), (PY, "Send 1 (FX1)", 30)),
        **rib("Pitch", 8), **delay("1/8.", 30, 15, True), **reverb(18, 2.0)})
add("Pocket Chip Lead", "Lead", "Bright", "A square-wave chip lead with fast vibrato and a gentle crush; the wheel "
    "speeds the vibrato, pad X crushes harder", {
        "o1_mix": 100, "o1_pw": 25, "flt_cut": hz(8000), **amp(1, 300, 85, 120), **mono(20, kind="LCR"),
        **lfo1(7.5), "modfx_on": "On", "modfx_type": "Bit Crusher", "modfx_bits": 10,
        "modfx_srate": logk(16000, 128, 44100), "modfx_mix": 50,
        **mx(vib(3), (MW, "LFO 1 Rate", 10), (PX, "Synth FX Bit Depth", -30), (AT, "Osc 1 PW", 20), (PY, "Send 1 (FX1)", 30)),
        **rib("Pitch", 8), **delay("1/8", 25, 15, True)})
add("Dirty Tape Solo", "Lead", "Dirty", "A driven saw lead into tape saturation, with a warbling tape echo; "
    "aftertouch drives harder, the wheel adds vibrato", {
        "o2_level": 60, "o2_freq": -0.06, "flt_cut": hz(2000), "flt_res": 25, "flt_drive": 55, "lad_kbd": 50,
        **amp(3, 500, 90, 300), **mono(50), **lfo1(5.5),
        "modfx_on": "On", "modfx_type": "Tape Delay", "modfx_sync": "On", "modfx_dtime": fxdiv("1/8."), "modfx_fb": 35,
        "modfx_wow": 30, "modfx_flutter": 25, "modfx_mix": 25,
        **mx(vib(3), (AT, "Ladder Drive", 30), (AT, "Ladder Cutoff", 12), (PX, "Synth FX Mix", 35), (PY, "Ladder Res", 30)),
        **rib("Pitch", 8), **reverb(12, 1.2, rev_type="Room")})
add("Formant Talker", "Lead", "Evolving", "Two band-pass formants make a vowel; aftertouch and pad X move it from "
    "'oo' towards 'ee', a slow LFO keeps it alive", {
        "o2_level": 70, "o2_freq": 0.05, **svf_only("o1", "o2"), "svf_mode": "BP", "svf_pair": "Parallel",
        "svf_space": 50, "svf_cut": hz(500), "svf_res": 70, "svf_kbd": 30, "svf_lfo": 10, "lfo2_rate": lfo(0.25),
        "lfo2_wave": "Sine", "flt_mix": 0, **amp(8, 500, 90, 300), **mono(70), **lfo1(5.3),
        **mx(vib(3), (AT, "SVF Cutoff", 25), (PX, "SVF Cutoff", 30), (PY, "SVF Spacing", 30), (VEL, "SVF Res", 10)),
        **rib("Pitch", 8), **delay("1/8.", 25, 12, True), **reverb(15)})
add("Canyon Echo Lead", "Lead", "Warm", "A round lead into long dotted echoes and a hall: made for slow melodies; pad "
    "Y feeds more echo, the wheel gives vibrato", {
        "o1_mix": 30, "o2_level": 50, "o2_oct": "16'", **tri(2), "flt_cut": hz(1800), "flt_res": 15, "lad_kbd": 60,
        **amp(8, 700, 85, 500), **mono(70), **lfo1(5.2),
        **mx(vib(3), (AT, "Ladder Cutoff", 20), vib(2, AT), (PY, "Send 1 (FX1)", 35), (PX, "Osc 1 Wave Mix", 40)),
        **rib("Pitch", 8), **delay("1/4.", 45, 25, True), **reverb(22, 3.0, rev_type="Hall", rev_size=70),
        "mfx_series": 25})
add("Fifth Stack Lead", "Lead", "Bright", "Root, fifth and octave saws gliding together: a bold, organ-bright "
    "lead; pad X fades the fifth, aftertouch opens the filter", {
        "o2_level": 70, "o2_freq": 7, "o3_level": 60, "o3_oct": "4'", "flt_cut": hz(2200), "flt_res": 20, "lad_kbd": 50,
        **amp(3, 600, 90, 300), **mono(80), **lfo1(5.5),
        **mx(vib(3), (PX, "Osc 2 Level", -60), (AT, "Ladder Cutoff", 20), vib(2, AT), (PY, "Send 1 (FX1)", 30)),
        **rib("Pitch", 8), **delay("1/8.", 30, 15, True), **reverb(15)})
add("Resonant Shriek", "Lead", "Aggressive", "High resonance and drive on a bright saw: a shrieking lead with a "
    "filter envelope stab; pad X tames or opens it", {
        "o2_level": 80, "o2_freq": 0.1, "flt_cut": hz(1100), "flt_res": 72, "flt_drive": 60, "lad_env": 35,
        "lad_kbd": 70, **fenv(1, 400, 30, 250), "fenv_vel": 35, **amp(2, 500, 90, 250), **mono(40), **lfo1(6),
        **mx(vib(3), (AT, "Ladder Cutoff", 20), (PX, "Ladder Cutoff", 25), (PY, "Ladder Res", 20)),
        **rib("Pitch", 8), **delay("1/8", 25, 12, True), **reverb(10)})
add("Soft Pipe Lead", "Lead", "Soft", "A gentle triangle pipe with chorus: for quiet melodies; aftertouch adds "
    "breath and vibrato", {
        **TRI, **tri(2, 40, "4'"), "flt_cut": hz(2500), "lad_kbd": 50, **amp(30, 500, 90, 300), **mono(40),
        **lfo1(5.0, delay_ms=200, fade_ms=500), **chorus(0.35, 7, 30, 30),
        **mx(vib(3), vib(3, AT), (AT, "Amp Level", 15), (PX, "Osc 2 Level", 40), (PY, "Send 2 (FX2)", 30)),
        **rib("Pitch", 8), **reverb(22, 2.4)})
add("Wah Pedal Lead", "Lead", "Dirty", "A wah pedal under your finger: aftertouch sweeps a resonant band-pass "
    "over a driven saw; the wheel adds vibrato", {
        "o2_level": 70, "o2_freq": 0.06, **both("o1", "o2"), "svf_mode": "BP", "svf_cut": hz(450), "svf_res": 60,
        "svf_kbd": 30, "flt_route": "Series", "flt_cut": hz(3000), "flt_drive": 50, "lad_kbd": 30,
        **amp(3, 500, 90, 250), **mono(40), **lfo1(5.5),
        **mx(vib(3), (AT, "SVF Cutoff", 45), (PX, "SVF Cutoff", 30), (PY, "Ladder Drive", 30)),
        **rib("SVF Cutoff", 40), **delay("1/8.", 25, 12, True)})
add("Noise Edge Lead", "Lead", "Aggressive", "A saw lead with a burst of noise on each attack and a snapping "
    "filter: hard and edgy; velocity adds the noise burst", {
        "o2_level": 60, "o2_oct": "4'", "o2_freq": 0.05, "noise_level": 25, "noise_env": "On", "noise_a": ms(1),
        "noise_r": ms(150), "noise_sus": "Off", "flt_cut": hz(1500), "flt_res": 35, "lad_env": 35, "lad_kbd": 60,
        **fenv(1, 300, 40, 200), "fenv_vel": 40, **amp(1, 500, 90, 250), **mono(30), **lfo1(6),
        **mx(vib(3), (VEL, "Noise Level", 25), (AT, "Ladder Cutoff", 20), (PX, "Ladder Res", 30), (PY, "Send 1 (FX1)", 30)),
        **rib("Pitch", 8), **delay("1/8", 25, 12, True)})
add("Detuned Pair Lead", "Lead", "Warm", "Two saws twelve cents apart with drift: the classic fat, warm mono lead; "
    "aftertouch opens it, the wheel adds vibrato", {
        "o2_level": 90, "o2_freq": 0.12, "drift": 35, "flt_cut": hz(1400), "flt_res": 25, "lad_env": 25, "lad_kbd": 60,
        **fenv(3, 700, 45, 300), "fenv_vel": 25, **amp(3, 600, 90, 300), **mono(60), **lfo1(5.5),
        **mx(vib(3), (AT, "Ladder Cutoff", 22), vib(2, AT), (PX, "Osc 2 Beat", 15), (PY, "Send 1 (FX1)", 30)),
        **rib("Pitch", 8), **delay("1/8.", 30, 15, True), **reverb(12)})
add("Cosmic Portamento", "Lead", "Evolving", "A long, exponential glide between every note into shimmering space; "
    "the wheel slows LFO-driven filter motion in, pad Y floods the shimmer", {
        "o2_level": 60, "o2_mix": 100, "o2_pw": 30, "flt_cut": hz(1600), "flt_res": 35, "lad_kbd": 60, "lad_lfo": 8,
        "lfo2_rate": lfo(0.2), "lfo2_wave": "Sine", **amp(15, 700, 90, 800), **mono(400, kind="EXP", legato=False),
        **lfo1(5.0),
        **mx(vib(3, AT), (MW, "Ladder LFO2 Amt", 25), (PY, "Send 2 (FX2)", 30), (PX, "Osc 2 PW", 30)),
        **rib("Pitch", 8), **delay("1/4.", 40, 18, True),
        **reverb(25, 4.0, rev_type="Shimmer", rev_size=70), "mfx_series": 25})
add("Angle Morph Lead", "Lead", "Evolving", "LFO 3 sweeps every oscillator's wave angle and pulse width: a lead that "
    "keeps changing colour; the wheel speeds the morph, aftertouch deepens it", {
        "o1_wave": "Tri", "o1_angle": 50, "o1_mix": 30, "o2_level": 70, "o2_wave": "Tri", "o2_angle": 30,
        "o2_freq": 0.06, "angle_lfo3": 40, "pw_lfo3": 30, "lfo3_rate": lfo(0.5), "lfo3_wave": "Triangle",
        "flt_cut": hz(2500), "flt_res": 20, "lad_kbd": 50, **amp(4, 600, 90, 350), **mono(60), **lfo1(5.5),
        **mx((MW, "LFO 3 Rate (st)", 60), (AT, "Angle LFO3 Amt", 30), vib(2, AT), (PY, "Send 1 (FX1)", 30)),
        **rib("Pitch", 8), **delay("1/8.", 30, 15, True), **reverb(15)})
add("Bass Left Solo Right", "Lead", "Bright", "Split: a punchy bass below C3 (Synth 1) and a gliding sync lead from "
    "C3 up (Synth 2), each with its own wheel and aftertouch moves", multi({
        "o2_level": 70, "o2_oct": "16'", "o2_mix": 100, "flt_cut": hz(300), "flt_res": 30, "lad_env": 45, "lad_kbd": 50,
        **fenv(1, 300, 20, 100), **amp(1, 450, 80, 100), **mono(), "key_hi": 47,
        **mx((AT, "Ladder Cutoff", 20), (MW, "Ladder Res", 20))}, {
        "o1_level": 20, "o2_level": 100, "o2_sync": "On", "o2_freq": 3, **menv(1, 500, 15, 300), "flt_cut": hz(3500),
        "flt_res": 15, "lad_kbd": 30, **amp(3, 500, 90, 250), **mono(70), "key_lo": 48, **lfo1(5.6),
        **mx(vib(3), ("Mod Env", "Osc 2 Pitch", 25), (AT, "Osc 2 Pitch", 20)), "mfx_send1": 18, "mfx_send2": 12},
        **delay("1/8.", 30, 0, True), **reverb(0)), "Split")
add("Lead Over Haze", "Lead", "Warm", "Layer: a singing mono lead (Synth 1) with a soft pad behind it (Synth 2) that "
    "follows the same notes; aftertouch opens the lead, pad Y the pad's space", multi({
        "o2_level": 70, "o2_freq": 0.06, "flt_cut": hz(1600), "flt_res": 20, "lad_env": 20, "lad_kbd": 60,
        **fenv(5, 600, 50, 300), **amp(4, 600, 90, 300), **mono(80), **lfo1(5.5), "volume": 70,
        **mx(vib(3), (AT, "Ladder Cutoff", 22), vib(2, AT)), **rib("Pitch", 8), "mfx_send1": 18, "mfx_send2": 12}, {
        "o1_mix": 100, "o1_pw": 40, "o2_level": 80, "o2_freq": 0.1, "flt_cut": hz(900), "flt_res": 10,
        **amp(400, 1200, 85, 1200), "voices": "4", "volume": 45, **chorus(0.3, 8, 35, 35), "mfx_send2": 30,
        **mx((PY, "Send 2 (FX2)", 35), (MW, "Ladder Cutoff", 15))},
        **delay("1/4.", 35, 0, True), **reverb(0, 2.6)), "Layer")
add("Octave Twin Lead", "Lead", "Aggressive", "Layer: a bright saw lead (Synth 1) doubled an octave down by a square "
    "(Synth 2); the wheel adds vibrato to both, aftertouch opens the top", multi({
        "o2_level": 70, "o2_freq": 0.05, "flt_cut": hz(2500), "flt_res": 25, "lad_kbd": 50, "lad_env": 20,
        **fenv(2, 500, 50, 250), **amp(2, 500, 90, 250), **mono(50), **lfo1(5.6),
        **mx(vib(3), (AT, "Ladder Cutoff", 20)), **rib("Pitch", 8), "mfx_send1": 15}, {
        "o1_mix": 100, "o1_oct": "16'", "flt_cut": hz(1200), "flt_res": 10, "lad_kbd": 50, **amp(2, 500, 90, 250),
        **mono(50), **lfo1(5.6), "volume": 55, **mx(vib(3)), **rib("Pitch", 8)},
        **delay("1/8.", 30, 0, True)), "Layer")
add("Hollow Sync Flute", "Lead", "Soft", "A gentle sync on triangles gives a hollow, wooden flute; aftertouch moves "
    "the sync a little and swells it", {
        **TRI, "o1_level": 30, **tri(2, 80), "o2_sync": "On", "o2_freq": 1.5, "flt_cut": hz(2200), "lad_kbd": 60,
        "noise_level": 8, **amp(40, 500, 90, 300), **mono(40), **lfo1(5.0, delay_ms=250, fade_ms=500),
        **mx(vib(3), (AT, "Osc 2 Pitch", 12), (AT, "Amp Level", 15), (PX, "Osc 2 Pitch", 20), (PY, "Send 2 (FX2)", 30)),
        **rib("Pitch", 8), **reverb(22, 2.2)})

# ======================================================================================================== KEYS (20)
TINE = {   # two-operator FM keys: osc 1 (silent) modulates osc 2; a tine on osc 3 from the filter envelope
    **TRI, "o1_level": 0, **tri(2, 100), **tri(3, 0, "2'"), "fm_route": "1 > 2", "fm_amt": 10,
    **menv(1, 900, 10, 400), **fenv(1, 140, 0, 80), "flt_cut": hz(6000), "lad_kbd": 30, **amp(2, 2400, 25, 450),
    "voices": "8", "lfo4_rate": lfo(4.5), "lfo4_wave": "Sine"}
add("Tine Electric", "Keys", "Warm", "Electric-piano-like FM keys: velocity brings the bark, the wheel an auto-pan "
    "tremolo, aftertouch a little more growl", {
        **TINE, **mx(("Mod Env", "FM Amount", 35), (VEL, "FM Amount", 22), ("Filter Env", "Osc 3 Level", 30),
                     ("LFO 4", "Pan", 60, MW), (AT, "FM Amount", 12), (PX, "Osc 1 Pitch", 8), (PY, "Send 2 (FX2)", 30)),
        **chorus(0.5, 6, 30, 25), **reverb(18, 1.6)})
add("Barking Tines", "Keys", "Dirty", "The tines driven: more FM on hard notes, a ladder push and a saturator; "
    "the wheel adds tremolo, pad X more drive", {
        **TINE, "fm_amt": 15, "flt_cut": hz(4500), "flt_drive": 45,
        **mx(("Mod Env", "FM Amount", 40), (VEL, "FM Amount", 30), ("Filter Env", "Osc 3 Level", 25),
             ("LFO 4", "Amp Level", 25, MW), (AT, "Ladder Drive", 30), (PX, "Synth FX Drive", 30)),
        "modfx_on": "On", "modfx_type": "Density", "modfx_drive": 40, "modfx_mix": 100, **reverb(12, 1.2, rev_type="Room")})
add("Reed Buzz Piano", "Keys", "Warm", "A reedy, buzzy electric keyboard: a narrow pulse over a triangle, a slight "
    "drive and an amp tremolo on the wheel", {
        "o1_mix": 60, "o1_pw": 25, **tri(2, 60), "flt_cut": hz(1800), "flt_res": 10, "flt_drive": 30, "lad_env": 30,
        "lad_kbd": 50, **fenv(1, 500, 20, 300), "fenv_vel": 45, **amp(2, 1800, 30, 350), "voices": "8",
        "lfo4_rate": lfo(5.5), "lfo4_wave": "Sine",
        **mx(("LFO 4", "Amp Level", 30, MW), (AT, "Ladder Cutoff", 15), (PX, "Osc 1 PW", 20), (PY, "Send 2 (FX2)", 30)),
        **reverb(15, 1.4, rev_type="Room")})
add("Funk Plectrum Keys", "Keys", "Bright", "A plucked-string keyboard for funk: a narrow pulse through a high-pass "
    "and a snappy ladder; velocity snaps it, pad X moves the pickup", {
        "o1_mix": 100, "o1_pw": 18, **tri(2, 40, "4'"), **both("o1", "o2"), "svf_mode": "HP", "svf_cut": hz(250),
        "flt_route": "Series", "flt_cut": hz(1500), "flt_res": 30, "lad_env": 45, "lad_kbd": 70,
        **fenv(1, 220, 15, 100), "fenv_vel": 55, **amp(1, 800, 20, 120), "voices": "8",
        **mx((PX, "Osc 1 PW", 30), (AT, "Ladder Cutoff", 20), (MW, "SVF Cutoff", 25), (PY, "Ladder Res", 30))})
add("Wah Plectrum Keys", "Keys", "Dirty", "The plucked keyboard through an envelope wah: each note quacks; the wheel "
    "brings an LFO wah, aftertouch opens it", {
        "o1_mix": 100, "o1_pw": 22, **both("o1"), "svf_mode": "BP", "svf_cut": hz(500), "svf_res": 55, "svf_env": 40,
        "svf_kbd": 40, "flt_route": "Series", "flt_cut": hz(3000), "flt_drive": 40, **fenv(1, 250, 10, 100),
        "fenv_vel": 50, **amp(1, 900, 25, 150), "voices": "8", "lfo2_rate": lfo(1.5), "lfo2_wave": "Sine",
        **mx((MW, "SVF LFO2 Amt", 35), (AT, "SVF Cutoff", 25), (PX, "SVF Res", 25), (PY, "Ladder Drive", 30))})
add("Glass FM Keys", "Keys", "Clean", "Clear, glassy FM keys at a 3:1 ratio; velocity adds sparkle, the wheel a slow "
    "chorus shimmer", {
        **TRI, "o1_level": 0, "o1_oct": "4'", "o1_freq": 7, **tri(2, 100), "fm_route": "1 > 2", "fm_amt": 6,
        **menv(1, 1200, 15, 600), "flt_cut": hz(9000), **amp(2, 2800, 30, 700), "voices": "8",
        **mx(("Mod Env", "FM Amount", 25), (VEL, "FM Amount", 20), (MW, "Synth FX Mix", 40), (AT, "FM Amount", 10),
             (PX, "Osc 1 Pitch", 4), (PY, "Send 2 (FX2)", 30)),
        **chorus(0.25, 9, 40, 10), **reverb(22, 2.4)})
add("Crystal Mallets", "Keys", "Bright", "Hard FM mallets at a 7:1 ratio: a sharp strike that rings off quickly; "
    "velocity sets the strike, pad Y the room", {
        **TRI, "o1_level": 0, "o1_oct": "2'", "o1_freq": -1, **tri(2, 100), "fm_route": "1 > 2", "fm_amt": 0,
        **menv(1, 300, 0, 200), "flt_cut": hz(10000), **amp(1, 1500, 0, 900), "voices": "8",
        **mx(("Mod Env", "FM Amount", 30), (VEL, "FM Amount", 15), (MW, "Mod Env Decay", 25), (AT, "Amp Level", 10),
             (PY, "Send 2 (FX2)", 30), (PX, "Osc 1 Pitch", 6)),
        **delay("1/8.", 25, 10, True), **reverb(20, 2.0)})
add("Toy Box Piano", "Keys", "Bright", "A little toy piano: a square two octaves up with a quick decay and a "
    "slightly crushed sound; pad X crushes it more", {
        "o1_mix": 100, "o1_oct": "4'", **tri(2, 50, "2'"), "flt_cut": hz(5000), "lad_env": 20, **fenv(1, 300, 0, 200),
        **amp(1, 900, 0, 300), "voices": "8", "modfx_on": "On", "modfx_type": "Bit Crusher", "modfx_bits": 10,
        "modfx_srate": logk(14000, 128, 44100), "modfx_mix": 40, "pitch_var": 4,
        **mx((PX, "Synth FX Mix", 40), (MW, "Amp Env Decay", 25), (VEL, "Ladder Cutoff", 15), (AT, "Pitch", 1)),
        **reverb(15, 1.0, rev_type="Room")})
add("Music Box Tines", "Keys", "Soft", "A music box: soft high FM tines that ring and fade; the wheel adds a gentle "
    "wobble, pad Y space", {
        **TRI, "o1_level": 0, "o1_oct": "2'", **tri(2, 100, "4'"), "fm_route": "1 > 2", "fm_amt": 3,
        **menv(1, 600, 0, 400), "flt_cut": hz(7000), **amp(1, 2500, 0, 1500), "voices": "8", "lfo4_rate": lfo(3),
        "lfo4_wave": "Sine",
        **mx(("Mod Env", "FM Amount", 18), (VEL, "FM Amount", 10), ("LFO 4", "Pitch", 1, MW), (PY, "Send 2 (FX2)", 30),
             (AT, "Amp Level", 10)),
        **reverb(25, 2.6, rev_type="Hall", rev_size=50)})
add("Felt Upright Keys", "Keys", "Soft", "A soft, felted piano-like tone: a filtered saw and triangle with a hammer "
    "thump; velocity opens the filter a lot", {
        "o1_mix": 0, "o1_level": 60, **tri(2, 80), "noise_level": 10, "noise_env": "On", "noise_a": ms(1),
        "noise_r": ms(30), "noise_sus": "Off", "flt_cut": hz(500), "flt_res": 5, "lad_env": 45, "lad_kbd": 70,
        **fenv(1, 1200, 15, 400), "fenv_vel": 70, **amp(2, 3000, 0, 500), "voices": "8",
        **mx((AT, "Ladder Cutoff", 15), (MW, "Synth FX Mix", 40), (PX, "Osc 1 Level", 40), (PY, "Send 2 (FX2)", 30)),
        **chorus(0.3, 8, 25, 5), **reverb(20, 1.8, rev_type="Room")})
add("Dusty Tape Keys", "Keys", "Dirty", "Lo-fi keys off an old tape: FM tines with wow, flutter and a warm "
    "saturation; the wheel adds more wobble", {
        **TINE, "flt_cut": hz(3500),
        **mx(("Mod Env", "FM Amount", 30), (VEL, "FM Amount", 20), ("Filter Env", "Osc 3 Level", 20),
             (MW, "Synth FX Wow", 40), (AT, "FM Amount", 10), (PX, "Ladder Cutoff", -20)),
        "modfx_on": "On", "modfx_type": "Tape Delay", "modfx_dtime": fxdiv("1/64T"), "modfx_fb": 0, "modfx_wow": 45,
        "modfx_flutter": 35, "modfx_bias": 50, "modfx_tone": 55, "modfx_mix": 50, **reverb(12, 1.2, rev_type="Room")})
add("Vibe Bars", "Keys", "Clean", "Vibraphone-like bars: FM at a 4:1 ratio with a soft mallet, and a motor tremolo "
    "on the wheel", {
        **TRI, "o1_level": 0, "o1_oct": "2'", **tri(2, 100), "fm_route": "1 > 2", "fm_amt": 2,
        **menv(1, 200, 0, 150), "flt_cut": hz(8000), **amp(1, 3500, 0, 1200), "voices": "8", "lfo4_rate": lfo(5.5),
        "lfo4_wave": "Sine",
        **mx(("Mod Env", "FM Amount", 18), (VEL, "FM Amount", 10), ("LFO 4", "Amp Level", 40, MW), (AT, "Amp Level", 10),
             (PY, "Send 2 (FX2)", 30)),
        **reverb(25, 2.2)})
add("Quill Keys", "Keys", "Bright", "A plucked-quill keyboard: a bright saw and pulse an octave apart, plucked "
    "with a sharp envelope; the wheel adds the upper octave", {
        "o1_mix": 0, "o2_level": 50, "o2_oct": "4'", "o2_mix": 100, "o2_pw": 30, "flt_cut": hz(2500), "flt_res": 15,
        "lad_env": 30, "lad_kbd": 60, **fenv(1, 400, 25, 300), "fenv_vel": 25, **amp(1, 1500, 10, 400), "voices": "8",
        **mx((MW, "Osc 2 Level", 40), (AT, "Ladder Cutoff", 15), (PX, "Osc 2 PW", 30), (PY, "Send 2 (FX2)", 30)),
        **reverb(20, 1.8, rev_type="Hall", rev_size=45)})
add("Velvet Poly Keys", "Keys", "Warm", "Warm poly-synth keys: two detuned saws with a gentle filter envelope and "
    "chorus; velocity and aftertouch open it, the wheel adds vibrato", {
        "o2_level": 80, "o2_freq": 0.08, "flt_cut": hz(900), "flt_res": 15, "lad_env": 30, "lad_kbd": 50,
        **fenv(3, 800, 35, 400), "fenv_vel": 40, **amp(3, 1500, 60, 450), "voices": "8", "an_cut": 40, "an_tune": 30,
        **lfo1(5.0), **chorus(0.3, 8.5, 35, 35),
        **mx(vib(2), (AT, "Ladder Cutoff", 18), (PX, "Ladder Res", 30), (PY, "Send 2 (FX2)", 30)), **reverb(18, 2.0)})
add("House Chord Stab", "Keys", "Bright", "A short, punchy chord stab for house and garage: saws and a square, a "
    "snappy filter and a synced echo; pad Y feeds the echo", {
        "o1_mix": 30, "o2_level": 70, "o2_freq": 0.1, "o2_mix": 100, "flt_cut": hz(800), "flt_res": 25, "lad_env": 45,
        "lad_kbd": 40, **fenv(1, 250, 10, 150), "fenv_vel": 40, **amp(1, 350, 15, 180), "voices": "8",
        **mx((MW, "Filter Env Decay", 30), (AT, "Ladder Cutoff", 15), (PX, "Ladder Res", 30), (PY, "Send 1 (FX1)", 35)),
        **delay("1/8.", 30, 15, True), **reverb(15, 1.4)})
add("Digital Bell Keys", "Keys", "Clean", "Ring-modulated bells over a soft triangle: clear and slightly digital; "
    "pad X detunes the bell partial, velocity strikes harder", {
        **TRI, "o1_level": 50, **tri(2, 0, "4'", 2), "ring_level": 50, "flt_cut": hz(7000), "lad_env": 15,
        **fenv(1, 600, 30, 400), **amp(1, 2500, 10, 900), "voices": "8",
        **mx((VEL, "Ring Level", 25), (PX, "Osc 2 Pitch", 10), (MW, "Synth FX Mix", 35), (AT, "Amp Level", 10),
             (PY, "Send 2 (FX2)", 30)),
        **chorus(0.4, 6, 30, 5), **reverb(22, 2.4)})
add("Warm Wood Keys", "Keys", "Soft", "Soft, woody keys: rounded triangles through a gentle low-pass, like a felt "
    "mallet on wood; aftertouch adds a touch of brightness", {
        **TRI, **tri(2, 50, "4'"), "flt_cut": hz(1200), "flt_res": 10, "lad_env": 25, "lad_kbd": 60,
        **fenv(1, 600, 20, 300), "fenv_vel": 35, **amp(1, 1800, 15, 500), "voices": "8",
        **mx((AT, "Ladder Cutoff", 18), (MW, "Osc 2 Level", 40), (PX, "Ladder Res", 25), (PY, "Send 2 (FX2)", 30)),
        **reverb(18, 1.4, rev_type="Room")})
add("Drifting Tines", "Keys", "Evolving", "FM tines that drift: a slow LFO moves the FM and the pan, and dotted "
    "echoes trail every chord; the wheel speeds the drift", {
        **TINE, "lfo3_rate": lfo(0.15), "lfo3_wave": "Sine", "drift": 30,
        **mx(("Mod Env", "FM Amount", 30), (VEL, "FM Amount", 18), ("LFO 3", "FM Amount", 8), ("LFO 4", "Pan", 30),
             (MW, "LFO 4 Rate (st)", 50), (AT, "FM Amount", 10), (PY, "Send 1 (FX1)", 30)),
        "lfo4_rate": lfo(0.3), **chorus(0.4, 7, 30, 25), **delay("1/4.", 40, 18, True), **reverb(22, 2.8),
        "mfx_series": 25})
add("Tines Over Haze", "Keys", "Warm", "Layer: FM tines (Synth 1) over a slow, soft string-like haze (Synth 2); the "
    "wheel trembles the tines, aftertouch swells the haze", multi({
        **TINE, **mx(("Mod Env", "FM Amount", 32), (VEL, "FM Amount", 20), ("Filter Env", "Osc 3 Level", 25),
                     ("LFO 4", "Pan", 50, MW)), "mfx_send2": 15}, {
        "o1_mix": 100, "o1_pw": 45, "o2_level": 80, "o2_freq": 0.1, "flt_cut": hz(1100), "flt_res": 5,
        **amp(600, 1500, 85, 1400), "voices": "8", "volume": 40, **chorus(0.25, 9, 40, 40), "mfx_send2": 30,
        **mx((AT, "Amp Level", 25), (AT, "Ladder Cutoff", 15))}, **reverb(0, 2.4)), "Layer")
add("Tines And Glass", "Keys", "Bright", "Layer: warm FM tines (Synth 1) with glassy 3:1 FM keys an octave up "
    "(Synth 2) for a bright, sparkling piano-like stack", multi({
        **TINE, **mx(("Mod Env", "FM Amount", 32), (VEL, "FM Amount", 20), ("Filter Env", "Osc 3 Level", 25),
                     ("LFO 4", "Pan", 50, MW)), "mfx_send2": 15}, {
        **TRI, "o1_level": 0, "o1_oct": "2'", "o1_freq": 7, **tri(2, 100, "4'"), "fm_route": "1 > 2", "fm_amt": 4,
        **menv(1, 900, 10, 500), "flt_cut": hz(9000), **amp(2, 2000, 15, 600), "voices": "8", "volume": 40,
        **mx(("Mod Env", "FM Amount", 22), (VEL, "FM Amount", 15)), "mfx_send2": 25}, **reverb(0, 2.0)), "Layer")

# ======================================================================================================= BRASS (16)
SECTION = {   # three detuned saws with a brassy filter envelope that opens a little ahead of the amp
    "o2_level": 85, "o2_freq": 0.07, "o3_level": 70, "o3_freq": -0.06, "flt_cut": hz(450), "flt_res": 12,
    "lad_env": 45, "lad_kbd": 50, **fenv(50, 600, 45, 350), "fenv_vel": 40, **amp(30, 700, 80, 350), "voices": "6",
    "an_cut": 40, "an_env": 40, "an_tune": 30, **lfo1(5.2, delay_ms=300, fade_ms=500)}
add("Bold Section", "Brass", "Bright", "A full synth brass section: velocity sets how hard it blows, aftertouch "
    "swells and brightens, the wheel brings a delayed vibrato", {
        **SECTION, **mx(vib(2), (AT, "Ladder Cutoff", 22), (AT, "Amp Level", 12), (PX, "Ladder Res", 30),
                        (PY, "Send 2 (FX2)", 30)),
        **reverb(20, 1.9)})
add("Soft Horn Bed", "Brass", "Soft", "Mellow, distant horns with a slow attack: for pads of brass; aftertouch brings "
    "them forward", {
        **SECTION, "flt_cut": hz(350), "lad_env": 30, **fenv(250, 900, 50, 600), **amp(200, 900, 85, 700),
        **mx(vib(2), (AT, "Ladder Cutoff", 25), (AT, "Amp Level", 15), (PY, "Send 2 (FX2)", 30)),
        **chorus(0.3, 8, 25, 20), **reverb(25, 2.6, rev_type="Hall", rev_size=60)})
add("Blat Stab", "Brass", "Aggressive", "A short, blatting brass stab: the filter envelope overshoots on the attack; "
    "velocity sets the blat, pad X the drive", {
        **SECTION, "flt_cut": hz(500), "flt_res": 20, "flt_drive": 35, "lad_env": 55, **fenv(15, 180, 20, 150),
        **amp(5, 350, 40, 200), "fenv_vel": 50,
        **mx((AT, "Ladder Cutoff", 20), (PX, "Ladder Drive", 40), (MW, "Amp Env Decay", 25), (PY, "Send 2 (FX2)", 30)),
        **reverb(15, 1.4)})
add("Funk Horn Hits", "Brass", "Bright", "Tight horn hits with a quick fall-off and a short echo, for funk riffs; the "
    "wheel lengthens the hits, aftertouch brightens", {
        **SECTION, "lad_env": 50, **fenv(10, 250, 30, 150), **amp(5, 450, 50, 180), **menv(1, 80, 0, 50),
        "pitch_env": -3,
        **mx((MW, "Amp Env Decay", 25), (AT, "Ladder Cutoff", 20), (VEL, "Pitch EG Amt", -3), (PY, "Send 1 (FX1)", 35)),
        **delay("1/8", 20, 12, True), **reverb(15, 1.4)})
add("Swell Horns", "Brass", "Evolving", "Horns that grow: a slow filter swell after each attack, and aftertouch "
    "pushes them to a roar; the wheel adds vibrato", {
        **SECTION, "flt_cut": hz(300), "lad_env": 55, **fenv(900, 1500, 70, 700), **amp(120, 900, 90, 700),
        **mx(vib(2), (AT, "Ladder Cutoff", 25), (AT, "Amp Level", 18), (PX, "Ladder Res", 30), (PY, "Send 2 (FX2)", 30)),
        **reverb(22, 2.6, rev_type="Hall", rev_size=60)})
add("Sync Brass", "Brass", "Aggressive", "Brass with a synced oscillator swept by the envelope: a ripping, "
    "metallic attack; aftertouch moves the sync", {
        **SECTION, "o1_level": 60, "o2_sync": "On", "o2_freq": 4, **menv(10, 400, 20, 300),
        **mx(("Mod Env", "Osc 2 Pitch", 20), (AT, "Osc 2 Pitch", 15), (AT, "Ladder Cutoff", 15), vib(2),
             (PY, "Send 2 (FX2)", 30)),
        **reverb(15, 1.6)})
add("Cup Mute Horn", "Brass", "Warm", "A muted, nasal horn: saws through a band-pass in series with the ladder; "
    "aftertouch opens the mute", {
        **SECTION, **both("o1", "o2", "o3"), "svf_mode": "BP", "svf_cut": hz(1100), "svf_res": 35, "svf_kbd": 60,
        "flt_route": "Series", "flt_cut": hz(900),
        **mx(vib(2), (AT, "SVF Cutoff", 25), (AT, "Ladder Cutoff", 15), (PX, "SVF Res", 30), (PY, "Send 2 (FX2)", 30)),
        **reverb(18, 1.6, rev_type="Room")})
add("Pulse Width Brass", "Brass", "Warm", "Brass made of moving pulse waves: softer and rounder than saws, with a "
    "slow width sweep; the wheel deepens the sweep", {
        **SECTION, "o1_mix": 100, "o1_pw": 35, "o2_mix": 100, "o2_pw": 60, "o3_level": 0, "pw_lfo3": 25,
        "lfo3_rate": lfo(0.7),
        **mx((MW, "PW LFO3 Amt", 30), (AT, "Ladder Cutoff", 22), vib(2, AT), (PY, "Send 2 (FX2)", 30)),
        **chorus(0.3, 8, 30, 25), **reverb(18, 1.8)})
add("Low Tuba Section", "Brass", "Dark", "Low, dark brass an octave down: thick and round under a band; aftertouch "
    "opens it up", {
        **SECTION, "o1_oct": "16'", "o2_oct": "16'", "o3_oct": "8'", "flt_cut": hz(280), "lad_env": 40,
        **mx(vib(2), (AT, "Ladder Cutoff", 25), (PX, "Ladder Res", 30), (PY, "Send 2 (FX2)", 25)),
        **reverb(15, 1.8)})
add("Scoop Horns", "Brass", "Bright", "Horns that scoop up into each note (a pitch envelope from below); velocity "
    "deepens the scoop, the wheel adds vibrato", {
        **SECTION, **menv(1, 120, 0, 80), "pitch_env": -4,
        **mx((VEL, "Pitch EG Amt", -4), vib(2), (AT, "Ladder Cutoff", 20), (PY, "Send 2 (FX2)", 30)),
        **reverb(18, 1.8)})
add("Overdrive Brass", "Brass", "Dirty", "Brass through a driven ladder and a saturator: rude and raspy; pad X "
    "turns the drive up", {
        **SECTION, "flt_drive": 60, "flt_res": 20,
        **mx((AT, "Ladder Cutoff", 20), (PX, "Synth FX Drive", 35), vib(2), (PY, "Ladder Res", 30)),
        "modfx_on": "On", "modfx_type": "Density", "modfx_drive": 45, "modfx_mix": 100, **reverb(12, 1.2, rev_type="Room")})
add("Synth Fanfare", "Brass", "Bright", "A bright fanfare: saws in octaves into a big hall, made for big chords; "
    "aftertouch brightens, the wheel adds vibrato", {
        **SECTION, "o3_oct": "4'", "o3_level": 50, "flt_cut": hz(700), "lad_env": 40,
        **mx(vib(2), (AT, "Ladder Cutoff", 20), (AT, "Amp Level", 10), (PY, "Send 2 (FX2)", 30)),
        **reverb(25, 3.0, rev_type="Hall", rev_size=70, rev_pre=20)})
add("Velvet Flugel", "Brass", "Soft", "A soft, breathy flugel-like horn: triangles and a saw with a little noise; "
    "aftertouch blows harder", {
        **SECTION, "o1_wave": "Tri", "o1_angle": 50, "o3_level": 0, "noise_level": 8, "flt_cut": hz(400),
        "lad_env": 35, **amp(50, 700, 85, 400),
        **mx(vib(2), (AT, "Ladder Cutoff", 22), (AT, "Amp Level", 15), (PY, "Send 2 (FX2)", 30)),
        **chorus(0.3, 8, 25, 20), **reverb(22, 2.2)})
add("Brass Pad Swell", "Brass", "Evolving", "A brass pad that keeps breathing: a slow LFO on the filter, long swells; "
    "the wheel speeds the breathing", {
        **SECTION, "flt_cut": hz(350), "lad_env": 35, "lad_lfo": 15, "lfo2_rate": lfo(0.18), "lfo2_wave": "Sine",
        **fenv(600, 1500, 60, 900), **amp(400, 1200, 90, 1000),
        **mx((MW, "LFO 2 Rate (st)", 50), (AT, "Ladder Cutoff", 25), (AT, "Amp Level", 12), (PY, "Send 2 (FX2)", 30)),
        **chorus(0.25, 9, 30, 25), **reverb(25, 3.0, rev_type="Hall", rev_size=65)})
add("Brass Over Low Horns", "Brass", "Warm", "Layer: a bright section (Synth 1) over soft low horns an octave "
    "down (Synth 2) for a full, warm stack", multi({
        **SECTION, **mx(vib(2), (AT, "Ladder Cutoff", 20)), "mfx_send2": 20}, {
        **SECTION, "o1_oct": "16'", "o2_oct": "16'", "o3_level": 0, "flt_cut": hz(300), "lad_env": 30,
        **amp(80, 700, 85, 450), "volume": 50, **mx(vib(2), (AT, "Amp Level", 15)), "mfx_send2": 20},
        **reverb(0, 2.0)), "Layer")
add("Bass Under Brass", "Brass", "Aggressive", "Split: a plucky bass below C3 (Synth 1), brass stabs from C3 up "
    "(Synth 2)", multi({
        "o2_level": 70, "o2_oct": "16'", "o2_mix": 100, "flt_cut": hz(280), "flt_res": 35, "lad_env": 45,
        "lad_kbd": 50, **fenv(1, 250, 15, 100), **amp(1, 400, 75, 100), **mono(), "key_hi": 47,
        **mx((AT, "Ladder Cutoff", 20), (MW, "Ladder Res", 20))}, {
        **SECTION, "lad_env": 50, **fenv(10, 250, 30, 150), **amp(5, 400, 50, 180), "key_lo": 48,
        **mx((AT, "Ladder Cutoff", 20), (MW, "Amp Env Decay", 25)), "mfx_send2": 15}, **reverb(0, 1.4)), "Split")

# ======================================================================================================= PLUCK (16)
add("Harp Glimmer", "Pluck", "Clean", "A harp-like pluck: a bright attack that softens as it rings; velocity sets "
    "the brightness, pad Y the hall", {
        **TRI, "o1_angle": 35, **tri(2, 40, "4'"), "flt_cut": hz(1200), "flt_res": 10, "lad_env": 40, "lad_kbd": 70,
        **fenv(1, 350, 10, 400), "fenv_vel": 40, **amp(1, 2500, 0, 1200), "voices": "8",
        **mx((PY, "Send 2 (FX2)", 30), (AT, "Ladder Cutoff", 15), (MW, "Amp Env Decay", 20), (PX, "Osc 1 Angle", -25)),
        **reverb(25, 2.8, rev_type="Hall", rev_size=60)})
add("Kalimba Thumb", "Pluck", "Clean", "A kalimba-like thumb piano: a short FM knock at an odd ratio over a soft "
    "tone; velocity sets the knock", {
        **TRI, "o1_level": 0, "o1_oct": "4'", "o1_freq": 2.5, **tri(2, 100), "fm_route": "1 > 2", "fm_amt": 2,
        **menv(1, 90, 0, 60), "flt_cut": hz(5000), **amp(1, 1200, 0, 600), "voices": "8",
        **mx(("Mod Env", "FM Amount", 30), (VEL, "FM Amount", 12), (MW, "Amp Env Decay", 20), (PX, "Osc 1 Pitch", 10),
             (PY, "Send 2 (FX2)", 30), (AT, "Amp Level", 10)),
        **reverb(20, 1.6, rev_type="Room")})
add("Modal Kalimba", "Pluck", "Warm", "A tiny pluck rings the modal resonator (tuned to each note): a warm, wooden "
    "tine; the wheel changes the resonator's material", {
        "o1_mix": 100, "flt_cut": hz(3000), "lad_env": 30, **fenv(1, 100, 0, 80), **amp(1, 200, 0, 150), "voices": "6",
        "modfx_on": "On", "modfx_type": "Modal Resonator", "modfx_model": "Modal", "modfx_struct": 55,
        "modfx_tone": 45, "modfx_decay": 50, "modfx_pos": 25, "modfx_mix": 75, "modfx_gain": 55,
        **mx((MW, "Synth FX Structure", 25), (PX, "Synth FX Position", 30), (VEL, "Ladder Cutoff", 15),
             (PY, "Send 2 (FX2)", 30)),
        **reverb(18, 1.8)})
add("Nylon Pick", "Pluck", "Warm", "A nylon-string-like pick: a saw through a band-pass body and a soft ladder; "
    "velocity brightens, the wheel rings longer", {
        "o1_mix": 20, **tri(2, 40), **both("o1", "o2"), "svf_mode": "BP", "svf_cut": hz(900), "svf_res": 20,
        "svf_kbd": 70, "flt_mix": 60, "flt_cut": hz(1000), "lad_env": 40, "lad_kbd": 70, **fenv(1, 250, 5, 300),
        "fenv_vel": 45, **amp(1, 1600, 0, 400), "voices": "8",
        **mx((MW, "Amp Env Decay", 20), (AT, "Ladder Cutoff", 15), (PX, "SVF Cutoff", 25), (PY, "Send 2 (FX2)", 30)),
        **reverb(15, 1.4, rev_type="Room")})
add("Snap Poly Pluck", "Pluck", "Bright", "A short, resonant poly pluck: the staple for riffs and chords; velocity "
    "and pad X set the snap", {
        "o2_level": 70, "o2_freq": 0.08, "flt_cut": hz(400), "flt_res": 40, "lad_env": 55, "lad_kbd": 50,
        **fenv(1, 180, 0, 150), "fenv_vel": 40, **amp(1, 400, 0, 250), "voices": "8",
        **mx((PX, "Ladder Res", 30), (MW, "Filter Env Decay", 25), (AT, "Ladder Cutoff", 15), (PY, "Send 1 (FX1)", 30)),
        **delay("1/8", 25, 10, True), **reverb(12, 1.4)})
add("Dotted Echo Pluck", "Pluck", "Bright", "A bright pluck into dotted-eighth echoes that bounce across the stereo "
    "field; pad Y feeds more echo", {
        "o1_mix": 50, "o1_pw": 35, "o2_level": 50, "o2_oct": "4'", "flt_cut": hz(800), "flt_res": 30, "lad_env": 45,
        "lad_kbd": 60, **fenv(1, 220, 0, 200), "fenv_vel": 35, **amp(1, 500, 0, 300), "voices": "8",
        **mx((PY, "Send 1 (FX1)", 35), (MW, "Filter Env Decay", 25), (AT, "Ladder Cutoff", 15), (PX, "Osc 1 PW", 30)),
        **delay("1/8.", 45, 25, True), "dly_offset": 30, **reverb(15, 2.0), "mfx_series": 20})
add("Water Drop Pluck", "Pluck", "Clean", "A sine-like blip with a fast downward pitch drop: drops of water; the "
    "wheel deepens the drop, pad Y adds space", {
        **TRI, "flt_cut": hz(6000), **menv(1, 60, 0, 40), "pitch_env": 12, **amp(1, 600, 0, 300), "voices": "8",
        **mx((MW, "Pitch EG Amt", 15), (VEL, "Pitch EG Amt", 6), (PY, "Send 2 (FX2)", 30), (AT, "Amp Level", 10)),
        **delay("1/8.", 35, 15, True), **reverb(20, 2.2)})
add("Silk Zither", "Pluck", "Bright", "A zither-like pluck: saw and pulse with a slight bend into the note and a "
    "long ring; velocity sets the bend", {
        "o1_mix": 30, "o1_pw": 30, "o2_level": 40, "o2_oct": "4'", **tri(2), "flt_cut": hz(1800), "flt_res": 15,
        "lad_env": 30, "lad_kbd": 70, **fenv(1, 400, 10, 400), **menv(1, 70, 0, 50), "pitch_env": -1,
        **amp(1, 2200, 0, 900), "voices": "8",
        **mx((VEL, "Pitch EG Amt", -2), (AT, "Pitch", 1), (MW, "Amp Env Decay", 20), (PY, "Send 2 (FX2)", 30)),
        **reverb(22, 2.4)})
add("Steel Pluck", "Pluck", "Bright", "A metallic pluck: ring modulation against a high partial, through a bright "
    "filter; pad X tunes the partial", {
        "o1_level": 50, **tri(2, 0, "4'", 4), "ring_level": 50, "flt_cut": hz(1500), "flt_res": 15, "lad_env": 40,
        "lad_kbd": 60, **fenv(1, 300, 0, 300), **amp(1, 1400, 0, 600), "voices": "8",
        **mx((PX, "Osc 2 Pitch", 12), (VEL, "Ring Level", 20), (MW, "Amp Env Decay", 20), (PY, "Send 2 (FX2)", 30)),
        **reverb(18, 1.8)})
add("Soft Felt Pluck", "Pluck", "Soft", "A gentle felted pluck: low-passed triangles with a soft attack edge; "
    "aftertouch brightens a little", {
        **TRI, **tri(2, 60, "4'"), "flt_cut": hz(700), "lad_env": 30, "lad_kbd": 60, **fenv(1, 300, 5, 300),
        "fenv_vel": 30, **amp(3, 1200, 0, 600), "voices": "8",
        **mx((AT, "Ladder Cutoff", 20), (MW, "Amp Env Decay", 20), (PY, "Send 2 (FX2)", 30), (PX, "Osc 2 Level", 40)),
        **chorus(0.3, 8, 30, 25), **reverb(22, 2.2)})
add("Dark Velvet Pluck", "Pluck", "Dark", "A dark, thick pluck an octave down with a muffled filter, for low chords "
    "and moody lines; aftertouch opens it", {
        "o1_oct": "16'", "o2_level": 70, "o2_freq": 0.07, "flt_cut": hz(250), "flt_res": 25, "lad_env": 40,
        "lad_kbd": 50, **fenv(1, 400, 0, 300), **amp(1, 1200, 0, 500), "voices": "8",
        **mx((AT, "Ladder Cutoff", 22), (MW, "Filter Env Decay", 25), (PX, "Ladder Res", 30), (PY, "Send 2 (FX2)", 30)),
        **reverb(18, 2.2)})
add("Strung Air", "Pluck", "Warm", "The MACRO channel's plucked string model through the ladder: a real-feeling "
    "string; the wheel changes its brightness, pad X its damping", {
        "o1_level": 0, "mac_model": "String", "mac_level": 100, "mac_harm": 30, "mac_timbre": 60, "mac_morph": 60,
        "flt_cut": hz(6000), **amp(1, 2500, 0, 1000), "voices": "6",
        **mx((MW, "Macro Timbre", 25), (PX, "Macro Morph", -30), (VEL, "Macro Timbre", 15), (PY, "Send 2 (FX2)", 30)),
        **delay(280, 20, 8), **reverb(20, 2.0)})
add("Sync Pluck", "Pluck", "Aggressive", "A plucked sync: the synced oscillator's pitch falls with the envelope for "
    "a sharp, zappy pluck; aftertouch deepens the zap", {
        "o1_level": 30, "o2_level": 90, "o2_sync": "On", "o2_freq": 2, **menv(1, 200, 0, 150), "flt_cut": hz(2500),
        "flt_res": 15, "lad_env": 25, **fenv(1, 300, 0, 200), **amp(1, 600, 0, 300), "voices": "8",
        **mx(("Mod Env", "Osc 2 Pitch", 35), (AT, "Osc 2 Pitch", 15), (VEL, "Osc 2 Pitch", 10), (PY, "Send 1 (FX1)", 30)),
        **delay("1/8.", 30, 12, True), **reverb(12)})
add("Bit Pluck", "Pluck", "Dirty", "A crunchy pluck through the bit crusher; pad X lowers the bits, the wheel the "
    "sample rate", {
        "o1_mix": 60, "o1_pw": 30, "o2_level": 50, "o2_oct": "4'", "flt_cut": hz(1500), "flt_res": 20, "lad_env": 40,
        **fenv(1, 250, 0, 200), **amp(1, 600, 0, 250), "voices": "8",
        "modfx_on": "On", "modfx_type": "Bit Crusher", "modfx_bits": 7, "modfx_srate": logk(12000, 128, 44100),
        "modfx_mix": 60,
        **mx((PX, "Synth FX Bit Depth", -25), (MW, "Synth FX Sample Rate", -20), (VEL, "Ladder Cutoff", 15),
             (PY, "Send 1 (FX1)", 30)),
        **delay("1/16", 30, 12, True)})
add("Grain Pluck Cloud", "Pluck", "Evolving", "Plucks that dissolve into a granular cloud: each note leaves a "
    "shimmering trail; the wheel moves the grains, pad Y the space", {
        **TRI, "o1_angle": 35, "o2_level": 50, "o2_oct": "4'", "flt_cut": hz(1500), "lad_env": 35, "lad_kbd": 60,
        **fenv(1, 300, 0, 300), **amp(1, 900, 0, 600), "voices": "8",
        "modfx_on": "On", "modfx_type": "Granular", "modfx_pos": 40, "modfx_size": 55, "modfx_density": 70,
        "modfx_texture": 60, "modfx_pitch": 12, "modfx_spread": 80, "modfx_fb": 30, "modfx_room": 40, "modfx_mix": 40,
        **mx((MW, "Synth FX Position", 35), (PY, "Send 2 (FX2)", 30), (PX, "Synth FX Texture", 30),
             (AT, "Synth FX Mix", 20)),
        **reverb(20, rev_type="Space", rev_decay=65, rev_size=70, rev_tone=50)})
add("Pluck Over Air", "Pluck", "Soft", "Layer: a soft pluck (Synth 1) over a slow, airy pad (Synth 2) that fades in "
    "behind each chord; aftertouch swells the air", multi({
        **TRI, **tri(2, 50, "4'"), "flt_cut": hz(900), "lad_env": 35, "lad_kbd": 60, **fenv(1, 300, 5, 300),
        **amp(1, 1500, 0, 600), "voices": "8", **mx((MW, "Amp Env Decay", 20)), "mfx_send2": 20}, {
        "o1_mix": 50, "o2_level": 70, "o2_freq": 0.1, "noise_level": 15, "flt_cut": hz(1500), "flt_res": 10,
        **amp(800, 1500, 80, 1500), "voices": "8", "volume": 40, **chorus(0.25, 9, 40, 40), "mfx_send2": 30,
        **mx((AT, "Amp Level", 25))}, **reverb(0, 2.8, rev_type="Hall", rev_size=60)), "Layer")

# ======================================================================================================= ORGAN (12)
STOPS = {   # three triangles at 16', 8' and 4' (flue-pipe-like stops), percussion and click added per preset
    **TRI, "o1_oct": "16'", "o1_level": 60, **tri(2, 90, "8'"), **tri(3, 60, "4'"), "flt_cut": hz(6000),
    "lad_kbd": 40, **amp(3, 300, 100, 60), "voices": "8", "lfo4_rate": lfo(0.8), "lfo4_wave": "Sine"}
ROTARY = {"modfx_on": "On", "modfx_type": "Stereo Chorus", "modfx_rate": fxrate(0.8), "modfx_depth": 45,
          "modfx_mix": 45, "modfx_spread": 90}
CLICK = {"noise_level": 12, "noise_env": "On", "noise_a": ms(1), "noise_r": ms(12), "noise_sus": "Off"}
add("Full Stops Organ", "Organ", "Bright", "A full organ: 16', 8' and 4' stops plus a 2' sparkle, key click and a "
    "rotary-style chorus; the wheel speeds the rotor, pad X adds the 2'", {
        **STOPS, **CLICK, "o1_level": 70, "o3_oct": "2'", "o3_level": 50, **ROTARY,
        **mx((MW, "Synth FX Rate", 28), (MW, "Synth FX Depth", 15), (PX, "Osc 3 Level", 40), (AT, "Amp Level", 10),
             (PY, "Send 2 (FX2)", 30)),
        **reverb(15, 1.6, rev_type="Room")})
add("Soft Flute Stops", "Organ", "Soft", "Gentle 8' and 4' flute stops with a slow chorus: quiet chapel or ballad "
    "organ; aftertouch swells it", {
        **STOPS, "o1_level": 0, "o3_level": 45, "flt_cut": hz(3000), **amp(15, 300, 100, 150), **ROTARY,
        "modfx_rate": fxrate(0.5), "modfx_mix": 35,
        **mx((AT, "Amp Level", 15), (MW, "Synth FX Rate", 28), (PX, "Osc 1 Level", 50), (PY, "Send 2 (FX2)", 30)),
        **reverb(25, 2.6, rev_type="Hall", rev_size=60)})
add("Percussive Click Organ", "Organ", "Bright", "A percussive jazz-style organ: a third-harmonic ping decays on "
    "each note, with key click; the wheel speeds the rotor, pad X lengthens the ping", {
        **STOPS, **CLICK, "o1_level": 55, **tri(3, 0, "4'", 7), **menv(1, 220, 0, 60), **ROTARY,
        **mx(("Mod Env", "Osc 3 Level", 70), (MW, "Synth FX Rate", 28), (PX, "Mod Env Decay", 25), (AT, "Amp Level", 10),
             (PY, "Send 2 (FX2)", 25)),
        **reverb(12, 1.4, rev_type="Room")})
add("Burnt Wheel Organ", "Organ", "Dirty", "An overdriven rock organ: the stops into a hot ladder and a saturator, "
    "then the rotor; aftertouch and pad Y drive it harder", {
        **STOPS, **CLICK, "o1_level": 70, "o3_level": 70, "flt_cut": hz(5000), "flt_drive": 55,
        "modfx_on": "On", "modfx_type": "Density", "modfx_drive": 30, "modfx_gain": 35, "modfx_mix": 100,
        "dly_type": "Stereo Chorus", "dly_rate": fxrate(0.8), "dly_depth": 50, "dly_mix": 100, "mfx_send1": 45,
        **mx((AT, "Ladder Drive", 25), (MW, "FX1 Rate L", 28), (MW, "FX1 Rate R", 28), (PY, "Synth FX Drive", 30),
             (PX, "Osc 3 Level", 30)),
        **reverb(10, 1.2, rev_type="Room")})
add("Rotor Swirl Organ", "Organ", "Warm", "The rotor in front: a warm 16' and 8' organ whose chorus and auto-pan "
    "speed up on the wheel, like a rotating speaker ramping", {
        **STOPS, "o3_level": 40, **ROTARY, "modfx_depth": 60, "modfx_mix": 55,
        **mx((MW, "Synth FX Rate", 30), ("LFO 4", "Pan", 25), (MW, "LFO 4 Rate (st)", 60), (AT, "Amp Level", 10),
             (PY, "Send 2 (FX2)", 30), (PX, "Osc 3 Level", 40)),
        **reverb(15, 1.6, rev_type="Room")})
add("Combo Pulse Organ", "Organ", "Bright", "A buzzy combo organ made of pulses, with a quick vibrato on the wheel: "
    "for garage and ska", {
        "o1_mix": 100, "o1_pw": 35, "o2_level": 60, "o2_oct": "4'", "o2_mix": 100, "o2_pw": 45, "flt_cut": hz(3500),
        "flt_res": 10, "lad_kbd": 40, **amp(2, 300, 100, 50), "voices": "8", **lfo1(6.5),
        **mx(vib(2), (AT, "Ladder Cutoff", 15), (PX, "Osc 1 PW", 25), (PY, "Send 2 (FX2)", 25)),
        **reverb(12, 1.2, rev_type="Room")})
add("Cathedral Pipes", "Organ", "Warm", "Big pipes in a long hall: 16', 8' and 4' with a slow speak and a deep "
    "reverb; pad X adds the 2' mixture, aftertouch swells", {
        **STOPS, "o1_level": 70, **amp(60, 300, 100, 900), "an_tune": 25,
        **mx((PX, "Osc 3 Level", 35),(AT, "Amp Level", 12), (MW, "Ladder Cutoff", 15),
             (PY, "Send 2 (FX2)", 30)),
        **reverb(35, 5.0, rev_type="Hall", rev_size=85, rev_pre=30)})
add("Pump Reed Organ", "Organ", "Warm", "A harmonium-like reed organ: narrow pulses with a breathing attack and a "
    "bellows wobble on the wheel", {
        "o1_mix": 100, "o1_pw": 20, "o2_level": 60, "o2_mix": 100, "o2_pw": 30, "o2_oct": "4'", "o2_freq": 0.04,
        "flt_cut": hz(2000), "flt_res": 10, "lad_kbd": 40, **amp(80, 300, 100, 200), "voices": "8",
        "lfo4_rate": lfo(4), "lfo4_wave": "Sine",
        **mx(("LFO 4", "Amp Level", 20, MW), (AT, "Amp Level", 15), (AT, "Ladder Cutoff", 15), (PX, "Osc 1 PW", 20),
             (PY, "Send 2 (FX2)", 30)),
        **reverb(18, 1.6, rev_type="Room")})
add("Dark Pedal Organ", "Organ", "Dark", "Deep 16' and 8' foundation stops, closed and dark, for low chords and "
    "drones; aftertouch opens the swell box", {
        **STOPS, "o1_level": 80, "o3_level": 25, "flt_cut": hz(900), "lad_kbd": 30, **amp(20, 300, 100, 300),
        **mx((AT, "Ladder Cutoff", 25), (MW, "Synth FX Mix", 40), (PY, "Send 2 (FX2)", 30), (PX, "Osc 3 Level", 40)),
        **ROTARY, "modfx_mix": 15, **reverb(20, 2.4, rev_type="Hall", rev_size=60)})
add("Ring Mod Organ", "Organ", "Evolving", "A strange sci-fi organ: the stops with ring modulation that a slow LFO "
    "detunes, so chords shimmer and shift; the wheel deepens it", {
        **STOPS, "o3_level": 40, "ring_level": 25, "ring_src": "2 x 3", "lfo3_rate": lfo(0.12), "lfo3_wave": "Sine",
        **mx(("LFO 3", "Osc 3 Pitch", 1), (MW, "Ring Level", 35), (AT, "Amp Level", 10), (PX, "Osc 3 Pitch", 8),
             (PY, "Send 1 (FX1)", 30)),
        **ROTARY, **delay("1/4.", 35, 12, True), **reverb(20, 2.4)})
add("Lo-Fi Toy Organ", "Organ", "Dirty", "A cheap toy organ: squares, a little crush and a wobbly vibrato; pad X "
    "crushes harder", {
        "o1_mix": 100, "o2_level": 50, "o2_oct": "4'", "o2_mix": 100, "flt_cut": hz(4000), **amp(2, 300, 100, 60),
        "voices": "8", **lfo1(5.0), "modfx_on": "On", "modfx_type": "Bit Crusher", "modfx_bits": 8,
        "modfx_srate": logk(10000, 128, 44100), "modfx_mix": 45,
        **mx(vib(2), (PX, "Synth FX Mix", 35), ("LFO 1", "Pitch", 1), (AT, "Ladder Cutoff", -20), (PY, "Send 2 (FX2)", 25)),
        **reverb(12, 1.0, rev_type="Room")})
add("Pedals Under Stops", "Organ", "Warm", "Split: deep 16' pedal stops below C3 (Synth 1) and a full rotary organ "
    "above (Synth 2)", multi({
        **STOPS, "o1_level": 80, "o2_level": 60, "o3_level": 0, "flt_cut": hz(1500), "voices": "4", "key_hi": 47,
        **mx((AT, "Ladder Cutoff", 20))}, {
        **STOPS, **CLICK, "o3_oct": "2'", "o3_level": 40, **ROTARY, "key_lo": 48,
        **mx((MW, "Synth FX Rate", 28), (PX, "Osc 3 Level", 40)), "mfx_send2": 15},
        **reverb(0, 1.6, rev_type="Room")), "Split")


# LEVEL: each preset's volume trim in dB (measured by test/bank_check.c, offline_check banka), so the bank plays at one
# loudness. Applied to every Synth the preset uses: VOLUME first; past 100% the rest comes from a matrix row, Constant
# into Amp Level (up to x2), in the first free slot.
LEVEL = {
    "Floor Sub": 2.8,
    "Round Ladder Bass": -7.3,
    "Velvet Thump": 4.6,
    "Squelch Spiral": -2.3,
    "Rubber Square Acid": -14.6,
    "Bubble Acid": 9.0,
    "Knock FM Bass": -0.6,
    "Steel Wire Bass": 1.5,
    "Rubber Filter FM": 0.2,
    "Reese Tunnel": -8.6,
    "Reese Grind": -11.4,
    "Notch Wobble": -6.8,
    "Ember Pluck Bass": -1.1,
    "Muted Pick Bass": 7.7,
    "Sync Bite Bass": -2.4,
    "Sync Growler": -15.2,
    "Ratchet Drive Seq": -6.0,
    "Octave Runner": 9.0,
    "Hollow Square Bass": -4.2,
    "Fuzz Pedal Bass": -19.0,
    "Deep Notch Bass": -4.7,
    "Slap Pop Bass": 4.1,
    "Fifth Glider": -0.7,
    "Ring Rumble": 2.3,
    "Wooden Upright": 4.3,
    "Vowel Bass": 3.0,
    "Stack Mono Brute": -11.4,
    "Crushed Sub": -1.7,
    "Triplet Roller": 1.8,
    "Bass Under Tines": -3.8,
    "Sub And Snarl": -7.5,
    "Bounce Ladder Bass": 2.1,
    "Silk Glide Lead": -4.6,
    "Ladder Cry Solo": -2.0,
    "Sync Lightning": -1.8,
    "Seven Saw Choir": -3.1,
    "Stacked Horizon": 6.2,
    "Reed PWM Lead": -4.2,
    "Twin Pulse Lead": -9.0,
    "Filter FM Howl": -1.7,
    "SVF FM Scream": -2.0,
    "Breath Flute": 0.7,
    "Ribbon Theremin": -2.0,
    "Pressure Horn Lead": -3.2,
    "Glass Sine Lead": -2.7,
    "Ring Bell Lead": 4.8,
    "Pocket Chip Lead": -4.2,
    "Dirty Tape Solo": -3.8,
    "Formant Talker": -4.2,
    "Canyon Echo Lead": 3.9,
    "Fifth Stack Lead": -3.4,
    "Resonant Shriek": -5.5,
    "Soft Pipe Lead": -2.0,
    "Wah Pedal Lead": -8.1,
    "Noise Edge Lead": -0.6,
    "Detuned Pair Lead": -2.7,
    "Cosmic Portamento": 4.0,
    "Angle Morph Lead": -3.9,
    "Bass Left Solo Right": -0.9,
    "Lead Over Haze": -3.3,
    "Octave Twin Lead": -5.2,
    "Hollow Sync Flute": -3.2,
    "Tine Electric": -6.2,
    "Barking Tines": -15.3,
    "Reed Buzz Piano": -5.3,
    "Funk Plectrum Keys": -6.2,
    "Wah Plectrum Keys": -12.1,
    "Glass FM Keys": -6.5,
    "Crystal Mallets": -4.7,
    "Toy Box Piano": -7.7,
    "Music Box Tines": -7.1,
    "Felt Upright Keys": -4.2,
    "Dusty Tape Keys": -8.8,
    "Vibe Bars": -6.9,
    "Quill Keys": -3.4,
    "Velvet Poly Keys": -7.6,
    "House Chord Stab": 0.5,
    "Digital Bell Keys": -2.0,
    "Warm Wood Keys": -5.4,
    "Drifting Tines": -7.7,
    "Tines Over Haze": -9.5,
    "Tines And Glass": -8.9,
    "Bold Section": -10.5,
    "Soft Horn Bed": -6.6,
    "Blat Stab": -9.1,
    "Funk Horn Hits": -8.4,
    "Swell Horns": -9.1,
    "Sync Brass": -8.5,
    "Cup Mute Horn": -12.6,
    "Pulse Width Brass": -11.2,
    "Low Tuba Section": -10.5,
    "Scoop Horns": -10.3,
    "Overdrive Brass": -16.5,
    "Synth Fanfare": -9.2,
    "Velvet Flugel": -7.9,
    "Brass Pad Swell": -6.6,
    "Brass Over Low Horns": -13.1,
    "Bass Under Brass": -9.3,
    "Harp Glimmer": -6.0,
    "Kalimba Thumb": -3.5,
    "Modal Kalimba": -11.7,
    "Nylon Pick": 4.2,
    "Snap Poly Pluck": -2.1,
    "Dotted Echo Pluck": 5.0,
    "Water Drop Pluck": -1.3,
    "Silk Zither": -0.3,
    "Steel Pluck": 0.7,
    "Soft Felt Pluck": -2.8,
    "Dark Velvet Pluck": -6.0,
    "Strung Air": 9.0,
    "Sync Pluck": -0.4,
    "Bit Pluck": 4.2,
    "Grain Pluck Cloud": 1.0,
    "Pluck Over Air": -5.5,
    "Full Stops Organ": -8.1,
    "Soft Flute Stops": -6.8,
    "Percussive Click Organ": -7.0,
    "Burnt Wheel Organ": -15.7,
    "Rotor Swirl Organ": -7.8,
    "Combo Pulse Organ": -13.6,
    "Cathedral Pipes": -12.7,
    "Pump Reed Organ": -11.4,
    "Dark Pedal Organ": -9.4,
    "Ring Mod Organ": -8.0,
    "Lo-Fi Toy Organ": -13.8,
    "Pedals Under Stops": -9.5,
}


def _level():
    for name, values, tags in BANK:
        g = 10 ** (LEVEL.get(name, 0.0) / 20)
        for pre in ("", "s2_", "s3_"):
            if pre and values.get(pre + "syn_on") != "On":
                continue
            v = values.get(pre + "volume", 70) * g
            if v > 100:
                boost = v / 100 - 1
                if boost > 1:
                    raise SystemExit("bank_a: %s needs %.0f%% volume (more than 200%%)" % (name, v))
                n = next(n for n in range(1, 17) if values.get("%smod%d_src" % (pre, n), "Off") == "Off")
                values.update({"%smod%d_src" % (pre, n): "Constant", "%smod%d_dst" % (pre, n): "Amp Level",
                               "%smod%d_amt" % (pre, n): round(100 * boost, 1), "%smod%d_via" % (pre, n): "None"})
                v = 100
            values[pre + "volume"] = round(v, 1)


_level()
assert len(BANK) == 128, len(BANK)
