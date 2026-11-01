// The page for the full plugin (web/full): starts the module (moowon_full.js), shows the panel the editor thread draws,
// sends it the mouse and keys, shows the menus, message boxes, dialogs and file pickers it asks for, plays its sound
// (ring_worklet.js) and passes MIDI keyboards through. Files the plugin saves live in the browser (IndexedDB).
import { createSections } from "./sections.js";
import { createCamera } from "./camera.js";
import { BASE_W, BASE_H, PANELS } from "./panels.js";
const $ = (id) => document.getElementById(id);
const EV = { MOVE: 1, DOWN: 2, UP: 3, WHEEL: 4, LEAVE: 5, KEYDOWN: 6, KEYUP: 7, RESULT: 8, BLUR: 9, SNAP: 10 };
const UI = { MENU: 1, MSGBOX: 2, DIALOG: 3, OPEN: 4, SAVE: 5 };
const params = new URLSearchParams(location.search);

let M = null;          // the module
let infoPtr = 0, presPtr = 0, panelW = 0, panelH = 0;
let ctx = null;
const canvas = $("panel");
const g2d = canvas.getContext("2d", { alpha: false });

// views of the module's memory, made again when another thread has grown it (its own views then go stale here)
let viewsOf = null, V = null;
function heap() {
  const buf = M.wasmMemory.buffer;
  if (viewsOf !== buf) {
    viewsOf = buf;
    V = { U8: new Uint8Array(buf), I32: new Int32Array(buf), U32: new Uint32Array(buf), F32: new Float32Array(buf) };
  }
  return V;
}

// ---- text to the module: its strings are Windows-1252 (as the desktop editor's are) ----
const CP1252 = "€\u0081‚ƒ„…†‡ˆ‰Š‹Œ\u008DŽ\u008F" +
  "\u0090‘’“”•–—˜™š›œ\u009DžŸ";
function cp1252Ptr(s) {
  const p = M._malloc(s.length + 1);
  const H = heap().U8;
  for (let i = 0; i < s.length; i++) {
    const c = s.charCodeAt(i);
    let b = 63;   // '?'
    if (c < 0x80 || (c >= 0xA0 && c < 0x100)) b = c;
    else { const k = CP1252.indexOf(s[i]); if (k >= 0) b = 0x80 + k; }
    H[p + i] = b;
  }
  H[p + s.length] = 0;
  return p;
}
const answer = (token, value, text = null) => M._web_event(EV.RESULT, token, value, text === null ? 0 : cp1252Ptr(text), 0);

// ---- the panel: copy what the editor painted ----
let imgBuf = null;
function present() {
  const H = heap().I32, p = presPtr >> 2;
  const seq = Atomics.load(H, p + 3);
  if (!(seq & 1) && seq !== Atomics.load(H, p + 4)) {
    const px = H[p] >>> 2, w = H[p + 1];
    let x0 = H[p + 5], y0 = H[p + 6], x1 = H[p + 7], y1 = H[p + 8];
    x0 = Math.max(0, x0); y0 = Math.max(0, y0); x1 = Math.min(w, x1); y1 = Math.min(H[p + 2], y1);
    if (x1 > x0 && y1 > y0) {
      const cw = x1 - x0, ch = y1 - y0;
      if (!imgBuf || imgBuf.width < cw || imgBuf.height < ch) imgBuf = new ImageData(Math.max(cw, 64), Math.max(ch, 64));
      const dst = new Uint32Array(imgBuf.data.buffer), U = heap().U32, dw = imgBuf.width;
      for (let y = 0; y < ch; y++) {
        let s = px + (y0 + y) * w + x0, d = y * dw;
        for (let x = 0; x < cw; x++) {
          const v = U[s++];
          dst[d++] = 0xFF000000 | (v & 0xFF) << 16 | (v & 0xFF00) | (v >>> 16 & 0xFF);
        }
      }
      g2d.putImageData(imgBuf, x0, y0, 0, 0, cw, ch);
      if (sections && mode === "sections") sections.draw();
    }
    if (Atomics.load(H, p + 3) === seq) Atomics.store(H, p + 4, seq);
  }
  requestAnimationFrame(present);
}

