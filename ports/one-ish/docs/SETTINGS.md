# ONE-ish settings: the SETTINGS page

Added 2026-10-29, tested offline (`test/midi_check.c` through `test/run.sh`) and in the desktop window (`load_test.exe`),
not yet by the owner in Carla. After the Moog One manual's SETTINGS screen (p124-139). Code: `desktop/settings.c`
(the page and the file), `src/midi_in.h` and `src/midi_in_run.h` (the engine's side: settings, MIDI learn, NRPN),
`desktop/scala.c` and `src/tuning.h` (microtuning).

## What it is

The **SETTINGS** button on the centre console (next to SETUP) opens the page on the screen. Settings here belong to
the instrument, not to a sound: loading a preset, a patch or a project never changes them, and they are the same for
every ONE-ish you open. Click a value (or scroll over it, Shift for finer steps) to change it; it is saved at once.

**GLOBAL**

| Setting | What it does | Default |
|---|---|---|
| PRESET AUTOLOAD | On: the PRESET knob, PREV and NEXT load each preset or patch as they step. Off: they only point at one (the name line shows `> F 004  Name ?`); click the PRESET knob or the name to load it. | On |
| RECEIVE MIDI CLOCK | On: the host's tempo and song position drive synced LFOs, envelopes, effects, the arp and the sequencer. Off: both are ignored; everything runs free at TEMPO. | On |
| TEMPO (CLOCK OFF) | The tempo ONE-ish keeps itself while RECEIVE MIDI CLOCK is off, 10-280 BPM. | 120 BPM |
| MIDI I/O FROM PRESET | On: a preset or patch you load brings its MIDI channels (MIDI IN (ALL) and each Synth's channel). Off: the channels you set stay. A project always restores everything. | On |
| SUSTAIN PEDAL CC | The CC number of your sustain pedal. | 64 |
| EXP 1 PEDAL CC | The CC number the mod matrix reads as Exp 1 (its list still says "Exp 1 (CC 11)", the default). | 11 |
| EXP 2 PEDAL CC | The CC number the mod matrix reads as Exp 2 ("Exp 2 (CC 4)"). | 4 |
| RESTORE DEFAULT SETTINGS | Every setting back to the defaults (asks first). Patches and sounds are not touched. | |

A pedal moved to another number no longer answers to its old one. A pedal can't take a number another pedal has, or
one ONE-ish reads for something else (0, 1, 2, 3, 6, 16, 17, 18, 32, 38, 66, 96-101): those are skipped.

**MIDI** (MIDI LEARN, manual p144-148)

1. Click **LEARN** (it becomes CANCEL).
2. Click the control on the panel you want to move (a knob, a button, a value on a MORE page). It belongs to the
   Synth in focus: pick SYNTH 1/2/3 first for another Synth's.
3. Move a knob or fader on your controller. Its CC now moves that control. A controller that sends the fine part
   too (the CC's LSB, CC + 32, right after it) is found to be **14-bit** by itself.

The list shows every mapping: the CC, the control, **7-BIT / 14-BIT** (14-bit only for CC 0-31) and **DELETE**;
**CLEAR ALL** deletes them all. Learning a CC or a control again replaces its old mapping. A mapped CC works on any
MIDI channel and moves the control as the host's automation would (the host sees the move). Reserved CCs can't be
learned: 0-3, 6, 16-18, 32, 38, 66, 96-101, 120-127 (bank select, mod wheel, X/Y pad, data entry, ribbon, sostenuto,
NRPN / RPN, channel mode) and the pedals' three numbers; moving a pedal onto a learned CC deletes that mapping. The map
is a setting like the others: the manual keeps it with the global settings, so it is the same for every sound.

**NRPN input** (the manual's table, p149-158) always works, for all three Synths, with nothing to set: CC 99 and 98
choose the number, CC 6 and 38 send the 14-bit value (0-16383). Synth 2's numbers are Synth 1's plus 3840, Synth 3's
plus 7680; NRPN 1 (MASTER VOLUME) is the shared master level. Option values follow the manual's bands (for example
LFO WAVEFORM: 0-4095 triangle, 4096-8191 pulse (square), 8192-12287 saw, 12288-16383 S&H). Free rates and times are
taken only while the LFO or envelope is not synced, synced ones only while it is. The oscillators' SAW and TRIANGLE
numbers both set ONE-ish's one WAVE ANGLE; POLYPHONY MONO on is 1 voice, off goes back to the default voice count;
HEADPHONES (2) has no plugin meaning. ONE-ish's own NRPN replaces the plugin wrapper's generic one (NRPN n = parameter
n), which is switched off for ONE-ish (`vst.json` `"nrpn": false`).

**LIBRARY**: where your patches and the settings file are (with buttons that open the folders), how many patches
there are, the version and the build date.

**TUNING**: MASTER FREQUENCY (400-480 Hz) and MASTER FINE TUNE (+-100 cents). These two stay part of the sound, as they
always were (shared by its three Synths), so old sounds keep their tuning.

Below them, **microtuning** for the Synth in focus (beyond the Moog One manual; audit package 16):

- **SCALE: LOAD .SCL** picks a Scala scale file (the format of the free Scala program; thousands of scales are
  published in it). **CLEAR** goes back to the usual 12-tone tuning.
- **KEYBOARD MAP: LOAD .KBM** picks a Scala keyboard map: which key plays the scale's first degree, which key has which
  frequency (the reference), and which keys play which degree or are left out (silent). Without one, key 60 (middle C)
  plays the first degree at its usual pitch and every key up or down the next degree.
- The scale and map belong to the sound: they are saved with the project, in patch files (`tun_scl=` / `tun_kbm=`
  lines), and go with SAVE S1/2/3 and INTO S1/2/3. Each Synth has its own. Master tune and fine tune still move
  everything. A sound without a scale (every sound made before) plays exactly as before.
- While gliding, the pitch moves smoothly between the two keys' tuned pitches. Pitch tracking (an oscillator's TRACK)
  follows the tuned pitches; the filters' keyboard tracking and the matrix's Keyboard source still count keys.
- Loading a scale is not a parameter move, so a host may not mark the project as changed; save it after loading one.

Tested offline (`test/tuning_check.c` with the files in `test/scales/`): a 12-tone equal scale gives exactly the usual
samples; 19-EDO puts every key a 19th of an octave from key 60 (within 0.001 cent); 5-limit just intonation with a
keyboard map (D as the first degree, A at 440 Hz, G# left out) gives the just pitches and a silent G#. CPU (PC,
2026-10-29, `test/run.sh`'s bench): 8 gliding voices 62.63 us per 128-frame block without a scale, 63.60 with 19-EDO
(+1.5%, about 0.12 us a voice); without a scale nothing changes.

## The file

`%USERPROFILE%\Documents\ONE-ish\Settings.txt` (beside the Patches folder; `%APPDATA%\ONE-ish` when Documents is
missing), or the path in the environment variable `ONEISH_SETTINGS`. Plain text, one `key=value` per line after the
header `ONE-ish settings 1`:

    ONE-ish settings 1
    autoload=1
    rx_clock=1
    tempo=120
    midi_io_preset=1
    sustain_cc=64
    exp1_cc=11
    exp2_cc=4
    map=74,flt_cut,7          a learned CC: its number, the param's key (s2_ / s3_ for Synths 2 and 3), 7 or 14 bits
    map=7,s2_svf_cut,14

Unknown lines are skipped and values out of range are clamped, so a file from a newer or older ONE-ish still loads.
It is read once when the first ONE-ish starts in a program and written after every change (to `Settings.txt.tmp`,
then over the old file), only on the window's thread.

## Engine side

The window sends the MIDI settings to each instance as a command through the chunk call (as the step editor's):
`midicfg\nrx_clock=1;tempo=120;sustain=64;exp1=11;exp2=4;map=74:flt_cut:7,7:s2_svf_cut:14` (any subset; `map=`
replaces the whole map). The engine keeps them in the instance (`midi_in_t`: the map as a fixed 128-entry table, the
NRPN selection per channel), not in any state; `get_param("midicfg")`, `"midimap"`, `"nrpn_table"` read them back and
`get_param("bpm")` gives the tempo the Synths follow (tests). Without the window (the MPC build) the defaults apply,
which are exactly the behaviour from before, plus NRPN input. In `midi()`: NRPN, then a learned CC, then the pedals'
numbers; a param set from MIDI is reported through `mpc_engine_changed`. Nothing allocates, locks or touches a file
there; the table changes only through the command (the wrapper's engine lock, from the window's thread).

Microtuning: the window reads the file (`desktop/scala.c`) and sends a compact text as `tunscl<n>\n<name>|<count>,<cents
...>` or `tunkbm<n>\n<name>|<size>,<first>,<last>,<middle>,<ref>,<freq>,<octave>,<map ...>` (`tunclear<n>` both off).
The engine (`src/tuning.h`) works out a 128-key pitch table from it, outside the audio path, and keeps the texts in
the state as `tun_scl=` / `tun_kbm=` (`s2_` / `s3_` for Synths 2 and 3); `get_param("tun_pitch<key>")` reads a key's
pitch (tests).

MIDI LEARN listens to the host's MIDI in the window's layer (`settings_midi_seen`, on the host's thread: one atomic word
with the last CC, a count and the 14-bit mark); the window's timer picks it up 250 ms after it came, so the LSB can
follow.
