// The phone and narrow-window view (layout B, the owner's pick, 2026-11-01): the panel's own sections one at a time
// on tabs, with the keyboard (and the ribbon over it) always at the bottom. The editor still draws the whole panel
// (into the page's full-size canvas, which this view keeps out of sight); this view copies the parts a tab shows,
// as large as the screen allows, and sends touches back at the panel's own coordinates, so every control works as on
// the whole panel. Rectangles are in the panel's base pixels (BASE_W x BASE_H, desktop/gen_layout.py's layout).
const BASE_W = 2162;
const TABS = [   // each tab: rows of rectangles [x, y, w, h], each row scaled to the width (never more than MAX_ZOOM)
  { name: "SCREEN", rows: [[[758, 10, 424, 592]]] },
  { name: "OSC", rows: [[[172, 14, 472, 296]], [[172, 310, 472, 96]]] },
  { name: "LFO", rows: [[[38, 14, 136, 288], [38, 302, 136, 292], [642, 14, 104, 392]]] },
  { name: "ARP", rows: [[[172, 404, 302, 194]], [[472, 404, 274, 194]]] },
  { name: "FILTER", rows: [[[1184, 14, 458, 584]]] },
  { name: "ENV", rows: [[[1640, 14, 486, 338]]] },
  { name: "FX", rows: [[[1640, 350, 486, 248]]] },
  { name: "PLAY", rows: [[[30, 612, 348, 238]]] },
];
const MAX_ZOOM = 1.5;
const KEYS_X0 = 384, KEYS_X1 = 2128, WHITE = (KEYS_X1 - KEYS_X0) / 36;   // five octaves of white keys, C2 to C7
const STRIP_Y0 = 610, STRIP_Y1 = 842;                                     // the ribbon and the keys