// ---- the mouse, touch and keys ----
// a view (the whole panel, or a piece of it in the sections view) turns a pointer position into panel pixels;
// locate(el, e, held) -> {piece, x, y} or null (held: the piece the press started on)
const wholeView = {
  locate(el, e) {
    const r = canvas.getBoundingClientRect();
    return { piece: null, x: Math.round((e.clientX - r.left) * panelW / r.width), y: Math.round((e.clientY - r.top) * panelH / r.height) };
  },
};
const mods = (e) => (e.shiftKey ? 1 : 0) | (e.ctrlKey || e.metaKey ? 2 : 0) | (e.altKey ? 4 : 0);
let mainPointer = null, heldPiece = null, wheelAcc = 0;
function attachPointer(el, viewOf, route = null) {   // route: someone else decides which events reach the control
  const down = (e) => {
    if (mainPointer !== null && e.pointerId !== mainPointer) return;   // one finger plays the mouse
    const at = viewOf().locate(el, e, null);
    if (!at) return;
    mainPointer = e.pointerId;
    heldPiece = at.piece;
    el.focus({ preventScroll: true });
    try { el.setPointerCapture(e.pointerId); } catch { /* (a pointer the browser no longer tracks) */ }
    const bit = [1, 4, 2, 8, 16][e.button] || 0;
    M._web_event(EV.MOVE, at.x, at.y, e.pointerType === "mouse" ? e.buttons & ~bit : 0, mods(e));
    M._web_event(EV.DOWN, at.x, at.y, e.pointerType === "mouse" ? e.button : 0, mods(e));
    e.preventDefault();
  };
  const move = (e) => {
    if (mainPointer !== null && e.pointerId !== mainPointer) return;
    const at = viewOf().locate(el, e, mainPointer !== null ? heldPiece : null);
    if (!at) return;
    M._web_event(EV.MOVE, at.x, at.y, e.pointerType === "mouse" ? e.buttons : (mainPointer !== null ? 1 : 0), mods(e));
  };
  const up = (e) => {
    if (e.pointerId !== mainPointer) return;
    const at = viewOf().locate(el, e, heldPiece) || { x: 0, y: 0 };
    mainPointer = null;
    heldPiece = null;
    M._web_event(EV.UP, at.x, at.y, e.pointerType === "mouse" ? e.button : 0, mods(e));
  };
  if (route) route({ down, move, up });
  else {
    el.addEventListener("pointerdown", down);
    el.addEventListener("pointermove", move);
    el.addEventListener("pointerup", up);
    el.addEventListener("pointercancel", up);
  }
  el.addEventListener("pointerleave", () => { if (mainPointer === null) M._web_event(EV.LEAVE, 0, 0, 0, 0); });
  el.addEventListener("contextmenu", (e) => e.preventDefault());
  el.addEventListener("wheel", (e) => {
    const at = viewOf().locate(el, e, null);
    if (!at) return;   // (off the pieces: the page scrolls)
    e.preventDefault();
    const px = e.deltaMode === 1 ? e.deltaY * 40 : e.deltaMode === 2 ? e.deltaY * 400 : e.deltaY;
    wheelAcc += px;
    const notches = Math.trunc(wheelAcc / 50);
    if (!notches) return;
    wheelAcc -= notches * 50;
    M._web_event(EV.WHEEL, at.x, at.y, -notches * 120, mods(e));
  }, { passive: false });
}
// a finger on the panel in the CABLES view lands on a cable's part this near it (touch.c): about a fingertip on the
// screen, at most SNAP_MAX base pixels, so a knob's middle still means the knob
const SNAP_CSS = 18, SNAP_MAX = 20;
function setupInput() {
  attachPointer(canvas, () => wholeView, (control) => {
    camera = createCamera({
      stage: $("stage"), canvas, ring: $("focus-ring"), baseW: BASE_W, baseH: BASE_H, panels: PANELS, control, isTouch,
      cableDragging,
      beforePress(e, k) {
        if (!cablesOn()) return;
        const at = wholeView.locate(canvas, e), s = panelW / BASE_W;
        M._web_event(EV.SNAP, at.x, at.y, Math.round(Math.min(SNAP_CSS / k, SNAP_MAX) * s), 0);
      },
      onChange: showCameraState,
    });
  });
  for (const el of sections.elements) attachPointer(el, () => sections);
  // keys go to the panel from anywhere on the page, but not while typing in a box or with a menu or dialog open
  const typing = (e) => e.target instanceof HTMLInputElement || e.target instanceof HTMLTextAreaElement ||
    e.target instanceof HTMLSelectElement || layer.childElementCount > 0;
  addEventListener("keydown", (e) => {
    if (typing(e)) return;
    const vk = vkOf(e);
    if (!vk) return;
    if (!browserKey(e)) e.preventDefault();
    let ch = 0;
    if (e.key.length === 1) { const c = e.key.charCodeAt(0); ch = c < 256 ? c : 0; }
    else if (e.key === "Enter") ch = 13;
    else if (e.key === "Backspace") ch = 8;
    else if (e.key === "Escape") ch = 27;
    else if (e.key === "Tab") ch = 9;
    M._web_event(EV.KEYDOWN, vk, ch, e.repeat ? 1 : 0, mods(e));
  });
  addEventListener("keyup", (e) => { const vk = vkOf(e); if (vk) M._web_event(EV.KEYUP, vk, 0, 0, mods(e)); });
  addEventListener("blur", () => M._web_event(EV.BLUR, 0, 0, 0, 0));   // (another window or tab: notes let go)
}

