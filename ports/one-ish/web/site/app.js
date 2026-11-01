// The page: starts the sound (worklet.js) on the first click, as browsers require, and sends it notes from the
// on-screen keyboard, the computer keyboard and any MIDI keyboard, plus the preset, wheels and volume.
const $ = (id) => document.getElementById(id);
const KEY_MAP = "awsedftgyhujkolp;";   // C C# D D# E F F# G G# A A# B C C# D D# E, from the octave's C
const BLACK = [1, 3, 6, 8, 10];
const OCTAVES = 3;

let node = null, gain = null, ctx = null;
let octave = 4;
let wasmBytes = null;
let presets = [];
const held = new Map();   // what is sounding: a source ("ptr:3", "key:a", "midi:60") -> note

const send = (m) => node?.port.postMessage(m);
const noteOn = (src, note, vel = 100) => {
  if (held.has(src)) return;
  held.set(src, note);
  send({ midi: [0x90, note, vel] });
  lightKey(note, true);
};
const noteOff = (src) => {
  const note = held.get(src);
  if (note === undefined) return;
  held.delete(src);
  send({ midi: [0x80, note, 0] });
  if (![...held.values()].includes(note)) lightKey(note, false);
};
const allOff = () => {
  for (const src of [...held.keys()]) noteOff(src);
  for (let ch = 0; ch < 16; ch++) send({ midi: [0xB0 | ch, 123, 0] });
  document.querySelectorAll(".key.on").forEach((k) => k.classList.remove("on"));
};

const store = {
  get(k) { try { return localStorage.getItem(k); } catch { return null; } },
  set(k, v) { try { localStorage.setItem(k, v); } catch { /* private window: not remembered */ } },
};

// ---- the on-screen keyboard ----
function baseNote() { return 12 * (octave + 1); }
function buildKeys() {
  const keys = $("keys");
  keys.innerHTML = "";
  const whites = OCTAVES * 7 + 1;
  let w = 0;
  for (let i = 0; i <= OCTAVES * 12; i++) {
    const note = baseNote() + i;
    const pc = i % 12;
    const el = document.createElement("div");
    el.className = "key" + (BLACK.includes(pc) ? " black" : "");
    el.dataset.note = note;
    const kc = KEY_MAP[i];
    if (kc) el.innerHTML = `<span class="lbl">${kc.toUpperCase()}</span>`;
    if (BLACK.includes(pc)) {
      el.style.left = `calc(${(w / whites) * 100}% - ${(0.6 / whites) * 50}%)`;
      el.style.width = `${(0.6 / whites) * 100}%`;
    } else {
      if (pc === 0) el.innerHTML += `<span class="lbl" style="bottom:22px">C${Math.floor(note / 12) - 1}</span>`;
      w++;
    }
    keys.appendChild(el);
  }
  for (const note of held.values()) lightKey(note, true);
}
function lightKey(note, on) {
  document.querySelector(`.key[data-note="${note}"]`)?.classList.toggle("on", on);
}
function keyAt(x, y) {
  const el = document.elementFromPoint(x, y)?.closest(".key");
  return el ? Number(el.dataset.note) : null;
}
function setupPointer() {
  const keys = $("keys");
  keys.addEventListener("pointerdown", (e) => {
    const note = keyAt(e.clientX, e.clientY);
    if (note === null) return;
    keys.setPointerCapture(e.pointerId);
    noteOn(`ptr:${e.pointerId}`, note, Math.round(60 + 60 * Math.min(1, e.pressure || 0.6)));
  });
  keys.addEventListener("pointermove", (e) => {   // sliding across the keys plays each one
    const src = `ptr:${e.pointerId}`;
    if (!held.has(src)) return;
    const note = keyAt(e.clientX, e.clientY);
    if (note !== null && note !== held.get(src)) { noteOff(src); noteOn(src, note); }
  });
  for (const t of ["pointerup", "pointercancel", "lostpointercapture"])
    keys.addEventListener(t, (e) => noteOff(`ptr:${e.pointerId}`));
}

// ---- the computer keyboard ----
function setOctave(o) {
  octave = Math.max(0, Math.min(8, o));
  $("oct").textContent = octave;
  buildKeys();
}
function setupComputerKeys() {
  addEventListener("keydown", (e) => {
    if (e.repeat || e.ctrlKey || e.metaKey || e.altKey) return;
    if (e.target instanceof HTMLInputElement && e.target.type === "search") return;
    if (e.target instanceof HTMLSelectElement) return;
    const k = e.key.toLowerCase();
    if (k === "z") return setOctave(octave - 1);
    if (k === "x") return setOctave(octave + 1);
    const i = KEY_MAP.indexOf(k);
    if (i >= 0) { e.preventDefault(); noteOn(`key:${k}`, baseNote() + i); }
  });
  addEventListener("keyup", (e) => noteOff(`key:${e.key.toLowerCase()}`));
  addEventListener("blur", allOff);
}

