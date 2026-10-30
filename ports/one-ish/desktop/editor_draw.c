/* Drawing for the desktop editor (see editor.c): the panels and their MORE triangles, knobs, LED lists, buttons, the
 * oscillators' wave screens, the GENERATIVE section's Euclidean ring, the centre console's screen (mod matrix, MORE
 * pages), the left-hand controller, the ribbon and the keyboard. All drawn from scratch: GDI for text, GDI+ (gfx.h) for
 * anti-aliased shapes, skin.c for the materials and the 3D knobs, buttons, LEDs, keys and wheels. UI thread only. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "editor.h"
#include "gfx.h"
#include "skin.h"
#include "../src/randomize.h"
#pragma GCC diagnostic push   /* only euclid() is used here; its neighbours' tables would warn as unused */
#pragma GCC diagnostic ignored "-Wunused-const-variable"
#pragma GCC diagnostic ignored "-Wunused-function"
#include "../src/seq.h"
#pragma GCC diagnostic pop

/* ---- help text: what the control under the mouse does (a key, or "_suffix" for every key ending in it) ---- */
static const char *help_for(int i) {
    static const struct { const char *key, *text; } H[] = {
        {"_fm_src", "Which oscillator frequency-modulates this filter's cutoff (with FM AMT)."},
        {"_src", "Mod source: what moves the destination (envelopes, LFOs, velocity, wheels, aftertouch, the key played, random values, expression pedals, the ribbon, the X/Y pad, a constant)."},
        {"_dst", "Mod destination: what the source moves. Pitch in semitones, cutoffs in octaves, levels and mix in % of their range."},
        {"_via_amt", "Via amount: how much the via source scales this row (100% = fully; minus turns it upside down)."},
        {"_xf_amt", "Transform amount: how strongly the transform reshapes the source (0% = not at all)."},
        {"_xf", "Transform: reshapes the source before it moves the destination. Limit Low/High clip one side; Squared and Cubed bend it into a curve."},
        {"_amt", "Mod amount: how far and which way (drag left/right; minus turns the source upside down). The middle is 0 (shortcut: double-click)."},
        {"_via", "Mod via: scales this row by a second source. LFO 1 -> Pitch via Mod Wheel = vibrato that the wheel brings in."},
        {"_svf", "Sends this mixer channel to the state-variable filter. With the Ladder button too it feeds both; with neither it is silent."},
        {"_lad", "Sends this mixer channel to the ladder filter. With the SVF button too it feeds both; with neither it is silent."},
#define OSC_HELP(n) \
        {"o" #n "_wave", "Tri: Wave Angle shapes a triangle (left: saw-like, centre: triangle, right: ramp). Saw: Wave Angle slows the saw's drop for a darker tone."}, \
        {"o" #n "_mix", "Blends this oscillator's two waves: 0% the triangle/saw only, 100% the pulse only. The little screen shows the result."}
        OSC_HELP(1), OSC_HELP(2), OSC_HELP(3),
#undef OSC_HELP
        {"_oct", "Octave: the oscillator's footage, 32' (two octaves down) to 2' (two up). Click to step, right-click for the list."},
        {"_angle", "Wave Angle: the shape of the triangle/saw wave (see the Wave button)."},
        {"_pw", "Pulse width: 50% is a square (hollow); narrower or wider sounds thinner and nasal."},
        {"_freq", "Tunes this oscillator up or down by up to 7 semitones, smoothly (7 = a fifth)."},
        {"_beat", "Detunes by a fixed number of Hz, so the beating stays at the same speed up and down the keyboard."},
        {"_track", "Pitch tracking: how much this oscillator follows the keys (100% = normal, 0% = the same pitch on every key)."},
        {"_bend_up_off", "Bend up offset: added to the bend range for this oscillator only, so the oscillators can bend by different amounts."},
        {"_bend_dn_off", "Bend down offset: added to the bend range for this oscillator only."},
        {"_sync_src", "Which oscillator restarts this one when Hard Sync is on."},
        {"angle_lfo3", "LFO 3 moves the wave angle of all three oscillators: a moving, animated tone."},
        {"pw_lfo3", "LFO 3 moves the pulse width of all three oscillators (pulse-width modulation)."},
        {"ring_src", "Which two oscillators the ring modulator multiplies (their triangle/saw waves): metallic, bell-like tones."},
        {"fm_amt", "Linear FM: one oscillator wobbles another's frequency at audio rate, for bells, brass and clangs."},
        {"fm_route", "Which oscillator modulates which: 1 > 2, 1 > 3 or 3 > 1. The modulator can be silent in the mixer."},
        {"noise_a", "Noise envelope attack (with ENV on): how fast the noise fades in at each note."},
        {"noise_r", "Noise envelope release (with ENV on): how fast the noise fades out (at once if Sustain is off)."},
        {"noise_sus", "Noise sustain: on, the noise holds while the key is down; off, it fades out after the attack (a chiff)."},
        {"noise_env", "Noise envelope: on, the noise gets its own attack/release; off, it follows the amplifier envelope."},
        {"noise_color_mix", "Colour mix: blends the two noise colours picked with COLOR."},
        {"noise_color_pair", "Noise colours: red (dark), white (bright), purple (very bright). COLOR MIX blends the pair."},
        {"flt_route", "Parallel: the two filters side by side. Series: the SVF feeds the ladder (send sources to the SVF, mix to Ladder)."},
        {"flt_mix", "Balance between the two filters' outputs: 0% all SVF, 100% all ladder."},
        {"_kbd", "Keyboard tracking: how much the cutoff follows the keys (100% = an octave per octave, around middle C)."},
        {"_timing", "Sync timing: with Sync on, the LFO's tempo division straight, dotted or triplet."},
        {"_sync", "Sync: for an oscillator, restart with another oscillator (hard sync); for an LFO, an effect or the delay, lock to the host tempo."},
        {"_range", "LFO range: Slow for long sweeps, Low and Medium for faster wobbles."},
        {"_var", "Variation: each cycle's rate varies a little at random, like an analog LFO."},
        {"_polarity", "Bipolar swings above and below; Unipolar only upwards from 0."},
        {"_phase", "Start phase: where in its cycle the LFO starts when it is reset."},
        {"_delay", "Delay: how long after the note this starts."},
        {"_fade_in", "Fade in: how long the LFO takes to reach full depth after its delay."},
        {"_fade_out", "Fade out: how long the LFO takes to die away (0 = never)."},
        {"_repeats", "Repeats: how many cycles the LFO runs after each reset (Inf = keeps running)."},
        {"_smooth", "Smoothing: rounds off the LFO's steps and corners (S&H becomes a smooth random wander)."},
        {"flt_res", "Ladder resonance: emphasis at the cutoff. Near the top the filter sings on its own as a sine (self-oscillation)."},
        {"flt_drive", "Drive: pushes the ladder's input into saturation for a thicker, growling tone."},
        {"flt_slope", "How steeply the ladder cuts: 24 dB per octave is the classic; 6 dB is gentle."},
        {"voices", "How many notes can sound at once. Mono plays one, legato, last key wins."},
        {"hold", "Hold: notes keep sounding after you let go, until Hold is switched off."},
        {"note_priority", "Mono note priority: with several keys held in Mono, which one sounds (lowest, highest or the last pressed)."},
        {"steal_mode", "Voice stealing: when all voices are busy, which note makes way for a new one."},
        {"bend_up", "How far the pitch wheel bends up, in semitones."},
        {"bend_down", "How far the pitch wheel bends down, in semitones."},
        {"vel_curve", "Velocity curve: how hard you must play for loud notes (Soft: little effort; Hard: a firm touch)."},
        {"transpose", "Transposes everything you play, in semitones."},
        {"octave_shift", "Shifts everything you play by octaves (the keyboard's OCTAVE buttons only move the on-screen keys)."},
        {"key_center", "Key tracking centre: the note around which key tracking turns cutoffs and envelopes neither up nor down."},
        {"master_tune", "Master tune: the pitch of A, 440 Hz as standard."},
        {"fine_tune", "Fine tune, in cents."},
        {"modfx_type", "The synth effect: delays, flangers, choruses, phasers, a bit crusher, resonators, granular, tape and console colour. Its MORE page shows its own controls."},
        {"modfx_ctrl1", "CTRL 1: moves the effect's most useful control (or the one set as its target on the MORE page), plus or minus."},
        {"modfx_ctrl2", "CTRL 2: moves a second effect control (its target is on the MORE page)."},
        {"modfx_c1_dst", "CTRL 1 target: the effect control CTRL 1 moves (Auto: the most useful one for the type)."},
        {"modfx_c2_dst", "CTRL 2 target: the effect control CTRL 2 moves (Auto: a second useful one for the type)."},
        {"modfx_gain", "Wet gain: the level of the effect's output (50% = as it comes)."},
        {"mfx_series", "FX 1 > FX 2: how much of master effect 1 goes on into effect 2 (Dual Mono)."},
        {"mfx_width", "Width of the master effects: 0% mono, 100% normal, 200% extra wide."},
        {"mfx_buss", "Buss mode: Dual Mono runs both master effects (FX 1 on SEND 1, FX 2 on SEND 2); Stereo runs FX 1 alone, with a stereo input."},
        {"dly_type", "Master effect 1's type: a reverb, a stereo or ping-pong delay, or any synth effect type. Its MORE page shows the controls that type uses."},
        {"rev_type", "Master effect 2's type: a reverb, a stereo or ping-pong delay, or any synth effect type. Its MORE page shows the controls that type uses."},
        {"dly_freeze", "Hold (reverbs): Freeze holds the tail and takes no new sound; Infinite never decays but still takes sound; Pitch Freeze (shimmer) holds its pitch loop."},
        {"rev_freeze", "Hold (reverbs): Freeze holds the tail and takes no new sound; Infinite never decays but still takes sound; Pitch Freeze (shimmer) holds its pitch loop."},
        {"pan", "Pan: where the sound sits between left and right."},
        {"spread", "Spread: spreads the voices across the stereo field."},
        {"drift", "Analog drift: slow, slight wandering of pitch and tone, different on every voice."},
        {"ribbon_dst", "What the ribbon moves (its own destination, apart from the mod matrix)."},
        {"ribbon_amt", "How far the ribbon moves its destination."},
        {"ribbon_mode", "Spring: lets go back to the origin when released. Hold: stays where you left it."},
        {"ribbon_origin", "Where the ribbon rests (its zero point)."},
        {"pitch_lfo", "LFO 1 to the pitch of all three oscillators (vibrato). The mod wheel also adds vibrato through matrix row 1."},
        /* the arpeggiator */
        {"arp_on", "Arpeggiator: plays the keys you hold one after another, in time with the host (or at its own FREE RATE)."},
        {"arp_latch", "Latch: the arpeggio keeps playing after you let go. Keys played while others are held add to it; a new chord replaces it."},
        {"arp_pattern", "Pattern: Up plays the held keys lowest first, Down highest first, Order in the order you pressed them, Random shuffled."},
        {"arp_oct", "Octaves: how many octaves the arpeggio climbs through (1 = only the keys you hold)."},
        {"arp_dir", "Octave direction: where the extra octaves go. Up, Down, or Up/Down and Down/Up, which go there and back."},
        {"arp_oct_order", "Octave order: off, every note in one octave before the next (C E G C' E' G'); on, each note through the octaves first (C C' E E' G G')."},
        {"arp_pendulum", "Pendulum: the pattern plays forwards, then backwards (1 2 3 2 1 ...)."},
        {"arp_pend_mode", "Pendulum ends: Once plays the top and bottom notes once (1 2 3 2), Twice repeats them (1 2 3 3 2 1)."},
        {"arp_rnd_mode", "Random mode (Random pattern): Non-Repeating plays each held key once per pass in a new order; Repeating may pick the same key again."},
        {"arp_sync", "Tempo sync: on, the arpeggio and the generative steps follow the host's tempo and song position; off, they run at FREE RATE."},
        {"arp_div", "Rate: the note value of each step, 1/1 (a whole note) to 1/128, at the host's tempo (or FREE RATE with SYNC off). TIMING (MORE page): dotted or triplet."},
        {"arp_timing", "Timing: each step straight, dotted (half as long again) or triplet (two thirds as long)."},
        {"arp_rate", "Free rate: the tempo in BPM while SYNC is off (RATE still sets the note value of each step)."},
        {"arp_gate", "Gate: how long each arpeggio or generative note sounds, as a part of its step (short = staccato)."},
        {"arp_swing", "Swing: moves every second step; 50% is straight, more delays it for a shuffle, less pulls it earlier."},
        {"arp_reset", "Note reset: on, the first key you play (with the host stopped) starts the pattern at once; off, it waits for the next step."},
        /* chord memory */
        {"chord_on", "Chord: every key you play sounds a whole chord, with that key as its root (the SHAPE, or the chord you learned)."},
        {"chord_learn", "Learn (lit while learning): play a chord, lowest note first as the root, then switch LEARN off to keep it as the Custom shape."},
        {"chord_shape", "Shape: the chord each key plays. Custom is the one you learned; the rest are built in (major, minor, sevenths, power ...)."},
        {"chord_iv1", "Chord note: how far above (or below) the root this note of the Custom chord is, in semitones. LEARN fills these in."},
        {"chord_iv2", "Chord note: how far above (or below) the root this note of the Custom chord is, in semitones. LEARN fills these in."},
        {"chord_iv3", "Chord note: how far above (or below) the root this note of the Custom chord is, in semitones. LEARN fills these in."},
        {"chord_iv4", "Chord note: how far above (or below) the root this note of the Custom chord is, in semitones. LEARN fills these in."},
        {"chord_iv5", "Chord note: how far above (or below) the root this note of the Custom chord is, in semitones. LEARN fills these in."},
        /* the generative section */
        {"gen_euclid_on", "Euclid: a rhythm that spreads HITS notes as evenly as it can over STEPS steps (the ring shows it). Plays the held keys, or the arpeggio."},
        {"gen_steps", "Steps: how long the Euclidean pattern is, 1 to 32 steps (the dots around the ring)."},
        {"gen_hits", "Hits: how many of the steps play (the lit dots). 3 of 8 or 5 of 16 give classic grooves."},
        {"gen_rot", "Rotate: turns the Euclidean pattern round by this many steps, so it starts in another place."},
        {"gen_prob", "Chance: the probability that each step plays at all (100% = always). Lower it for patterns that breathe."},
        {"gen_vel_rand", "Velocity random: each generated note's loudness varies at random by up to this much."},
        {"gen_tm_on", "Turing: a looping random melody over the held keys or a SCALE. CHANGE sets how often it alters a note (MORE page)."},
        {"gen_tm_len", "Turing length: how many steps the Turing machine's melody loops over."},
        {"gen_tm_change", "Turing change: how likely each step's note is replaced by a new random one. 0% locks the loop, 100% is new every time."},
        {"gen_scale", "Turing scale: Held Notes picks from the keys you hold; a scale picks degrees from the lowest held key."},
        {"gen_range", "Turing octaves: how many octaves up the Turing melody may wander."},
        /* the bouncing balls and particles (2026-10-29) */
        {"phys_balls_on", "Balls: balls bounce in a box; the floor plays a note of the KEY by where it lands, the ceiling an octave up, the walls the root and the fifth."},
        {"phys_balls", "Balls: how many bounce in the box (1-8)."},
        {"phys_speed", "Speed: how fast the balls and particles move."},
        {"phys_gravity", "Gravity: pulls the balls and particles down. 0: they float in straight lines."},
        {"phys_size", "Box size: a smaller box means more hits, so more notes."},
        {"phys_bounce", "Bounce: how much speed a ball keeps at each wall. Under 100% it bounces lower and faster until it is thrown again."},
        {"phys_parts_on", "Particles: fall from the top of the box; crossing the line, colliding or dying plays a note of the KEY by where."},
        {"phys_rate", "Particle rate: how many particles start each second."},
        {"phys_spread", "Particle spread: 0 sends them straight down, 100% in every direction."},
        {"phys_life", "Particle life: how long each particle lasts."},
        {"phys_attract", "Attraction: pulls the particles to the middle of the box, so they swirl and play more."},
        {"phys_trigger", "Particles play: on crossing the line, on colliding with each other, or when they die."},
        {"phys_keys", "Keys: Free plays whenever it is on; Held Keys only while you hold keys, from the lowest; Key Launch: each key throws in a ball and a burst of particles; Key Emit: a ball per key held, particles while held (faster when played harder)."},
        {"phys_restart", "Key restarts: a key starts the balls and particles again from the start (each key its own start)."},
        {"phys_sync", "Sync: puts each note on the next beat line at the host's tempo (Off: as it hits)."},
        {"phys_len", "Note length: how long each ball or particle note lasts."},
        {"phys_octave", "Low octave: where the lowest note is (with Free keys)."},
        {"phys_range", "Octaves: how many octaves the box spans, left to right."},
        {"phys_max", "Max notes/s: at most this many notes a second (the rest are not played)."},
        {"phys_dyn", "Dynamics: how much a hard hit plays louder than a soft one. 0: every note at the same velocity."},
        /* (2026-10-30) rain and ripples, the Markov melody */
        {"rain_on", "Rain: drops fall on a pond; each plays a note of the KEY by where it lands (left low, right high), louder the bigger it is."},
        {"rain_rate", "Rain rate: about how many drops fall each second (not evenly, like rain)."},
        {"rain_sync", "Rain sync: puts each drop and echo on the next beat line at the host's tempo (Off: as it falls)."},
        {"rain_spread", "Drop size spread: 0 makes every drop the same size (the same velocity); more mixes big and small drops."},
        {"rain_speed", "Ripple speed: how fast the rings spread, so how soon they meet each other and the stones."},
        {"rain_life", "Ripple life: how long a ring lasts before it fades away."},
        {"rain_echo", "Echo: where two rings meet, or a ring reaches a stone, a softer note may play. 0: no echoes; more: more often and louder."},
        {"rain_stones", "Stones: dots in the pond; a ring reaching one plays an echo there."},
        {"rain_keys", "Rain keys: Free plays whenever it is on; Held Keys plays only while you hold keys, from the lowest one."},
        {"rain_len", "Rain note length: how long each drop or echo note lasts."},
        {"rain_octave", "Rain low octave: where the left edge of the pond is (with Free keys)."},
        {"rain_range", "Rain octaves: how many octaves the pond spans, left to right."},
        {"rain_max", "Rain max notes/s: at most this many notes a second (the rest are not played)."},
        {"mkv_on", "Markov: improvises in the style of what you played into this Synth (its steps, its rhythm), on the RATE grid."},
        {"mkv_learn", "Learn: on, every key you play into this Synth is learned (while Keys is Free). Off: it keeps what it knows."},
        {"mkv_forget", "Forget: press to clear what it learned; it goes back to its gentle built-in style."},
        {"mkv_order", "Order: 1 looks at the last note to choose the next; 2 at the last two (closer to what you played)."},
        {"mkv_creat", "Creativity: 0 always takes the most likely next note; higher draws more freely; over 50% it also wanders off."},
        {"mkv_rhythm", "Rhythm: Learned plays your gaps between notes; Straight a note every step; Sparse leaves gaps of 2 to 4 steps."},
        {"mkv_rate", "Markov rate: the length of one step (a note value at the host's tempo)."},
        {"mkv_gate", "Markov note length: how long each note lasts, as a part of the time to the next one."},
        {"mkv_quant", "In key: on, it learns and plays degrees of the KEY and SCALE; off, plain semitones as you played them."},
        {"mkv_range", "Markov octaves: how far it may wander, around where you played (or up from the lowest held key)."},
        {"mkv_keys", "Markov keys: Free plays whenever it is on; Held Keys plays only while you hold keys, from the lowest one (and does not learn them)."},
        {"mkv_max", "Markov max notes/s: at most this many notes a second."},
        /* (2026-10-30) the walls and FOLLOW OCTAVE */
        {"phys_floor", "Floor: Plays a note when a ball lands, Silent bounces without a note, Open lets balls and particles fall out (new balls come in)."},
        {"phys_ceil", "Ceiling: Plays (an octave up), Silent (bounces, no note) or Open (balls and particles leave; new balls come in)."},
        {"phys_left", "Left wall: Plays the root, Silent bounces without a note, Open lets balls and particles out (new balls come in)."},
        {"phys_right", "Right wall: Plays the fifth, Silent bounces without a note, Open lets balls and particles out (new balls come in)."},
        {"phys_follow_oct", "Follow octave: with Free keys, the keyboard's octave buttons move the balls' and particles' notes too."},
        /* (2026-10-30) FOLLOW TEMPO */
        {"gen_follow_tempo", "Follow tempo (all three Synths): on, the balls, particles, rain, orbits and Life's free rate run faster or slower with the BPM (twice as fast at 240, half at 60; as set at 120). Off: their own speeds."},
        /* (2026-10-30) GAME OF LIFE, GRAVITY ORBITS, PENDULUMS */
        {"life_on", "Life: a grid of cells lives and dies by the RULE; births, or a playhead scanning it, play notes of the KEY (bottom row lowest)."},
        {"life_keys", "Keys: Free plays whenever it is on; Held Keys plays only while you hold keys, from the lowest one."},
        {"life_rate", "Rate: how often the grid steps, as a note value at the host's tempo, or Free (its own FREE RATE)."},
        {"life_speed", "Free rate: steps a second while RATE is Free."},
        {"life_mode", "Plays: Births plays each cell as it is born; Scan Column plays the live cells under a playhead moving across (row = note); Scan Row moves up the rows (column = note)."},
        {"life_rule", "Rule: Life (born with 3 neighbours, lives on with 2 or 3), HighLife (also born with 6), Seeds (born with 2, never lives on: wild)."},
        {"life_density", "Density: how full a new random grid starts."},
        {"life_seed", "Seed: turn it to start a new random grid at once (the same seed, the same grid). A grid that dies or loops starts again by itself."},
        {"life_reseed", "Reseed: click for a new random grid now (each click a new one; it lights only for a moment)."},
        {"life_wrap", "Wrap edges: on, the grid's edges join round (top to bottom, left to right); off, outside is empty."},
        {"life_notes", "Notes per step: at most this many notes each step (spread over the grid, not only the lowest)."},
        {"life_len", "Note length: how long each LIFE note lasts."},
        {"life_octave", "Low octave: where the bottom row's note is (with Free keys)."},
        {"life_range", "Octaves: how many octaves the grid spans, bottom to top."},
        {"life_max", "Max notes/s: at most this many LIFE notes a second (the rest are not played)."},
        {"orb_on", "Orbits: planets circle a sun; a planet crossing a spoke plays that spoke's note of the KEY (spread over an octave)."},
        {"orb_keys", "Keys: Free plays whenever it is on; Held Keys plays only while you hold keys, from the lowest one."},
        {"orb_planets", "Planets: how many circle the sun (1-6). The inner ones go round faster."},
        {"orb_speed", "Speed: how fast time runs for the planets (50%: the innermost goes round in a few seconds)."},
        {"orb_gravity", "Gravity: how much the planets pull on each other. 0: each keeps its own orbit exactly (a steady pattern); more, the pattern wanders."},
        {"orb_ecc", "Eccentricity: 0 round orbits, more stretched ones that speed up near the sun."},
        {"orb_spread", "Spread: how far apart the orbits are. 0: all at the same distance (the same speed)."},
        {"orb_spokes", "Spokes: how many lines from the sun play notes (1-8), each its own note."},
        {"orb_planet_to", "Planets as: Octave puts each planet an octave higher than the one before; Velocity plays them all in one octave, the first loudest."},
        {"orb_quant", "Sync: puts each orbit note on the next beat line at the host's tempo (Off: as it crosses)."},
        {"orb_len", "Note length: how long each orbit note lasts."},
        {"orb_octave", "Low octave: where the first spoke's note is (with Free keys)."},
        {"orb_range", "Octaves: how many octaves the planets spread over (Planets as Octave)."},
        {"orb_max", "Max notes/s: at most this many orbit notes a second (the rest are not played)."},
        {"pend_on", "Pendulums: a row of pendulums, each a little faster than the last, drift out of step and meet again; each plays its own note of the KEY as it swings."},
        {"pend_keys", "Keys: Free plays whenever it is on; Held Keys plays only while you hold keys, from the lowest one."},
        {"pend_count", "Pendulums: how many (2-12); each the next note of the scale."},
        {"pend_cycle", "Cycle: how many bars until they are all in step again, at the host's tempo."},
        {"pend_speed", "Swings: how many times the first pendulum swings in one cycle."},
        {"pend_spread", "Spread: how many more swings each pendulum makes than the one before (how fast they drift apart)."},
        {"pend_damp", "Damping: each swing a little smaller and softer, until the next cycle pushes them again."},
        {"pend_trigger", "Plays at: passing the centre, at the ends of the swing, or both."},
        {"pend_restart", "Key restarts: a key starts the swing again from the start (all in step)."},
        {"pend_len", "Note length: how long each pendulum note lasts."},
        {"pend_octave", "Low octave: where the first pendulum's note is (with Free keys)."},
        {"pend_max", "Max notes/s: at most this many pendulum notes a second (the rest are not played)."},
        {"pend_dyn", "Dynamics: how much a big, fast swing plays louder than a small one. 0: every note at the same velocity."},
        /* the macro oscillator */
        {"mac_model", "Macro oscillator model: the kind of sound it makes (virtual analog, FM, wavetables, chords, speech, percussion and more)."},
        {"mac_harm", "Harmonics: the model's main tone control (what it does depends on the model)."},
        {"mac_timbre", "Timbre: the model's second tone control, often brightness or shape."},
        {"mac_morph", "Morph: the model's third control, often a blend or a variation."},
        {"mac_oct", "Octave: the macro oscillator's footage, down or up in whole octaves."},
        {"mac_freq", "Frequency: tunes the macro oscillator up or down, in semitones."},
        {"mac_aux", "Aux mix: blends in the model's second output (a variation of its sound)."},
        {"mac_level", "Macro oscillator level in the mixer. Its SVF and LADDER buttons pick the filters it goes through."},
        /* the step sequencer */
        {"seq_on", "Sequencer ON: whether the sequencer plays the synth (it can run silently with ON off). The steps: the SEQ button."},
        {"seq_play", "PLAY: starts the sequence from step 1, or stops it. Synced with the host playing, it lines up with the song."},
        {"seq_rec", "REC: keys go into the sequence. Stopped: into the selected step, then the next; running: onto the nearest step (RECORD MODE)."},
        {"seq_step_mod", "STEP MOD: turn knobs while it runs (or on the selected step) and their moves are kept per step in modulation lanes. The knob itself stays."},
        {"seq_sync", "Sync: the sequencer follows the host's tempo (and song position); off, it runs at its FREE RATE."},
        {"seq_rate", "Free rate: the tempo in BPM while SYNC is off (RATE still sets each step's note value)."},
        {"seq_div", "Rate: each step's note value, 1/1 to 1/128 (1/16: four steps a beat). TIMING: dotted or triplet."},
        {"seq_timing", "Timing: each step straight, dotted (half as long again) or triplet (two thirds)."},
        {"seq_length", "Length: how many steps play before it starts again (1-64; four pages of 16)."},
        {"seq_follow_swing", "Follow swing: while synced, every second step is moved by the MASTER SWING."},
        {"clock_swing", "Master swing: 50% is straight; more delays every second step for a shuffle (the sequencer follows it with FOLLOW SWING)."},
        {"seq_note_trig", "Note trig: the sequence plays while a key is held (the key doesn't sound) and stops when all are up. Not with PLAY."},
        {"seq_note_reset", "Note reset (note trig): on, a key starts the sequence at once; off, it waits for the next step of the clock."},
        {"seq_restart_hold", "Restart on hold (note trig with HOLD): a new key with none held starts the sequence again from step 1."},
        {"seq_restart_trig", "Restart on trig: each note trig starts from step 1; off, it carries on from where it stopped."},
        {"seq_transpose", "Transpose: keys move the whole sequence (and don't sound). TRANSPOSE MODE: from its first note, or from middle C."},
        {"seq_tr_mode", "Start Key: the key becomes the sequence's first note. Interval: the key's distance from middle C is added."},
        {"seq_one_shot", "One shot: the sequence plays to its end once and stops (PLAY goes off), instead of looping."},
        {"seq_rec_mode", "Record mode (running): Replace clears a step the first time you play into it, Overdub adds, Focused replaces the selected step."},
        {"seq_pattern", "Factory sequence: picks one of the built-in sequences (replacing the steps). User loads nothing: your own steps stay."},
        /* (2026-10-30) */
        {"volume", "Volume: the Synth in focus's own level. Light MASTER under it and the knob becomes MASTER, the whole instrument's level."},
        {"master_level", "MASTER: the level of the whole instrument, all three Synths together (shared; saved with the sound). SYNTH OUTPUT's MASTER button off: the knob is VOLUME again."},    };
    const char *key = PARAMS[i].key;
    if (!strcmp(key, "link_cut")) return "ALL 3: lit, turning either CUTOFF moves that cutoff on all three Synths by the "
                                         "same amount (each keeps its own offset; a cutoff at its end stays there). One "
                                         "undo step, the host told. Saved with the sound.";
    if (!strncmp(key, "link_", 5)) return "LINK: lit, changing this setting (or group) on the Synth in focus sets the same "
                                         "on all three Synths: one undo step, the host told of each. Off, each Synth "
                                         "keeps its own. Saved with the sound.";
    size_t kl = strlen(key);
    if (!strncmp(key, "an_", 3)) return "Analog character: how much the voices differ from each other in this respect (0 = all exactly alike).";
    for (unsigned h = 0; h < sizeof H / sizeof H[0]; h++)   /* the key itself first, then the first matching ending */
        if (H[h].key[0] != '_' && !strcmp(key, H[h].key)) return H[h].text;
    for (unsigned h = 0; h < sizeof H / sizeof H[0]; h++) {
        size_t hl = strlen(H[h].key);
        if (H[h].key[0] == '_' && kl > hl && !strcmp(key + kl - hl, H[h].key)) return H[h].text;
    }
    return NULL;
}

static const char *zone_help(ed_t *e, int z) {
    if (seq_zone(z)) return seq_zone_help(e, z);   /* the step editor, the page LEDs */
    if (z >= Z_QM && z < Z_QM_END) return qm_zone_help(e, z);   /* a DEST button (quickmod.c) */
    if (z >= Z_HUB_ON && z < Z_HUB_END) return hub_zone_help(e, z);   /* the hub page, the console's third row */
    if (z >= Z_SET_BASE && z < Z_SET_END) return settings_zone_help(e, z);   /* the SETTINGS page */
    if (z == Z_TAB_SETTINGS) return "SETTINGS: the instrument's own settings, kept apart from the sounds: preset "
                                    "autoload, MIDI clock and channels, pedals, MIDI learn, the library and tuning.";
    if (z >= Z_PATCH_ACT) return patches_zone_help(z);   /* the PATCHES page */
    if (z >= Z_PAGE && z < Z_PAGE + NPAGES) return "MORE: shows this module's other controls on the screen (click again for its next page, if any).";
    switch (z) {
    case Z_KEYS: return "Keyboard: click a key to play it (lower on the key = louder), drag across to slide between notes. "
                        "The octave buttons shift it. The computer keyboard plays it too: A S D F G H J K L the white keys from C, "
                        "W E T Y U O P the black ones, Z / X the octave (click the panel first). To modulate from "
                        "velocity, aftertouch, the key or the pedals: the DEST button at the top left of the controller panel.";
    case Z_RIBBON: return "Ribbon: touch and slide along it. It sends MIDI CC 16 (position, left to right) and CC 17 (touched). "
                          "Its settings: the triangle at its right end. To modulate from it: the controller panel's DEST "
                          "button (top left).";
    case Z_PITCH: return "Pitch wheel: drag up or down to bend; it springs back to the centre. BEND + and BEND - set the range. "
                         "To modulate from it: the DEST button at the top left of this panel.";
    case Z_MODW: return "Mod wheel (MIDI CC 1): drag up to bring in the mod matrix's Mod Wheel rows (vibrato in most presets). "
                        "To modulate from it: the DEST button at the top left of this panel.";
    case Z_PAD: return "X/Y pad: drag inside it. Left/right is the matrix source Pad X (MIDI CC 2), up/down Pad Y (CC 3); "
                       "both stay where you let go. Pad Gate (CC 18) is on while you touch it. Up/down also sends "
                       "aftertouch (the source Aftertouch, back to 0 when you let go). To modulate from X, Y or its gate: "
                       "the DEST button at the top left of this panel.";
    case Z_OCT_DOWN: case Z_OCT_UP:
        return "Octave: shifts the on-screen keyboard down or up, two octaves each way. Click an LED above for that "
               "octave at once (0: the default); PANIC (under glide's ON) stops every note. Shortcut: hold one, 1 s the "
               "default octave, 2 s PANIC.";
    case Z_OCT_PICK: return "Octave LEDs: the keyboard's octave (lit). Click one to go straight to it; 0 is the default.";
    case Z_PANIC: return "PANIC: stops every note at once (all notes and sounds off on every MIDI channel, the pedals up). "
                         "Use it when a note hangs. Shortcut: hold an OCTAVE button for 2 s.";
    case Z_ENCODER: return "Preset knob: drag up/down or scroll to step through the factory presets, then your own "
                           "patches; click it for the patch list.";
    case Z_PREV: case Z_NEXT: return "Previous / next patch: the factory presets (F), then your own (U).";
    case Z_NAME: return "The patch playing now (F: factory, U: yours; * = changed since it was loaded or saved). Click "
                        "for the patch list, where you can also save, rename, delete, import and export.";
    case Z_TAB_MATRIX: return "MATRIX: the 16 mod matrix rows, 8 at a time (click again, or scroll over the screen, for the others). "
                              "Click a row's number to switch the row off (red) or on again: its settings are kept.";
    case Z_TAB_MORE: return "MORE: the list of every module's MORE page. The small triangles on the modules open them directly.";
    case Z_TAB_ANALOG: return "ANALOG: the analog character page (drift and per-voice differences).";
    case Z_TAB_RANDOM: return "RANDOM: the patch randomiser. New sounds to play with, variations of this one, locks, undo.";
    case Z_FOCUS1: case Z_FOCUS2: case Z_FOCUS3:
        return e->together
            ? "SYNTH 1 / 2 / 3, TOGETHER lit: a click adds this Synth to the ones edited together (lit) or takes it out; "
              "the panel shows the last one clicked. TOGETHER again: back to one Synth. S solos, M mutes."
            : "SYNTH 1 / 2 / 3 (Panel Focus): the panel, the MORE pages, the matrix and the step editor show and edit this "
              "Synth. To edit several at once, light TOGETHER (below) and click them. S beside it solos, M mutes. Sections "
              "marked SHARED are the same for all three. Shortcuts: keys 1 / 2 / 3, Ctrl-click adds.";
    case Z_MUTE1: case Z_MUTE2: case Z_MUTE3:
        return "M (MUTE): red, this Synth is off (its ON on the SETUP page): it stops and plays nothing. Saved with the "
               "sound.";
    case Z_SOLO1: case Z_SOLO2: case Z_SOLO3:
        return "S (SOLO): lit, you hear only the soloed Synths (several can be); the others keep playing silently. Not "
               "saved with the sound: a listening aid. (Shortcut: double-click the SYNTH button.)";
    case Z_TAB_SETUP: return "SETUP: the three Synths side by side: on, MIDI channel, key range (splits and layers), "
                             "transpose, octave, voices, level, pan, sends; then MIDI, master level, PLAY ALL.";
    case Z_TAB_SEQ: return "SEQ: the step sequencer's step editor (64 steps in four pages, note lanes and modulation lanes). "
                           "Click again for its other view.";
    case Z_RND_GO: return "RANDOMISE: a new sound, made to be playable (heard, not harsh, in tune). WILD sets how strange. "
                          "Locked sections stay as they are; volume, tuning, bend and keyboard settings are never changed.";
    case Z_RND_MUTATE: return "MUTATE: a variation of the sound playing now. AMOUNT sets how far: 10-20% a nudge, 100% a lot; "
                              "WILD makes the moves bigger and stranger. Click again for another variation.";
    case Z_RND_AMOUNT: return "AMOUNT: how far MUTATE moves the sound (drag or scroll). Default 20%.";
    case Z_RND_WILD: return "WILD: how strange RANDOMISE and MUTATE get (drag or scroll). 0% the tame, playable sounds; higher: "
                            "more matrix rows (often every slot), any source to any destination with controllers and "
                            "transforms, audio-rate LFOs, odd filter routings, FM, ring and sync, extreme envelopes, unusual "
                            "effects. Still always heard and kept out of harshness. Default 0%.";
    case Z_RND_ALSO: return "ARPEGGIATOR (lit): RANDOMISE and MUTATE also set the arpeggiator: on or off, pattern, octaves, "
                            "division, gate, swing. Off: it stays as it is.";
    case Z_RND_ALSO + 1: return "GENERATIVE (lit): RANDOMISE and MUTATE also set the GENERATIVE section: the Euclidean rhythm "
                                "(steps, hits, rotation), probability, the Turing melody, KEY and SCALE. Off: they stay.";
    case Z_RND_ALSO + 2: return "SEQUENCER (lit): RANDOMISE and MUTATE also set the sequencer's settings (on, division, length, "
                                "swing, trigger and transpose by the keys if it has notes) and its modulation lanes. The "
                                "steps' notes stay. The console's UNDO takes the lanes back too; the randomiser's UNDO only the settings.";
    case Z_RND_UNDO: return "UNDO: back to the sound before (the last 32 are kept, your own edits in between too).";
    case Z_RND_REDO: return "REDO: forward again after UNDO.";
    case Z_RND_ALL: return "ALL 3 SYNTHS: lit, RANDOMISE and MUTATE give all three Synths a new sound (each its own; ON, "
                           "channels and key ranges stay). Keys: R randomise, M mutate, Shift+R / Shift+M all three.";
    case Z_SUB_PREV: case Z_SUB_NEXT: return "The previous / next part of this page.";
    case Z_EUCLID: return "The Euclidean pattern (STEPS, HITS, ROTATE): one dot per step, step 1 at the top, lit = the step "
                          "plays. The middle shows hits / steps. Click for the GENERATIVE page.";
    }
    if (z >= Z_RND_LOCK && z < Z_RND_LOCK + RS_COUNT) {
        static const char *const LOCK_HELP[RS_COUNT] = {
            "Lock OSCILLATORS (lit = kept): waves, footages, tuning, sync, FM, ring source, LFO 3's wave-shape amounts.",
            "Lock MIXER (lit = kept): levels, each source's filter buttons, noise colour and envelope.",
            "Lock FILTERS (lit = kept): both filters, route and mix, envelope / LFO / keyboard amounts, filter FM.",
            "Lock ENVELOPES (lit = kept): filter, amp and mod envelopes with their extras.",
            "Lock MODULATION (lit = kept): the LFOs, pitch LFO / envelope amounts and the mod matrix.",
            "Lock EFFECTS (lit = kept): the synth effect, the delay and reverb sends and settings.",
            "Lock PERFORMANCE (lit = kept): voices, glide, unison, analog character, spread and drift."};
        return LOCK_HELP[z - Z_RND_LOCK];
    }
    return NULL;
}

/* ---- drawing ----
 * Two layers. The static one (bake, below: wood, metal frames, black panels, titles, knob scales, display glass, the
 * keyboard at rest) is made once per window size by skin.c and copied in at each paint; everything that moves or
 * lights up is drawn over it, only where the window needs repainting (the clip box). Knobs, buttons and LEDs are 3D
 * sprites (skin.c), their pointers and value arcs GDI+ lines, the text GDI. */
#define C_PANEL_BG 0xFF1E1E20
#define C_LINE 0xFF6A6A6E
#define C_STRIP 0xFFC8C9CB
#define T_LABEL RGB(214, 214, 216)
#define T_HOT RGB(255, 255, 255)
#define T_VALUE RGB(140, 140, 146)
#define T_DARK RGB(24, 24, 26)
#define C_SCREEN 0xFF0B1621
#define T_LCD RGB(150, 215, 232)
#define T_LCD_DIM RGB(90, 130, 145)

static RECT clipr;   /* what this paint redraws */
static int vis(const RECT *r, int pad) {   /* r (grown by pad) needs drawing */
    RECT a = *r, o;
    InflateRect(&a, pad, pad);
    return IntersectRect(&o, &a, &clipr);
}

static void text(HDC dc, const RECT *r, const char *s, HFONT f, COLORREF c, UINT fmt) {
    if (skin_text(r, s, f, c, fmt)) return;   /* kept as a mask after its first time (skin.c) */
    SelectObject(dc, f);
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    RECT rr = *r;
    DrawTextA(dc, s, -1, &rr, fmt | DT_NOPREFIX);
}
static void text_c(HDC dc, int cx, int top, int w, int h, const char *s, HFONT f, COLORREF c) {   /* centred */
    RECT r = {cx - w / 2, top, cx + w / 2, top + h};
    text(dc, &r, s, f, c, DT_CENTER | DT_TOP | DT_SINGLELINE);
}

static void draw_panels(HDC dc) {   /* static: drawn once into the static layer */
    for (int p = 0; p < NPANELS; p++) {
        const lay_panel_t *q = &PANELS[p];
        float x = (float)sc(q->x), y = (float)sc(q->y), w = (float)sc(q->w), h = (float)sc(q->h);
        if (q->style == 0) {   /* a black panel in a raised, bevelled aluminium frame */
            int b = sc(6);
            skin_metal_frame((int)x, (int)y, (int)w, (int)h, b);
            skin_panel((int)x + b, (int)y + b, (int)w - 2 * b, (int)h - 2 * b);
        } else if (q->style == 1) {   /* a module: printed hairline frame and a light title strip */
            gfx_frame(x + 0.5f, y + 0.5f, w - 1, h - 1, 1.0f, C_LINE);
            float sh = (float)sc(13);
            const char *bar = strchr(q->title, '|');
            char a[64];
            snprintf(a, sizeof a, "%.*s", bar ? (int)(bar - q->title) : 63, q->title);
            if (bar) {   /* two titles side by side (the two filters) */
                float half = w / 2;
                gfx_rect(x, y, half - 1, sh, C_STRIP);
                gfx_rect(x + half + 1, y, half - 1, sh, C_STRIP);
                RECT r1 = {(int)x, (int)y, (int)(x + half), (int)(y + sh)}, r2 = {(int)(x + half), (int)y, (int)(x + w), (int)(y + sh)};
                text(dc, &r1, a, font_title, T_DARK, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                text(dc, &r2, bar + 1, font_title, T_DARK, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            } else {
                gfx_rect(x, y, w, sh, C_STRIP);
                RECT r = {(int)x, (int)y, (int)(x + w), (int)(y + sh)};
                text(dc, &r, a, font_title, T_DARK, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
        } else {   /* a row caption */
            RECT r = {(int)x, (int)y, (int)(x + w), (int)(y + h)};
            text(dc, &r, q->title, font_tiny, T_VALUE, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
    }
}

/* the MORE triangles in the modules' corners: small black wedges with a bevelled edge; lit while their page is on the
 * screen */
static void draw_mores(ed_t *e) {
    for (int k = 0; k < NMORES; k++) {
        const RECT *r = &L.more[k];
        if (!vis(r, sc(4))) continue;
        int open = e->screen == MORES[k].page, hot = e->hover == Z_PAGE + MORES[k].page;
        gpt_t t[3] = {{(float)r->left, (float)r->top}, {(float)r->right, (float)r->top}, {(float)r->right, (float)r->bottom}};
        gpt_t s[3] = {{t[0].x + 1, t[0].y + 1.5f}, {t[1].x + 1, t[1].y + 1.5f}, {t[2].x + 1, t[2].y + 1.5f}};
        gfx_poly(s, 3, 0x90000000);   /* its shadow */
        if (open) {
            gpt_t g[3] = {{t[0].x - 3, t[0].y - 2}, {t[1].x + 2, t[1].y - 2}, {t[2].x + 2, t[2].y + 3}};
            gfx_poly(g, 3, 0x40FFF0D8);
        }
        gfx_poly(t, 3, open ? 0xFFF6F2E8 : hot ? 0xFF2A2A2E : 0xFF0A0A0B);
        gfx_line(t[0].x, t[0].y, t[1].x, t[1].y, 1.0f, open ? 0xFFFFFFFF : hot ? 0xFFD8D8DC : 0xFF8A8A8E);   /* lit top edge */
        gfx_line(t[1].x, t[1].y, t[2].x, t[2].y, 1.0f, open ? 0xFFB8B4AA : 0xFF3A3A3E);
        gfx_line(t[0].x, t[0].y, t[2].x, t[2].y, 1.0f, open ? 0xFFD8D4CA : 0xFF242426);
    }
}

/* the big knobs (cutoffs, the preset knob) get the larger style */
static int big_knob(float r) { return r > 20.0f * L.s; }

/* a knob's printed scale and the dark groove its value arc lights: static */
static void knob_scale(float cx, float cy, float r) {
    for (int t = 0; t <= 10 && r > 10 * L.s; t++) {   /* a scale, except on the smallest knobs */
        gpt_t a = gfx_polar(cx, cy, r + 3.0f * L.s + 1, -135.0f + 27.0f * t), b = gfx_polar(cx, cy, r + 6.5f * L.s + 1, -135.0f + 27.0f * t);
        gfx_line(a.x, a.y, b.x, b.y, 1.0f, (t == 0 || t == 10 || t == 5) ? 0xFFA4A4A8 : 0xFF707074);
    }
    float ra = r + 1.8f * L.s + 0.5f;
    gfx_arc(cx, cy, ra, -135.0f, 135.0f, 2.6f * L.s, 0xFF040404);
}

/* a knob: the lit 3D body (skin.c), a value arc lit in its groove, the pointer: a line engraved in the cap and a white
 * mark on the skirt */
static void knob(float cx, float cy, float r, float v, int bipolar) {
    float a = -135.0f + 270.0f * v;
    int big = big_knob(r);
    skin_knob(cx, cy, r, a, big);
    float ra = r + 1.8f * L.s + 0.5f, from = bipolar ? 0.0f : -135.0f;
    gfx_arc(cx, cy, ra, from, a, 3.6f * L.s, 0x30FFD8A0);
    gfx_arc(cx, cy, ra, from, a, 1.5f * L.s, 0xFFF6EAD2);
    float cr = r * (big ? 0.56f : 0.62f), lw = r * 0.075f;
    lw = lw < 1.3f ? 1.3f : lw > 2.6f ? 2.6f : lw;
    gpt_t p0 = gfx_polar(cx, cy, cr * 0.20f, a), p1 = gfx_polar(cx, cy, cr * 0.90f, a);
    gfx_line(p0.x + 0.6f, p0.y + 0.6f, p1.x + 0.6f, p1.y + 0.6f, lw, 0x60FFFFFF);   /* the engraving's lit lower edge */
    gfx_line(p0.x, p0.y, p1.x, p1.y, lw, 0xFF19191B);
    gpt_t s0 = gfx_polar(cx, cy, cr * 1.14f, a), s1 = gfx_polar(cx, cy, r * 0.93f, a);
    gfx_line(s0.x, s0.y, s1.x, s1.y, lw * 0.85f, 0xFFF0EEE8);
}

void knob_geom(int i, float *cx, float *cy, float *r, int *label_y) {   /* where a panel knob sits */
    const RECT *b = &L.ctl[i];
    int d = L.size[i], small = L.kind[i] == K_SMALL, lh = sc(12), vh = small ? 0 : sc(11);
    int content = d + sc(9) + lh + vh, top = b->top + (rh(b) - content) / 2;
    *cx = (float)((b->left + b->right) / 2);
    *cy = top + d / 2.0f + sc(4);
    *r = d / 2.0f;
    if (label_y) *label_y = top + d + sc(10);
}

static void draw_knob(HDC dc, ed_t *e, int i) {
    const RECT *b = &L.ctl[i];
    int small = L.kind[i] == K_SMALL, lh = sc(12), vh = small ? 0 : sc(11), ly;
    float cx, cy, r;
    knob_geom(i, &cx, &cy, &r, &ly);
    int src = panel_source(e, i);   /* (VOLUME with SYNTH OUTPUT's MASTER lit: master_level, hub.c) */
    float v = ed_get(e, src);   /* the focused Synth's (Panel Focus) */
    knob(cx, cy, r, v, PARAMS[src].min < 0 && PARAMS[src].max > 0);
    int tw = rw(b) > sc(76) ? rw(b) : sc(76);
    text_c(dc, (int)cx, ly, tw, lh + 2, src != i ? "MASTER" : L.label[i], font_label,
           src == e->hover ? (src != i ? RGB(255, 214, 150) : T_HOT) : src != i ? RGB(232, 190, 120) : T_LABEL);
    if (!small) {   /* the value no wider than its cell (and a little), so neighbours' values don't run together */
        char val[64];
        if (!qm_value_text(e, i, val, sizeof val)) param_text(e, src, val, sizeof val);   /* (being turned: its mod amount) */
        RECT vr = {(int)cx - (rw(b) + sc(8)) / 2, ly + lh, (int)cx + (rw(b) + sc(8)) / 2, ly + lh + vh + 2};
        text(dc, &vr, val, font_value, T_VALUE, DT_CENTER | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
}

static void led(float cx, float cy, int on) { skin_led(cx, cy, 3.0f * L.s + 0.5f, on); }

static void red_button(const RECT *r, int hot) {
    skin_button((float)r->left, (float)r->top, (float)rw(r), (float)rh(r), SK_RED, hot ? SK_HOT : SK_UP);
}

/* an LED list: one lit LED beside each option, a red button under it that steps through them */
void leds_geom(int i, int *x0, int *y0, int *row, RECT *button) {
    const RECT *b = &L.ctl[i];
    int n = PARAMS[i].nopts, content = n * sc(10) + sc(4) + sc(12) + sc(3) + sc(12);
    *row = sc(10);
    *y0 = b->top + (rh(b) - content) / 2;
    *x0 = (b->left + b->right) / 2 - sc(16);
    int cx = (b->left + b->right) / 2, by = *y0 + n * *row + sc(4);
    SetRect(button, cx - sc(13), by, cx + sc(13), by + sc(12));
}

static void draw_leds(HDC dc, ed_t *e, int i) {
    int x0, y0, row, cur = option_of(e, i);
    RECT bt;
    leds_geom(i, &x0, &y0, &row, &bt);
    for (int k = 0; k < PARAMS[i].nopts; k++) {
        int y = y0 + k * row;
        led((float)x0, y + row / 2.0f, k == cur);
        RECT tr = {x0 + sc(7), y - sc(1), x0 + sc(52), y + row + sc(1)};
        text(dc, &tr, L.leds[i] ? L.leds[i][k] : PARAMS[i].opts[k], font_tiny, k == cur ? T_LABEL : T_VALUE,
             DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    red_button(&bt, i == e->hover);
    int cx = (L.ctl[i].left + L.ctl[i].right) / 2;
    text_c(dc, cx, bt.bottom + sc(3), sc(76), sc(14), L.label[i], font_label, i == e->hover ? T_HOT : T_LABEL);
}

static void draw_toggle(HDC dc, ed_t *e, int i) {   /* a black push button: latched in and lit white when on */
    const RECT *b = &L.ctl[i];
    int side = L.size[i] == 1;   /* the label beside the button (rows of envelope options) or under it */
    int cx = (b->left + b->right) / 2, bw = sc(side ? 18 : 22), bh = sc(side ? 10 : 11), content = side ? bh : bh + sc(4) + sc(12);
    int top = b->top + (rh(b) - content) / 2, on = option_of(e, i) == 1;
    float x = side ? b->left + 1.0f : cx - bw / 2.0f, y = (float)top;
    skin_button(x, y, (float)bw, (float)bh, SK_BLACK, on ? SK_ON : i == e->hover ? SK_HOT : SK_UP);
    if (side) {
        RECT r = {(int)x + bw + sc(5), b->top, b->right + sc(6), b->bottom};
        text(dc, &r, L.label[i], font_label, i == e->hover ? T_HOT : T_LABEL, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    } else {
        text_c(dc, cx, top + bh + sc(4), sc(76), sc(14), L.label[i], font_label, i == e->hover ? T_HOT : T_LABEL);
    }
}

/* a small display: dark glass sunk into the panel, the value in it, the glass's sheen over it */
static void small_lcd_glass(const RECT *w) {   /* static: the glass, sunk into the panel */
    skin_lcd(w->left, w->top, rw(w), rh(w));
    skin_recess(w->left, w->top, rw(w), rh(w), 2.0f * L.s);
}
static void small_lcd(HDC dc, const RECT *w, const char *s, HFONT f) {   /* the value on it, the glass's sheen over it */
    text(dc, w, s, f, T_LCD, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    skin_glare(w->left, w->top, rw(w), rh(w));
}

static RECT button_win(int i) {   /* a long option list's little window */
    const RECT *b = &L.ctl[i];
    int cx = (b->left + b->right) / 2, ww = rw(b) - sc(4), content = sc(15) + sc(4) + sc(12) + sc(3) + sc(12);
    int top = b->top + (rh(b) - content) / 2;
    RECT win = {cx - ww / 2, top, cx + ww / 2, top + sc(15)};
    return win;
}

static void draw_button(HDC dc, ed_t *e, int i) {   /* a long option list: its value in a little window, a red button */
    const RECT *b = &L.ctl[i];
    int cx = (b->left + b->right) / 2;
    RECT win = button_win(i);
    char val[64];
    param_text(e, i, val, sizeof val);
    small_lcd(dc, &win, val, font_tiny);
    RECT bt = {cx - sc(13), win.bottom + sc(4), cx + sc(13), win.bottom + sc(16)};
    red_button(&bt, i == e->hover);
    text_c(dc, cx, bt.bottom + sc(3), sc(76), sc(14), L.label[i], font_label, i == e->hover ? T_HOT : T_LABEL);
}

static void octave_geom(int i, float *cx, float *cy, float *r) {
    const RECT *b = &L.ctl[i];
    *r = L.size[i] / 2.0f;
    *cx = (float)((b->left + b->right) / 2);
    *cy = b->top + sc(14) + *r;
}

static void draw_octave(HDC dc, ed_t *e, int i) {   /* a rotary switch with its footages around it */
    int n = PARAMS[i].nopts, cur = option_of(e, i);
    float cx, cy, r, spread = 150.0f;
    octave_geom(i, &cx, &cy, &r);
    float a = n > 1 ? -spread / 2 + spread * cur / (n - 1) : 0;
    for (int k = 0; k < n; k++) {
        float ak = n > 1 ? -spread / 2 + spread * k / (n - 1) : 0;
        gpt_t p = gfx_polar(cx, cy, r + sc(11), ak);
        RECT tr = {(int)p.x - sc(14), (int)p.y - sc(7), (int)p.x + sc(14), (int)p.y + sc(7)};
        text(dc, &tr, PARAMS[i].opts[k], font_tiny, k == cur ? T_HOT : T_VALUE, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    knob(cx, cy, r, (a + 135.0f) / 270.0f, 0);
    text_c(dc, (int)cx, (int)(cy + r + sc(8)), sc(76), sc(14), L.label[i], font_label, i == e->hover ? T_HOT : T_LABEL);
}

/* the GENERATIVE section's Euclidean pattern as a ring of LEDs, step 1 at the top and on clockwise: lit = a hit. It is
 * worked out here from STEPS / HITS / ROTATION with the engine's own function (src/seq.h), so it is the very pattern
 * that plays (the engine doesn't tell the window which step it is on, so no step is marked). Dim while EUCLID is off. */
static void draw_euclid(HDC dc, ed_t *e) {
    if (EUCLID_P[1] < 0 || rw(&L.euclid) <= 0) return;
    int n = (int)lroundf(real_of(e, EUCLID_P[1])), hits = (int)lroundf(real_of(e, EUCLID_P[2]));
    n = n < 1 ? 1 : n > EU_MAX ? EU_MAX : n;
    hits = hits < 0 ? 0 : hits > n ? n : hits;
    int rot = (int)lroundf(real_of(e, EUCLID_P[3])) % n, on = EUCLID_P[0] < 0 || option_of(e, EUCLID_P[0]) == 1;
    uint8_t pat[EU_MAX];
    euclid(n, hits, pat);
    const RECT *b = &L.euclid;
    float R = sc(22), lh = (float)sc(12), top = b->top + (rh(b) - (2 * R + sc(10) + lh)) / 2.0f;
    float cx = (b->left + b->right) / 2.0f, cy = top + R + sc(2);
    float dot = 3.14159265f * R / n * 0.62f;
    if (dot > 3.0f * L.s + 0.4f) dot = 3.0f * L.s + 0.4f;
    if (dot < 1.2f) dot = 1.2f;
    gfx_circle(cx, cy, R, 1.0f, 0xFF3A3A3E);
    gpt_t t0 = gfx_polar(cx, cy, R + dot + 2.0f * L.s, 0), t1 = gfx_polar(cx, cy, R + dot + 5.0f * L.s, 0);
    gfx_line(t0.x, t0.y, t1.x, t1.y, 1.2f, 0xFF9A9A9E);   /* where the cycle starts */
    for (int k = 0; k < n; k++) {
        gpt_t p = gfx_polar(cx, cy, R, 360.0f * k / n);
        int hit = pat[(k - rot + n) % n];   /* rotated as the engine does it (engine.c apply_perf) */
        skin_led(p.x, p.y, dot, hit && on);
        if (hit && !on) gfx_disc(p.x, p.y, dot * 0.45f, 0xFF8A8A8E);
    }
    char t[16];
    snprintf(t, sizeof t, "%d/%d", hits, n);
    RECT c = {(int)(cx - R), (int)(cy - sc(7)), (int)(cx + R), (int)(cy + sc(7))};
    text(dc, &c, t, font_tiny, on ? T_LABEL : T_VALUE, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    text_c(dc, (int)cx, (int)(cy + R + sc(8)), sc(76), (int)lh + 2, "PATTERN", font_label,
           e->hover == Z_EUCLID ? T_HOT : T_LABEL);
}

static RECT wave_rect(const lay_wave_t *w) {
    RECT r = {sc(w->x), sc(w->y), sc(w->x + w->w), sc(w->y + w->h)};
    return r;
}

static void draw_wave(ed_t *e, const lay_wave_t *w) {   /* the oscillator's wave, from its wave/angle/pw/mix */
    float x = (float)sc(w->x), y = (float)sc(w->y), ww = (float)sc(w->w), hh = (float)sc(w->h);
    if (w->param[0] >= 0 && w->param[1] >= 0 && w->param[2] >= 0 && w->param[3] >= 0) {
        int saw = option_of(e, w->param[0]) == 1;
        float angle = real_of(e, w->param[1]) / 100.0f, pw = real_of(e, w->param[2]) / 100.0f, mix = real_of(e, w->param[3]) / 100.0f;
        float a = saw ? 1.0f - (0.03f + 0.25f * angle) : 1.0f - angle;
        a = a < 0.02f ? 0.02f : a > 0.98f ? 0.98f : a;
        enum { N = 160 };
        gpt_t pts[N + 1];
        float px = x + 4, pw_ = ww - 8, mid = y + hh / 2, amp = hh / 2 - 5;
        for (int k = 0; k <= N; k++) {
            float t = fmodf(2.0f * k / N, 1.0f);
            if (k == N) t = 1.0f;
            float tri = t < a ? -1.0f + 2.0f * t / a : 1.0f - 2.0f * (t - a) / (1.0f - a);
            float pul = t < pw ? 1.0f : -1.0f;
            float vv = tri + mix * (pul - tri);
            vv = vv < -1.0f ? -1.0f : vv > 1.0f ? 1.0f : vv;
            pts[k].x = px + pw_ * k / N;
            pts[k].y = mid - vv * amp;
        }
        gfx_lines(pts, N + 1, 3.6f, 0x3887DFFF);
        gfx_lines(pts, N + 1, 1.3f, 0xFFA8ECFF);
    }
    skin_glare((int)x + 2, (int)y + 2, (int)ww - 4, (int)hh - 4);
}

/* ---- the centre screen ---- */
static void screen_amount(HDC dc, ed_t *e, int i, RECT c, int active, int compact) {   /* a bar and the value */
    char val[64];
    param_text(e, i, val, sizeof val);
    float v = ed_get(e, i);
    int bip = PARAMS[i].min < 0 && PARAMS[i].max > 0;
    COLORREF col = active ? T_LCD : T_LCD_DIM;
    DWORD fill = active ? 0xFF96D7E8 : 0xFF5A8291;
    if (compact) {   /* the matrix: the value, a thin bar under it */
        float tl = (float)c.left, tr = (float)c.right, by = c.bottom - 2.0f * L.s;
        gfx_rect(tl, by, tr - tl, 2.0f * L.s, 0xFF1C2C36);
        float from = bip ? (tl + tr) / 2 : tl, to = tl + v * (tr - tl);
        gfx_rect(from < to ? from : to, by, fabsf(to - from) + 1, 2.0f * L.s, fill);
        RECT vr = {c.left, c.top, c.right, c.bottom - sc(2)};
        text(dc, &vr, val, font_cell, col, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    } else {
        int vw = sc(46), cy = (c.top + c.bottom) / 2;
        float tl = (float)c.left, tr = (float)(c.right - vw - sc(3));
        gfx_rect(tl, cy - 3.0f * L.s, tr - tl, 6.0f * L.s, 0xFF1C2C36);
        float from = bip ? (tl + tr) / 2 : tl, to = tl + v * (tr - tl);
        gfx_rect(from < to ? from : to, cy - 3.0f * L.s, fabsf(to - from), 6.0f * L.s, fill);
        RECT vr = {c.right - vw, c.top, c.right, c.bottom};
        text(dc, &vr, val, font_cell, col, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    if (i == e->hover && e->hit_screen) gfx_frame((float)c.left - 1, (float)c.top, (float)rw(&c) + 2, (float)rh(&c), 1.0f, 0x6096D7E8);
}

static void screen_menu(HDC dc, ed_t *e, int i, RECT c, int active, HFONT f) {   /* an option list: its value */
    char val[64];
    param_text(e, i, val, sizeof val);
    if (i == e->hover && e->hit_screen) gfx_frame((float)c.left, (float)c.top, (float)rw(&c), (float)rh(&c), 1.0f, 0xFF96D7E8);
    RECT tr = {c.left + sc(3), c.top, c.right, c.bottom};
    text(dc, &tr, val, f, active ? T_LCD : T_LCD_DIM, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

static void title_line(HDC dc, ed_t *e, const char *t, int subs) {
    RECT r = L.tabline;
    if (subs > 1) r.right = L.sub_prev.left - sc(4);
    char st[200], who[16] = "";
    if (e->screen != SCR_SETUP && e->screen != SCR_PATCHES && e->screen != SCR_SETTINGS) {   /* the Synth it shows */
        int n = 0;
        for (int c = 0; c < NSYNTHS; c++)
            if (e->focus_mask >> c & 1u) n += snprintf(who + n, sizeof who - n, n ? "+%d" : "SYNTH %d", c + 1);
        snprintf(st, sizeof st, "%s  |  %s", who, t);
        t = st;
    }
    text(dc, &r, t, font_tiny, T_LCD_DIM, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (subs <= 1) return;
    char n[16];
    if (e->screen == SCR_MATRIX) snprintf(n, sizeof n, "%d-%d", e->sub * ROWS + 1, e->sub * ROWS + ROWS);   /* rows */
    else snprintf(n, sizeof n, "%d / %d", e->sub + 1, subs);
    RECT nr = {L.sub_prev.right, L.tabline.top, L.sub_next.left, L.tabline.bottom};
    text(dc, &nr, n, font_tiny, T_LCD, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    text(dc, &L.sub_prev, "<", font_screen, e->hover == Z_SUB_PREV ? T_HOT : T_LCD, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    text(dc, &L.sub_next, ">", font_screen, e->hover == Z_SUB_NEXT ? T_HOT : T_LCD, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void draw_matrix(HDC dc, ed_t *e, int subs) {
    title_line(dc, e, "MOD MATRIX:  SOURCE moves DESTINATION by AMOUNT x VIA", subs);
    static const char *const head[] = {"#", "SOURCE", "DESTINATION", "AMOUNT", "VIA", "V.AMT", "XFORM", "X.AMT"};
    for (int c = 0; c < MX_COLS + 1; c++) {
        RECT h = {L.head.left + sc(MXC[c]), L.head.top, L.head.left + sc(MXC[c + 1]) - sc(4), L.head.bottom};
        int right = c == 1 + MX_AMT || c == 1 + MX_VIA_AMT || c == 1 + MX_XF_AMT;
        text(dc, &h, head[c], font_tiny, T_LCD_DIM, (right ? DT_RIGHT : DT_LEFT) | DT_VCENTER | DT_SINGLELINE);
    }
    for (int r = 0; r < ROWS; r++) {
        int m = e->sub * ROWS + r;
        if (m >= NMATRIX) break;
        const short *row = MATRIX[m];
        int enabled = MATRIX_ON[m] < 0 || option_of(e, MATRIX_ON[m]) > 0;   /* the number toggles it (2026-10-29) */
        int active = enabled && option_of(e, row[MX_SRC]) > 0 && option_of(e, row[MX_DST]) > 0;
        char num[8];
        snprintf(num, sizeof num, "%d", m + 1);
        RECT nr = matrix_cell(r, 0);
        nr.right += sc(4);
        text(dc, &nr, num, font_cell, !enabled ? RGB(214, 96, 84) : active ? T_LCD : T_LCD_DIM, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        for (int k = 0; k < MX_COLS; k++) {
            int i = row[k];
            if (i < 0) continue;
            RECT c = matrix_cell(r, k + 1);
            if (PARAMS[i].nopts) screen_menu(dc, e, i, c, active, font_cell);
            else screen_amount(dc, e, i, c, active, 1);
        }
        if (qm_marked(e) == m)   /* the row a modulated knob's OPEN IN MATRIX jumped to */
            gfx_frame((float)L.row[r].left, (float)L.row[r].top, (float)rw(&L.row[r]), (float)rh(&L.row[r]), 1.4f, 0xE0F2AA4C);
    }
}

static void draw_page(HDC dc, ed_t *e, int subs) {
    int items[512], n = page_items(e, e->screen, items);
    char t[128];
    snprintf(t, sizeof t, "MORE:  %s", PAGES[e->screen].title);
    title_line(dc, e, t, subs);
    text(dc, &L.head, PAGES[e->screen].note, font_tiny, T_LCD_DIM, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    int per = page_slots(e->screen);
    for (int j = e->sub * per; j < n && j < (e->sub + 1) * per; j++) {
        const lay_item_t *it = &ITEMS[items[j]];
        RECT nm, c;
        slot_rects(page_slot(e->screen, j), &nm, &c);
        text(dc, &nm, it->label, font_cell, it->param == e->hover ? T_HOT : T_LCD_DIM,
             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (PARAMS[it->param].nopts) screen_menu(dc, e, it->param, c, 1, font_cell);
        else screen_amount(dc, e, it->param, c, 1, 0);
    }
    if (PAGES[e->screen].pic) {   /* its picture under the items (more_pics.c: its own drawing surface) */
        RECT a = {L.row[0].left, L.row[ROWS - PIC_ROWS].top + sc(2), L.row[0].right, L.row[ROWS - 1].bottom};
        gfx_end();
        skin_bind(NULL);
        pics_draw(dc, e, e->screen, a);
        gfx_begin(dc);
        skin_bind(gfx_g);
    }
}

static void draw_index(HDC dc, ed_t *e, int subs) {
    title_line(dc, e, "MORE:  every module's second-layer controls", subs);
    text(dc, &L.head, "Click a page (or a module's triangle on the panel).", font_tiny, T_LCD_DIM,
         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    for (int p = e->sub * SLOTS; p < NPAGES && p < (e->sub + 1) * SLOTS; p++) {
        RECT nm, c;
        slot_rects(p % SLOTS, &nm, &c);
        RECT all = {nm.left, nm.top + sc(2), c.right, nm.bottom - sc(2)};
        int hot = e->hover == Z_PAGE + p;
        gfx_frame((float)all.left, (float)all.top, (float)rw(&all), (float)rh(&all), 1.0f, hot ? 0xFF96D7E8 : 0xFF27404E);
        RECT tr = {all.left + sc(6), all.top, all.right - sc(16), all.bottom};
        text(dc, &tr, PAGES[p].title, font_cell, hot ? T_HOT : T_LCD, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        RECT ar = {all.right - sc(16), all.top, all.right - sc(4), all.bottom};
        text(dc, &ar, ">", font_cell, T_LCD_DIM, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
}

/* SETUP: the three Synths side by side (on, MIDI channel, key range, transpose, octave, voices, level, pan, sends,
 * synth effect), then the shared settings across all three columns. Each cell is that Synth's own param. */
static void draw_setup(HDC dc, ed_t *e, int subs) {
    title_line(dc, e, "SETUP:  the three Synths side by side (split, layer, mix)", subs);
    for (int c = 0; c < NSYNTHS; c++) {   /* column heads: the focused Synth bright */
        RECT h = setup_cell(0, c);
        h.top = L.head.top;
        h.bottom = L.head.bottom;
        char t[16];
        snprintf(t, sizeof t, "SYNTH %d", c + 1);
        text(dc, &h, t, font_tiny, (e->focus_mask >> c & 1u) ? T_LCD : T_LCD_DIM, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    int keys[ROWS], was = e->abs;
    setup_rows(e->sub, keys);
    e->abs = 1;   /* the cells name their own params */
    for (int r = 0; r < ROWS; r++) {
        if (keys[r] < 0) continue;
        RECT lr = setup_cell(r, -1);
        lr.left += sc(2);
        text(dc, &lr, SETUP_LABEL[e->sub * ROWS + r], font_cell, T_LCD_DIM, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        int shared = shared_param(keys[r]);
        for (int c = 0; c < (shared ? 1 : NSYNTHS); c++) {
            RECT cell = setup_cell(r, shared ? NSYNTHS : c);
            int i = synth_param(keys[r], c);   /* this Synth's own copy (a shared one: itself) */
            int on = shared || e->sub || keys[0] < 0 || e->fx->getParameter(e->fx, synth_param(keys[0], c)) > 0.5f;
            if (PARAMS[i].nopts) screen_menu(dc, e, i, cell, on, font_cell);
            else screen_amount(dc, e, i, cell, on, 1);
            if (e->hover == Z_ABS + i)
                gfx_frame((float)cell.left - 1, (float)cell.top, (float)rw(&cell) + 2, (float)rh(&cell), 1.0f, 0xFF96D7E8);
        }
    }
    e->abs = was;
}

/* a button on the screen: framed, filled when lit */
static void screen_button(HDC dc, RECT r, const char *label, int lit, int hot, HFONT f) {
    gfx_rect((float)r.left, (float)r.top, (float)rw(&r), (float)rh(&r), lit ? 0xFF96D7E8 : 0xFF0F2230);
    gfx_frame((float)r.left, (float)r.top, (float)rw(&r), (float)rh(&r), 1.0f, hot ? 0xFFFFFFFF : lit ? 0xFF96D7E8 : 0xFF3E6273);
    text(dc, &r, label, f, lit ? RGB(11, 22, 33) : hot ? T_HOT : T_LCD, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

/* the GENERATIVE hub page (hub.c): the generators with their ON and the Synths they are on in, KEY and SCALE with
 * their LINKs, ALL OFF and what is on */
static void draw_hub(HDC dc, ed_t *e) {
    title_line(dc, e, "GENERATIVE:  the generators, KEY and SCALE", 1);
    text(dc, &L.head, "ON: the Synth in focus (1 2 3: the Synths it is on in). Click a name for its page.", font_tiny,
         T_LCD_DIM, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    int on_n[NSYNTHS] = {0, 0, 0};
    for (int k = 0; k < NHUB; k++) {
        RECT on = hub_rect(Z_HUB_ON + k), op = hub_rect(Z_HUB_OPEN + k);
        int hot = e->hover == Z_HUB_OPEN + k;
        if (HUB[k].on >= 0)
            screen_button(dc, on, "ON", hub_on(e, k, e->focus), e->hover == Z_HUB_ON + k, font_cell);
        gfx_frame((float)op.left, (float)op.top, (float)rw(&op), (float)rh(&op), 1.0f, hot ? 0xFF96D7E8 : 0xFF27404E);
        RECT tr = {op.left + sc(6), op.top, op.right - sc(56), op.bottom};
        text(dc, &tr, HUB[k].label, font_cell, hot ? T_HOT : T_LCD, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        for (int c = 0; c < NSYNTHS; c++) {   /* 1 2 3: lit where it is on */
            int lit = hub_on(e, k, c);
            on_n[c] += lit;
            char d[2] = {(char)('1' + c), 0};
            RECT dr = {op.right - sc(52) + c * sc(12), op.top, op.right - sc(42) + c * sc(12), op.bottom};
            text(dc, &dr, d, font_tiny, lit ? T_LCD : RGB(46, 72, 84), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        RECT ar = {op.right - sc(14), op.top, op.right - sc(4), op.bottom};
        text(dc, &ar, ">", font_cell, T_LCD_DIM, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    static const char *const NAME[2] = {"KEY", "SCALE"}, *const KEY[2] = {"key_root", "key_scale"},
                      *const LNK[2] = {"link_key", "link_scale"};
    for (int row = 0; row < 2; row++) {   /* KEY and SCALE of the Synth in focus, and their LINKs */
        int iv = rnd_key(KEY[row]) < NPARAMS ? rnd_key(KEY[row]) : -1, il = rnd_key(LNK[row]) < NPARAMS ? rnd_key(LNK[row]) : -1;
        RECT v = hub_value_rect(row), l = hub_link_rect(row), nm = {v.left - sc(50), v.top, v.left - sc(2), v.bottom};
        if (iv < 0) continue;
        text(dc, &nm, NAME[row], font_cell, iv == e->hover ? T_HOT : T_LCD_DIM, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        gfx_frame((float)v.left, (float)v.top, (float)rw(&v), (float)rh(&v), 1.0f, 0xFF27404E);
        screen_menu(dc, e, iv, v, 1, font_cell);
        if (il >= 0) screen_button(dc, l, "LINK", option_of(e, il) == 1, il == e->hover, font_tiny);
    }
    int ft = rnd_key("gen_follow_tempo") < NPARAMS ? rnd_key("gen_follow_tempo") : -1;   /* (2026-10-30) */
    if (ft >= 0) {
        RECT f = hub_follow_rect(), fr = {f.right + sc(6), f.top, L.row[0].right, f.bottom};
        char ftx[64];
        screen_button(dc, f, "FOLLOW TEMPO", option_of(e, ft) == 1, ft == e->hover, font_cell);
        hub_follow_text(e, ftx, sizeof ftx);
        text(dc, &fr, ftx, font_tiny, T_LCD_DIM, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    RECT a = hub_rect(Z_HUB_ALLOFF);
    int back = hub_restore_n(e);
    screen_button(dc, a, back ? "BRING BACK (ALL 3 SYNTHS)" : "ALL OFF (ALL 3 SYNTHS)", 0, e->hover == Z_HUB_ALLOFF,
                  font_cell);
    char st[96];
    if (on_n[0] + on_n[1] + on_n[2])
        snprintf(st, sizeof st, "On: Synth 1 %d, Synth 2 %d, Synth 3 %d", on_n[0], on_n[1], on_n[2]);
    else snprintf(st, sizeof st, back ? "All off (%d to bring back)." : "All off.", back);
    RECT sr = {a.right + sc(8), a.top, L.row[0].right, a.bottom};
    text(dc, &sr, st, font_tiny, T_LCD_DIM, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

/* the randomiser page: which sound of the history is playing, what it is, the buttons, the amount and the locks */
static void draw_random(HDC dc, ed_t *e) {
    char t[128], d[200];
    if (e->hist_n) snprintf(t, sizeof t, "RANDOM:  %s   (%d of %d)", e->hist_name[e->hist_pos], e->hist_pos + 1, e->hist_n);
    else snprintf(t, sizeof t, "RANDOM:  new sounds to play with");
    title_line(dc, e, t, 1);
    float cur[NPARAMS];
    for (int i = 0; i < NPARAMS; i++) cur[i] = ed_get(e, i);   /* the focused Synth's sound */
    rnd_describe(cur, d, sizeof d);
    text(dc, &L.head, d, font_tiny, T_LCD_DIM, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    screen_button(dc, rnd_rect(Z_RND_GO), "RANDOMISE", 0, e->hover == Z_RND_GO, font_big);
    screen_button(dc, rnd_rect(Z_RND_MUTATE), "MUTATE", 0, e->hover == Z_RND_MUTATE, font_big);
    int half = (rw(&L.row[0]) - sc(8)) / 2;
    for (int b = 0; b < 2; b++) {   /* WILD (under RANDOMISE: both buttons use it) and AMOUNT (under MUTATE) */
        int z = b ? Z_RND_AMOUNT : Z_RND_WILD, hot = e->hover == z, x = L.row[2].left + b * (half + sc(8));
        float v = b ? e->rnd_amount : e->rnd_wild;
        RECT bar = rnd_rect(z), al = {x, L.row[2].top, bar.left - sc(4), L.row[2].bottom};
        RECT av = {bar.right + sc(4), L.row[2].top, x + half, L.row[2].bottom};
        unsigned fill = b ? 0xFF96D7E8 : 0xFFE8B96A;   /* WILD in amber */
        text(dc, &al, b ? "AMOUNT" : "WILD", font_cell, hot ? T_HOT : T_LCD_DIM, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        float cy = (bar.top + bar.bottom) / 2.0f;
        gfx_rect((float)bar.left, cy - 3.0f * L.s, (float)rw(&bar), 6.0f * L.s, 0xFF1C2C36);
        gfx_rect((float)bar.left, cy - 3.0f * L.s, rw(&bar) * v, 6.0f * L.s, fill);
        if (hot) gfx_frame((float)bar.left - 1, (float)bar.top, (float)rw(&bar) + 2, (float)rh(&bar), 1.0f, 0x6096D7E8);
        char pct[16];
        snprintf(pct, sizeof pct, "%.0f%%", v * 100.0f);
        text(dc, &av, pct, font_cell, T_LCD, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    RECT c0 = rnd_col(0), c1 = rnd_col(1), c2 = rnd_col(2);
    RECT lk = {c0.left, L.row[3].top, c1.right, L.row[3].bottom}, al = {c2.left, L.row[3].top, c2.right, L.row[3].bottom};
    text(dc, &lk, "LOCK (lit = keep as it is):", font_cell, T_LCD_DIM, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    text(dc, &al, "ALSO RANDOMISE:", font_cell, T_LCD_DIM, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    for (int k = 0; k < RS_COUNT; k++)
        screen_button(dc, rnd_rect(Z_RND_LOCK + k), RND_SECTION[k], e->rnd_locks >> k & 1u, e->hover == Z_RND_LOCK + k,
                      font_cell);
    screen_button(dc, rnd_rect(Z_RND_UNDO), "UNDO", 0, e->hover == Z_RND_UNDO && e->hist_pos > 0, font_cell);
    screen_button(dc, rnd_rect(Z_RND_REDO), "REDO", 0, e->hover == Z_RND_REDO && e->hist_pos < e->hist_n - 1, font_cell);
    for (int k = 0; k < 3; k++) {   /* the tick buttons: a box ticked (filled) when lit */
        RECT r = rnd_rect(Z_RND_ALSO + k);
        int lit = e->rnd_also >> (RS_ARP + k) & 1u;
        screen_button(dc, r, RND_SECTION[RS_ARP + k], lit, e->hover == Z_RND_ALSO + k, font_cell);
        float bs = 7.0f * L.s, bx = r.left + 5.0f * L.s, by = (r.top + r.bottom) / 2.0f - bs / 2;
        gfx_frame(bx, by, bs, bs, 1.0f, lit ? 0xFF0B1621 : 0xFF3E6273);
        if (lit) gfx_rect(bx + 2.0f * L.s, by + 2.0f * L.s, bs - 4.0f * L.s, bs - 4.0f * L.s, 0xFF0B1621);
    }
    if (NSYNTHS > 1) screen_button(dc, rnd_rect(Z_RND_ALL), "ALL 3 SYNTHS", e->rnd_all, e->hover == Z_RND_ALL, font_cell);
}

/* the screen's glass, bezel, stripes and rules: static */
static void screen_static(void) {
    RECT *s = &L.screen;
    int b = sc(4);
    skin_shadow((float)s->left - b, (float)s->top - b + sc(2), (float)rw(s) + 2 * b, (float)rh(s) + 2 * b, 4.0f * L.s, 0.6f);
    gfx_rect((float)s->left - b, (float)s->top - b, (float)rw(s) + 2 * b, (float)rh(s) + 2 * b, 0xFF050506);
    skin_lcd(s->left, s->top, rw(s), rh(s));
    skin_recess(s->left, s->top, rw(s), rh(s), 5.0f * L.s);
    gfx_frame((float)L.name.left, (float)L.name.top, (float)rw(&L.name), (float)rh(&L.name), 1.2f, 0xFFDCE6EE);
}

/* the help line: what the control under the mouse does, or what the last action did (also for load_test: HT_HELP) */
void help_line(ed_t *e, char *out, int n) {
    char help[480];
    const char *zh = zone_help(e, e->hover), *pm = patches_message(e), *sm = settings_message(e);
    if (sm && (!pm || e->screen == SCR_SETTINGS)) pm = sm;   /* (the SETTINGS page's own first, on that page) */
    if (qm_help(e, help, sizeof help)) {   /* QUICK ASSIGN: picking, connecting, the amount (quickmod.c) */
    } else if (pm) {   /* what the last patch operation did (or why it couldn't) */
        snprintf(help, sizeof help, "%s", pm);
    } else if ((e->hover >= 0 && e->hover < NPARAMS) || (e->hover >= Z_ABS && e->hover < Z_ABS + NPARAMS)) {
        char val[64];
        int absolute = e->hover >= Z_ABS, i = absolute ? e->hover - Z_ABS : e->hover, was = e->abs;
        e->abs = absolute;
        param_text(e, i, val, sizeof val);
        const char *h = help_for(base_of(i));
        snprintf(help, sizeof help, "%s: %s%s%s%s", PARAMS[fp(e, i)].name, val,
                 shared_param(i) ? "   (shared: the same for all three Synths)" : "", h ? "\n" : "", h ? h : "");
        if (!absolute) {   /* what modulates it, on the first line */
            char links[200] = "", *nl = strchr(help, '\n');
            qm_links_text(e, i, links, sizeof links);
            if (links[0] && nl) {
                char rest[480];
                snprintf(rest, sizeof rest, "%s", nl);
                snprintf(nl, sizeof help - (nl - help), "%s%s", links, rest);
            } else if (links[0]) {
                snprintf(help + strlen(help), sizeof help - strlen(help), "%s", links);
            }
        }
        e->abs = was;
    } else if (zh) {
        snprintf(help, sizeof help, "%s", zh);
    } else if (lib_notes_help(e, help, sizeof help)) {   /* SHOW NOTES: the patch's notes */
    } else if (e->screen == SCR_RANDOM) {
        snprintf(help, sizeof help, "RANDOMISE makes a new sound; MUTATE a variation of this one (AMOUNT: how far; WILD: how "
                                    "strange). Light a LOCK to keep a section, a tick to change the arp, GENERATIVE or sequencer too. UNDO / REDO step through the last sounds. Keys: R, M; Shift: all 3 Synths. Like it? "
                                    "Save it in your host (a preset or the project).");
    } else if (lib_screen(e->screen)) {
        lib_help(e, help, sizeof help);
    } else if (e->screen == SCR_SETTINGS) {
        settings_help(e, help, sizeof help);
    } else if (e->screen == SCR_GEN) {
        hub_help(e, help, sizeof help);
    } else if (e->screen == SCR_SEQ) {
        seq_help(e, help, sizeof help);
    } else if (e->screen == SCR_MATRIX) {
        snprintf(help, sizeof help, "Each row: SOURCE x AMOUNT moves DESTINATION. VIA scales it by a second source "
                                    "(V.AMT: how much); XFORM reshapes the source (X.AMT: how strongly). Click a value "
                                    "for its list, drag an amount sideways. MATRIX again: rows 9-16.");
    } else {
        snprintf(help, sizeof help, "Drag a knob up/down or scroll to nudge (Shift: fine; double-click: back to its default). "
                                    "Click a red button to step, an LED to pick. A corner triangle shows the module's MORE "
                                    "page here. Modulation: a DEST button, then a knob. UNDO / REDO: on the console.");
    }
    if (e->screen == SCR_SEQ && seq_message(e) && !pm) snprintf(help, sizeof help, "%s", seq_message(e));
    snprintf(out, n, "%s", help);
}

static void draw_screen(HDC dc, ed_t *e) {
    char title[96];
    patches_title(e, title, sizeof title);   /* "F 001  Init", "U  Bright Pad *" (patches.c) */
    char home[160];
    RECT nr = L.name;
    if (lib_home_line(e, home, sizeof home)) {   /* the home line: the patch's tags and the tempo (library.c) */
        RECT hr = {L.name.left + sc(6), L.name.bottom - sc(13), L.name.right - sc(6), L.name.bottom - sc(1)};
        nr.bottom -= sc(10);
        text(dc, &hr, home, font_tiny, T_LCD_DIM, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    text(dc, &nr, title, font_big, e->hover == Z_NAME ? T_HOT : T_LCD, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    text(dc, &L.prev, "<", font_big, T_LCD, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    text(dc, &L.next, ">", font_big, T_LCD, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (e->screen >= NPAGES) e->screen = SCR_MATRIX;
    int subs = screen_subs(e);
    if (e->sub >= subs) e->sub = subs - 1;   /* the page got shorter (another effect type) */
    int striped = e->screen >= 0 && e->screen < NPAGES ? page_rows(e->screen) : ROWS;   /* (not under a picture) */
    for (int r = 0; r < striped && e->screen != SCR_SEQ; r++)   /* (the step editor has its own rows) */
        if (r % 2) gfx_rect((float)L.row[r].left, (float)L.row[r].top, (float)rw(&L.row[r]), (float)rh(&L.row[r]), 0x109CC8E6);
    if (e->screen == SCR_MATRIX) draw_matrix(dc, e, subs);
    else if (e->screen == SCR_INDEX) draw_index(dc, e, subs);
    else if (e->screen == SCR_RANDOM) draw_random(dc, e);
    else if (e->screen == SCR_SEQ) {   /* (its own surface: seq_page.c) */
        gfx_end();
        skin_bind(NULL);
        seq_draw(dc, e);
        gfx_begin(dc);
        skin_bind(gfx_g);
    }
    else if (lib_screen(e->screen)) lib_draw(dc, e);   /* PATCHES and the library's pages, with their tabs */
    else if (e->screen == SCR_SETUP) draw_setup(dc, e, subs);
    else if (e->screen == SCR_GEN) draw_hub(dc, e);   /* (hub.c) */
    else if (e->screen == SCR_QMOD) {   /* MOD FROM <source> while one is picked (quickmod.c: its own surface) */
        char t[96];
        qm_page_title(e, t, sizeof t);
        title_line(dc, e, t, subs);
        gfx_end();
        skin_bind(NULL);
        qm_page_draw(dc, e);
        gfx_begin(dc);
        skin_bind(gfx_g);
    }
    else if (e->screen == SCR_SETTINGS) {   /* (settings.c) */
        char t[64];
        settings_title(e, t, sizeof t);
        title_line(dc, e, t, subs);
        settings_draw(dc, e);
    }
    else draw_page(dc, e, subs);
    char help[480];   /* help: what the control under the mouse does */
    help_line(e, help, sizeof help);
    RECT hr = e->screen == SCR_SEQ ? seq_help_rect() : L.help;   /* (the step editor uses the top of it) */
    gfx_line((float)hr.left, hr.top - 4.0f, (float)hr.right, hr.top - 4.0f, 1.0f, 0xFF27404E);
    text(dc, &hr, help, e->screen == SCR_SEQ ? font_cell : font_screen, T_LCD, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);
    skin_glare(L.screen.left, L.screen.top, rw(&L.screen), rh(&L.screen));
}

static void panel_button(HDC dc, const RECT *r, const char *label, int lit, int hot) {   /* console buttons */
    if (!vis(r, sc(8))) return;
    int bw = sc(30), bh = sc(13), cx = (r->left + r->right) / 2;
    float x = cx - bw / 2.0f, y = (float)(r->top + sc(6));
    skin_button(x, y, (float)bw, (float)bh, SK_CONSOLE, lit ? SK_ON : hot ? SK_HOT : SK_UP);
    text_c(dc, cx, (int)y + bh + sc(5), rw(r) + sc(10), sc(14), label, font_label, hot ? T_HOT : T_LABEL);
}

/* SYNTH c: the select cap (lit: in focus), the M | S split cap (M red: off; S lit: soloed), the label under both */
static void synth_button(HDC dc, ed_t *e, int c, const char *label) {
    const RECT *z = &L.focus[c];
    if (!vis(z, sc(8))) return;
    RECT cap = focus_part(c, 0), m = focus_part(c, 1), s = focus_part(c, 2);
    int hot = e->hover == Z_FOCUS1 + c, mhot = e->hover == Z_MUTE1 + c, shot = e->hover == Z_SOLO1 + c;
    int muted = synth_muted(e, c), solo = synth_soloed(e, c);
    skin_button((float)cap.left, (float)cap.top, (float)rw(&cap), (float)rh(&cap), SK_CONSOLE,
                e->focus_mask >> c & 1u ? SK_ON : hot ? SK_HOT : SK_UP);
    skin_button((float)m.left, (float)m.top, (float)rw(&m), (float)rh(&m), muted ? SK_RED : SK_BLACK,
                muted ? SK_ON : mhot ? SK_HOT : SK_UP);
    skin_button((float)s.left, (float)s.top, (float)rw(&s), (float)rh(&s), SK_CONSOLE, solo ? SK_ON : shot ? SK_HOT : SK_UP);
    text(dc, &m, "M", font_tiny, muted ? RGB(255, 236, 228) : mhot ? T_HOT : T_LABEL, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    text(dc, &s, "S", font_tiny, solo ? RGB(40, 34, 24) : shot ? T_HOT : T_LABEL, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    int cx = (z->left + z->right) / 2;
    text_c(dc, cx, cap.bottom + sc(5), rw(z) + sc(10), sc(14), label, font_label, hot || mhot || shot ? T_HOT : T_LABEL);
}

static void encoder_geom(float *cx, float *cy, float *r) {
    *r = (float)sc(31);
    *cx = (float)((L.encoder.left + L.encoder.right) / 2);
    *cy = (float)L.encoder.top + sc(8) + *r;
}

static void draw_console(HDC dc, ed_t *e) {
    if (vis(&L.screen, sc(2))) draw_screen(dc, e);
    panel_button(dc, &L.tab_matrix, "MATRIX", e->screen == SCR_MATRIX, e->hover == Z_TAB_MATRIX);
    panel_button(dc, &L.tab_more, "MORE", e->screen >= 0 && e->screen != PG_ANALOG ? 1 : e->screen == SCR_INDEX,
                 e->hover == Z_TAB_MORE);
    panel_button(dc, &L.tab_analog, "ANALOG", PG_ANALOG >= 0 && e->screen == PG_ANALOG, e->hover == Z_TAB_ANALOG);
    panel_button(dc, &L.tab_random, "RANDOM", e->screen == SCR_RANDOM, e->hover == Z_TAB_RANDOM);
    panel_button(dc, &L.tab_seq, "SEQ", e->screen == SCR_SEQ, e->hover == Z_TAB_SEQ);
    static const char *const FOCUS_NAME[NSYNTHS] = {"SYNTH 1", "SYNTH 2", "SYNTH 3"};
    for (int c = 0; c < NSYNTHS; c++)   /* PANEL FOCUS: lit = the panel shows (and edits) that Synth */
        if (NSYNTHS > 1) synth_button(dc, e, c, FOCUS_NAME[c]);
        else panel_button(dc, &L.focus[c], FOCUS_NAME[c], e->focus_mask >> c & 1u, e->hover == Z_FOCUS1 + c);
    panel_button(dc, &L.tab_setup, "SETUP", e->screen == SCR_SETUP, e->hover == Z_TAB_SETUP);
    panel_button(dc, &L.tab_settings, "SETTINGS", e->screen == SCR_SETTINGS, e->hover == Z_TAB_SETTINGS);
    panel_button(dc, &L.bprev, "PREV", 0, e->hover == Z_PREV);
    panel_button(dc, &L.bnext, "NEXT", 0, e->hover == Z_NEXT);
    panel_button(dc, &L.compare, "COMPARE", lib_comparing(e), e->hover == Z_COMPARE);   /* (library.c) */
    panel_button(dc, &L.snapshot, "SNAPSHOT", e->screen == SCR_SNAPS, e->hover == Z_SNAPSHOT);
    if (vis(&L.vol_master, sc(6)) && !IsRectEmpty(&L.vol_master)) {   /* SYNTH OUTPUT's MASTER (hub.c) */
        const RECT *r = &L.vol_master;
        int bh = sc(10), bw = sc(18), h = e->hover == Z_VOL_MASTER;
        skin_button((float)r->left + 1, (r->top + r->bottom - bh) / 2.0f, (float)bw, (float)bh, SK_BLACK,
                    e->vol_master ? SK_ON : h ? SK_HOT : SK_UP);
        RECT tr = {r->left + bw + sc(5), r->top, r->right + sc(10), r->bottom};
        text(dc, &tr, "MASTER", font_label, h ? T_HOT : T_LABEL, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    /* the third row (hub.c): GENERATIVE, GEN OFF (GEN BACK while it can bring them back), TEMPO with HOST / OWN */
    panel_button(dc, &L.tab_gen, "GENERATIVE", e->screen == SCR_GEN, e->hover == Z_TAB_GEN);
    panel_button(dc, &L.gen_off, hub_restore_n(e) ? "GEN BACK" : "GEN OFF", 0, e->hover == Z_GEN_OFF);
    if (NSYNTHS > 1) panel_button(dc, &L.together, "TOGETHER", e->together, e->hover == Z_TOGETHER);   /* (hub.c) */
    panel_button(dc, &L.undo, "UNDO", 0, e->hover == Z_UNDO);
    panel_button(dc, &L.redo, "REDO", 0, e->hover == Z_REDO);
    if (vis(&L.tempo, sc(16)) || vis(&L.tempo_host, sc(4)) || vis(&L.tempo_own, sc(4))) {
        char t[16];
        int own = settings_tempo(NULL), hot = e->hover == Z_TEMPO;
        tempo_text(e, t, sizeof t);
        small_lcd(dc, &L.tempo, t, font_big);
        RECT dn = hub_rect(Z_TEMPO_DOWN), up = hub_rect(Z_TEMPO_UP);   /* its < > (a click steps it) */
        text(dc, &dn, "<", font_screen, e->hover == Z_TEMPO_DOWN ? T_HOT : T_LCD_DIM, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        text(dc, &up, ">", font_screen, e->hover == Z_TEMPO_UP ? T_HOT : T_LCD_DIM, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        text_c(dc, (L.tempo.left + L.tempo.right) / 2, L.tempo.bottom + sc(4), rw(&L.tempo) + sc(10), sc(14), "TEMPO (BPM)",
               font_label, hot ? T_HOT : T_LABEL);
        for (int b = 0; b < 2; b++) {   /* HOST, OWN: one lit */
            const RECT *r = b ? &L.tempo_own : &L.tempo_host;
            int lit = b ? own : !own, bh = sc(10), bw = sc(18), h = e->hover == (b ? Z_TEMPO_OWN : Z_TEMPO_HOST);
            float y = (r->top + r->bottom - bh) / 2.0f;
            skin_button((float)r->left + 1, y, (float)bw, (float)bh, SK_BLACK, lit ? SK_ON : h ? SK_HOT : SK_UP);
            RECT tr = {r->left + bw + sc(6), r->top, r->right + sc(6), r->bottom};
            text(dc, &tr, b ? "OWN" : "HOST", font_label, h ? T_HOT : T_LABEL, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
    }
    if (vis(&L.encoder, sc(12))) {
        float cx, cy, r;
        encoder_geom(&cx, &cy, &r);
        int pos = patches_pos(e), np = patches_total(e);   /* factory presets, then user patches */
        knob(cx, cy, r, np > 1 && pos > 0 ? (float)pos / (np - 1) : 0.0f, 0);
        text_c(dc, (int)cx, L.encoder.top + sc(14) + 2 * (int)r, sc(80), sc(14), "PRESET", font_label,
               e->hover == Z_ENCODER ? T_HOT : T_LABEL);
    }
    if (vis(&L.seq_leds, sc(8))) {
        gfx_end();   /* the page LEDs draw on their own surface (seq_page.c) */
        skin_bind(NULL);
        seq_panel_draw(dc, e);
        gfx_begin(dc);
        skin_bind(gfx_g);
    }
}

static void wheel(HDC dc, const RECT *r, float pos, const char *label, int hot) {   /* pos -1..1 */
    if (!vis(r, sc(18))) return;
    skin_wheel(r->left + sc(2), r->top + sc(1), rw(r) - sc(4), rh(r) - sc(2), pos);
    text_c(dc, r->left + rw(r) / 2, r->bottom + sc(4), sc(60), sc(14), label, font_label, hot ? T_HOT : T_LABEL);
}
static void wheel_static(const RECT *r, int centred) {   /* the slot the wheel turns in */
    int m = sc(3);
    gfx_rect((float)r->left - m, (float)r->top - m, (float)rw(r) + 2 * m, (float)rh(r) + 2 * m, 0xFF060607);
    skin_recess(r->left - m, r->top - m, rw(r) + 2 * m, rh(r) + 2 * m, 6.0f * L.s);
    if (centred) gfx_line(r->left - 7.0f * L.s, r->top + rh(r) / 2.0f, r->left - 4.0f * L.s, r->top + rh(r) / 2.0f, 1.2f, 0xFFB0B0B4);
}

static void draw_controller(HDC dc, ed_t *e) {
    if (vis(&L.oct_leds, sc(8)))
        for (int k = 0; k < 5; k++) {
            float x = L.oct_leds.left + rw(&L.oct_leds) * (k + 0.5f) / 5;
            led(x, (float)(L.oct_leds.top + sc(5)), e->oct == k - 2);
        }
    if (vis(&L.oct_down, sc(6))) red_button(&L.oct_down, e->hover == Z_OCT_DOWN);
    if (vis(&L.oct_up, sc(6))) red_button(&L.oct_up, e->hover == Z_OCT_UP);
    if (vis(&L.panic, sc(4)) && !IsRectEmpty(&L.panic)) {   /* (2026-10-30) PANIC: a small black cap, its name beside */
        const RECT *r = &L.panic;
        int bh = sc(10), bw = sc(18), hot = e->hover == Z_PANIC;
        skin_button((float)r->left + 1, (r->top + r->bottom - bh) / 2.0f, (float)bw, (float)bh, SK_BLACK, hot ? SK_HOT : SK_UP);
        RECT tr = {r->left + bw + sc(5), r->top, r->right + sc(6), r->bottom};
        text(dc, &tr, "PANIC", font_label, hot ? T_HOT : T_LABEL, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    wheel(dc, &L.pitch, e->bend, "PITCH", e->hover == Z_PITCH);
    wheel(dc, &L.modw, e->wheel * 2 - 1, "MOD", e->hover == Z_MODW);
    RECT *p = &L.pad;
    if (vis(p, sc(4))) {
        if (e->hover == Z_PAD) gfx_frame((float)p->left, (float)p->top, (float)rw(p), (float)rh(p), 1.0f, 0xFF8A8A8E);
        float px = p->left + e->pad_x * rw(p), py = p->bottom - e->pad_y * rh(p);
        gfx_line(px, (float)p->top + 1, px, (float)p->bottom - 1, 0.8f, e->pad_on ? 0x80A8ECFF : 0x30A8ECFF);
        gfx_line((float)p->left + 1, py, (float)p->right - 1, py, 0.8f, e->pad_on ? 0x80A8ECFF : 0x30A8ECFF);
        if (e->pad_on) gfx_disc(px, py, 9.0f * L.s, 0x2890E4FF);
        gfx_disc(px, py, 4.0f * L.s, e->pad_on ? 0xFFDDF6FF : 0x6088A8B4);
    }
}
static void controller_static(HDC dc) {
    for (int k = 0; k < 5; k++) {
        float x = L.oct_leds.left + rw(&L.oct_leds) * (k + 0.5f) / 5;
        static const char *const lab[] = {"-2", "-1", "0", "+1", "+2"};
        text_c(dc, (int)x, L.oct_leds.top + sc(10), sc(22), sc(13), lab[k], font_tiny, T_VALUE);
    }
    float ty = (L.oct_down.top + L.oct_down.bottom) / 2.0f, t = 3.5f * L.s;
    gpt_t l[3] = {{L.oct_down.left - 5 * L.s - t, ty}, {L.oct_down.left - 5 * L.s, ty - t}, {L.oct_down.left - 5 * L.s, ty + t}};
    gpt_t g[3] = {{L.oct_up.right + 5 * L.s + t, ty}, {L.oct_up.right + 5 * L.s, ty - t}, {L.oct_up.right + 5 * L.s, ty + t}};
    gfx_poly(l, 3, 0xFFB4B4B8);
    gfx_poly(g, 3, 0xFFB4B4B8);
    text_c(dc, (L.oct_down.left + L.oct_up.right) / 2, L.oct_down.bottom + sc(3), sc(80), sc(14), "OCTAVE", font_label, T_LABEL);
    wheel_static(&L.pitch, 1);
    wheel_static(&L.modw, 0);
    RECT *p = &L.pad;   /* the X/Y pad: a matte touch surface sunk into the panel, a faint printed grid */
    gfx_rect((float)p->left - 1, (float)p->top - 1, (float)rw(p) + 2, (float)rh(p) + 2, 0xFF2A2A2D);
    skin_touch(p->left, p->top, rw(p), rh(p));
    skin_recess(p->left, p->top, rw(p), rh(p), 5.0f * L.s);
    for (int k = 1; k < 4; k++) {
        float gx = p->left + rw(p) * k / 4.0f, gy = p->top + rh(p) * k / 4.0f;
        gfx_line(gx, p->top + 2.0f, gx, p->bottom - 2.0f, 0.8f, 0x18FFFFFF);
        gfx_line(p->left + 2.0f, gy, p->right - 2.0f, gy, 0.8f, 0x18FFFFFF);
    }
    text_c(dc, (p->left + p->right) / 2, p->bottom + sc(4), sc(80), sc(14), "X/Y PAD", font_label, T_LABEL);
}

/* the ribbon: a matte black touch strip in a brushed aluminium rim over the keys, a glowing marker where it is touched */
static void ribbon_static(void) {
    RECT *r = &L.ribbon;
    int b = (int)lroundf(2.5f * L.s);
    skin_shadow((float)r->left, (float)r->top + sc(2), (float)rw(r), (float)rh(r), 3.0f * L.s, 0.6f);
    skin_metal_plate(r->left, r->top, rw(r), rh(r));
    skin_touch(r->left + b, r->top + b, rw(r) - 2 * b, rh(r) - 2 * b);
    skin_recess(r->left + b, r->top + b, rw(r) - 2 * b, rh(r) - 2 * b, 3.0f * L.s);
    float x = (float)r->left, y = (float)r->top, w = (float)rw(r), h = (float)rh(r);
    for (int k = 1; k < 24; k++) {   /* faint tick marks along it, a longer one at the centre */
        float tx = x + b + (w - 2 * b) * k / 24.0f, th = k == 12 ? h - 2 * b - 2 : (h - 2 * b) * 0.3f;
        gfx_line(tx, y + h - b - 1, tx, y + h - b - 1 - th, 1.0f, k == 12 ? 0x50FFFFFF : 0x26FFFFFF);
    }
}
static void draw_ribbon(HDC dc, ed_t *e) {
    RECT *r = &L.ribbon;
    float x = (float)r->left, y = (float)r->top, w = (float)rw(r), h = (float)rh(r), b = 2.5f * L.s;
    RECT lr = {(int)(x + b + sc(6)), (int)y, (int)(x + sc(80)), (int)(y + h)};
    text(dc, &lr, "RIBBON", font_tiny, e->hover == Z_RIBBON ? T_LABEL : RGB(96, 96, 100), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    float mx = x + b + e->ribbon * (w - 2 * b), my = y + h / 2;
    if (e->ribbon_on) {
        gfx_rect(mx - 9 * L.s, y + b, 18 * L.s, h - 2 * b, 0x3070D8FF);
        gfx_rect(mx - 4 * L.s, y + b, 8 * L.s, h - 2 * b, 0x6090E4FF);
        gfx_line(mx, y + b, mx, y + h - b, 2.0f * L.s, 0xFFE0F8FF);
        gfx_disc(mx, my, 3.0f * L.s, 0xFFFFFFFF);
    } else {
        gfx_line(mx, y + b + 1, mx, y + h - b - 1, 1.2f, 0x6088A8B4);
    }
}

/* the keyboard: white keys with a front lip, glossy black keys casting soft shadows, all in the shade of the strip
 * behind them. At rest it is part of the static layer; a key held down is drawn again, pressed. */
static const int HAS_BLACK[] = {1, 1, 0, 1, 1, 1, 0};
static float key_w(void) { return rw(&L.keys) / (float)KEYS_WHITE; }
static void black_x(int j, float *x0, float *x1) {   /* the black key to the right of white key j */
    float kw = key_w(), bw = kw * 0.58f, x = L.keys.left + (j + 1) * kw - bw / 2;
    *x0 = x;
    *x1 = x + bw;
}
static void keys_static(void) {
    RECT *k = &L.keys;
    float kw = key_w();
    int top = k->top, h = rh(k), bh = (int)lroundf(h * 0.62f);
    gfx_rect((float)k->left - 2, top - 4.0f, (float)rw(k) + 4, h + 4.0f, 0xFF070707);
    for (int j = 0; j < KEYS_WHITE; j++) skin_white_key(k->left + j * kw, k->left + (j + 1) * kw, top, h, 0);
    for (int j = 0; j < KEYS_WHITE - 1; j++) {
        if (!HAS_BLACK[j % 7]) continue;
        float x0, x1;
        black_x(j, &x0, &x1);
        skin_key_shadow(x0, x1, top, bh);
    }
    for (int j = 0; j < KEYS_WHITE - 1; j++) {
        if (!HAS_BLACK[j % 7]) continue;
        float x0, x1;
        black_x(j, &x0, &x1);
        skin_black_key(x0, x1, top, bh, 0);
    }
}
static int key_held(ed_t *e, int n) {   /* by the mouse, or the computer keyboard */
    return n == e->key || (n >= 0 && n < 128 && e->qk_down[n]);
}
static void draw_keys(HDC dc, ed_t *e) {
    RECT *k = &L.keys;
    float kw = key_w();
    int top = k->top, h = rh(k), bh = (int)lroundf(h * 0.62f), base = KEY_BASE + 12 * e->oct;
    for (int j = 0; j < KEYS_WHITE; j++) {   /* a held white key: down, with the black keys beside it on top again */
        if (!key_held(e, note_of_white(j, base))) continue;
        RECT kr = {(int)floorf(k->left + j * kw), top, (int)ceilf(k->left + (j + 1) * kw), top + h};
        skin_clip_push(&kr);
        skin_white_key(k->left + j * kw, k->left + (j + 1) * kw, top, h, 1);
        for (int b = j - 1; b <= j; b++) {
            if (b < 0 || b >= KEYS_WHITE - 1 || !HAS_BLACK[b % 7]) continue;
            float x0, x1;
            black_x(b, &x0, &x1);
            skin_key_shadow(x0, x1, top, bh);
            skin_black_key(x0, x1, top, bh, 0);
        }
        skin_clip_pop();
    }
    for (int j = 0; j < KEYS_WHITE - 1; j++) {   /* a held black key */
        if (!HAS_BLACK[j % 7] || !key_held(e, note_of_white(j, base) + 1)) continue;
        float x0, x1;
        black_x(j, &x0, &x1);
        skin_black_key(x0, x1, top, bh, 1);
    }
    for (int j = 0; j < KEYS_WHITE; j += 7) {
        float x = k->left + j * kw;
        char lab[8];
        snprintf(lab, sizeof lab, "C%d", note_of_white(j, base) / 12 - 1);
        text_c(dc, (int)(x + kw / 2), (int)(top + h - sc(24)), (int)kw, sc(13), lab, font_tiny, RGB(150, 150, 150));
    }
}

/* the static layer: the case, wood, frames and panels, everything printed, glass, slots, the keyboard at rest */
static void bake(HDC dc) {
    gfx_begin(dc);
    skin_bind(gfx_g);
    int cheek = sc(26), strip = L.ribbon.top - sc(6);   /* (the wood from just over the ribbon down) */
    skin_chassis(0, 0, L.w, L.h);
    skin_wood(cheek, strip, L.w - 2 * cheek, L.h - strip, 0, 3);   /* the strip over the keys and the front rail */
    for (int p = 0; p < NPANELS; p++)   /* the frames' shadows on the case and the wood */
        if (PANELS[p].style == 0)
            skin_shadow((float)sc(PANELS[p].x), (float)sc(PANELS[p].y) + 3 * L.s, (float)sc(PANELS[p].w),
                        (float)sc(PANELS[p].h), 6.0f * L.s, 0.65f);
    skin_shadow(-(float)sc(10), 0, (float)cheek + sc(10) + 2 * L.s, (float)L.h, 6.0f * L.s, 0.7f);   /* the cheeks' */
    skin_shadow((float)(L.w - cheek) - 2 * L.s, 0, (float)cheek + sc(10), (float)L.h, 6.0f * L.s, 0.7f);
    skin_wood(0, 0, cheek, L.h, 1, 5);
    skin_wood(L.w - cheek, 0, cheek, L.h, 1, 6);
    for (int side = 0; side < 2; side++) {   /* the cheeks' square ends and outer edges, slightly bevelled */
        float x0 = side ? (float)(L.w - cheek) : 0.0f, b = 1.5f * L.s;   /* (filled rects: square ends, no round caps) */
        gfx_rect(side ? x0 + cheek - b : x0, 0, b, (float)L.h, side ? 0x60000000 : 0x50FFF4E0);   /* outer edge */
        gfx_rect(x0, 0, (float)cheek, b, 0x58FFF4E0);                                   /* top end: catching the light */
        gfx_rect(x0, L.h - b, (float)cheek, b, 0x70000000);                             /* bottom end: in shade */
    }
    gfx_rect((float)cheek, L.h - 1.5f * L.s, (float)(L.w - 2 * cheek), 1.5f * L.s, 0x60000000);   /* the rail's lower edge */
    draw_panels(dc);
    for (int c = 0; c < NCTLS; c++) {   /* knob scales */
        int i = CTLS[c].param;
        float cx, cy, r;
        if (L.kind[i] == K_KNOB || L.kind[i] == K_SMALL || L.kind[i] == K_BIG) knob_geom(i, &cx, &cy, &r, NULL);
        else if (L.kind[i] == K_OCT) octave_geom(i, &cx, &cy, &r);
        else {
            if (L.kind[i] != K_LEDS && L.kind[i] != K_TOGGLE) {   /* an option list's window */
                RECT w = button_win(i);
                small_lcd_glass(&w);
            }
            continue;
        }
        knob_scale(cx, cy, r);
    }
    {
        float cx, cy, r;
        encoder_geom(&cx, &cy, &r);
        knob_scale(cx, cy, r);
    }
    for (int w = 0; w < NWAVES; w++) {   /* the oscillators' little screens */
        RECT r = wave_rect(&WAVES[w]);
        skin_lcd(r.left, r.top, rw(&r), rh(&r));
        skin_recess(r.left, r.top, rw(&r), rh(&r), 3.0f * L.s);
        gfx_frame((float)r.left, (float)r.top, (float)rw(&r), (float)rh(&r), 1.2f, 0xFFB9C6CE);
    }
    screen_static();
    small_lcd_glass(&L.tempo);   /* the TEMPO readout's glass (hub.c) */
    {   /* our name, printed on the console with a faint shadow */
        RECT b1 = L.brand;
        OffsetRect(&b1, 0, 1);
        text(dc, &b1, "Moo Won", font_brand, RGB(8, 8, 9), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        text(dc, &L.brand, "Moo Won", font_brand, RGB(196, 198, 202), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    controller_static(dc);
    rate_leds_static(dc);   /* (skin_text only: no GDI+ of its own) */
    ribbon_static();
    keys_static();
    if (!IsRectEmpty(&L.kb_hint)) {   /* (2026-10-30) the computer keyboard's hint, burnt into the rail like the name */
        static const char T[] = "COMPUTER KEYBOARD: CLICK THE PANEL OR A KEY FIRST, THEN PLAY  A S D F G H J K L  (WHITE "
                                "KEYS)  W E T Y U O P  (BLACK KEYS)   Z / X  OCTAVE";
        RECT r = L.kb_hint, r1 = r;
        OffsetRect(&r1, 0, 1);
        text(dc, &r1, T, font_tiny, RGB(232, 190, 130), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        text(dc, &r, T, font_tiny, RGB(74, 40, 18), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    {   /* our name, burnt into the front rail under the keys */
        RECT r = {L.keys.right - sc(150), L.keys.bottom + sc(1), L.keys.right - sc(10), L.h - sc(1)};
        RECT r1 = r;
        OffsetRect(&r1, 0, 1);
        text(dc, &r1, "Moo Won", font_title, RGB(232, 190, 130), DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        text(dc, &r, "Moo Won", font_title, RGB(74, 40, 18), DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    gfx_end();
    skin_bind(NULL);
}

static void paint_rect(ed_t *e, HDC out, const RECT *r) {   /* everything that shows in r */
    clipr = *r;
    HDC dc = skin_frame(L.w, L.h, L.s, &clipr, bake);
    if (!dc) return;
    int saved = SaveDC(dc);
    IntersectClipRect(dc, clipr.left, clipr.top, clipr.right, clipr.bottom);
    gfx_begin(dc);
    skin_bind(gfx_g);
    draw_mores(e);
    for (int w = 0; w < NWAVES; w++) {
        RECT wr = wave_rect(&WAVES[w]);
        if (vis(&wr, 0)) draw_wave(e, &WAVES[w]);
    }
    if (vis(&L.euclid, sc(4))) draw_euclid(dc, e);
    rate_leds_draw(e, &clipr);
    int pad = sc(16);
    for (int c = 0; c < NCTLS; c++) {
        int i = CTLS[c].param;
        if (!vis(&L.ctl[i], pad)) continue;
        switch (L.kind[i]) {
        case K_KNOB: case K_SMALL: case K_BIG: draw_knob(dc, e, i); break;
        case K_OCT: draw_octave(dc, e, i); break;
        case K_LEDS: draw_leds(dc, e, i); break;
        case K_TOGGLE: draw_toggle(dc, e, i); break;
        default: draw_button(dc, e, i); break;
        }
    }
    gfx_end();   /* quick assign's rings, DEST buttons and the source picked (quickmod.c: its own surface) */
    skin_bind(NULL);
    qm_draw(dc, e, &clipr);
    gfx_begin(dc);
    skin_bind(gfx_g);
    draw_console(dc, e);
    draw_controller(dc, e);
    if (vis(&L.ribbon, sc(4))) draw_ribbon(dc, e);
    if (vis(&L.keys, 0)) draw_keys(dc, e);
    gfx_end();
    skin_bind(NULL);
    RestoreDC(dc, saved);
    skin_present(out, &clipr);
}

/* the part of the window to paint: the DC's clip region (WM_PRINTCLIENT), else its system region (WM_PAINT: what is
 * visible and needs painting, in screen coordinates), as rectangles; 0 for "all of it" */
static int paint_rects(HDC out, RECT *rs, int max) {
    HRGN rgn = CreateRectRgn(0, 0, 0, 0);
    int n = 0, got = rgn ? GetClipRgn(out, rgn) : -1;
    if (got == 0 && GetRandomRgn(out, rgn, SYSRGN) == 1) {
        POINT o = {0, 0};
        HWND w = WindowFromDC(out);
        if (w) {
            ClientToScreen(w, &o);
            OffsetRgn(rgn, -o.x, -o.y);
            got = 1;
        }
    }
    if (got == 1) {
        DWORD size = GetRegionData(rgn, 0, NULL);
        RGNDATA *d = size ? malloc(size) : NULL;
        if (d && GetRegionData(rgn, size, d) && d->rdh.nCount > 0 && (int)d->rdh.nCount <= max) {
            const RECT *r = (const RECT *)d->Buffer;
            for (DWORD k = 0; k < d->rdh.nCount; k++) rs[n++] = r[k];
        } else if (d && d->rdh.nCount > (DWORD)max) {
            rs[n++] = d->rdh.rcBound;   /* many pieces: their bounds */
        }
        free(d);
    }
    if (rgn) DeleteObject(rgn);
    return n;
}

void paint(ed_t *e, HDC out) {
    RECT all = {0, 0, L.w, L.h}, rs[16];
    int was = e->abs;
    e->abs = 0;   /* the panel shows the focused Synth (a SETUP cell being dragged keeps its own flag) */
    int n = paint_rects(out, rs, 16);
    if (n == 0) {   /* no region: the clip box, or everything */
        if (GetClipBox(out, &rs[0]) == ERROR || IsRectEmpty(&rs[0])) rs[0] = all;
        n = 1;
    }
    for (int k = 0; k < n; k++)
        if (IntersectRect(&rs[k], &rs[k], &all)) paint_rect(e, out, &rs[k]);
    e->abs = was;
}