// ---- whole panel or sections (layout B: phones and narrow windows) ----
let sections = null, camera = null, mode = "whole";
const isTouch = navigator.maxTouchPoints > 0 || matchMedia("(pointer: coarse)").matches;
const wantsSections = () => innerWidth < 600 || (innerWidth < 1000 && innerHeight > innerWidth);
function setMode(m) {
  mode = m;
  $("stage").hidden = m !== "whole";
  $("sections").hidden = m !== "sections";
  $("to-sections").hidden = m !== "whole";
  document.body.classList.toggle("sections-mode", m === "sections");
  if (m === "sections") sections.layout();
  else if (camera) camera.layout();
}
function toggleMode() {
  const m = mode === "whole" ? "sections" : "whole";
  try { localStorage.setItem("mw.view", m); } catch { /* (not kept) */ }
  setMode(m);
}
// ---- the bar's camera buttons, a short message, the CABLES view (it spans the whole panel) ----
function showCameraState() {
  if (!camera) return;
  $("panel-zoom").classList.toggle("on", camera.panelZoom);
  $("pz-steps").hidden = !camera.panelZoom;
  $("pz-name").textContent = camera.focusName || "tap a panel";
  $("show-all").disabled = !camera.zoomed;
}
let toastTimer = 0;
function toast(text) {
  const t = $("toast");
  t.textContent = text;
  t.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { t.hidden = true; }, 4000);
}
function setupCameraButtons() {
  $("panel-zoom").addEventListener("click", () => {
    const on = !camera.panelZoom;
    camera.setPanelZoom(on);
    try { localStorage.setItem("mw.pz", on ? "1" : ""); } catch { /* (not kept) */ }
    if (on) toast("Tap a panel to zoom to it. Inside it, the controls work; tap outside it, or use < >, to move on.");
  });
  $("zoom-in").addEventListener("click", () => camera.zoomBy(1.5));
  $("zoom-out").addEventListener("click", () => camera.zoomBy(1 / 1.5));
  $("show-all").addEventListener("click", () => camera.showAll());
  $("pz-prev").addEventListener("click", () => camera.step(-1));
  $("pz-next").addEventListener("click", () => camera.step(1));
  $("cables").addEventListener("click", toggleCables);
  let pz = false;
  try { pz = localStorage.getItem("mw.pz") === "1"; } catch { /* (none) */ }
  if (pz) camera.setPanelZoom(true);
  showCameraState();
  // the view follows the CABLES view: it needs the whole panel, so from the sections view it goes there and comes back
  let was = false, cameFromSections = false;
  setInterval(() => {
    const on = cablesOn();
    if (on === was) return;
    was = on;
    $("cables").classList.toggle("on", on);
    sections.setCables(on);
    if (on && mode === "sections") {
      cameFromSections = true;
      setMode("whole");
      toast("Cables run across the whole panel, so this is the whole panel. Pinch to zoom; CABLES again goes back.");
    } else if (!on && cameFromSections) {
      cameFromSections = false;
      if (mode === "whole") setMode("sections");
    }
  }, 100);
}
const cablesOn = () => infoPtr !== 0 && heap().I32[(infoPtr >> 2) + 12] !== 0;
function cableDragging() {   // a cable in the hand (cables.c: CD_NEW, CD_DST, CD_SRC, CD_REV)
  const d = infoPtr ? heap().I32[(infoPtr >> 2) + 13] : 0;
  return d === 3 || d === 4 || d === 5 || d === 7;
}
function toggleCables() {   // what the key C does: the MATRIX page with its cables, again: hidden
  M._web_event(EV.KEYDOWN, 67, 0, 0, 0);
  M._web_event(EV.KEYUP, 67, 0, 0, 0);
}
function browserKey(e) {   // keys left to the browser
  if (["F5", "F11", "F12"].includes(e.key)) return true;
  if ((e.ctrlKey || e.metaKey) && ["r", "w", "t", "l", "n", "q", "+", "-", "="].includes(e.key.toLowerCase())) return true;
  return (e.ctrlKey || e.metaKey) && e.shiftKey && e.key.toLowerCase() === "i";
}
const VK = {
  Backspace: 8, Tab: 9, Enter: 13, NumpadEnter: 13, ShiftLeft: 16, ShiftRight: 16, ControlLeft: 17, ControlRight: 17,
  AltLeft: 18, AltRight: 18, Pause: 19, CapsLock: 20, Escape: 27, Space: 32, PageUp: 33, PageDown: 34, End: 35,
  Home: 36, ArrowLeft: 37, ArrowUp: 38, ArrowRight: 39, ArrowDown: 40, Insert: 45, Delete: 46, NumpadMultiply: 0x6A,
  NumpadAdd: 0x6B, NumpadSubtract: 0x6D, NumpadDecimal: 0x6E, NumpadDivide: 0x6F, Semicolon: 0xBA, Equal: 0xBB,
  Comma: 0xBC, Minus: 0xBD, Period: 0xBE, Slash: 0xBF, Backquote: 0xC0, BracketLeft: 0xDB, Backslash: 0xDC,
  BracketRight: 0xDD, Quote: 0xDE, MetaLeft: 17, MetaRight: 17,
};
function vkOf(e) {
  const c = e.code;
  if (VK[c]) return VK[c];
  let m = /^Key([A-Z])$/.exec(c);
  if (m) return m[1].charCodeAt(0);
  m = /^Digit(\d)$/.exec(c);
  if (m) return 0x30 + Number(m[1]);
  m = /^Numpad(\d)$/.exec(c);
  if (m) return 0x60 + Number(m[1]);
  m = /^F(\d+)$/.exec(c);
  if (m) return 0x6F + Number(m[1]);
  return 0;
}

