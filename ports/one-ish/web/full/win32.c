/* The web build's Win32 (win32/windows.h): windows, messages, timers, the mouse and keys, painting, menus, message
 * boxes, dialogs, file pickers and files, enough for the desktop editor to run unchanged.
 *
 * Threads: all of this runs on one thread, the editor thread (host.c), as a Windows UI thread does. The page (main
 * browser thread, site/full.js) sends input with web_event() into a queue here; the editor thread takes it from the
 * queue and turns it into window messages. What the editor draws lands in one framebuffer the size of the panel
 * (framebuffer pixels = the editor's own pixels); after each paint the dirty part is published (pres_t) and the page
 * copies it to its canvas. Anything modal (a menu, a message box, a dialog, a file picker) is shown by the page; the
 * editor thread meanwhile keeps running its message loop (timers and painting go on, as in a Windows modal loop) until
 * the page sends the answer back (EV_RESULT). */
#include <emscripten.h>
#include <emscripten/threading.h>
#include <errno.h>
#include <stdarg.h>
#include <dirent.h>
#include <fnmatch.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "commdlg.h"
#include "gdi.h"
#include "win32_web.h"
#undef fopen

/* ---- small things ---- */
static __thread DWORD last_error;
DWORD GetLastError(void) { return last_error; }
void SetLastError(DWORD e) { last_error = e; }
static double now_ms(void) { return emscripten_get_now(); }
DWORD GetTickCount(void) { return (DWORD)(uint64_t)now_ms(); }
ULONGLONG GetTickCount64(void) { return (ULONGLONG)now_ms(); }
BOOL QueryPerformanceFrequency(LARGE_INTEGER *f) { f->QuadPart = 1000000; return TRUE; }
BOOL QueryPerformanceCounter(LARGE_INTEGER *c) { c->QuadPart = (LONGLONG)(now_ms() * 1000.0); return TRUE; }
static void systime(SYSTEMTIME *t, int local) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    time_t s = ts.tv_sec;
    struct tm tm;
    if (local) localtime_r(&s, &tm); else gmtime_r(&s, &tm);
    t->wYear = tm.tm_year + 1900; t->wMonth = tm.tm_mon + 1; t->wDayOfWeek = tm.tm_wday; t->wDay = tm.tm_mday;
    t->wHour = tm.tm_hour; t->wMinute = tm.tm_min; t->wSecond = tm.tm_sec;
    t->wMilliseconds = (WORD)(ts.tv_nsec / 1000000);
}
void GetLocalTime(SYSTEMTIME *t) { systime(t, 1); }
void GetSystemTime(SYSTEMTIME *t) { systime(t, 0); }
void Sleep(DWORD ms) { usleep(ms * 1000); }
DWORD GetCurrentThreadId(void) { return (DWORD)(uintptr_t)pthread_self(); }
LONG InterlockedExchange(volatile LONG *t, LONG v) { return __atomic_exchange_n(t, v, __ATOMIC_SEQ_CST); }
LONG InterlockedIncrement(volatile LONG *t) { return __atomic_add_fetch(t, 1, __ATOMIC_SEQ_CST); }
LONG InterlockedDecrement(volatile LONG *t) { return __atomic_sub_fetch(t, 1, __ATOMIC_SEQ_CST); }
LONG InterlockedCompareExchange(volatile LONG *t, LONG v, LONG cmp) {
    __atomic_compare_exchange_n(t, &cmp, v, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return cmp;
}
DWORD GetEnvironmentVariableA(LPCSTR name, LPSTR buf, DWORD size) {
    const char *v = getenv(name);
    if (!v) { last_error = 203; return 0; }
    DWORD n = (DWORD)strlen(v);
    if (n + 1 > size) return n + 1;
    memcpy(buf, v, n + 1);
    return n;
}
BOOL SetEnvironmentVariableA(LPCSTR name, LPCSTR value) { return (value ? setenv(name, value, 1) : unsetenv(name)) == 0; }
void OutputDebugStringA(LPCSTR s) { fprintf(stderr, "%s", s); }
static char MODULE;   /* (the "DLL": one address to hand out as its handle) */
HMODULE GetModuleHandleA(LPCSTR name) { return (HMODULE)&MODULE; }
BOOL GetModuleHandleExA(DWORD flags, LPCSTR name, HMODULE *out) { *out = (HMODULE)&MODULE; return TRUE; }
DWORD GetModuleFileNameA(HMODULE m, LPSTR buf, DWORD size) {
    snprintf(buf, size, "C:\\Moo Won\\Moo Won.dll");
    return (DWORD)strlen(buf);
}
HMODULE LoadLibraryA(LPCSTR name) { return NULL; }
HMODULE LoadLibraryExA(LPCSTR name, HANDLE f, DWORD flags) { return NULL; }
FARPROC GetProcAddress(HMODULE m, LPCSTR name) { return NULL; }
BOOL FreeLibrary(HMODULE m) { return TRUE; }
BOOL SystemParametersInfoA(UINT action, UINT p, void *pv, UINT win_ini) {
    if (action != SPI_GETWORKAREA) return FALSE;
    RECT *r = pv;   /* the panel's own size plus its margins: the editor then picks the scale it was given */
    SetRect(r, 0, 0, 100000, 100000);
    return TRUE;
}
int GetSystemMetrics(int i) { return i == SM_CXDOUBLECLK || i == SM_CYDOUBLECLK ? 4 : 0; }
UINT GetDoubleClickTime(void) { return 500; }
BOOL MessageBeep(UINT t) { return TRUE; }

/* ---- rectangles ---- */
BOOL SetRect(LPRECT r, int l, int t, int ri, int b) { r->left = l; r->top = t; r->right = ri; r->bottom = b; return TRUE; }
BOOL SetRectEmpty(LPRECT r) { return SetRect(r, 0, 0, 0, 0); }
BOOL CopyRect(LPRECT d, const RECT *s) { *d = *s; return TRUE; }
BOOL IsRectEmpty(const RECT *r) { return r->right <= r->left || r->bottom <= r->top; }
BOOL PtInRect(const RECT *r, POINT p) { return p.x >= r->left && p.x < r->right && p.y >= r->top && p.y < r->bottom; }
BOOL InflateRect(LPRECT r, int dx, int dy) { r->left -= dx; r->top -= dy; r->right += dx; r->bottom += dy; return TRUE; }
BOOL OffsetRect(LPRECT r, int dx, int dy) { r->left += dx; r->top += dy; r->right += dx; r->bottom += dy; return TRUE; }
BOOL IntersectRect(LPRECT d, const RECT *a, const RECT *b) {
    RECT r = {max(a->left, b->left), max(a->top, b->top), min(a->right, b->right), min(a->bottom, b->bottom)};
    if (IsRectEmpty(&r)) { SetRectEmpty(d); return FALSE; }
    *d = r;
    return TRUE;
}
BOOL UnionRect(LPRECT d, const RECT *a, const RECT *b) {
    if (IsRectEmpty(a)) { *d = *b; return !IsRectEmpty(b); }
    if (IsRectEmpty(b)) { *d = *a; return TRUE; }
    RECT r = {min(a->left, b->left), min(a->top, b->top), max(a->right, b->right), max(a->bottom, b->bottom)};
    *d = r;
    return TRUE;
}
BOOL SubtractRect(LPRECT d, const RECT *a, const RECT *b) {
    RECT i;
    *d = *a;
    if (!IntersectRect(&i, a, b)) return !IsRectEmpty(d);
    if (i.top == a->top && i.bottom == a->bottom) {
        if (i.left == a->left) d->left = i.right;
        else if (i.right == a->right) d->right = i.left;
    } else if (i.left == a->left && i.right == a->right) {
        if (i.top == a->top) d->top = i.bottom;
        else if (i.bottom == a->bottom) d->bottom = i.top;
    }
    return !IsRectEmpty(d);
}
BOOL EqualRect(const RECT *a, const RECT *b) { return !memcmp(a, b, sizeof *a); }

/* ---- windows ---- */
typedef struct win win_t;
struct win {
    int alive, kind;          /* kind: W_WINDOW, W_DIALOG, W_CONTROL */
    WNDPROC proc;
    DLGPROC dlgproc;
    UINT cls_style;
    win_t *parent;
    int x, y, w, h;           /* in its parent's client coordinates */
    int visible;
    LONG_PTR user, dlg_user;
    char text[4096];          /* (room for a dialog's notes box) */
    int id;                   /* a control's ID */
    int ctl_class;            /* 0x80 button, 0x81 edit, 0x82 static, 0x85 combo box */
    DWORD style;
    int check, cursel, limit;
    char **items;             /* a combo box's lines */
    int nitems;
    rgn_t *update;            /* what needs painting (client coordinates) */
    int track_leave;
    INT_PTR end_result;
    int ended;
};
enum { W_WINDOW, W_DIALOG, W_CONTROL };
#define MAX_WIN 128
static win_t WINS[MAX_WIN];
static win_t *root;                 /* the page's canvas: the host window the editor opens in */
static surf_t fb;                   /* its pixels */
static win_t *capture, *focus_w, *hover_w;
static HCURSOR cursor_now;

typedef struct { char name[64]; WNDPROC proc; UINT style; } wclass_t;
static wclass_t CLASSES[16];
static int nclasses;
ATOM RegisterClassA(const WNDCLASSA *wc) {
    for (int k = 0; k < nclasses; k++)
        if (!strcmp(CLASSES[k].name, wc->lpszClassName)) { CLASSES[k].proc = wc->lpfnWndProc; return (ATOM)(k + 1); }
    if (nclasses >= 16) return 0;
    wclass_t *c = &CLASSES[nclasses++];
    snprintf(c->name, sizeof c->name, "%s", wc->lpszClassName);
    c->proc = wc->lpfnWndProc;
    c->style = wc->style;
    return (ATOM)nclasses;
}
BOOL UnregisterClassA(LPCSTR name, HINSTANCE i) {
    for (int k = 0; k < nclasses; k++)
        if (!strcmp(CLASSES[k].name, name)) { CLASSES[k] = CLASSES[--nclasses]; return TRUE; }
    return FALSE;
}
static win_t *W(HWND h) {
    win_t *w = (win_t *)h;
    return w >= WINS && w < WINS + MAX_WIN && w->alive ? w : NULL;
}
static win_t *win_new(void) {
    for (int k = 0; k < MAX_WIN; k++)
        if (!WINS[k].alive) {
            win_t *w = &WINS[k];
            memset(w, 0, sizeof *w);
            w->alive = 1;
            return w;
        }
    return NULL;
}
static void abs_pos(const win_t *w, int *x, int *y) {
    *x = *y = 0;
    for (; w; w = w->parent) { *x += w->x; *y += w->y; }
}

static LRESULT send(win_t *w, UINT m, WPARAM wp, LPARAM lp) {
    if (!w) return 0;
    if (w->kind == W_DIALOG && w->dlgproc) return w->dlgproc((HWND)w, m, wp, lp);
    if (w->proc) return w->proc((HWND)w, m, wp, lp);
    return DefWindowProcA((HWND)w, m, wp, lp);
}

HWND CreateWindowExA(DWORD ex, LPCSTR cls, LPCSTR title, DWORD style, int x, int y, int w, int h, HWND parent,
                     HMENU menu, HINSTANCE inst, LPVOID param) {
    wclass_t *c = NULL;
    for (int k = 0; k < nclasses; k++)
        if (!strcmp(CLASSES[k].name, cls)) c = &CLASSES[k];
    if (!c) { last_error = 1407; return NULL; }
    win_t *n = win_new();
    if (!n) return NULL;
    n->kind = W_WINDOW;
    n->proc = c->proc;
    n->cls_style = c->style;
    n->parent = parent == HWND_MESSAGE ? NULL : W(parent);
    n->x = x; n->y = y; n->w = w; n->h = h;
    n->visible = parent != HWND_MESSAGE && (style & WS_VISIBLE);
    n->style = style;
    snprintf(n->text, sizeof n->text, "%s", title ? title : "");
    n->update = rgn_make();
    CREATESTRUCTA cs = {param, inst, menu, parent, h, w, y, x, (LONG)style, title, cls, ex};
    send(n, WM_NCCREATE, 0, (LPARAM)&cs);
    if (send(n, WM_CREATE, 0, (LPARAM)&cs) == -1) { DestroyWindow((HWND)n); return NULL; }
    send(n, WM_SIZE, 0, MAKELPARAM(w, h));
    if (n->visible) InvalidateRect((HWND)n, NULL, TRUE);
    return (HWND)n;
}
static void kill_timers_of(win_t *w);
BOOL DestroyWindow(HWND h) {
    win_t *w = W(h);
    if (!w) return FALSE;
    for (int k = 0; k < MAX_WIN; k++)
        if (WINS[k].alive && WINS[k].parent == w) DestroyWindow((HWND)&WINS[k]);
    send(w, WM_DESTROY, 0, 0);
    send(w, WM_NCDESTROY, 0, 0);
    kill_timers_of(w);
    if (capture == w) capture = NULL;
    if (focus_w == w) focus_w = NULL;
    if (hover_w == w) hover_w = NULL;
    rgn_free(w->update);
    for (int k = 0; k < w->nitems; k++) free(w->items[k]);
    free(w->items);
    w->alive = 0;
    return TRUE;
}
LRESULT DefWindowProcA(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    win_t *w = W(h);
    switch (m) {
    case WM_NCCREATE: return TRUE;
    case WM_ERASEBKGND: return 1;
    case WM_SETTEXT: if (w) snprintf(w->text, sizeof w->text, "%s", (const char *)lp); return TRUE;
    case WM_GETTEXT: if (w && wp) { snprintf((char *)lp, wp, "%s", w->text); return (LRESULT)strlen((char *)lp); } return 0;
    case WM_GETTEXTLENGTH: return w ? (LRESULT)strlen(w->text) : 0;
    case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); EndPaint(h, &ps); return 0; }
    }
    return 0;
}
LRESULT CallWindowProcA(WNDPROC p, HWND w, UINT m, WPARAM wp, LPARAM lp) { return p ? p(w, m, wp, lp) : 0; }

