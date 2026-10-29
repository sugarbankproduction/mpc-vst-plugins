# ONE-ish settings: the SETTINGS page

Added 2026-10-29, tested offline (`test/midi_check.c` through `test/run.sh`) and in the desktop window (`load_test.exe`),
not yet by the owner in Carla. After the Moog One manual's SETTINGS screen (p124-139). Code: `desktop/settings.c`
(the page and the file), `src/midi_in.h` and `src/midi_in_run.h` (the engine's side).

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

**LIBRARY**: where your patches and the settings file are (with buttons that open the folders), how many patches
there are, the version and the build date.

**TUNING**: MASTER FREQUENCY (400-480 Hz) and MASTER FINE TUNE (+-100 cents). These two stay part of the sound, as they
always were (shared by its three Synths), so old sounds keep their tuning.

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

Unknown lines are skipped and values out of range are clamped, so a file from a newer or older ONE-ish still loads.
It is read once when the first ONE-ish starts in a program and written after every change (to `Settings.txt.tmp`,
then over the old file), only on the window's thread.

## Engine side

The window sends the MIDI settings to each instance as a command through the chunk call (as the step editor's):
`midicfg\nrx_clock=1;tempo=120;sustain=64;exp1=11;exp2=4` (any subset). The engine keeps them in the instance
(`midi_in_t`), not in any state; `get_param("midicfg")` reads them back and `get_param("bpm")` gives the tempo the Synths
follow (tests). Without the window (the MPC build) the defaults apply, which are exactly the behaviour from before.
`midi()` only maps the pedals' numbers; nothing allocates, locks or touches a file.
