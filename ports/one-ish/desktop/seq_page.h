/* The step sequencer's page on the centre screen (seq_page.c): the step editor, the SEQUENCER module's page LEDs, and
 * sequences saved as .oneseq files. Included by editor.h (ed_t holds a seq_ui_t) and by load_test.c (the test hooks). */
#pragma once

enum { SCR_SEQ = -4 };          /* the centre screen showing the step editor (see SCR_MATRIX) */
#define Z_SEQ_BASE 400000       /* hit codes of the step editor and the page LEDs: Z_SEQ_BASE and up (seq_page.c) */

/* load_test drives the page through effVendorSpecific: index TEST_SEQ, value = SQT_ op, ptr = a text buffer of
 * SQT_BUF bytes (a name in; a folder, the sequence's text or a message out) */
#define TEST_SEQ 0x4F315351
enum { SQT_SAVE = 1, SQT_LOAD, SQT_DIR, SQT_TEXT, SQT_MESSAGE, SQT_SELECTED, SQT_BUF = 8192 };

#ifdef NPARAMS   /* the editor (params.h); load_test only needs the numbers above */
#include "../src/stepseq.h"
typedef struct {
    sq_seq_t q;                 /* the engine's sequence as last read (a copy: the engine's own is changed by commands) */
    int len;                    /* seq_length then */
    int view;                   /* 0: NOTES, 1: MODS */
    int page;                   /* the page of 16 steps shown (0-3) */
    int step, lane, mlane;      /* the selected step, note lane and modulation lane */
    int range_to;               /* SHIFT + click on a step number: DUPLICATE copies step .. range_to (-1: the whole length) */
    int play;                   /* the step playing (-1: stopped), from the readout */
    char readout[32];           /* the engine's "seq_pos" text as last read */
    int dirty;                  /* read the sequence again at the next timer tick */
    int drag, drag_x, drag_y, drag_axis;   /* a drag on the page: what (a hit code), from where, which way */
    int drag_v0, drag_v1;       /* the value(s) it started from */
    int last_note, last_vel, last_gate;    /* what a new note gets (the last one edited) */
    char msg[240];              /* the last file operation's result, shown for a while */
    DWORD msg_at;
} seq_ui_t;
#endif