static LRESULT control_msg(win_t *c, UINT m, WPARAM wp, LPARAM lp);
LRESULT SendMessageA(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    win_t *w = W(h);
    if (!w) return 0;
    if (w->kind == W_CONTROL) return control_msg(w, m, wp, lp);
    return send(w, m, wp, lp);
}

typedef struct { win_t *w; UINT m; WPARAM wp; LPARAM lp; } posted_t;
static posted_t POSTED[256];
static int npost_r, npost_w;
BOOL PostMessageA(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if ((npost_w + 1) % 256 == npost_r) return FALSE;
    POSTED[npost_w] = (posted_t){W(h), m, wp, lp};
    npost_w = (npost_w + 1) % 256;
    return TRUE;
}
BOOL IsWindow(HWND h) { return W(h) != NULL; }
BOOL IsWindowVisible(HWND h) { win_t *w = W(h); return w && w->visible; }
BOOL ShowWindow(HWND h, int cmd) {
    win_t *w = W(h);
    if (!w) return FALSE;
    int was = w->visible;
    w->visible = cmd != SW_HIDE;
    if (w->visible && !was) InvalidateRect(h, NULL, TRUE);
    return was;
}
BOOL EnableWindow(HWND h, BOOL on) { return TRUE; }
HWND GetParent(HWND h) { win_t *w = W(h); return w ? (HWND)w->parent : NULL; }
HWND GetAncestor(HWND h, UINT flags) {
    win_t *w = W(h);
    if (!w) return NULL;
    if (flags == GA_PARENT) return (HWND)w->parent;
    while (w->parent) w = w->parent;
    return (HWND)w;
}
HWND GetDesktopWindow(void) { return (HWND)root; }
HWND GetForegroundWindow(void) { return (HWND)root; }
BOOL SetForegroundWindow(HWND w) { return TRUE; }
HWND GetFocus(void) { return (HWND)focus_w; }
static LRESULT send(win_t *w, UINT m, WPARAM wp, LPARAM lp);
HWND SetFocus(HWND h) {
    win_t *o = focus_w, *n = W(h);
    if (n == o) return (HWND)o;
    focus_w = n;
    if (o && o->kind == W_WINDOW) send(o, WM_KILLFOCUS, (WPARAM)n, 0);
    if (n && n->kind == W_WINDOW) send(n, WM_SETFOCUS, (WPARAM)o, 0);
    return (HWND)o;
}
HWND GetActiveWindow(void) { return (HWND)root; }
HWND SetActiveWindow(HWND h) { return (HWND)root; }
LONG_PTR GetWindowLongPtrA(HWND h, int idx) {
    win_t *w = W(h);
    if (!w) return 0;
    if (idx == GWLP_USERDATA) return w->user;
    if (idx == DWLP_USER && w->kind == W_DIALOG) return w->dlg_user;
    if (idx == GWLP_HINSTANCE) return (LONG_PTR)&MODULE;
    if (idx == GWLP_WNDPROC) return (LONG_PTR)w->proc;
    return 0;
}
LONG_PTR SetWindowLongPtrA(HWND h, int idx, LONG_PTR v) {
    win_t *w = W(h);
    if (!w) return 0;
    LONG_PTR o = GetWindowLongPtrA(h, idx);
    if (idx == GWLP_USERDATA) w->user = v;
    else if (idx == DWLP_USER && w->kind == W_DIALOG) w->dlg_user = v;
    else if (idx == GWLP_WNDPROC) w->proc = (WNDPROC)v;
    return o;
}
BOOL GetClientRect(HWND h, LPRECT r) {
    win_t *w = W(h);
    if (!w) return FALSE;
    SetRect(r, 0, 0, w->w, w->h);
    return TRUE;
}
BOOL GetWindowRect(HWND h, LPRECT r) {
    win_t *w = W(h);
    if (!w) return FALSE;
    int x, y;
    abs_pos(w, &x, &y);
    SetRect(r, x, y, x + w->w, y + w->h);
    return TRUE;
}
BOOL SetWindowPos(HWND h, HWND after, int x, int y, int cx, int cy, UINT flags) {
    win_t *w = W(h);
    if (!w) return FALSE;
    if (!(flags & SWP_NOMOVE)) { w->x = x; w->y = y; }
    int resized = !(flags & SWP_NOSIZE) && (cx != w->w || cy != w->h);
    if (!(flags & SWP_NOSIZE)) { w->w = cx; w->h = cy; }
    if (flags & SWP_SHOWWINDOW) w->visible = 1;
    if (flags & SWP_HIDEWINDOW) w->visible = 0;
    if (resized) send(w, WM_SIZE, 0, MAKELPARAM(cx, cy));
    InvalidateRect(h, NULL, TRUE);
    return TRUE;
}
BOOL MoveWindow(HWND h, int x, int y, int cx, int cy, BOOL repaint) { return SetWindowPos(h, NULL, x, y, cx, cy, 0); }
BOOL ClientToScreen(HWND h, LPPOINT p) {
    int x, y;
    abs_pos(W(h), &x, &y);
    p->x += x;
    p->y += y;
    return TRUE;
}
BOOL ScreenToClient(HWND h, LPPOINT p) {
    int x, y;
    abs_pos(W(h), &x, &y);
    p->x -= x;
    p->y -= y;
    return TRUE;
}
int MapWindowPoints(HWND from, HWND to, LPPOINT p, UINT n) {
    int fx, fy, tx, ty;
    abs_pos(W(from), &fx, &fy);
    abs_pos(W(to), &tx, &ty);
    for (UINT k = 0; k < n; k++) { p[k].x += fx - tx; p[k].y += fy - ty; }
    return MAKELONG(fx - tx, fy - ty);
}
static POINT mouse_pt;
BOOL GetCursorPos(LPPOINT p) { *p = mouse_pt; return TRUE; }
static win_t *window_at(win_t *in, int x, int y) {   /* the topmost visible window under (x, y), absolute */
    for (int k = MAX_WIN - 1; k >= 0; k--) {
        win_t *c = &WINS[k];
        if (!c->alive || c->parent != in || !c->visible || c->kind != W_WINDOW) continue;
        int ax, ay;
        abs_pos(c, &ax, &ay);
        if (x >= ax && y >= ay && x < ax + c->w && y < ay + c->h) return window_at(c, x, y);
    }
    return in;
}
HWND WindowFromPoint(POINT p) { return root ? (HWND)window_at(root, p.x, p.y) : NULL; }
BOOL SetWindowTextA(HWND h, LPCSTR s) {
    win_t *w = W(h);
    if (!w) return FALSE;
    snprintf(w->text, sizeof w->text, "%s", s ? s : "");
    return TRUE;
}
int GetWindowTextA(HWND h, LPSTR s, int n) {
    win_t *w = W(h);
    if (!w || n <= 0) return 0;
    snprintf(s, n, "%s", w->text);
    return (int)strlen(s);
}
int GetWindowTextLengthA(HWND h) { win_t *w = W(h); return w ? (int)strlen(w->text) : 0; }
HCURSOR LoadCursorA(HINSTANCE i, LPCSTR name) { return (HCURSOR)name; }
HCURSOR SetCursor(HCURSOR c) {
    HCURSOR o = cursor_now;
    if (c != cursor_now) {
        cursor_now = c;
        int id = (int)(uintptr_t)c;
        MAIN_THREAD_ASYNC_EM_ASM({ if (Module.mwCursor) Module.mwCursor($0); }, id);
    }
    return o;
}
BOOL TrackMouseEvent(TRACKMOUSEEVENT *t) {
    win_t *w = W(t->hwndTrack);
    if (w && (t->dwFlags & TME_LEAVE)) w->track_leave = 1;
    return TRUE;
}

