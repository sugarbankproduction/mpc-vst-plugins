/* (2026-10-31) Moog One preset files (.m1p) into Moo Won sounds (desktop/m1p.c, its table desktop/m1p_map.h). Plain C
 * with no Windows calls, so the offline checks (test/m1p_check.c) test the same code the window runs. Window thread
 * only (desktop/m1p_import.c), never the audio path. docs/PATCHES.md "Importing Moog One presets". */
#pragma once
#include <stddef.h>

/* m1p_convert's results */
enum { M1P_OK = 1, M1P_EMPTY = 0, M1P_NOT_M1P = -1, M1P_BINARY = -2, M1P_TOO_BIG = -3, M1P_NOTHING_MAPPED = -4 };
enum { M1P_TEXT_MAX = 4 << 20 };   /* a bigger file is not a preset */

typedef struct {
    int status;                   /* M1P_ */
    int settings;                 /* lines that are settings ("/key value") */
    int mapped, guessed;          /* settings turned into ours; of those, how many by a guess ('G' rows) */
    int covered;                  /* knob positions of values already taken, the matrix's own lines, the labels */
    int matrix;                   /* matrix rows turned into ours */
    int stored;                   /* ".../types/<type>/..." lines: an effect type's settings kept for when it is picked */
    int unknown;                  /* settings with no row in the table (and matrix rows that didn't fit) */
    int shared;                   /* Synth 2/3 settings of a control Moo Won shares between the Synths: skipped */
    int unreadable;               /* lines that aren't "key value", or values that don't make sense */
    int empty;                    /* values "null" */
    int synths;                   /* bit k: the file has settings for Synth k+1 */
    /* the preset's own labels ("/meta/name" ...; "": none), plain characters only */
    char name[64], category[32], type[32], mood[32], notes[600];
    char *assign;                 /* "key=value;..." in the engine's own units (malloc'd) */
    char *report;                 /* the report's lines for this file (malloc'd) */
    size_t alen, acap, rlen, rcap;
} m1p_t;

int m1p_looks_like(const char *text);                   /* 1: the first setting line is a "/key value" line */
int m1p_convert(const char *text, size_t len, m1p_t *m);   /* fills m (m1p_free it after), returns m->status */
void m1p_free(m1p_t *m);
const char *m1p_status_text(int status);                /* a failure in words */

/* The sound: a fresh engine instance (the defaults), then the file's values (Synths 2 and 3 off unless the file turns
 * them on); played a chord to check the level (a peak over -1 dBFS turns every Synth's Volume down towards a -4 dBFS
 * peak; silence is reported); then its state into `state` (n bytes). 1: done, 0: no engine or the state didn't fit. A separate instance: the one playing is untouched. */
typedef struct {
    double peak_db;    /* the loudest sample of the check, after any trim */
    double trim_db;    /* how much Volume was turned down (0: not) */
    int silent, finite;
} m1p_level_t;
int m1p_make_state(const char *assign, char *state, int n, m1p_level_t *lv);
