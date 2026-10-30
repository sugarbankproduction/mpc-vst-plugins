/* The library around the user patches (library.c): edit safety (UNDO / REDO of any edit, COMPARE, SNAPSHOTs and
 * autosaves), tags and notes, performance sets, and MIDI program / bank change reaching user patches. Internal to the
 * desktop editor: included by patches.c, library.c and editor.c after editor.h. File formats: docs/PATCHES.md. */
#pragma once
#include <stdarg.h>
#include "editor.h"

enum { TAG_TYPE, TAG_CAT, TAG_MOOD, TAG_GROUP, TAG_KINDS };
enum { TAG_LEN = 24, TAG_LIST_MAX = 64, NOTES_MAX = 1000 };
enum { UNDO_MAX = 100, AUTOSAVE_KEEP = 32, SET_COUNT = 128, SET_SLOTS = 64, SNAP_NOTE_SHORT = 120 };
enum { LONG_PRESS_MS = 600, UNDO_IDLE_MS = 400 };

typedef struct { char tag[TAG_KINDS][TAG_LEN + 1]; char notes[NOTES_MAX + 1]; } meta_t;

typedef struct {   /* a sound and who it is: an undo step, the edit COMPARE hides */
    char *state, *base_state;   /* the engine's state; the saved version COMPARE plays (NULL: none) */
    float *base;                /* patches_t.base: the values the * compares with */
    int bank, prog, forced;
    char name[PATCH_NAME_MAX + 1];
    meta_t meta;
    char what[48];              /* "Cutoff", "loading Acid Line", "Randomise" */
} take_t;

typedef struct { char kind; int prog; char name[PATCH_NAME_MAX + 1]; } slot_t;   /* kind 0 empty, 'F' or 'U' */
typedef struct { char name[PATCH_NAME_MAX + 1]; char notes[NOTES_MAX + 1]; slot_t slot[SET_SLOTS]; } set_t;

typedef struct {
    char file[48];               /* its file name in the snapshot folder */
    char when[24];               /* "2026-10-29 14:03:12" */
    char of_bank;                /* 'F', 'U' or 'N': what it was a snapshot of */
    int of_prog;
    char of_name[PATCH_NAME_MAX + 1];
    int autosave;
    char note[SNAP_NOTE_SHORT + 1];
} snap_t;

typedef struct { char name[PATCH_NAME_MAX + 1]; FILETIME t; char tag[TAG_KINDS][TAG_LEN + 1]; } lmeta_t;

typedef struct lib {
    /* undo: steps before (undo) and after (redo) the sound now; a gesture's "before" while it is open */
    take_t undo[UNDO_MAX], redo[UNDO_MAX];
    int nundo, nredo, open, hold, applying, in_load;
    take_t before;
    float before_vals[NPARAMS];
    DWORD touched_at;
    char label[48];
    /* compare and autosave */
    int comparing;
    take_t stash;                /* the edit while COMPARE plays the saved version */
    char *base_state;            /* the saved version of the sound (NULL: none) */
    char *last_snap;             /* the newest snapshot taken or loaded (an autosave never repeats it) */
    /* tags and notes */
    meta_t cur;                  /* the current sound's */
    char list[TAG_KINDS][TAG_LIST_MAX][TAG_LEN + 1];
    int nlist[TAG_KINDS], lists_read;
    char filter[TAG_KINDS][TAG_LEN + 1];   /* the browser's filters, one per kind ("" none) */
    int sel_kind, sel_idx, show_notes;     /* the TAGS page's selected tag (-1 none) */
    lmeta_t *lm;                 /* the user patches' tags, by name (read again when a file changes) */
    int nlm, caplm;
    int *fview, *uview, nfview, nuview, capview;   /* the PATCHES page after the filters */
    meta_t save_meta;            /* the save dialog's choice, for the save that follows it */
    int save_meta_set;
    /* snapshots */
    snap_t *snaps;
    int nsnaps, capsnaps, snap_sel;
    /* performance sets */
    set_t *sets;
    int nsets, sets_read, cur_set, bank, cur_slot, arr_sel;
    int hold_slot, hold_done;
    DWORD hold_t0;
    /* MIDI program and bank change (the audio thread writes pending, the window's thread loads) */
    volatile LONG pending;
    int msb, lsb;
    DWORD ui_thread;
    int busy;                    /* a dialog of ours is open */
    int stale;                   /* a host call on another thread left: 1 the saved version, 2 the tags */
    double tempo;                /* the host's, for the home line (0: unknown) */
} lib_t;

