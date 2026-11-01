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

**What is not there yet:** a layout for phones and narrow windows (the panel is only scaled down), MIDI clock and song
position (the plugin's clock runs free), and the MIDI OUT notes (nothing receives them in the browser).

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