// ---- menus, message boxes, dialogs, file pickers (Module.mwUI from the editor thread) ----
const layer = $("ui-layer");
const panelToPage = (x, y) => {
  if (mode === "sections") return sections.pageOf(x, y) || [innerWidth / 2 - 100, innerHeight / 3];
  const r = canvas.getBoundingClientRect();
  return [r.left + x * r.width / panelW, r.top + y * r.height / panelH];
};
const label = (t) => t.replace(/&&|&/g, (m) => (m === "&&" ? "&" : ""));
function veil(dim, onDismiss) {
  const v = document.createElement("div");
  v.className = "veil" + (dim ? " dim" : "");
  if (onDismiss) v.addEventListener("pointerdown", (e) => { if (e.target === v) onDismiss(); });
  layer.appendChild(v);
  return v;
}

function showMenu(token, spec) {
  let done = false;
  const finish = (id) => {
    if (done) return;
    done = true;
    v.remove();
    removeEventListener("keydown", esc, true);
    answer(token, id);
    canvas.focus({ preventScroll: true });
  };
  const esc = (e) => { if (e.key === "Escape") { e.preventDefault(); finish(0); } };
  addEventListener("keydown", esc, true);
  const v = veil(false, () => finish(0));
  v.addEventListener("contextmenu", (e) => e.preventDefault());
  const [px, py] = panelToPage(spec.x, spec.y);
  const open = (items, x, y, parentMenu) => {
    const m = document.createElement("div");
    m.className = "menu";
    let col = document.createElement("div");
    col.className = "col";
    m.appendChild(col);
    let child = null;
    for (const it of items) {
      if (it.f & 0x60 && col.childElementCount) { col = document.createElement("div"); col.className = "col"; m.appendChild(col); }
      if (it.f & 0x800) { const s = document.createElement("div"); s.className = "sep"; col.appendChild(s); continue; }
      const el = document.createElement("div");
      const [text, key] = it.t.split("\t");
      el.className = "it" + (it.f & 0x3 ? " off" : "") + (it.f & 0x8 ? " chk" : "") + (it.sub ? " sub" : "");
      el.textContent = label(text);
      if (key) { const k = document.createElement("span"); k.className = "key"; k.textContent = key; el.appendChild(k); }
      const openSub = () => {
        if (child) { child.remove(); child = null; }
        m.querySelectorAll(".it.open").forEach((o) => o.classList.remove("open"));
        if (!it.sub || it.f & 0x3) return;
        el.classList.add("open");
        const r = el.getBoundingClientRect();
        child = open(it.sub, r.right - 2, r.top - 4, m);
      };
      el.addEventListener("pointerenter", openSub);
      el.addEventListener("click", () => {
        if (it.f & 0x3) return;
        if (it.sub) { openSub(); return; }
        finish(it.id);
      });
      col.appendChild(el);
    }
    v.appendChild(m);
    const r = m.getBoundingClientRect();   // keep it on the screen
    let left = x, top = y;
    if (!parentMenu && spec.align & 0x8) left = x - r.width;
    else if (!parentMenu && spec.align & 0x4) left = x - r.width / 2;
    if (!parentMenu && spec.align & 0x20) top = y - r.height;
    if (left + r.width > innerWidth - 4) left = parentMenu ? parentMenu.getBoundingClientRect().left - r.width + 2 : innerWidth - r.width - 4;
    if (top + r.height > innerHeight - 4) top = innerHeight - r.height - 4;
    m.style.left = `${Math.max(4, left)}px`;
    m.style.top = `${Math.max(4, top)}px`;
    return m;
  };
  open(spec.items, px, py, null);
}