/* ---- painting: update regions, the framebuffer, publishing to the page ---- */
static pres_t PRES;
pres_t *win32_pres(void) { return &PRES; }
static int painting;

BOOL InvalidateRect(HWND h, const RECT *r, BOOL erase) {
    win_t *w = W(h);
    if (!w) {   /* NULL: every window */
        for (int k = 0; k < MAX_WIN; k++)
            if (WINS[k].alive && WINS[k].kind == W_WINDOW && WINS[k].visible) InvalidateRect((HWND)&WINS[k], r, erase);
        return TRUE;
    }
    if (w->kind != W_WINDOW || !w->update) return TRUE;
    RECT all = {0, 0, w->w, w->h}, x;
    if (!r) x = all;
    else if (!IntersectRect(&x, r, &all)) return TRUE;
    rgn_add_rect(w->update, &x);
    return TRUE;
}
BOOL ValidateRect(HWND h, const RECT *r) {
    win_t *w = W(h);
    if (!w || !w->update) return FALSE;
    if (!r) { SetRectRgn(w->update, 0, 0, 0, 0); return TRUE; }
    HRGN one = CreateRectRgnIndirect(r);
    CombineRgn(w->update, w->update, one, RGN_DIFF);
    DeleteObject(one);
    return TRUE;
}
BOOL GetUpdateRect(HWND h, LPRECT r, BOOL erase) {
    win_t *w = W(h);
    if (!w || !w->update) return FALSE;
    RECT b;
    int k = GetRgnBox(w->update, &b);
    if (r) *r = b;
    return k != NULLREGION;
}
int GetUpdateRgn(HWND h, HRGN rgn, BOOL erase) {
    win_t *w = W(h);
    if (!w || !w->update) return ERROR;
    return CombineRgn(rgn, w->update, w->update, RGN_COPY);
}

static int published;   /* PRES.seq as last published (even) */
static void pres_add(const RECT *r) {   /* (editor thread, framebuffer marked busy) a part of it changed */
    RECT a = {PRES.x0, PRES.y0, PRES.x1, PRES.y1};
    if (__atomic_load_n(&PRES.ack, __ATOMIC_ACQUIRE) == published) a = *r;   /* the page has shown the last one */
    else UnionRect(&a, &a, r);
    PRES.x0 = a.left; PRES.y0 = a.top; PRES.x1 = a.right; PRES.y1 = a.bottom;
}

HDC BeginPaint(HWND h, PAINTSTRUCT *ps) {
    win_t *w = W(h);
    memset(ps, 0, sizeof *ps);
    if (!w) return NULL;
    int ax, ay;
    abs_pos(w, &ax, &ay);
    HDC dc = dc_for_surface(&fb, h);
    dc_t *d = (dc_t *)dc;
    d->org = (POINT){ax, ay};
    rgn_t *sys = rgn_make();
    CombineRgn(sys, w->update, w->update, RGN_COPY);
    OffsetRgn(sys, ax, ay);
    RECT win = {ax, ay, ax + w->w, ay + w->h};
    HRGN wr = CreateRectRgnIndirect(&win);
    CombineRgn(sys, sys, wr, RGN_AND);
    DeleteObject(wr);
    dc_set_sysrgn(dc, sys);
    GetRgnBox(w->update, &ps->rcPaint);
    SetRectRgn(w->update, 0, 0, 0, 0);
    rgn_free(sys);
    ps->hdc = dc;
    if (painting++ == 0) __atomic_store_n(&PRES.seq, PRES.seq + 1, __ATOMIC_RELEASE);   /* odd: being drawn on */
    return dc;
}
BOOL EndPaint(HWND h, const PAINTSTRUCT *ps) {
    win_t *w = W(h);
    if (ps->hdc) {
        if (w) {
            int ax, ay;
            abs_pos(w, &ax, &ay);
            RECT r = ps->rcPaint;
            OffsetRect(&r, ax, ay);
            if (!IsRectEmpty(&r)) pres_add(&r);
        }
        DeleteDC(ps->hdc);
    }
    if (painting > 0 && --painting == 0) {
        __atomic_store_n(&PRES.seq, PRES.seq + 1, __ATOMIC_RELEASE);   /* even again: ready to show */
        published = PRES.seq;
    }
    return TRUE;
}
HDC GetDC(HWND h) {
    win_t *w = W(h);
    if (!w) return dc_for_surface(NULL, NULL);   /* the "screen": only good for CreateCompatibleDC */
    HDC dc = dc_for_surface(&fb, h);
    int ax, ay;
    abs_pos(w, &ax, &ay);
    ((dc_t *)dc)->org = (POINT){ax, ay};
    return dc;
}
int ReleaseDC(HWND h, HDC dc) {
    if (W(h)) {   /* drawn on directly (outside WM_PAINT): show all of the window */
        RECT r;
        GetWindowRect(h, &r);
        if (painting) pres_add(&r);   /* (inside a paint: shown with it) */
        else {
            __atomic_store_n(&PRES.seq, PRES.seq + 1, __ATOMIC_RELEASE);
            pres_add(&r);
            __atomic_store_n(&PRES.seq, PRES.seq + 1, __ATOMIC_RELEASE);
            published = PRES.seq;
        }
    }
    DeleteDC(dc);
    return 1;
}
static void paint_window(win_t *w) {
    if (!w->update || GetRgnBox(w->update, &(RECT){0}) == NULLREGION) return;
    double t0 = now_ms();
    send(w, WM_PAINT, 0, 0);
    double dt = now_ms() - t0;
    static double max_t, max_at;
    PRES.paint_ms += (float)((dt - PRES.paint_ms) * 0.1);
    if (dt > max_t || t0 - max_at > 1000) { max_t = dt; max_at = t0; }
    PRES.max_ms = (float)max_t;
    if (w->update) SetRectRgn(w->update, 0, 0, 0, 0);   /* (a WM_PAINT that didn't BeginPaint) */
}
BOOL UpdateWindow(HWND h) {
    win_t *w = W(h);
    if (w) paint_window(w);
    return TRUE;
}
BOOL RedrawWindow(HWND h, const RECT *r, HRGN rgn, UINT flags) {
    if (flags & RDW_INVALIDATE) InvalidateRect(h, r, TRUE);
    if (flags & RDW_UPDATENOW) UpdateWindow(h);
    return TRUE;
}
static void paint_all(void) {
    for (int k = 0; k < MAX_WIN; k++) {
        win_t *w = &WINS[k];
        if (w->alive && w->kind == W_WINDOW && w->visible) paint_window(w);
    }
}

