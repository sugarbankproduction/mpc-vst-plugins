# Moo Won in the browser (web build, sound only)

Added 2026-11-01. The same engine as the plugin, compiled to WebAssembly and played by a small web page. No panel
yet: a preset list, an on-screen keyboard, the computer keyboard, MIDI keyboards, mod wheel, pitch bend and volume.

## Why not load the plugin itself

`Moo Won.dll` is a Windows program; a browser can't run it without imitating a whole Windows PC, which is far too slow
for live sound (searched 2026-11-01: nothing does this for plugins). The engine is our own C code with no Windows parts,
so it is rebuilt for the browser instead, the way other plugins reach the web (Web Audio Modules, iPlug2's web target).

## The parts

| File | What it does |
|---|---|
| `web/glue.c` | The engine's way in: create, MIDI, render 128 frames (float stereo, 44.1 kHz), factory presets (from the generated `params.h`, loaded as the VST wrapper does: every value in file order), set a parameter by key |
| `web/build.sh` | Compiles the engine and `glue.c` with Emscripten into `web/site/moowon.wasm`, a standalone module (no Emscripten JavaScript) |
| `web/site/engine.js` | Loads the module for JavaScript; its few system calls (printing, closing files) do nothing |
| `web/site/worklet.js` | The browser's sound thread (an AudioWorklet): one engine block per call, notes and presets arrive as messages |
| `web/site/app.js`, `index.html`, `style.css` | The page |
| `web/test_node.mjs` | Offline check: plays five presets in Node, checks for sound and bad samples, reports speed |

The page asks the browser for a 44.1 kHz AudioContext, the engine's own rate; the browser converts to the sound card's
rate, so no converter of our own is needed. No host clock: the sequencer and synced LFOs run free on their own tempo, as
with RECEIVE CLOCK off. The sound thread can't use TextEncoder/TextDecoder, so names and keys are read byte by byte.

## Build and check

Emscripten 3.1.74 is installed at `H:\Tools\emsdk`, outside Dropbox and off the C: drive (C: was full, 2026-11-01).
From the repo root in Git Bash:

```bash
EMSDK=/h/Tools/emsdk bash ports/one-ish/web/build.sh
node ports/one-ish/web/test_node.mjs
```

Results 2026-11-01 (offline, Node 22 on the PC): 296 presets, all five tested presets sound, no bad samples, 11 to 108
times faster than real time (Init 11.5x, Warm Pad 15x, Colossus Stack 14.5x). In the app's built-in browser (Chromium,
2026-11-01): the page loads, sound starts on the click, presets switch, and Fat Bass, Seq Bass Line, Colossus Stack and
Markov Daydream play. Not yet checked by ear, on a phone, in Safari or Firefox, or with a MIDI keyboard.

To try it locally: `python -m http.server 8737` in `ports/one-ish/web/site`, then open http://127.0.0.1:8737/.

## Publishing (Vercel)

`web/site` is linked to the Vercel project `moo-won` (the `.vercel` folder there, not committed). From `web/site`:

```bash
vercel deploy --yes
```

That makes a preview: its address (`moo-…-sugarbankproductions-projects.vercel.app`) is behind Vercel's sign-in, so
only the owner's account opens it. The public page is **https://moo-won.vercel.app** (owner, 2026-11-01: "Public is
fine"); `vercel deploy --prod --yes` updates it. Checked 2026-11-01 in the app's built-in browser: the public page
loads, all 296 presets are listed and Fat Bass plays. The owner played it on their own browser the same day: it works.

## Next

The full panel: the editor draws through 15 small functions (`desktop/gfx.h`) plus GDI text, menus, file dialogs and
message boxes, which all need web versions (browser drawing, HTML menus, downloads and uploads, presets kept in the
browser).