/* patches.c's helpers (patches.c, "for library.c") */
char *pt_read(const char *path);
int pt_write(const char *path, const char *text);
const char *pt_why(DWORD err);
void pt_vsay(ed_t *e, const char *fmt, va_list ap);
void pt_apply_state(ed_t *e, const char *state);
void pt_parse(char *text, char *state, int n, int *bad, int *header);
char *pt_build(const char *name, const char *meta, const char *state);
int pt_user_index(ed_t *e, const char *name);
int pt_confirm(ed_t *e, const char *text);
HWND pt_owner(ed_t *e);
void pt_ui_save_as(ed_t *e);
int pt_patch_path(const char *name, char *out, int n, int create);
char *pt_saved_state(ed_t *e);
WORD *dlg_wstr(WORD *w, const char *s);
WORD *dlg_item(WORD *w, DWORD style, short x, short y, short cx, short cy, WORD id, WORD cls, const char *text);

/* library.c, for patches.c and editor.c */
lib_t *lib_new(ed_t *e);
void lib_free(ed_t *e);
void lib_touch(ed_t *e, const char *label);   /* an edit from the window is about to happen */
void lib_commit(ed_t *e);                    /* the gesture is over: one undo step if the sound changed */
void lib_hold(ed_t *e, int d);               /* +1 / -1: edits in between are one step */
int lib_undo(ed_t *e);
int lib_redo(ed_t *e);
void lib_before_load(ed_t *e, const char *what);   /* another sound is about to load: autosave, undo */
void lib_after_load(ed_t *e);
void lib_set_base(ed_t *e);                  /* what is loaded now is the saved version (mark_clean) */
void lib_drop_base(ed_t *e);                 /* there is no saved version */
void lib_saved_base(ed_t *e);                /* the saved version is the current patch's file or preset (not loaded) */
void lib_identity(ed_t *e);                  /* the current patch changed: its tags and notes */
void lib_host_restored(ed_t *e);             /* the host restored a project */
void lib_meta_read(const char *text, meta_t *m);
void lib_meta_lines(const meta_t *m, char *out, int n);
void lib_meta_for_save(ed_t *e, const char *kept, char *out, int n);
void lib_after_save(ed_t *e, const char *meta_text);
void lib_list_meta(ed_t *e);                 /* after patches_rescan */
int lib_view_count(ed_t *e, int col);        /* the PATCHES page's columns after the filters */
int lib_view_pos(ed_t *e, int col, int k);   /* entry k of column col: its list position, or -1 */
int lib_filtering(ed_t *e);
int lib_step_filtered(ed_t *e, int d);       /* PREV / NEXT through the filtered list; 0: not filtering */
int lib_load_pos(ed_t *e, int pos);          /* patches_load_pos with autosave and undo */
int lib_view_index(ed_t *e, int pos);        /* where list position pos is in its column after the filters (-1) */
int lib_cats(ed_t *e, char (*names)[TAG_LEN + 1], int *counts, int max);   /* the category tabs: ALL, then each */
const char *lib_cat_filter(ed_t *e);         /* the CATEGORY filter ("" none) */
void lib_cat_pick(ed_t *e, const char *cat); /* a category tab clicked ("ALL": no CATEGORY filter) */
void lib_end_compare(ed_t *e);               /* COMPARE off (the edit back) before a save reads the sound */
int lib_save_dialog(ed_t *e, char *name, meta_t *m);   /* the save box: name, tags, notes; 0 cancelled */
void lib_save_defaults(ed_t *e, meta_t *m);  /* the save box's starting tags */
/* (2026-10-30) the one-line boxes for parts.c: DK_PNAME a patch-style name (a file's), DK_SYNNAME a Synth's name */
enum { DK_TAG, DK_SETNAME, DK_NOTES, DK_SAVE, DK_PNAME, DK_SYNNAME };
int lib_ask(ed_t *e, int kind, const char *title, const char *prompt, char *text, int n);   /* 0: cancelled */
int synname_ok(const char *s, char *why, int n);   /* parts.c: a Synth's name (SYNNAME_MAX, no ; | \) */
void lib_space_changed(ed_t *e);             /* another user library: tags, sets, snapshots read again */