/* ---- timers ---- */
typedef struct { win_t *w; UINT_PTR id; UINT ms; TIMERPROC fn; double due; int on; } timer_t_;
#define MAX_TIMERS 64
static timer_t_ TIMERS[MAX_TIMERS];
UINT_PTR SetTimer(HWND h, UINT_PTR id, UINT ms, TIMERPROC fn) {
    win_t *w = W(h);
    if (ms < 10) ms = 10;
    for (int k = 0; k < MAX_TIMERS; k++)
        if (TIMERS[k].on && TIMERS[k].w == w && TIMERS[k].id == id) {
            TIMERS[k].ms = ms; TIMERS[k].fn = fn; TIMERS[k].due = now_ms() + ms;
            return id;
        }
    for (int k = 0; k < MAX_TIMERS; k++)
        if (!TIMERS[k].on) {
            TIMERS[k] = (timer_t_){w, id ? id : (UINT_PTR)(k + 1000), ms, fn, now_ms() + ms, 1};
            return TIMERS[k].id;
        }
    return 0;
}
BOOL KillTimer(HWND h, UINT_PTR id) {
    win_t *w = W(h);
    for (int k = 0; k < MAX_TIMERS; k++)
        if (TIMERS[k].on && TIMERS[k].w == w && TIMERS[k].id == id) { TIMERS[k].on = 0; return TRUE; }
    return FALSE;
}
static void kill_timers_of(win_t *w) {
    for (int k = 0; k < MAX_TIMERS; k++)
        if (TIMERS[k].w == w) TIMERS[k].on = 0;
}
static double run_timers(void) {   /* fires what is due; returns ms until the next one */
    double t = now_ms(), next = 1e9;
    for (int k = 0; k < MAX_TIMERS; k++) {
        timer_t_ *tm = &TIMERS[k];
        if (!tm->on) continue;
        if (tm->due <= t) {
            tm->due = t + tm->ms;
            if (tm->fn) tm->fn((HWND)tm->w, WM_TIMER, tm->id, (DWORD)t);
            else if (tm->w && tm->w->alive) send(tm->w, WM_TIMER, tm->id, 0);
        }
        if (tm->on && tm->due - t < next) next = tm->due - t;
    }
    return next;
}

/* ---- the event queue (page -> editor thread) ---- */
typedef struct { int type, x, y, a, b; } ev_t;
#define NEV 1024
#define NEV_KEEP 128
static ev_t EVQ[NEV];
static int ev_r, ev_w;
static ev_t KEEPQ[NEV_KEEP];   /* events that must arrive, held here while the queue is full */
static int nkeep;
static pthread_mutex_t ev_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ev_cond = PTHREAD_COND_INITIALIZER;

EMSCRIPTEN_KEEPALIVE void web_event(int type, int x, int y, int a, int b) {   /* (any thread: the page) */
    pthread_mutex_lock(&ev_lock);
    int last = (ev_w + NEV - 1) % NEV;
    if (ev_r != ev_w && type == EV_MOVE && EVQ[last].type == EV_MOVE) {   /* the newest position is enough */
        EVQ[last] = (ev_t){type, x, y, a, b};
    } else if (ev_r != ev_w && type == EV_WHEEL && EVQ[last].type == EV_WHEEL && EVQ[last].b == b) {
        EVQ[last].a += a;   /* (turns of the wheel add up) */
    } else if ((ev_w + 1) % NEV != ev_r) {
        EVQ[ev_w] = (ev_t){type, x, y, a, b};
        ev_w = (ev_w + 1) % NEV;
    } else if (type != EV_MOVE && type != EV_WHEEL && type != EV_KEYDOWN && nkeep < NEV_KEEP) {
        KEEPQ[nkeep++] = (ev_t){type, x, y, a, b};   /* full: moves, wheel turns and key repeats can go; not these */
    }
    pthread_cond_signal(&ev_cond);
    pthread_mutex_unlock(&ev_lock);
}
static int ev_take(ev_t *e, double wait_ms) {
    pthread_mutex_lock(&ev_lock);
    if (ev_r == ev_w && !nkeep && wait_ms > 0) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        long long ns = ts.tv_nsec + (long long)(wait_ms * 1e6);
        ts.tv_sec += ns / 1000000000;
        ts.tv_nsec = ns % 1000000000;
        pthread_cond_timedwait(&ev_cond, &ev_lock, &ts);
    }
    int got = ev_r != ev_w;
    if (got) { *e = EVQ[ev_r]; ev_r = (ev_r + 1) % NEV; }
    else if (nkeep) { *e = KEEPQ[0]; memmove(KEEPQ, KEEPQ + 1, --nkeep * sizeof *KEEPQ); got = 1; }
    pthread_mutex_unlock(&ev_lock);
    return got;
}

/* ---- the mouse and keys ---- */
static BYTE keys[256];
static int mods_now, buttons_now;
SHORT GetKeyState(int vk) { return (vk >= 0 && vk < 256 && keys[vk]) ? (SHORT)0x8000 : 0; }
SHORT GetAsyncKeyState(int vk) { return GetKeyState(vk); }
HWND SetCapture(HWND h) { win_t *o = capture; capture = W(h); return (HWND)o; }
BOOL ReleaseCapture(void) {
    win_t *o = capture;
    capture = NULL;
    if (o) send(o, WM_CAPTURECHANGED, 0, 0);
    return TRUE;
}
HWND GetCapture(void) { return (HWND)capture; }
static void set_mods(int m) {
    mods_now = m;
    keys[VK_SHIFT] = keys[VK_LSHIFT] = (m & 1) != 0;
    keys[VK_CONTROL] = keys[VK_LCONTROL] = (m & 2) != 0;
    keys[VK_MENU] = keys[VK_LMENU] = (m & 4) != 0;
}
static WPARAM mk_flags(void) {
    WPARAM f = 0;
    if (buttons_now & 1) f |= MK_LBUTTON;
    if (buttons_now & 2) f |= MK_RBUTTON;
    if (buttons_now & 4) f |= MK_MBUTTON;
    if (buttons_now & 8) f |= MK_XBUTTON1;
    if (buttons_now & 16) f |= MK_XBUTTON2;
    if (mods_now & 1) f |= MK_SHIFT;
    if (mods_now & 2) f |= MK_CONTROL;
    return f;
}
static int modal_depth;   /* while a menu or dialog is up the panel takes no input (as its owner is disabled) */
static double last_down_t;
static POINT last_down_pt;
static int last_down_btn = -1;

static int snap_dx, snap_dy;   /* (a finger near a cable's part: touch.c; until the press ends) */
static void mouse_event(const ev_t *in) {
    ev_t shifted = *in;
    const ev_t *e = &shifted;
    if (in->type == EV_MOVE || in->type == EV_DOWN || in->type == EV_UP) { shifted.x += snap_dx; shifted.y += snap_dy; }
    if (in->type == EV_UP) snap_dx = snap_dy = 0;
    mouse_pt = (POINT){e->x, e->y};
    if (!root || modal_depth) return;
    win_t *w = capture ? capture : window_at(root, e->x, e->y);
    if (!w || w == root) w = capture ? capture : NULL;
    if (!w) return;
    int ax, ay;
    abs_pos(w, &ax, &ay);
    LPARAM lp = MAKELPARAM((short)(e->x - ax), (short)(e->y - ay));
    if (e->type == EV_WHEEL) {   /* wheel messages carry screen coordinates */
        set_mods(e->b);
        WPARAM wp = MAKEWPARAM(mk_flags(), (short)e->a);
        send(w, WM_MOUSEWHEEL, wp, MAKELPARAM((short)e->x, (short)e->y));
        return;
    }
    if (e->type == EV_MOVE) {
        set_mods(e->b);
        buttons_now = e->a;
        if (hover_w && hover_w != w && hover_w->track_leave) { hover_w->track_leave = 0; send(hover_w, WM_MOUSELEAVE, 0, 0); }
        hover_w = w;
        send(w, WM_SETCURSOR, (WPARAM)w, MAKELPARAM(1 /* HTCLIENT */, WM_MOUSEMOVE));
        send(w, WM_MOUSEMOVE, mk_flags(), lp);
        return;
    }
    if (e->type == EV_LEAVE) {
        if (hover_w && hover_w->track_leave && !capture) { hover_w->track_leave = 0; send(hover_w, WM_MOUSELEAVE, 0, 0); }
        hover_w = NULL;
        return;
    }
    int btn = e->a;   /* 0 left, 1 middle, 2 right, 3 back, 4 forward (the browser's numbering) */
    set_mods(e->b);
    static const int BIT[5] = {1, 4, 2, 8, 16};
    static const int VK[5] = {VK_LBUTTON, VK_MBUTTON, VK_RBUTTON, VK_XBUTTON1, VK_XBUTTON2};
    if (btn < 0 || btn > 4) return;
    if (e->type == EV_DOWN) {
        buttons_now |= BIT[btn];
        keys[VK[btn]] = 1;
        double t = now_ms();
        int dbl = (w->cls_style & CS_DBLCLKS) && btn == last_down_btn && t - last_down_t < 500 &&
                  abs(e->x - last_down_pt.x) <= 4 && abs(e->y - last_down_pt.y) <= 4;
        last_down_t = dbl ? 0 : t;
        last_down_pt = mouse_pt;
        last_down_btn = btn;
        UINT m = btn == 0 ? (dbl ? WM_LBUTTONDBLCLK : WM_LBUTTONDOWN) : btn == 2 ? (dbl ? WM_RBUTTONDBLCLK : WM_RBUTTONDOWN)
                 : btn == 1 ? WM_MBUTTONDOWN : WM_XBUTTONDOWN;
        WPARAM wp = mk_flags();
        if (btn >= 3) wp |= (WPARAM)(btn == 3 ? XBUTTON1 : XBUTTON2) << 16;
        send(w, m, wp, lp);
    } else {
        buttons_now &= ~BIT[btn];
        keys[VK[btn]] = 0;
        UINT m = btn == 0 ? WM_LBUTTONUP : btn == 2 ? WM_RBUTTONUP : btn == 1 ? WM_MBUTTONUP : WM_XBUTTONUP;
        WPARAM wp = mk_flags();
        if (btn >= 3) wp |= (WPARAM)(btn == 3 ? XBUTTON1 : XBUTTON2) << 16;
        send(w, m, wp, lp);
    }
}
static win_t *key_target(void) {
    if (focus_w && focus_w->kind == W_WINDOW) return focus_w;
    for (int k = 0; k < MAX_WIN; k++)   /* else the panel: the first window inside the canvas */
        if (WINS[k].alive && WINS[k].kind == W_WINDOW && WINS[k].parent == root && WINS[k].visible) return &WINS[k];
    return NULL;
}
static void key_event(const ev_t *e) {
    set_mods(e->b);
    int vk = e->x & 0xFF;
    if (e->type == EV_KEYDOWN) keys[vk] = 1; else keys[vk] = 0;
    set_mods(e->b);
    if (!root || (modal_depth && e->type == EV_KEYDOWN)) return;   /* (a release still arrives: no hanging notes) */
    win_t *w = key_target();
    if (!w) return;
    int alt = (e->b & 4) != 0;
    if (e->type == EV_KEYDOWN) {
        send(w, alt ? WM_SYSKEYDOWN : WM_KEYDOWN, vk, e->a ? (LPARAM)0x40000001 : 1);   /* bit 30: a repeat */
        if (e->y > 0 && !(e->b & 2) && !alt) send(w, WM_CHAR, (WPARAM)e->y, 1);   /* a typed character */
    } else {
        send(w, alt ? WM_SYSKEYUP : WM_KEYUP, vk, (LPARAM)0xC0000001u);
    }
}