function box(title, onEsc) {
  const v = veil(true, null);
  const b = document.createElement("div");
  b.className = "box";
  b.innerHTML = '<div class="title"></div><div class="body"></div><div class="btns"></div>';
  b.querySelector(".title").textContent = title;
  v.appendChild(b);
  const esc = (e) => { if (e.key === "Escape") { e.preventDefault(); onEsc(); } };
  addEventListener("keydown", esc, true);
  return { v, b, body: b.querySelector(".body"), btns: b.querySelector(".btns"),
    close() { v.remove(); removeEventListener("keydown", esc, true); canvas.focus({ preventScroll: true }); } };
}
function button(parent, text, isDef, fn) {
  const btn = document.createElement("button");
  btn.textContent = text;
  if (isDef) btn.className = "def";
  btn.addEventListener("click", fn);
  parent.appendChild(btn);
  return btn;
}

function showMessage(token, spec) {
  const kind = spec.type & 0xF;
  const sets = { 0: [["OK", 1]], 1: [["OK", 1], ["Cancel", 2]], 3: [["Yes", 6], ["No", 7], ["Cancel", 2]],
    4: [["Yes", 6], ["No", 7]], 5: [["Retry", 4], ["Cancel", 2]] };
  const btns = sets[kind] || sets[0];
  const def = (spec.type & 0x100) ? 1 : (spec.type & 0x200) ? 2 : 0;
  const cancel = btns.find((b) => b[1] === 2) ? 2 : kind === 4 ? 7 : 1;
  let done = false;
  const finish = (v) => { if (done) return; done = true; bx.close(); answer(token, v); };
  const bx = box(spec.caption, () => finish(cancel));
  bx.body.textContent = spec.text;
  btns.forEach(([t, v], i) => { const b = button(bx.btns, t, i === def, () => finish(v)); if (i === def) setTimeout(() => b.focus()); });
}

let dlg = null;   // the open dialog: { token, bx, ctls: Map(id -> element) }
const DLU_X = 1.75, DLU_Y = 1.75;
function showDialog(token, spec) {
  if (dlg && !spec.open) {   // the same dialog again (a button didn't close it): new texts, same fields
    dlg.token = token;
    for (const c of spec.controls) {
      const el = dlg.ctls.get(c.id);
      if (!el) continue;
      if (c.cls === 0x82) el.textContent = label(c.text);
      else if (c.cls === 0x81 && el.value !== c.text.replace(/\r\n/g, "\n")) el.value = c.text.replace(/\r\n/g, "\n");
      else if (c.cls === 0x85) {
        if (el.options.length !== c.items.length) { el.length = 0; c.items.forEach((t, i) => el.add(new Option(label(t), i))); }
        el.selectedIndex = c.sel;
      } else if (el.type === "checkbox") el.checked = !!c.check;
    }
    return;
  }
  if (dlg) dlg.bx.close();
  const send = (cmd) => {
    if (!dlg) return;
    const lines = [String(cmd)];
    for (const [id, el] of dlg.ctls) {
      const check = el.type === "checkbox" ? (el.checked ? 1 : 0) : 0;
      const sel = el.tagName === "SELECT" ? el.selectedIndex : -1;
      // (one line per control: a typed line break goes as a lone CR, which win32.c turns back into CR LF)
      const text = el.tagName === "INPUT" || el.tagName === "TEXTAREA" ? el.value.replace(/\r?\n/g, "\r").replace(/\t/g, " ") : "";
      lines.push(`${id}\t${check}\t${sel}\t${text}`);
    }
    answer(dlg.token, 1, lines.join("\n"));
  };
  const bx = box(spec.title || "Moo Won", () => send(2));
  bx.b.classList.add("dlg");
  bx.btns.remove();
  bx.body.style.width = `${spec.cx * DLU_X + 28}px`;
  bx.body.style.height = `${spec.cy * DLU_Y + 28}px`;
  const ctls = new Map();
  let focusEl = null, firstEdit = null;
  for (const c of spec.controls) {
    let el;
    const kind = c.style & 0xF;
    if (c.cls === 0x80 && (kind === 2 || kind === 3 || kind === 5 || kind === 6)) {
      const l = document.createElement("label");
      l.className = "ctl chk";
      el = document.createElement("input");
      el.type = "checkbox";
      el.checked = !!c.check;
      l.append(el, document.createTextNode(label(c.text)));
      place(l, c);
      bx.body.appendChild(l);
      ctls.set(c.id, el);
      continue;
    }
    if (c.cls === 0x80) {
      el = document.createElement("button");
      el.textContent = label(c.text);
      if (kind === 1) el.className = "def";
      el.addEventListener("click", () => send(c.id));
    } else if (c.cls === 0x81) {
      el = document.createElement(c.style & 0x4 ? "textarea" : "input");
      if (el.tagName === "INPUT") el.type = "text";
      el.value = c.text.replace(/\r\n/g, "\n");
      if (c.limit > 0) el.maxLength = c.limit;
      if (el.tagName === "INPUT") el.addEventListener("keydown", (e) => { if (e.key === "Enter") { e.preventDefault(); send(1); } });
      firstEdit ??= el;
    } else if (c.cls === 0x85) {
      el = document.createElement("select");
      c.items.forEach((t, i) => el.add(new Option(label(t), i)));
      el.selectedIndex = c.sel;
    } else {
      el = document.createElement("div");
      el.className = "static";
      el.textContent = label(c.text);
    }
    el.classList.add("ctl");
    place(el, c);
    bx.body.appendChild(el);
    if (c.cls !== 0x82) ctls.set(c.id, el);
    else ctls.set(c.id, el);
    if (c.focus) focusEl = el;
  }
  dlg = { token, bx, ctls };
  setTimeout(() => { const f = focusEl || firstEdit; if (f) { f.focus(); if (f.select) f.select(); } });
  function place(el, c) {
    el.style.left = `${14 + c.x * DLU_X}px`;
    el.style.top = `${14 + c.y * DLU_Y}px`;
    el.style.width = `${c.w * DLU_X}px`;
    // (a Windows drop-down list's height includes its open list: the closed box is one line)
    el.style.height = `${(c.cls === 0x85 ? Math.min(c.h, 13) : c.h) * DLU_Y}px`;
  }
}
function closeDialog() { if (dlg) { dlg.bx.close(); dlg = null; } }