export function createSections({ source, scale, root, onToggle }) {
  const s = scale;   // panel pixels per base pixel
  const store = {
    get(k) { try { return localStorage.getItem(k); } catch { return null; } },
    set(k, v) { try { localStorage.setItem(k, v); } catch { /* (not kept) */ } },
  };
  let tab = Math.min(TABS.length - 1, Math.max(0, Number(store.get("mw.tab")) || 0));
  const savedOct = store.get("mw.oct");   // (nothing saved yet: the keys from C4)
  let oct = savedOct === null ? 2 : Number(savedOct);
  if (!Number.isFinite(oct)) oct = 2;

  root.innerHTML = `<div class="sec-tabs"></div><div class="sec-body"><canvas class="sec-view" tabindex="0"></canvas></div>
    <div class="sec-kbar"><button class="sec-oct" data-d="-1" aria-label="Lower keys">&lt;</button>
    <span class="sec-octlabel"></span><button class="sec-oct" data-d="1" aria-label="Higher keys">&gt;</button>
    <button class="sec-whole">WHOLE PANEL</button></div><canvas class="sec-keys"></canvas>`;
  const tabsEl = root.querySelector(".sec-tabs"), body = root.querySelector(".sec-body");
  const view = root.querySelector(".sec-view"), keys = root.querySelector(".sec-keys");
  const octLabel = root.querySelector(".sec-octlabel");
  TABS.forEach((t, i) => {
    const b = document.createElement("button");
    b.textContent = t.name;
    b.addEventListener("click", () => { tab = i; store.set("mw.tab", i); layout(); body.scrollTop = 0; });
    tabsEl.appendChild(b);
  });
  root.querySelectorAll(".sec-oct").forEach((b) => b.addEventListener("click", () => {
    oct += Number(b.dataset.d);
    store.set("mw.oct", oct);
    layout();
  }));
  root.querySelector(".sec-whole").addEventListener("click", onToggle);

  let placed = [], kplaced = [];   // what each canvas shows: {dx, dy, dw, dh (CSS pixels), sx, sy, sw, sh (base)}
  function layout() {
    const W = Math.max(200, body.clientWidth);
    tabsEl.querySelectorAll("button").forEach((b, i) => b.classList.toggle("on", i === tab));
    placed = [];
    let y = 4;
    for (const row of TABS[tab].rows) {
      const sum = row.reduce((a, r) => a + r[2], 0) + 6 * (row.length - 1);
      const k = Math.min((W - 8) / sum, MAX_ZOOM);
      let x = (W - sum * k) / 2, h = 0;
      for (const [sx, sy, sw, sh] of row) {
        placed.push({ dx: x, dy: y, dw: sw * k, dh: sh * k, sx, sy, sw, sh });
        x += (sw + 6) * k;
        h = Math.max(h, sh * k);
      }
      y += h + 8;
    }
    size(view, W, y);
    // the keys: as many white keys as fit at about 40 px each, from the octave picked
    const whites = Math.max(7, Math.min(36, Math.round(W / 40)));
    const maxOct = Math.max(0, Math.floor((36 - whites) / 7));
    oct = Math.max(0, Math.min(maxOct, oct));
    const sx = KEYS_X0 + oct * 7 * WHITE, sw = Math.min(whites * WHITE, KEYS_X1 - sx);
    const k = W / sw, kh = (STRIP_Y1 - STRIP_Y0) * k;
    kplaced = [{ dx: 0, dy: 0, dw: W, dh: kh, sx, sy: STRIP_Y0, sw, sh: STRIP_Y1 - STRIP_Y0 }];
    size(keys, W, kh);
    octLabel.textContent = `C${2 + oct}`;
    draw();
  }
  function size(c, w, h) {
    const dpr = window.devicePixelRatio || 1;
    c.style.width = `${w}px`;
    c.style.height = `${h}px`;
    c.width = Math.round(w * dpr);
    c.height = Math.round(h * dpr);
  }
  function paint(c, list) {
    const g = c.getContext("2d", { alpha: false });
    const dpr = c.width / parseFloat(c.style.width);
    g.fillStyle = "#121314";
    g.fillRect(0, 0, c.width, c.height);
    g.imageSmoothingEnabled = true;
    g.imageSmoothingQuality = "high";
    for (const p of list)
      g.drawImage(source, p.sx * s, p.sy * s, p.sw * s, p.sh * s, p.dx * dpr, p.dy * dpr, p.dw * dpr, p.dh * dpr);
  }
  function draw() { paint(view, placed); paint(keys, kplaced); }

  // a point on one of the two canvases -> panel pixels; `held` is the piece a drag started on (it keeps mapping
  // through that piece, even past its edge, so a knob turned by dragging keeps turning)
  function locate(el, e, held) {
    const list = el === keys ? kplaced : placed;
    const r = el.getBoundingClientRect();
    const cx = e.clientX - r.left, cy = e.clientY - r.top;
    const p = held || list.find((q) => cx >= q.dx && cx < q.dx + q.dw && cy >= q.dy && cy < q.dy + q.dh);
    if (!p) return null;
    return { piece: p, x: Math.round((p.sx + (cx - p.dx) * p.sw / p.dw) * s), y: Math.round((p.sy + (cy - p.dy) * p.sh / p.dh) * s) };
  }
  // panel pixels -> a point on the page (for a menu the editor opens there), or null when it isn't on screen
  function pageOf(x, y) {
    const bx = x / s, by = y / s;
    for (const [el, list] of [[view, placed], [keys, kplaced]]) {
      const p = list.find((q) => bx >= q.sx && bx < q.sx + q.sw && by >= q.sy && by < q.sy + q.sh);
      if (!p) continue;
      const r = el.getBoundingClientRect();
      const px = r.left + p.dx + (bx - p.sx) * p.dw / p.sw, py = r.top + p.dy + (by - p.sy) * p.dh / p.sh;
      if (py >= 0 && py <= innerHeight) return [px, py];
    }
    return null;
  }
  addEventListener("resize", () => { if (!root.hidden) layout(); });
  return { layout, draw, locate, pageOf, elements: [view, keys] };
}