/* ---- the message loop ---- */
typedef struct { int token, done, value; char *text; } wait_t;
static wait_t *WAITS[8];
static int nwaits;
static char *pending_download;   /* GetSaveFileName's file: handed to the page once written */
static void offer_download(const char *path);

static void dispatch(const ev_t *e) {
    switch (e->type) {
    case EV_MOVE: case EV_DOWN: case EV_UP: case EV_WHEEL: case EV_LEAVE: mouse_event(e); break;
    case EV_KEYDOWN: case EV_KEYUP: key_event(e); break;
    case EV_SNAP: touch_snap(e->x, e->y, e->a, &snap_dx, &snap_dy); break;
    case EV_RESULT:
        for (int k = 0; k < nwaits; k++)
            if (WAITS[k]->token == e->x) {
                WAITS[k]->value = e->y;
                WAITS[k]->text = (char *)(intptr_t)e->a;
                WAITS[k]->done = 1;
                return;
            }
        free((void *)(intptr_t)e->a);
        break;
    case EV_BLUR: {   /* the page lost the keyboard: as a Windows window losing focus (the editor lets go of notes) */
        memset(keys, 0, sizeof keys);
        mods_now = buttons_now = 0;
        win_t *w = key_target();
        if (w) send(w, WM_KILLFOCUS, 0, 0);
        if (capture) ReleaseCapture();
        break;
    }
    }
}
static void loop_once(double max_wait) {
    while (npost_r != npost_w) {
        posted_t p = POSTED[npost_r];
        npost_r = (npost_r + 1) % 256;
        if (p.w && p.w->alive) SendMessageA((HWND)p.w, p.m, p.wp, p.lp);
    }
    ev_t e;
    int n = 0;
    while (n < 64 && ev_take(&e, 0)) { dispatch(&e); n++; }
    double next = run_timers();
    paint_all();
    if (pending_download && !modal_depth) { offer_download(pending_download); free(pending_download); pending_download = NULL; }
    if (n == 0) {
        double wait = next < max_wait ? next : max_wait;
        if (wait > 0 && ev_take(&e, wait)) dispatch(&e);
    }
}
void win32_run(void) {
    for (;;) loop_once(1000);
}

static int next_token = 1;
static int modal_wait(wait_t *w) {   /* the editor thread's modal loop: until the page answers */
    if (nwaits >= 8) return 0;
    WAITS[nwaits++] = w;
    modal_depth++;
    while (!w->done) loop_once(50);
    modal_depth--;
    for (int k = 0; k < nwaits; k++)
        if (WAITS[k] == w) { WAITS[k] = WAITS[--nwaits]; break; }
    return w->value;
}
BOOL GetMessageA(MSG *m, HWND w, UINT lo, UINT hi) { loop_once(50); memset(m, 0, sizeof *m); return TRUE; }
BOOL PeekMessageA(MSG *m, HWND w, UINT lo, UINT hi, UINT rm) { return FALSE; }
BOOL TranslateMessage(const MSG *m) { return FALSE; }
LRESULT DispatchMessageA(const MSG *m) { return 0; }
void PostQuitMessage(int code) {}