// ---- MIDI keyboards (Chrome, Edge; Firefox asks for permission) ----
async function setupMidi() {
  if (!navigator.requestMIDIAccess) { $("midi-status").textContent = "This browser has no MIDI."; return; }
  try {
    const access = await navigator.requestMIDIAccess();
    const pick = $("midi-in");
    const refresh = () => {
      const was = pick.value;
      pick.innerHTML = '<option value="">all keyboards</option>';
      for (const inp of access.inputs.values()) pick.add(new Option(inp.name, inp.id));
      pick.value = [...pick.options].some((o) => o.value === was) ? was : "";
      for (const inp of access.inputs.values()) inp.onmidimessage = onMidi;
      $("midi-status").textContent = access.inputs.size ? `${access.inputs.size} MIDI input(s) found.` : "No MIDI keyboard found.";
    };
    const onMidi = (e) => {
      if (pick.value && e.target.id !== pick.value) return;
      const [s, d1 = 0, d2 = 0] = e.data;
      if (s >= 0xF0) return;   // clock and system messages: not used here
      const hi = s & 0xF0;
      if (hi === 0x90 && d2 > 0) lightKey(d1, true);
      else if (hi === 0x80 || hi === 0x90) lightKey(d1, false);
      send({ midi: [s, d1, d2] });
    };
    access.onstatechange = refresh;
    refresh();
  } catch {
    $("midi-status").textContent = "MIDI was not allowed.";
  }
}

// ---- presets ----
function fillPresets(filter = "") {
  const sel = $("preset");
  const cur = sel.value;
  sel.innerHTML = "";
  const f = filter.trim().toLowerCase();
  presets.forEach((name, i) => {
    if (!f || name.toLowerCase().includes(f)) sel.add(new Option(`${String(i + 1).padStart(3, "0")}  ${name}`, i));
  });
  if ([...sel.options].some((o) => o.value === cur)) sel.value = cur;
}
function choosePreset(i) {
  i = (i + presets.length) % presets.length;
  if (![...$("preset").options].some((o) => Number(o.value) === i)) { $("search").value = ""; fillPresets(); }
  $("preset").value = i;
  send({ preset: i });
  store.set("mw.preset", i);
}
function setupPresets() {
  $("preset").addEventListener("change", (e) => choosePreset(Number(e.target.value)));
  $("prev").addEventListener("click", () => choosePreset(Number($("preset").value) - 1));
  $("next").addEventListener("click", () => choosePreset(Number($("preset").value) + 1));
  $("search").addEventListener("input", (e) => fillPresets(e.target.value));
}

// ---- wheels, volume ----
function setupControls() {
  $("volume").addEventListener("input", (e) => { if (gain) gain.gain.value = Number(e.target.value) ** 2; });
  $("mod").addEventListener("input", (e) => send({ midi: [0xB0, 1, Number(e.target.value)] }));
  const bend = $("bend");
  const sendBend = () => { const v = Number(bend.value) + 8192; send({ midi: [0xE0, v & 127, v >> 7] }); };
  bend.addEventListener("input", sendBend);
  for (const t of ["pointerup", "touchend", "change"])   // springs back, like a real bend wheel
    bend.addEventListener(t, () => { bend.value = 0; sendBend(); });
  $("oct-down").addEventListener("click", () => setOctave(octave - 1));
  $("oct-up").addEventListener("click", () => setOctave(octave + 1));
  $("panic").addEventListener("click", allOff);
}

// ---- starting the sound ----
async function start() {
  $("start").disabled = true;
  $("status").textContent = "Starting…";
  try {
    ctx = new AudioContext({ sampleRate: 44100, latencyHint: "interactive" });   // the engine's own rate
    await ctx.audioWorklet.addModule("worklet.js");
    node = new AudioWorkletNode(ctx, "moo-won", {
      numberOfInputs: 0, outputChannelCount: [2], processorOptions: { wasm: wasmBytes },
    });
    node.onprocessorerror = () => { $("status").textContent = "The synth stopped with an error. Reload the page."; };
    gain = new GainNode(ctx, { gain: Number($("volume").value) ** 2 });
    node.connect(gain).connect(ctx.destination);
    node.port.onmessage = (e) => {
      const m = e.data;
      if (m.ready) {
        presets = m.presets;
        fillPresets();
        const saved = Number(store.get("mw.preset"));
        choosePreset(Number.isInteger(saved) && saved > 0 && saved < presets.length ? saved : 1);
        $("start-box").hidden = true;
        document.querySelectorAll(".off").forEach((el) => el.classList.remove("off"));
      } else if (m.peak !== undefined) {
        $("meter-fill").style.width = `${Math.min(100, m.peak * 100)}%`;
      }
    };
    await ctx.resume();
    setupMidi();
  } catch (err) {
    $("status").textContent = `The sound could not start: ${err instanceof Error ? err.message : err}`;
    $("start").disabled = false;
  }
}

async function init() {
  for (const sel of [".presets", ".controls", ".keys-wrap"]) document.querySelector(sel).classList.add("off");
  buildKeys();
  setupPointer();
  setupComputerKeys();
  setupPresets();
  setupControls();
  $("start").addEventListener("click", start);
  try {
    const r = await fetch("moowon.wasm");
    if (!r.ok) throw new Error(`HTTP ${r.status}`);
    wasmBytes = await r.arrayBuffer();
    $("status").textContent = "Ready. Sound starts when you click.";
  } catch (err) {
    $("status").textContent = `The synth could not load: ${err instanceof Error ? err.message : err}`;
    $("start").disabled = true;
  }
}
init();
