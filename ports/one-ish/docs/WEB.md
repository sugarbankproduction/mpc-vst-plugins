# Moo Won in the browser

Two web builds, both published at **https://moo-won.vercel.app**:

- **The full plugin** (added 2026-11-01, the main page `/`): the same plugin as `Moo Won.dll`, with its whole panel,
  every page, menu and dialog, compiled for the browser with the editor's code unchanged.
- **Sound only** (added 2026-11-01, `/simple.html`): just the engine, with a preset list and a plain keyboard. It was the
  first step and stays as a light test page.

## Why not load the plugin file itself

`Moo Won.dll` is a Windows program; a browser can't run it without imitating a whole Windows PC, which is far too slow
for live sound (searched 2026-11-01: nothing does this for plugins). All of Moo Won is our own C code, so it is rebuilt
for the browser instead, the way other plugins reach the web (Web Audio Modules, iPlug2's web target).

## The full plugin (`web/full`)

The engine, the shared VST2 wrapper (`wrapper/vst2_wrap.c`) and the desktop editor (`desktop/*.c`, about 17,000 lines)
are compiled exactly as for Windows, against a small stand-in for the parts of Windows they use:

| File | What it does |
|---|---|
| `web/full/win32/windows.h` (+ `commdlg.h`, `windowsx.h`, ...) | The Win32, GDI and shell names the editor uses, and nothing more. `fopen` goes through `web_fopen` so Windows paths work |
| `web/full/win32.c` | Windows, messages, the mouse and keys, timers, painting, menus, message boxes, dialogs (read from the editor's in-memory dialog templates), file pickers, files |
| `web/full/gdi.c`, `gdi.h` | Device contexts, bitmaps, brushes, fonts, text (DrawText and friends), regions and clipping, BitBlt. Surfaces are 32-bit pixels in memory, laid out as a Windows DIB, so the editor's own pixel code works on them |
| `web/full/gdiplus.c` | The GDI+ calls `desktop/gfx.h`, `cables.c`, `more_pics.c` and `skin.c` declare (anti-aliased shapes, gradients), half a pixel shifted as GDI+ draws |
| `third_party/plutovg` | plutovg 1.3.2 (MIT): the anti-aliased drawing and font rendering behind gdi.c and gdiplus.c |
| `web/full/fonts` | Selawik (Microsoft, SIL Open Font License): the open stand-in for Segoe UI, the panel's font. Its letters are a touch wider |
| `web/full/host.c` | The host (what Carla is on Windows): makes the plugin, opens its editor, plays it, passes MIDI in, saves the sound |
| `web/full/build.sh` | Builds it all into `web/site/moowon_full.js` and `.wasm` (about 16 MB) |
| `web/full/serve.py` | A local server with the two headers the page needs (below); `POST /__shot` saves a canvas picture for checks |
| `web/site/index.html`, `full.js`, `full.css`, `ring_worklet.js` | The page: canvas, input, menus and dialogs as HTML, file pickers and downloads, the sound thread, MIDI |
| `web/site/sections.js` | The phone and narrow-window view: the panel's sections on tabs, the keyboard at the bottom |
| `web/site/camera.js` | The whole panel's camera: pinch, pan, PANEL ZOOM (below) |
| `web/full/gen_panels.py` | Run by the build: writes `web/site/panels.js` (not in git), PANEL ZOOM's rectangles, from the generated `layout.h` |
| `web/full/touch.c` | For fingers in the CABLES view: tells the page its state, lands a press on a nearby cable part (below) |

**Threads.** The page (the browser's main thread) only shows things and passes input on. An *editor thread* runs the
Windows message loop and makes every plugin call except sound, as a Windows UI thread does. An *audio thread* renders
128-frame blocks with `processReplacing` into a ring of samples in shared memory; the browser's sound thread
(`ring_worklet.js`) plays them, 1024 frames (about 23 ms) ahead by default (`?latency=` in the address changes it).
The plugin's own lock keeps the editor and audio threads apart, as on Windows. Threads that share memory need a
*cross-origin isolated* page: the server sends `Cross-Origin-Opener-Policy: same-origin` and
`Cross-Origin-Embedder-Policy: require-corp` (`web/site/vercel.json`, `serve.py`).

**Drawing.** The editor draws into one framebuffer the size of the panel; after each paint the editor thread publishes
the changed rectangle and the page copies it onto its canvas (`pres_t` in `win32_web.h`: a counter that is odd while a
paint is going on, and the page's acknowledgement). The panel's scale is picked at start from the window width and
the screen's pixel density (1 to 2: drawn at least at full size, since below that the labels crowd, and shrunk by
the browser to fit; `?scale=` overrides it, 0.5 to 2); after that the canvas is only stretched to fit.

**Menus, dialogs, files.** A menu, message box, dialog or file picker the editor asks for is drawn by the page as HTML;
the editor thread keeps running its message loop (timers and painting carry on) until the answer comes back, as a
Windows modal loop does. Dialogs are read from the editor's own in-memory templates (edit boxes, labels, buttons,
check boxes, drop-down lists). Files live in the browser (IndexedDB, mounted at `C:\Users\web`): patches, sets,
snapshots, settings and the current sound (`web_state.bin`, saved every 3 s when it changed) are all there on the next
visit. IMPORT and the other open boxes copy the files you pick into `C:\Upload`; EXPORT and other save boxes ask for a
name and hand the file to the browser's downloads. Opening a folder (SETTINGS, the importer's report) can't work in a
browser: a file is downloaded instead, a folder gives a message.

**Phones and narrow windows** (2026-11-01, the owner's pick of four mock-ups: "B could be cool for mobile"; `web/site/
sections.js`): the panel's own sections one at a time on tabs (SCREEN, OSC, LFO, ARP, FILTER, ENV, FX, PLAY), each as
large as the screen allows (at most 1.5 times full size; since 2026-11-02 fitted to the room, it never scrolls), with the keyboard and
the ribbon always at the bottom (`<` `>` move it an octave; from C4 at first) and WHOLE PANEL to switch back (SECTIONS
VIEW, under the whole panel, switches again; the choice is remembered). It starts by itself on a screen under 600
pixels wide, or under 1000 and upright. The editor still draws the whole panel; this view copies the parts it shows
and sends touches back at the panel's own coordinates (a drag stays on the piece it started on), and menus open where
the tap was. Sections come from the module boxes in `desktop/gen_layout.py` (base pixels). Checked in the built-in
browser at 375 x 812 (2026-11-01): every tab draws, a tapped key plays, tabs switch. Not on a real phone yet.

**Fingers, 2026-11-02** (offline; the engine, the sound and the editor are unchanged, the additions are in the page and
`web/full`. Nobody has tried any of this on a real phone yet: see "Checked" below).

- *The page is the screen.* Once playing it fills the window inside the safe areas (a notch, a home bar), never scrolls
  sideways, and the browser's own zoom is off (`viewport`, `touch-action`, and on iPhones `gesturestart`). A rotation
  lays everything out again, no reload (a `ResizeObserver` on the stage and on the sections view).
- *Sections view on its side* (a screen under 520 px tall): the tabs go down the left; the keys are shorter, 20% of the
  height, and still at the bottom; the KEYS button hides them for a bigger section (remembered). A tab's rectangles are
  fitted to the room, stacked or in one line, whichever comes out larger.
- *The camera* (`camera.js`, the whole panel only): the canvas is moved and scaled by a CSS transform, so nothing about
  drawing or sound changes; a touch still maps back through the canvas's on-screen rectangle. Two fingers always pinch
  and pan, wherever they land (3 screen pixels per panel pixel at most; never smaller than the whole panel; the panel
  can't leave the screen). One finger is the control under it. So that two fingers never press a button, a control hears
  of a finger only after 90 ms or as soon as it moves or lifts; the keyboard hears at once (notes aren't late), and a
  second finger after that lets go of what the first held. Visible buttons on the bar do the same: `-`, `+`, SHOW ALL. A
  mouse is never held back and its wheel still turns knobs.
- *PANEL ZOOM* (the bar's button, remembered): a tap on a panel glides the camera to frame it, whatever the screen's
  shape. **Tap versus focus: the focused panel's controls work at once; a tap anywhere else only focuses that panel, and a
  drag anywhere else pans** (with one finger; a tap never turns or presses anything outside the focus). `<` `>` go to the
  panel before and after, so a panel filling the screen isn't a dead end; SHOW ALL, or PANEL ZOOM again, is the whole
  panel. The rest of the panel is dimmed. A panel too wide for the screen at half size (the keyboard, upright) is shown
  at half size around the tap and panned with two fingers. The panels are the layout's own, plus the keyboard
  (`gen_panels.py`, so they follow the layout); taps between panels frame the big block around them.
- *CABLES on touch.* A CABLES button on the bar and beside the sections view's keys does what the key C does (the MATRIX
  page with its cables, again: hidden). Cables run across the whole panel, so from the sections view it goes to the whole
  panel and says so, and back when they are hidden. With a finger, a press lands on the nearest jack, cable knob, end dot,
  X or box control within about a fingertip (18 screen pixels, 20 panel base pixels at most; a jack wins over an end dot;
  `touch.c`, asked of the editor through the calls `load_test` uses, on the editor thread). The "+" dots are left exact, so
  a knob under them still turns. A cable dragged within 44 pixels of the screen's edge moves the camera, so it reaches
  the whole panel while zoomed in, and a second finger is ignored while a cable is in the hand. A double tap on a cable
  removes it, as a double-click does (UNDO brings it back).
- Not on touch: the cable's right-click menu has no long press (its box has ON / OFF and X; the row is on the MATRIX
  page). The selected cable's box can land partly off screen when zoomed in: two fingers pan to it.

**Bundled libraries** (2026-11-01): Steveo's six banks (`docs/PATCHES.md`, "The bundled libraries") are built into
the module (`--embed-file libraries@/bundled`, about 1.4 MB more) and copied into the browser's library folder on the
first visit, a bank at a time, only when it isn't there yet.

**What is not there yet:** MIDI clock and song position (the plugin's clock runs free), and the MIDI OUT notes (nothing
receives them in the browser).

### Checked (2026-11-01, Chromium in the app's built-in browser, offline from the device)

- The whole panel draws as on Windows (compared against a desktop screenshot): all modules, the centre screen, the
  keyboard; the MATRIX, SEQ, MORE, SETUP, SETTINGS and PATCHES pages; the CABLES view (the panel a quarter darker under
  it, as on Windows).
- Mouse clicks and drags, the computer keyboard (A plays a note), the preset arrows, the matrix's SOURCE menu (two
  columns, the current choice ticked; picking LFO 1 sets the row).
- PATCHES: SAVE AS (its dialog with name, TYPE, CATEGORY, MOOD, GROUP, notes), the saved patch listed; DELETE (its
  Yes/No box), EXPORT (a download), IMPORT (the file copied in and listed). After a reload the patch and the current
  sound came back.
- Sound: a held key or a MIDI note plays; with a chord held and a knob being dragged, the audio thread used 7% of its
  time and the sound had no gaps. A paint takes about 1.4 ms on average.
- An independent code review (2026-11-01) found no critical problems; its five serious ones are fixed: text handed
  to the page is a copy the page frees; a key release, a dialog's answer or a focus loss is never dropped from a full
  input queue; losing focus lets go of held notes and a held key's repeats are marked as repeats; the sound counters
  stay right after 12 hours; a paint split into many pieces paints all of them. Also: the saved sound is written
  whole and then renamed, uploaded and saved file names are kept to plain ASCII, and the page's views of memory are
  made again when another thread grows it. Checked again in the browser after the fixes.
- Not yet checked: by ear, with a real MIDI keyboard, on a phone or tablet, in Safari or Firefox, the m1p importer,
  the tuning (Scala) loader.

### Checked (2026-11-02, the fingers above, offline in the app's built-in browser)

All with **synthetic pointer events** sent from the page's console at 740 x 360 and 375 x 812 (the tools can't pinch).
No real finger or phone yet, so how it feels, and iPhone Safari above all, is untested. Animation and the edge's camera
move were checked with a timer in place of the browser's frame callback for part of it, the pane being in the background.

- Two fingers on knobs: zoomed, panned, stopped at the panel's edges, zoomed back out to the whole panel; the plugin
  received nothing. One finger: a press after the 90 ms, at once when it moves or lifts; on the keyboard at once. A second
  finger on a held control: released once, nothing more sent.
- PANEL ZOOM: oscillators, keyboard, screen and hand controls framed; a tap outside focused, sent nothing; a drag panned;
  a tap inside pressed; `<` `>`, SHOW ALL; rotated with a panel focused: framed again for the new shape.
- CABLES (the bar's button): new cable from a jack to a knob from 4 pixels off (zoomed in), the end dot dragged to another
  knob, a tap on a cable's knob and wire (selected, its box), the X from 5 pixels off (removed), across the panel by the
  screen's edge; from the sections view it went to the whole panel and back.
- Rotating both ways in both views, no sideways scroll. A real mouse at desktop size: wheel, knob drag, switch click, a
  key, the hover help as before. The audio thread's load and gaps meter unchanged (1-2%, no gaps).

### Build and try

```bash
EMSDK=/h/Tools/emsdk bash ports/one-ish/web/full/build.sh
python ports/one-ish/web/full/serve.py
```

Then open http://127.0.0.1:8737/. The first build compiles the engine with thread support and takes several minutes;
after that only changed files are compiled. Inside this Dropbox folder `build/params.h` is sometimes held by Dropbox
for a moment; the build tries that step again.

## Sound only (`web/build.sh`)

| File | What it does |
|---|---|
| `web/glue.c` | The engine's way in: create, MIDI, render 128 frames (float stereo, 44.1 kHz), factory presets (from the generated `params.h`, loaded as the VST wrapper does: every value in file order), set a parameter by key |
| `web/build.sh` | Compiles the engine and `glue.c` with Emscripten into `web/site/moowon.wasm`, a standalone module (no Emscripten JavaScript) |
| `web/site/engine.js` | Loads the module for JavaScript; its few system calls (printing, closing files) do nothing |
| `web/site/worklet.js` | The browser's sound thread (an AudioWorklet): one engine block per call, notes and presets arrive as messages |
| `web/site/simple.html`, `app.js`, `style.css` | The page |
| `web/test_node.mjs` | Offline check: plays five presets in Node, checks for sound and bad samples, reports speed |

The page asks the browser for a 44.1 kHz AudioContext, the engine's own rate; the browser converts to the sound card's
rate. No host clock: the sequencer and synced LFOs run free on their own tempo, as with RECEIVE CLOCK off. The sound
thread can't use TextEncoder/TextDecoder, so names and keys are read byte by byte.

```bash
EMSDK=/h/Tools/emsdk bash ports/one-ish/web/build.sh
node ports/one-ish/web/test_node.mjs
```

Results 2026-11-01 (offline, Node 22 on the PC): 296 presets, all five tested presets sound, no bad samples, 11 to 108
times faster than real time (Init 11.5x, Warm Pad 15x, Colossus Stack 14.5x). In the built-in browser the page loads,
presets switch, and Fat Bass, Seq Bass Line, Colossus Stack and Markov Daydream play. The owner played it the same day.

## Publishing (Vercel)

Emscripten 3.1.74 is installed at `H:\Tools\emsdk`, outside Dropbox and off the C: drive (C: was full, 2026-11-01).
`web/site` is linked to the Vercel project `moo-won` (the `.vercel` folder there, not committed). After both builds,
from `web/site`:

```bash
vercel deploy --prod --yes
```

That updates the public page **https://moo-won.vercel.app** (owner, 2026-11-01: "Public is fine"). Without `--prod`
it makes a preview behind Vercel's sign-in. The built files (`moowon.wasm`, `moowon_full.*`) are not in git; build
them before deploying.

## Browsers built into other apps (2026-11-02)

Messenger, Facebook and Instagram open links in their own built-in browser, which has no shared memory between threads
(`crossOriginIsolated` false, whatever the headers say), so the plugin can't start there. The page says so and offers
OPEN IN CHROME (Android, an `intent://` link) and COPY THE LINK. Checked with an emulated Android browser on a local server
without the headers; not yet tried from Messenger itself.

## Starts on Init (2026-11-02, owner)

The page always starts on Init. An edited last session moves from `web_state.bin` to `web_last.bin`, and LAST SESSION (on the
bar and in the sections view) brings it back; Init left unchanged is never saved, so opening the page and leaving doesn't
overwrite it. User patches and the libraries aren't touched. Checked in the browser: R, reload (a clean Init), LAST SESSION.