/* ---- JSON for the page (ASCII only: the editor's Windows-1252 text goes as \u escapes) ---- */
typedef struct { char *s; size_t n, cap; } jbuf_t;
static void jput(jbuf_t *b, const char *s, size_t n) {
    if (b->n + n + 1 > b->cap) {
        size_t cap = (b->cap ? b->cap * 2 : 1024) + n;
        char *ns = realloc(b->s, cap);
        if (!ns) return;
        b->s = ns;
        b->cap = cap;
    }
    memcpy(b->s + b->n, s, n);
    b->n += n;
    b->s[b->n] = 0;
}
static void jraw(jbuf_t *b, const char *s) { jput(b, s, strlen(s)); }
static void jfmt(jbuf_t *b, const char *fmt, ...) {
    char t[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(t, sizeof t, fmt, ap);
    va_end(ap);
    jraw(b, t);
}
static const uint16_t W1252[32] = {0x20AC, 0x81, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030,
                                   0x0160, 0x2039, 0x0152, 0x8D, 0x017D, 0x8F, 0x90, 0x2018, 0x2019, 0x201C, 0x201D,
                                   0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D, 0x017E, 0x0178};
static void jstr(jbuf_t *b, const char *s) {
    jraw(b, "\"");
    for (; s && *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { char t[3] = {'\\', (char)c, 0}; jraw(b, t); }
        else if (c >= 0x20 && c < 0x7F) jput(b, (const char *)&c, 1);
        else jfmt(b, "\\u%04x", c >= 0x80 && c < 0xA0 ? W1252[c - 0x80] : c);
    }
    jraw(b, "\"");
}
static int ui_call(int kind, jbuf_t *j, int *value, char **text) {   /* show something modal on the page; wait */
    wait_t w = {next_token++, 0, 0, NULL};
    if (nwaits >= 8 || !j->s) {   /* (modal boxes nested this deep, or no memory: as if cancelled) */
        free(j->s);
        if (value) *value = 0;
        if (text) *text = NULL;
        return 0;
    }
    MAIN_THREAD_ASYNC_EM_ASM({ var s = UTF8ToString($2); _free($2); Module.mwUI($0, $1, s); }, kind, w.token, j->s);
    int v = modal_wait(&w);   /* (j->s is the page's to free now) */
    if (value) *value = v;
    if (text) *text = w.text; else free(w.text);
    return v;
}

/* ---- menus ---- */
typedef struct menu menu_t;
typedef struct { UINT flags; UINT_PTR id; char *text; } mitem_t;
struct menu { int magic, n; mitem_t *items; };
#define MENU_MAGIC 0x4D454E55
HMENU CreatePopupMenu(void) {
    menu_t *m = calloc(1, sizeof *m);
    if (m) m->magic = MENU_MAGIC;
    return (HMENU)m;
}
HMENU CreateMenu(void) { return CreatePopupMenu(); }
static menu_t *M(HMENU h) { menu_t *m = (menu_t *)h; return m && m->magic == MENU_MAGIC ? m : NULL; }
BOOL InsertMenuA(HMENU h, UINT pos, UINT flags, UINT_PTR id, LPCSTR text) {
    menu_t *m = M(h);
    if (!m) return FALSE;
    mitem_t *ni = realloc(m->items, (m->n + 1) * sizeof *ni);
    if (!ni) return FALSE;
    m->items = ni;
    int at = (flags & MF_BYPOSITION) && pos < (UINT)m->n ? (int)pos : m->n;
    memmove(&m->items[at + 1], &m->items[at], (m->n - at) * sizeof *ni);
    m->items[at] = (mitem_t){flags & ~MF_BYPOSITION, id, strdup((flags & MF_SEPARATOR) || !text ? "" : text)};
    m->n++;
    return TRUE;
}
BOOL AppendMenuA(HMENU h, UINT flags, UINT_PTR id, LPCSTR text) { return InsertMenuA(h, (UINT)-1, flags, id, text); }
BOOL DestroyMenu(HMENU h) {
    menu_t *m = M(h);
    if (!m) return FALSE;
    for (int k = 0; k < m->n; k++) {
        if (m->items[k].flags & MF_POPUP) DestroyMenu((HMENU)m->items[k].id);
        free(m->items[k].text);
    }
    free(m->items);
    m->magic = 0;
    free(m);
    return TRUE;
}
int GetMenuItemCount(HMENU h) { menu_t *m = M(h); return m ? m->n : -1; }
static mitem_t *find_item(menu_t *m, UINT id, UINT flags) {
    if (flags & MF_BYPOSITION) return id < (UINT)m->n ? &m->items[id] : NULL;
    for (int k = 0; k < m->n; k++) {
        if (!(m->items[k].flags & MF_POPUP) && m->items[k].id == id) return &m->items[k];
        if (m->items[k].flags & MF_POPUP) {
            mitem_t *in = find_item(M((HMENU)m->items[k].id), id, flags);
            if (in) return in;
        }
    }
    return NULL;
}
DWORD CheckMenuItem(HMENU h, UINT id, UINT flags) {
    menu_t *m = M(h);
    mitem_t *it = m ? find_item(m, id, flags) : NULL;
    if (!it) return (DWORD)-1;
    DWORD was = it->flags & MF_CHECKED;
    it->flags = (it->flags & ~MF_CHECKED) | (flags & MF_CHECKED);
    return was;
}
BOOL EnableMenuItem(HMENU h, UINT id, UINT flags) {
    menu_t *m = M(h);
    mitem_t *it = m ? find_item(m, id, flags) : NULL;
    if (!it) return -1;
    it->flags = (it->flags & ~(MF_GRAYED | MF_DISABLED)) | (flags & (MF_GRAYED | MF_DISABLED));
    return TRUE;
}
static void menu_json(jbuf_t *j, menu_t *m) {
    jraw(j, "[");
    for (int k = 0; m && k < m->n; k++) {
        mitem_t *it = &m->items[k];
        if (k) jraw(j, ",");
        jfmt(j, "{\"f\":%u,", it->flags);
        if (it->flags & MF_POPUP) { jraw(j, "\"sub\":"); menu_json(j, M((HMENU)it->id)); jraw(j, ",\"id\":0"); }
        else jfmt(j, "\"id\":%u", (unsigned)it->id);
        jraw(j, ",\"t\":");
        jstr(j, it->text);
        jraw(j, "}");
    }
    jraw(j, "]");
}
BOOL TrackPopupMenu(HMENU h, UINT flags, int x, int y, int reserved, HWND hw, const RECT *r) {
    menu_t *m = M(h);
    if (!m) return FALSE;
    jbuf_t j = {0};
    jfmt(&j, "{\"x\":%d,\"y\":%d,\"align\":%u,\"items\":", x, y, flags);
    menu_json(&j, m);
    jraw(&j, "}");
    int id = 0;
    ui_call(UI_MENU, &j, &id, NULL);
    ReleaseCapture();
    if (flags & TPM_RETURNCMD) return id;
    if (id && !(flags & TPM_NONOTIFY)) PostMessageA(hw, WM_COMMAND, MAKEWPARAM(id, 0), 0);
    return id != 0;
}
BOOL EndMenu(void) { return TRUE; }

/* ---- message boxes ---- */
int MessageBoxA(HWND w, LPCSTR text, LPCSTR caption, UINT type) {
    jbuf_t j = {0};
    jfmt(&j, "{\"type\":%u,\"text\":", type);
    jstr(&j, text);
    jraw(&j, ",\"caption\":");
    jstr(&j, caption ? caption : "Moo Won");
    jraw(&j, "}");
    int v = 0;
    ui_call(UI_MSGBOX, &j, &v, NULL);
    return v ? v : IDCANCEL;
}

/* ---- dialogs: the template's controls, kept here; the page shows them and sends back what was typed ---- */
static const WORD *skip_sz(const WORD *p, char *out, int n) {   /* a template string or ordinal */
    if (out && n) out[0] = 0;
    if (*p == 0xFFFF) return p + 2;
    int k = 0;
    while (*p) {
        if (out && k < n - 1) out[k++] = (char)(*p < 256 ? *p : '?');
        p++;
    }
    if (out && n) out[k] = 0;
    return p + 1;
}
static win_t *dlg_ctl(win_t *d, int id) {
    for (int k = 0; k < MAX_WIN; k++)
        if (WINS[k].alive && WINS[k].parent == d && WINS[k].kind == W_CONTROL && WINS[k].id == id) return &WINS[k];
    return NULL;
}
static LRESULT control_msg(win_t *c, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_SETTEXT: snprintf(c->text, sizeof c->text, "%s", (const char *)lp); return TRUE;
    case WM_GETTEXT: if (!wp) return 0; snprintf((char *)lp, wp, "%s", c->text); return (LRESULT)strlen((char *)lp);
    case WM_GETTEXTLENGTH: return (LRESULT)strlen(c->text);
    case EM_LIMITTEXT: c->limit = (int)wp; return 0;
    case EM_SETSEL: return 0;
    case BM_GETCHECK: return c->check;
    case BM_SETCHECK: c->check = (int)wp; return 0;
    case CB_ADDSTRING: {
        char **ni = realloc(c->items, (c->nitems + 1) * sizeof *ni);
        if (!ni) return CB_ERR;
        c->items = ni;
        c->items[c->nitems] = strdup((const char *)lp);
        return c->nitems++;
    }
    case CB_RESETCONTENT:
        for (int k = 0; k < c->nitems; k++) free(c->items[k]);
        c->nitems = 0;
        c->cursel = -1;
        return 0;
    case CB_GETCOUNT: return c->nitems;
    case CB_SETCURSEL: c->cursel = (int)wp < c->nitems ? (int)wp : -1; return c->cursel;
    case CB_GETCURSEL: return c->cursel;
    case CB_GETLBTEXTLEN: return (int)wp >= 0 && (int)wp < c->nitems ? (LRESULT)strlen(c->items[wp]) : CB_ERR;
    case CB_GETLBTEXT:
        if ((int)wp < 0 || (int)wp >= c->nitems) return CB_ERR;
        strcpy((char *)lp, c->items[wp]);
        return (LRESULT)strlen(c->items[wp]);
    case CB_FINDSTRINGEXACT:
        for (int k = (int)wp + 1 < 0 ? 0 : (int)wp + 1; k < c->nitems; k++)
            if (!strcasecmp(c->items[k], (const char *)lp)) return k;
        for (int k = 0; k < c->nitems && k <= (int)wp; k++)
            if (!strcasecmp(c->items[k], (const char *)lp)) return k;
        return CB_ERR;
    }
    return 0;
}
HWND GetDlgItem(HWND d, int id) { return (HWND)dlg_ctl(W(d), id); }
int GetDlgCtrlID(HWND h) { win_t *w = W(h); return w ? w->id : 0; }
UINT GetDlgItemTextA(HWND d, int id, LPSTR s, int n) {
    win_t *c = dlg_ctl(W(d), id);
    if (!c || n <= 0) { if (n > 0) s[0] = 0; return 0; }
    snprintf(s, n, "%s", c->text);
    return (UINT)strlen(s);
}
BOOL SetDlgItemTextA(HWND d, int id, LPCSTR s) {
    win_t *c = dlg_ctl(W(d), id);
    if (!c) return FALSE;
    snprintf(c->text, sizeof c->text, "%s", s ? s : "");
    return TRUE;
}
UINT GetDlgItemInt(HWND d, int id, BOOL *ok, BOOL sign) {
    char t[32];
    GetDlgItemTextA(d, id, t, sizeof t);
    char *end;
    long v = strtol(t, &end, 10);
    if (ok) *ok = end != t;
    return (UINT)v;
}
BOOL SetDlgItemInt(HWND d, int id, UINT v, BOOL sign) {
    char t[32];
    snprintf(t, sizeof t, sign ? "%d" : "%u", v);
    return SetDlgItemTextA(d, id, t);
}
LRESULT SendDlgItemMessageA(HWND d, int id, UINT m, WPARAM wp, LPARAM lp) {
    win_t *c = dlg_ctl(W(d), id);
    return c ? control_msg(c, m, wp, lp) : 0;
}
BOOL CheckDlgButton(HWND d, int id, UINT check) { return SendDlgItemMessageA(d, id, BM_SETCHECK, check, 0) == 0; }
UINT IsDlgButtonChecked(HWND d, int id) { return (UINT)SendDlgItemMessageA(d, id, BM_GETCHECK, 0, 0); }
BOOL EndDialog(HWND h, INT_PTR result) {
    win_t *d = W(h);
    if (!d || d->kind != W_DIALOG) return FALSE;
    d->end_result = result;
    d->ended = 1;
    return TRUE;
}
static void dialog_json(jbuf_t *j, win_t *d, int cx, int cy, int first) {
    jfmt(j, "{\"cx\":%d,\"cy\":%d,\"open\":%d,\"title\":", cx, cy, first);
    jstr(j, d->text);
    jraw(j, ",\"controls\":[");
    int any = 0;
    for (int k = 0; k < MAX_WIN; k++) {
        win_t *c = &WINS[k];
        if (!c->alive || c->parent != d || c->kind != W_CONTROL) continue;
        if (any++) jraw(j, ",");
        jfmt(j, "{\"id\":%d,\"cls\":%d,\"style\":%u,\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d,\"check\":%d,\"sel\":%d,\"limit\":%d,"
                "\"focus\":%d,\"text\":", c->id, c->ctl_class, c->style, c->x, c->y, c->w, c->h, c->check, c->cursel,
             c->limit, focus_w == c);
        jstr(j, c->text);
        jraw(j, ",\"items\":[");
        for (int i = 0; i < c->nitems; i++) {
            if (i) jraw(j, ",");
            jstr(j, c->items[i]);
        }
        jraw(j, "]}");
    }
    jraw(j, "]}");
}
/* the page's answer: "<command id>\n" then one line per control, "<id>\t<check>\t<sel>\t<text>" */
static int apply_answer(win_t *d, const char *t) {
    if (!t) return IDCANCEL;
    int cmd = atoi(t);
    const char *line = strchr(t, '\n');
    while (line && *++line) {
        int id, check, sel;
        const char *p = line;
        id = atoi(p); p = strchr(p, '\t'); if (!p) break;
        check = atoi(++p); p = strchr(p, '\t'); if (!p) break;
        sel = atoi(++p); p = strchr(p, '\t'); if (!p) break;
        p++;
        const char *e = strchr(p, '\n');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        win_t *c = dlg_ctl(d, id);
        if (c) {
            c->check = check;
            c->cursel = sel;
            if (c->ctl_class == 0x81 || c->ctl_class == 0x85) {
                size_t k = 0;   /* (a lone CR is a typed line break: CR LF, as an edit box gives it) */
                for (size_t i = 0; i < n && k + 2 < sizeof c->text; i++) {
                    c->text[k++] = p[i];
                    if (p[i] == '\r') c->text[k++] = '\n';
                }
                c->text[k] = 0;
                if (c->ctl_class == 0x85 && sel >= 0 && sel < c->nitems) snprintf(c->text, sizeof c->text, "%s", c->items[sel]);
            }
        }
        line = e;
    }
    return cmd;
}
INT_PTR DialogBoxIndirectParamA(HINSTANCE i, LPCDLGTEMPLATEA t, HWND owner, DLGPROC proc, LPARAM lp) {
    win_t *d = win_new();
    if (!d) return -1;
    d->kind = W_DIALOG;
    d->dlgproc = proc;
    d->parent = NULL;
    const WORD *p = (const WORD *)(t + 1);
    p = skip_sz(p, NULL, 0);   /* menu */
    p = skip_sz(p, NULL, 0);   /* class */
    p = skip_sz(p, d->text, sizeof d->text);
    if (t->style & DS_SETFONT) { p++; p = skip_sz(p, NULL, 0); }
    for (int k = 0; k < t->cdit; k++) {
        p = (const WORD *)(((uintptr_t)p + 3) & ~(uintptr_t)3);
        const DLGITEMTEMPLATE *it = (const DLGITEMTEMPLATE *)p;
        p = (const WORD *)(it + 1);
        win_t *c = win_new();
        if (!c) break;
        c->kind = W_CONTROL;
        c->parent = d;
        c->id = it->id;
        c->style = it->style;
        c->x = it->x; c->y = it->y; c->w = it->cx; c->h = it->cy;
        c->cursel = -1;
        if (*p == 0xFFFF) { c->ctl_class = p[1]; p += 2; }
        else { char cls[32]; p = skip_sz(p, cls, sizeof cls); c->ctl_class = !strcasecmp(cls, "EDIT") ? 0x81 : !strcasecmp(cls, "BUTTON") ? 0x80 : !strcasecmp(cls, "COMBOBOX") ? 0x85 : 0x82; }
        p = skip_sz(p, c->text, sizeof c->text);
        WORD extra = *p++;
        p = (const WORD *)((const BYTE *)p + extra);
    }
    win_t *was_focus = focus_w;
    d->dlg_user = 0;
    if (proc((HWND)d, WM_INITDIALOG, 0, lp)) {
        for (int k = 0; k < MAX_WIN; k++)   /* TRUE: Windows focuses the first tab stop */
            if (WINS[k].alive && WINS[k].parent == d && (WINS[k].style & WS_TABSTOP)) { focus_w = &WINS[k]; break; }
    }
    int first = 1;
    while (!d->ended) {
        jbuf_t j = {0};
        dialog_json(&j, d, t->cx, t->cy, first);
        first = 0;
        char *ans = NULL;
        ui_call(UI_DIALOG, &j, NULL, &ans);
        int cmd = apply_answer(d, ans);
        free(ans);
        win_t *c = dlg_ctl(d, cmd);
        proc((HWND)d, WM_COMMAND, MAKEWPARAM(cmd, BN_CLICKED), (LPARAM)c);
        if (cmd == IDCANCEL && !d->ended) EndDialog((HWND)d, IDCANCEL);
    }
    MAIN_THREAD_ASYNC_EM_ASM({ if (Module.mwDialogClose) Module.mwDialogClose(); });
    INT_PTR r = d->end_result;
    DestroyWindow((HWND)d);
    focus_w = was_focus;
    return r;
}

