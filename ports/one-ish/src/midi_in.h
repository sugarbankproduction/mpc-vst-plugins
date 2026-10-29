/* MIDI input settings of an instance (src/synths.h holds one; src/midi_in_run.h works it). Not part of any sound: the
 * desktop window's SETTINGS page (desktop/settings.c) keeps them in its own file and sends them in as a "midicfg"
 * command (set_param "state", the path the window's other commands take), on its own thread. render() and midi() only
 * read them. Defaults: everything as before the SETTINGS page existed. */
#ifndef ONEISH_MIDI_IN_H
#define ONEISH_MIDI_IN_H
#include <stdint.h>

enum { PEDAL_SUS, PEDAL_EXP1, PEDAL_EXP2, PEDALS };
static const uint8_t PEDAL_ROLE[PEDALS] = {64, 11, 4};   /* the CC each pedal's function answers to inside the engine */

typedef struct {
    int rx_clock;            /* RECEIVE MIDI CLOCK: the host's tempo and song position drive the Synths (1, default) */
    float tempo;             /* the tempo they follow when it is off (10-280 BPM, default 120) */
    float host_bpm;          /* the host's last tempo (0: none yet), kept for when receiving goes back on */
    uint8_t pedal[PEDALS];   /* the CC numbers of the sustain, EXP 1 and EXP 2 pedals (default 64, 11, 4) */
} midi_in_t;
#endif
