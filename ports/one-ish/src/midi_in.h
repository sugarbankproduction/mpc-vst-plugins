/* MIDI input settings of an instance (src/synths.h holds one; src/midi_in_run.h works it). Not part of any sound: the
 * desktop window's SETTINGS page (desktop/settings.c) keeps them in its own file and sends them in as a "midicfg"
 * command (set_param "state", the path the window's other commands take), on its own thread. render() and midi() only
 * read them. Defaults: everything as before the SETTINGS page existed. */
#ifndef ONEISH_MIDI_IN_H
#define ONEISH_MIDI_IN_H
#include <stdint.h>

enum { PEDAL_SUS, PEDAL_EXP1, PEDAL_EXP2, PEDALS };
static const uint8_t PEDAL_ROLE[PEDALS] = {64, 11, 4};   /* the CC each pedal's function answers to inside the engine */

/* CC numbers a learned mapping can't take (besides the pedals' own): what the engine reads for other things (bank
 * select, mod wheel, the X/Y pad, NRPN data, the ribbon, sostenuto, NRPN and RPN numbers) and the channel mode
 * messages. The window's SETTINGS page shows the same list. */
static inline int mi_fixed(int cc) {
    return cc < 0 || cc > 119 || cc == 0 || cc == 1 || cc == 2 || cc == 3 || cc == 6 || cc == 16 || cc == 17
           || cc == 18 || cc == 32 || cc == 38 || cc == 66 || (cc >= 96 && cc <= 101);
}

typedef struct {
    int rx_clock;            /* RECEIVE MIDI CLOCK: the host's tempo and song position drive the Synths (1, default) */
    float tempo;             /* the tempo they follow when it is off (10-280 BPM, default 120) */
    float host_bpm;          /* the host's last tempo (0: none yet), kept for when receiving goes back on */
    uint8_t pedal[PEDALS];   /* the CC numbers of the sustain, EXP 1 and EXP 2 pedals (default 64, 11, 4) */
    /* MIDI LEARN (the SETTINGS page's MIDI tab): which param each CC moves, a fixed table set whole by "midicfg" */
    short map[128];          /* the params.json index CC n sets (-1: none) */
    uint8_t map14[128];      /* 1: 14-bit, CC n the MSB and CC n + 32 the LSB (n < 32) */
    uint8_t msb[16][32];     /* per channel, each 14-bit pair's last MSB */
    /* NRPN input (manual p149-158) */
    short nrpn[16];          /* per channel, the number CC 99 / 98 selected (-1: none, or an RPN) */
    uint8_t nmsb[16];        /* and its last data MSB (CC 6) */
    uint64_t chg[(NPARAMS + 63) / 64];   /* params set from MIDI since the host was last told (mpc_engine_changed) */
} midi_in_t;
#endif