/* ---- files: Windows paths on the browser's file system ("C:\Users\web\..." is /Users/web/...) ---- */
const char *web_path(const char *in, char *out, size_t n) {
    if (!in) return NULL;
    if (((in[0] >= 'A' && in[0] <= 'Z') || (in[0] >= 'a' && in[0] <= 'z')) && in[1] == ':') in += 2;
    size_t k = 0;
    for (; *in && k + 1 < n; in++) out[k++] = *in == '\\' ? '/' : *in;
    out[k] = 0;
    if (k == 0) snprintf(out, n, "/");
    return out;
}
FILE *web_fopen(const char *path, const char *mode) {
    char p[1024];
    FILE *f = fopen(web_path(path, p, sizeof p), mode);
    if (f && strpbrk(mode, "wa+")) web_files_changed();
    return f;
}
static void set_err_from_errno(void) {
    last_error = errno == ENOENT ? ERROR_FILE_NOT_FOUND : errno == EACCES ? ERROR_ACCESS_DENIED
                 : errno == EEXIST ? ERROR_ALREADY_EXISTS : errno == ENOSPC ? ERROR_DISK_FULL : ERROR_PATH_NOT_FOUND;
}
static void fill_find(WIN32_FIND_DATAA *d, const char *dir, const char *name) {
    char full[1024];
    struct stat st;
    memset(d, 0, sizeof *d);
    snprintf(d->cFileName, sizeof d->cFileName, "%s", name);
    snprintf(full, sizeof full, "%s/%s", dir, name);
    if (stat(full, &st) == 0) {
        d->dwFileAttributes = S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
        d->nFileSizeLow = (DWORD)st.st_size;
        uint64_t ft = ((uint64_t)st.st_mtime + 11644473600ULL) * 10000000ULL;   /* Windows file time */
        d->ftLastWriteTime.dwLowDateTime = (DWORD)ft;
        d->ftLastWriteTime.dwHighDateTime = (DWORD)(ft >> 32);
        d->ftCreationTime = d->ftLastAccessTime = d->ftLastWriteTime;
    }
}
typedef struct { DIR *dir; char path[1024], pat[256]; } find_t;
static int find_next(find_t *f, WIN32_FIND_DATAA *d) {
    struct dirent *de;
    while ((de = readdir(f->dir)))
        if (!fnmatch(f->pat, de->d_name, FNM_CASEFOLD)) { fill_find(d, f->path, de->d_name); return 1; }
    return 0;
}
HANDLE FindFirstFileA(LPCSTR pattern, WIN32_FIND_DATAA *d) {
    find_t *f = calloc(1, sizeof *f);
    if (!f) return INVALID_HANDLE_VALUE;
    web_path(pattern, f->path, sizeof f->path);
    char *slash = strrchr(f->path, '/');
    if (slash) { snprintf(f->pat, sizeof f->pat, "%s", slash + 1); if (slash == f->path) slash[1] = 0; else *slash = 0; }
    else { snprintf(f->pat, sizeof f->pat, "%s", f->path); snprintf(f->path, sizeof f->path, "."); }
    if (!strcmp(f->pat, "*.*")) snprintf(f->pat, sizeof f->pat, "*");
    f->dir = opendir(f->path);
    if (!f->dir || !find_next(f, d)) {
        if (f->dir) closedir(f->dir);
        free(f);
        last_error = ERROR_FILE_NOT_FOUND;
        return INVALID_HANDLE_VALUE;
    }
    return f;
}
BOOL FindNextFileA(HANDLE h, WIN32_FIND_DATAA *d) {
    if (find_next(h, d)) return TRUE;
    last_error = 18;   /* ERROR_NO_MORE_FILES */
    return FALSE;
}
BOOL FindClose(HANDLE h) {
    find_t *f = h;
    if (!f || h == INVALID_HANDLE_VALUE) return FALSE;
    closedir(f->dir);
    free(f);
    return TRUE;
}
DWORD GetFileAttributesA(LPCSTR path) {
    char p[1024];
    struct stat st;
    if (stat(web_path(path, p, sizeof p), &st)) { set_err_from_errno(); return INVALID_FILE_ATTRIBUTES; }
    return S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
}
BOOL GetFileAttributesExA(LPCSTR path, GET_FILEEX_INFO_LEVELS lvl, void *out) {
    char p[1024];
    struct stat st;
    if (stat(web_path(path, p, sizeof p), &st)) { set_err_from_errno(); return FALSE; }
    WIN32_FILE_ATTRIBUTE_DATA *a = out;
    memset(a, 0, sizeof *a);
    a->dwFileAttributes = S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    a->nFileSizeLow = (DWORD)st.st_size;
    uint64_t ft = ((uint64_t)st.st_mtime + 11644473600ULL) * 10000000ULL;
    a->ftLastWriteTime.dwLowDateTime = (DWORD)ft;
    a->ftLastWriteTime.dwHighDateTime = (DWORD)(ft >> 32);
    a->ftCreationTime = a->ftLastAccessTime = a->ftLastWriteTime;
    return TRUE;
}
LONG CompareFileTime(const FILETIME *a, const FILETIME *b) {
    uint64_t x = (uint64_t)a->dwHighDateTime << 32 | a->dwLowDateTime, y = (uint64_t)b->dwHighDateTime << 32 | b->dwLowDateTime;
    return x < y ? -1 : x > y;
}
BOOL CreateDirectoryA(LPCSTR path, void *sec) {
    char p[1024];
    if (mkdir(web_path(path, p, sizeof p), 0777)) { set_err_from_errno(); return FALSE; }
    web_files_changed();
    return TRUE;
}
int SHCreateDirectoryExA(HWND w, LPCSTR path, const void *sec) {
    char p[1024];
    web_path(path, p, sizeof p);
    for (char *s = p + 1; *s; s++)
        if (*s == '/') { *s = 0; mkdir(p, 0777); *s = '/'; }
    if (mkdir(p, 0777) && errno != EEXIST) { set_err_from_errno(); return (int)last_error; }
    web_files_changed();
    return errno == EEXIST ? ERROR_ALREADY_EXISTS : ERROR_SUCCESS;
}
BOOL RemoveDirectoryA(LPCSTR path) {
    char p[1024];
    if (rmdir(web_path(path, p, sizeof p))) { set_err_from_errno(); return FALSE; }
    web_files_changed();
    return TRUE;
}
BOOL DeleteFileA(LPCSTR path) {
    char p[1024];
    if (unlink(web_path(path, p, sizeof p))) { set_err_from_errno(); return FALSE; }
    web_files_changed();
    return TRUE;
}
BOOL MoveFileExA(LPCSTR from, LPCSTR to, DWORD flags) {
    char a[1024], b[1024];
    web_path(from, a, sizeof a);
    web_path(to, b, sizeof b);
    struct stat st;
    if (!(flags & MOVEFILE_REPLACE_EXISTING) && stat(b, &st) == 0) { last_error = ERROR_ALREADY_EXISTS; return FALSE; }
    if (rename(a, b)) { set_err_from_errno(); return FALSE; }
    web_files_changed();
    return TRUE;
}
BOOL MoveFileA(LPCSTR from, LPCSTR to) { return MoveFileExA(from, to, 0); }
BOOL CopyFileA(LPCSTR from, LPCSTR to, BOOL fail_if_exists) {
    char a[1024], b[1024];
    struct stat st;
    web_path(from, a, sizeof a);
    web_path(to, b, sizeof b);
    if (fail_if_exists && stat(b, &st) == 0) { last_error = ERROR_ALREADY_EXISTS; return FALSE; }
    FILE *i = fopen(a, "rb"), *o = i ? fopen(b, "wb") : NULL;
    if (!i || !o) { if (i) fclose(i); set_err_from_errno(); return FALSE; }
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, i)) > 0) fwrite(buf, 1, n, o);
    fclose(i);
    fclose(o);
    web_files_changed();
    return TRUE;
}
DWORD GetFullPathNameA(LPCSTR path, DWORD size, LPSTR buf, LPSTR *file_part) {
    char t[1024];
    if (path[0] && path[1] == ':') snprintf(t, sizeof t, "%s", path);
    else if (path[0] == '\\' || path[0] == '/') snprintf(t, sizeof t, "C:%s", path);
    else snprintf(t, sizeof t, "C:\\%s", path);
    for (char *s = t; *s; s++)
        if (*s == '/') *s = '\\';
    DWORD n = (DWORD)strlen(t);
    if (n + 1 > size) return n + 1;
    memcpy(buf, t, n + 1);
    if (file_part) { char *s = strrchr(buf, '\\'); *file_part = s ? s + 1 : buf; }
    return n;
}
DWORD GetTempPathA(DWORD size, LPSTR buf) { snprintf(buf, size, "C:\\Temp\\"); mkdir("/Temp", 0777); return (DWORD)strlen(buf); }
DWORD GetCurrentDirectoryA(DWORD size, LPSTR buf) { snprintf(buf, size, "C:\\Users\\web"); return (DWORD)strlen(buf); }
HRESULT SHGetFolderPathA(HWND w, int csidl, HANDLE token, DWORD flags, LPSTR path) {
    int c = csidl & 0xFF;
    const char *p = c == CSIDL_APPDATA ? "C:\\Users\\web\\AppData\\Roaming" : "C:\\Users\\web\\Documents";
    snprintf(path, MAX_PATH, "%s", p);
    SHCreateDirectoryExA(NULL, p, NULL);
    return S_OK;
}