function acceptOf(filter) {
  const exts = new Set();
  for (const [, pat] of filter) for (const p of pat.split(";")) { const m = /\*(\.[^.*;]+)$/.exec(p.trim()); if (m) exts.add(m[1].toLowerCase()); }
  return [...exts].join(",");
}
function showOpen(token, spec) {
  let done = false;
  const finish = (ok, names) => { if (done) return; done = true; bx.close(); answer(token, ok ? 1 : 0, ok ? names : null); };
  const input = document.createElement("input");
  input.type = "file";
  input.multiple = !!spec.multi;
  input.accept = acceptOf(spec.filter);
  input.addEventListener("change", async () => {
    const files = [...input.files];
    if (!files.length) return finish(false);
    try { M.FS.mkdir("/Upload"); } catch { /* there already */ }
    const names = [];
    for (const f of files) {
      const name = f.name.replace(/[^\x20-\x7E]|[\\/:*?"<>|]/g, "_");
      M.FS.writeFile(`/Upload/${name}`, new Uint8Array(await f.arrayBuffer()));
      names.push(name);
    }
    finish(true, names.join("\n"));
  });
  input.addEventListener("cancel", () => finish(false));
  const bx = box(spec.title || "Open", () => finish(false));
  bx.body.textContent = `Pick ${spec.multi ? "one or more files" : "a file"} from your computer${input.accept ? ` (${input.accept})` : ""}.`;
  button(bx.btns, "Choose…", true, () => input.click()).focus();
  button(bx.btns, "Cancel", false, () => finish(false));
}
function showSave(token, spec) {
  let done = false;
  const finish = (name) => { if (done) return; done = true; bx.close(); answer(token, name ? 1 : 0, name); };
  const bx = box(spec.title || "Save", () => finish(null));
  const input = document.createElement("input");
  input.type = "text";
  input.style.width = "100%";
  let name = spec.file || "Moo Won";
  if (spec.ext && !name.toLowerCase().endsWith(`.${spec.ext.toLowerCase()}`)) name += `.${spec.ext}`;
  input.value = name;
  bx.body.textContent = "The file is saved to your computer's downloads.\n";
  bx.body.appendChild(input);
  const go = () => { const n = input.value.trim().replace(/[^\x20-\x7E]|[\\/:*?"<>|]/g, "_"); finish(n || null); };
  input.addEventListener("keydown", (e) => { if (e.key === "Enter") { e.preventDefault(); go(); } });
  button(bx.btns, "Save", true, go);
  button(bx.btns, "Cancel", false, () => finish(null));
  setTimeout(() => { input.focus(); input.setSelectionRange(0, input.value.lastIndexOf(".") > 0 ? input.value.lastIndexOf(".") : input.value.length); });
}
function download(path, name) {
  try {
    const data = M.FS.readFile(path);
    const url = URL.createObjectURL(new Blob([data]));
    const a = document.createElement("a");
    a.href = url;
    a.download = name;
    a.click();
    setTimeout(() => URL.revokeObjectURL(url), 10000);
  } catch (err) { console.warn("download failed", err); }
}
const CURSORS = { 32512: "default", 32649: "pointer", 32513: "text", 32644: "ew-resize", 32645: "ns-resize", 32646: "move" };

// ---- saving the browser's copy of the files ----
let syncTimer = 0, syncing = false, syncAgain = false;
function syncSoon() {
  clearTimeout(syncTimer);
  syncTimer = setTimeout(syncNow, 1500);
}
function syncNow() {
  if (syncing) { syncAgain = true; return; }
  syncing = true;
  M.FS.syncfs(false, () => { syncing = false; if (syncAgain) { syncAgain = false; syncSoon(); } });
}

// ---- MIDI keyboards ----
async function setupMidi() {
  const st = $("midi-status");
  if (!navigator.requestMIDIAccess) { st.textContent = "This browser has no MIDI (Chrome and Edge do)."; return; }
  try {
    const access = await navigator.requestMIDIAccess();
    const hook = () => {
      for (const inp of access.inputs.values()) inp.onmidimessage = (e) => {
        const [s, a = 0, b = 0] = e.data;
        if (s < 0xF0) M._web_midi(s, a, b);
      };
      st.textContent = access.inputs.size ? `MIDI: ${[...access.inputs.values()].map((i) => i.name).join(", ")}` : "No MIDI keyboard found.";
    };
    access.onstatechange = hook;
    hook();
  } catch { st.textContent = "MIDI was not allowed."; }
}

// (2026-11-02) a browser built into another app (Messenger, Facebook, Instagram ...) has no shared memory between threads,
// which the plugin needs: say so plainly, and offer the way out
function cantRunHere() {
  const ua = navigator.userAgent, link = location.href.split("#")[0];
  const android = /Android/i.test(ua), ios = /iPhone|iPad|iPod/i.test(ua);
  const inApp = /FBAN|FBAV|FB_IAB|FBIOS|Messenger|Instagram|MicroMessenger|Line\/|; wv\)/i.test(ua);
  $("start").hidden = true;
  $("status").textContent = (inApp ? "This app's built-in browser can't run Moo Won." : "This browser can't run Moo Won.") +
    (ios ? " Open it in Safari: tap the ... menu, then Open in Safari, or copy the link." :
     android ? " Open it in Chrome with the button below, or from the ... menu." :
     " Open it in Chrome, Edge, Firefox or Safari.");
  const box = $("status").parentNode;
  if (android) {
    const a = document.createElement("a");
    a.className = "big red";
    a.style.cssText = "display:inline-block;text-decoration:none;margin:6px;padding:14px 26px;border-radius:6px;background:var(--red);color:#fff;font-size:18px";
    a.textContent = "Open in Chrome";
    a.href = `intent://${location.host}${location.pathname}#Intent;scheme=https;package=com.android.chrome;` +
      `S.browser_fallback_url=${encodeURIComponent(link)};end`;
    box.appendChild(a);
  }
  const c = document.createElement("button");
  c.className = "big";
  c.style.margin = "6px";
  c.textContent = "Copy the link";
  c.addEventListener("click", async () => {
    try { await navigator.clipboard.writeText(link); c.textContent = "Copied"; }
    catch { c.textContent = link; }
  });
  box.appendChild(c);
}

// ---- starting ----
// (2026-11-02) the page starts on Init; LAST SESSION (the bar's and the sections view's) brings back what was playing
function wireLastSession() {
  const dir = "/Users/web/AppData/Roaming/Moo Won/";
  const there = (f) => { try { M.FS.stat(dir + f); return true; } catch { return false; } };
  const bs = [$("last-session"), ...document.querySelectorAll(".sec-last")];
  for (const b of bs) {
    b.hidden = !there("web_state.bin") && !there("web_last.bin");
    b.addEventListener("click", () => { M._web_last_session(); bs.forEach((x) => { x.hidden = true; }); });
  }
}

async function load() {
  if (!crossOriginIsolated) {
    cantRunHere();
    return;
  }
  M = await createMooWon({
    locateFile: (f) => f,
    mwUI: (kind, token, json) => {
      const spec = JSON.parse(json);
      if (kind === UI.MENU) showMenu(token, spec);
      else if (kind === UI.MSGBOX) showMessage(token, spec);
      else if (kind === UI.DIALOG) showDialog(token, spec);
      else if (kind === UI.OPEN) showOpen(token, spec);
      else if (kind === UI.SAVE) showSave(token, spec);
    },
    mwDialogClose: closeDialog,
    mwDownload: download,
    mwCursor: (id) => { canvas.style.cursor = CURSORS[id] || "default"; },
    mwSync: syncSoon,
  });
  window.mooWon = M;   // (for checking from the browser's console)
  M.FS.mkdir("/Users");
  M.FS.mount(M.IDBFS, {}, "/Users");
  await new Promise((res) => M.FS.syncfs(true, () => res()));
  addBundled();
  $("status").textContent = "Ready. Sound starts when you click.";
  $("start").disabled = false;
}
// the bundled libraries (ports/one-ish/libraries, in the module at /bundled): a bank not in the browser's library
// folder yet is copied there; once copied it is the user's (edits and deletions stay), as the Windows installer does
function addBundled() {
  const libs = "/Users/web/Documents/ONE-ish/Libraries";
  const mk = (p) => { try { M.FS.mkdir(p); } catch { /* there already */ } };
  let banks = [];
  try { banks = M.FS.readdir("/bundled").filter((n) => n !== "." && n !== ".."); } catch { return; }
  let added = 0;
  for (const bank of banks) {
    const to = `${libs}/${bank}`;
    try { M.FS.stat(to); continue; } catch { /* not there: copy it */ }
    for (const p of ["/Users/web", "/Users/web/Documents", "/Users/web/Documents/ONE-ish", libs, to]) mk(p);
    for (const f of M.FS.readdir(`/bundled/${bank}`)) {
      if (f.endsWith(".oneish")) M.FS.writeFile(`${to}/${f}`, M.FS.readFile(`/bundled/${bank}/${f}`));
    }
    added++;
  }
  if (added) syncSoon();
}

async function start() {
  $("start").disabled = true;
  $("status").textContent = "Starting…";
  try {
    ctx = new AudioContext({ latencyHint: "interactive" });
    await ctx.audioWorklet.addModule("ring_worklet.js");
    const dpr = window.devicePixelRatio || 1;
    const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));
    // (drawn at least at full size and shrunk by the browser: below 1 the panel's labels crowd, as on a small screen)
    const asked = Number(params.get("scale"));
    const zoomed = wantsSections() || isTouch ? dpr * 1.25 : 0;   // (pieces shown up to 1.5x, the camera closer: draw sharper)
    const scale = asked ? clamp(asked, 0.5, 2) : clamp(Math.max((innerWidth * dpr) / 2162, zoomed), 1, 2);
    const latency = clamp(Math.round(Number(params.get("latency")) || 1024), 256, 3072);
    if (!M._web_start(ctx.sampleRate, scale, latency)) throw new Error("the plugin could not start");
    infoPtr = M._web_info();
    await new Promise((res, rej) => {
      const wait = () => {
        const H = heap().I32, i = infoPtr >> 2;
        if (H[i + 9]) return rej(new Error(`the plugin could not start (${H[i + 9]})`));
        if (Atomics.load(H, i) === 1) return res();
        setTimeout(wait, 30);
      };
      wait();
    });
    const H = heap().I32, i = infoPtr >> 2;
    presPtr = H[i + 1];
    panelW = H[i + 7];
    panelH = H[i + 8];
    canvas.width = panelW;
    canvas.height = panelH;
    const node = new AudioWorkletNode(ctx, "moo-won-ring", {
      numberOfInputs: 0, outputChannelCount: [2],
      processorOptions: { memory: M.wasmMemory.buffer, ring: H[i + 2], frames: H[i + 3], info: infoPtr },
    });
    node.connect(ctx.destination);
    await ctx.resume();
    $("start-box").hidden = true;
    $("bar").hidden = false;
    document.body.classList.add("running");
    document.body.classList.toggle("mouse", !isTouch);
    sections = createSections({ source: canvas, scale: panelW / 2162, root: $("sections"), onToggle: toggleMode, onCables: toggleCables });
    wireLastSession();
    $("to-sections").addEventListener("click", toggleMode);
    let saved = null;
    try { saved = localStorage.getItem("mw.view"); } catch { /* (none) */ }
    setMode(saved === "whole" || saved === "sections" ? saved : wantsSections() ? "sections" : "whole");
    setupInput();
    setupCameraButtons();
    window.mooWonCamera = camera;   // (for checking from the browser's console)
    requestAnimationFrame(present);
    canvas.focus({ preventScroll: true });
    setupMidi();
    const blockMs = 128 / ctx.sampleRate * 1000;
    setInterval(() => {
      const F = heap().F32, I = heap().I32;
      $("cpu").textContent = `audio ${Math.round(F[(infoPtr >> 2) + 11] / blockMs * 100)}% · gaps ${I[(infoPtr >> 2) + 10]}`;
    }, 1000);
    addEventListener("pagehide", syncNow);
    document.addEventListener("visibilitychange", () => { if (document.hidden) syncNow(); });
  } catch (err) {
    $("status").textContent = `The sound could not start: ${err instanceof Error ? err.message : err}`;
    $("start").disabled = false;
  }
}
$("start").addEventListener("click", start);
// the browser's own page zoom stays out of it: iPhones ignore the viewport's no-zoom and touch-action on some gestures
document.addEventListener("gesturestart", (e) => e.preventDefault());
document.addEventListener("touchmove", (e) => {
  if (document.body.classList.contains("running") && (e.touches.length > 1 || e.target.closest("#stage, #bar"))) e.preventDefault();
}, { passive: false });
load().catch((err) => { $("status").textContent = `The synth could not load: ${err instanceof Error ? err.message : err}`; });