/* ---- the page's file pickers ---- */
static void offer_download(const char *path) {   /* (editor thread) hand a written file to the browser */
    char p[1024];
    struct stat st;
    if (stat(web_path(path, p, sizeof p), &st) || S_ISDIR(st.st_mode)) return;
    const char *name = strrchr(p, '/');
    char *cp = strdup(p), *base = strdup(name ? name + 1 : p);   /* (the page reads them later: copies it frees) */
    MAIN_THREAD_ASYNC_EM_ASM({ Module.mwDownload(UTF8ToString($0), UTF8ToString($1)); _free($0); _free($1); }, cp, base);
}
static BOOL file_dialog(OPENFILENAMEA *o, int save) {
    jbuf_t j = {0};
    jfmt(&j, "{\"save\":%d,\"multi\":%d,\"filter\":", save, (o->Flags & OFN_ALLOWMULTISELECT) != 0);
    jraw(&j, "[");
    int any = 0;
    for (const char *f = o->lpstrFilter; f && *f;) {   /* "Name\0*.a;*.b\0...\0\0" */
        const char *pat = f + strlen(f) + 1;
        if (any++) jraw(&j, ",");
        jraw(&j, "[");
        jstr(&j, f);
        jraw(&j, ",");
        jstr(&j, pat);
        jraw(&j, "]");
        f = pat + strlen(pat) + 1;
    }
    jraw(&j, "],\"title\":");
    jstr(&j, o->lpstrTitle ? o->lpstrTitle : "");
    jraw(&j, ",\"file\":");
    const char *def = o->lpstrFile ? o->lpstrFile : "";
    const char *base = strrchr(def, '\\');
    jstr(&j, base ? base + 1 : def);
    jraw(&j, ",\"ext\":");
    jstr(&j, o->lpstrDefExt ? o->lpstrDefExt : "");
    jraw(&j, "}");
    char *ans = NULL;
    int ok = 0;
    ui_call(save ? UI_SAVE : UI_OPEN, &j, &ok, &ans);
    if (!ok || !ans) { free(ans); return FALSE; }
    /* open: the page put the files in C:\Upload; ans is "C:\Upload\0a\0b\0\0"-style as '\n'-separated text */
    if (save) {
        snprintf(o->lpstrFile, o->nMaxFile, "C:\\Download\\%s", ans);
        mkdir("/Download", 0777);
        free(pending_download);
        pending_download = strdup(o->lpstrFile);
    } else {
        int n = 0;
        char *names[64], *s = ans;
        while (s && *s && n < 64) {
            names[n++] = s;
            char *e = strchr(s, '\n');
            if (!e) break;
            *e = 0;
            s = e + 1;
        }
        if (n == 0) { free(ans); return FALSE; }
        if (n == 1 || !(o->Flags & OFN_ALLOWMULTISELECT)) {
            snprintf(o->lpstrFile, o->nMaxFile, "C:\\Upload\\%s", names[0]);
            const char *b = strrchr(o->lpstrFile, '\\');
            o->nFileOffset = (WORD)(b - o->lpstrFile + 1);
        } else {   /* the folder, then each name, each ending in NUL, then one more NUL */
            size_t k = 0, cap = o->nMaxFile;
            if (cap < 16) { free(ans); return FALSE; }
            k += snprintf(o->lpstrFile, cap, "C:\\Upload") + 1;
            o->nFileOffset = (WORD)k;
            for (int i = 0; i < n && k + strlen(names[i]) + 2 < cap; i++) {
                strcpy(o->lpstrFile + k, names[i]);
                k += strlen(names[i]) + 1;
            }
            o->lpstrFile[k] = 0;
        }
    }
    free(ans);
    return TRUE;
}
BOOL GetOpenFileNameA(OPENFILENAMEA *o) { return file_dialog(o, 0); }
BOOL GetSaveFileNameA(OPENFILENAMEA *o) { return file_dialog(o, 1); }
DWORD CommDlgExtendedError(void) { return 0; }
HINSTANCE ShellExecuteA(HWND w, LPCSTR op, LPCSTR file, LPCSTR params, LPCSTR dir, INT show) {
    if (!file) return (HINSTANCE)(intptr_t)2;
    if (!strncmp(file, "http://", 7) || !strncmp(file, "https://", 8)) {
        MAIN_THREAD_ASYNC_EM_ASM({ window.open(UTF8ToString($0), "_blank", "noopener"); _free($0); }, strdup(file));
        return (HINSTANCE)(intptr_t)33;
    }
    char p[1024];
    struct stat st;
    if (stat(web_path(file, p, sizeof p), &st)) return (HINSTANCE)(intptr_t)2;
    if (S_ISDIR(st.st_mode)) {   /* a folder: the browser has nothing to open it with */
        MessageBoxA(w, "Folders can't be opened in the browser. Use EXPORT to save files to your computer.", "Moo Won", MB_OK);
        return (HINSTANCE)(intptr_t)33;
    }
    offer_download(file);   /* a file (a report, the settings): the browser saves it */
    return (HINSTANCE)(intptr_t)33;
}

/* ---- the page's canvas: the root window the editor opens in ---- */
static LRESULT CALLBACK root_proc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    if (m == WM_PAINT) {   /* (behind the panel: black) */
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        FillRect(dc, &ps.rcPaint, GetStockObject(BLACK_BRUSH));
        EndPaint(w, &ps);
        return 0;
    }
    return DefWindowProcA(w, m, wp, lp);
}
HWND win32_root(int w, int h) {
    fb.px = calloc((size_t)w * h, 4);
    if (!fb.px) return NULL;
    fb.w = fb.stride = w;
    fb.h = h;
    PRES.px = fb.px;
    PRES.w = w;
    PRES.h = h;
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = root_proc;
    wc.lpszClassName = "MooWonPage";
    RegisterClassA(&wc);
    root = (win_t *)CreateWindowExA(0, "MooWonPage", "Moo Won", WS_VISIBLE, 0, 0, w, h, NULL, NULL, NULL, NULL);
    return (HWND)root;
}
