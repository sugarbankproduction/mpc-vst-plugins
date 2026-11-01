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
const MAX_ZOOM = 2.5;   // (2026-11-02: a big monitor fills too; a tab always fits, never scrolls)
const KEYS_X0 = 384, KEYS_X1 = 2128, WHITE = (KEYS_X1 - KEYS_X0) / 36;   // five octaves of white keys, C2 to C7
const STRIP_Y0 = 610, STRIP_Y1 = 842;                                     // the ribbon and the keys

export function createSections({ source, scale, root, onToggle, onCables }) {
  const s = scale;   // panel pixels per base pixel
  const store = {
    get(k) { try { return localStorage.getItem(k); } catch { return null; } },
    set(k, v) { try { localStorage.setItem(k, v); } catch { /* (not kept) */ } },
  };
  let tab = Math.min(TABS.length - 1, Math.max(0, Number(store.get("mw.tab")) || 0));
  const savedOct = store.get("mw.oct");   // (nothing saved yet: the keys from C4)
  let oct = savedOct === null ? 2 : Number(savedOct);
  if (!Number.isFinite(oct)) oct = 2;
  let keysShown = store.get("mw.keys") !== "0";

  root.innerHTML = `<div class="sec-tabs"></div><div class="sec-body"><canvas class="sec-view" tabindex="0"></canvas></div>
    <div class="sec-kbar"><button class="sec-oct" data-d="-1" aria-label="Lower keys">&lt;</button>
    <span class="sec-octlabel"></span><button class="sec-oct" data-d="1" aria-label="Higher keys">&gt;</button>
    <button class="sec-small sec-cables">CABLES</button><button class="sec-small sec-showkeys">KEYS</button>
    <button class="sec-small sec-whole">WHOLE PANEL</button></div><canvas class="sec-keys"></canvas>`;
  const tabsEl = root.querySelector(".sec-tabs"), body = root.querySelector(".sec-body");
  const view = root.querySelector(".sec-view"), keys = root.querySelector(".sec-keys");
  const octLabel = root.querySelector(".sec-octlabel"), kbar = root.querySelector(".sec-kbar");
  const showKeys = root.querySelector(".sec-showkeys"), cablesBtn = root.querySelector(".sec-cables");
  TABS.forEach((t, i) => {
    const b = document.createElement("button");
    b.textContent = t.name;
    b.addEventListener("click", () => { tab = i; store.set("mw.tab", i); layout(); });
    tabsEl.appendChild(b);
  });
  root.querySelectorAll(".sec-oct").forEach((b) => b.addEventListener("click", () => {
    oct += Number(b.dataset.d);
    store.set("mw.oct", oct);
    layout();
  }));
  root.querySelector(".sec-whole").addEventListener("click", onToggle);
  cablesBtn.addEventListener("click", onCables);
  showKeys.addEventListener("click", () => {
    keysShown = !keysShown;
    store.set("mw.keys", keysShown ? "1" : "0");
    layout();
  });

  let placed = [], kplaced = [];   // what each canvas shows: {dx, dy, dw, dh (CSS pixels), sx, sy, sw, sh (base)}
  // the tab's rectangles, in the room there is, the larger of two ways: its rows one over the other (each as wide as it
  // can be), or all in one line (a wide screen). Never scrolls: a finger on a piece is always the control's.
  function arrange(rows, W, H) {
    const GAP = 6, EDGE = 8;
    const wide = (row) => row.reduce((a, r) => a + r[2], 0) + GAP * (row.length - 1);
    const tall = (row) => Math.max(...row.map((r) => r[3]));
    // stacked: each row to the width, then all of them down together if that's too tall
    let ks = rows.map((row) => Math.min((W - EDGE) / wide(row), MAX_ZOOM));
    const gaps = 8 * (rows.length - 1) + EDGE;
    const sum = rows.reduce((a, row, i) => a + tall(row) * ks[i], 0);
    if (sum > H - gaps) ks = ks.map((v) => v * (H - gaps) / sum);
    const flat = rows.flat();
    const kLine = Math.min((W - EDGE) / wide(flat), (H - EDGE) / tall(flat), MAX_ZOOM);
    const area = (list, k) => list.reduce((a, r) => a + r[2] * r[3] * k * k, 0);
    const useLine = rows.length > 1 && area(flat, kLine) > rows.reduce((a, row, i) => a + area(row, ks[i]), 0);
    const lines = useLine ? [flat] : rows, kOf = useLine ? [kLine] : ks;
    const used = lines.reduce((a, row, i) => a + tall(row) * kOf[i], 0) + 8 * (lines.length - 1);
    let y = Math.max(4, (H - used) / 2);
    const out = [];
    lines.forEach((row, i) => {
      const k = kOf[i];
      let x = (W - wide(row) * k) / 2;
      for (const [sx, sy, sw, sh] of row) {
        out.push({ dx: x, dy: y, dw: sw * k, dh: sh * k, sx, sy, sw, sh });
        x += (sw + GAP) * k;
      }
      y += tall(row) * k + 8;
    });
    return out;
  }
  function layout() {
    if (root.hidden) return;
    const KW = Math.max(200, kbar.clientWidth);
    tabsEl.querySelectorAll("button").forEach((b, i) => b.classList.toggle("on", i === tab));
    showKeys.classList.toggle("on", keysShown);
    // the keys first (the section gets what's left): as many white keys as fit at about 40 px each, from the octave
    // picked; on a low screen they are shorter rather than fewer
    const whites = Math.max(7, Math.min(36, Math.round(KW / 40)));
    const maxOct = Math.max(0, Math.floor((36 - whites) / 7));
    oct = Math.max(0, Math.min(maxOct, oct));
    const sx = KEYS_X0 + oct * 7 * WHITE, sw = Math.min(whites * WHITE, KEYS_X1 - sx);
    const natural = (STRIP_Y1 - STRIP_Y0) * KW / sw;
    const kh = Math.min(natural, Math.max(64, root.clientHeight * (innerWidth > innerHeight ? 0.2 : 0.36)));
    keys.hidden = !keysShown;
    kplaced = keysShown ? [{ dx: 0, dy: 0, dw: KW, dh: kh, sx, sy: STRIP_Y0, sw, sh: STRIP_Y1 - STRIP_Y0 }] : [];
    if (keysShown) size(keys, KW, kh);
    octLabel.textContent = `C${2 + oct}`;
    const W = Math.max(100, body.clientWidth), H = Math.max(100, body.clientHeight);
    placed = arrange(TABS[tab].rows, W, H);
    size(view, W, H);
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
  function draw() { paint(view, placed); if (keysShown) paint(keys, kplaced); }

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
  // (a rotation: the size comes in stages on some phones, so whatever says it changed)
  new ResizeObserver(() => layout()).observe(root);
  return { layout, draw, locate, pageOf, elements: [view, keys], setCables(on) { cablesBtn.classList.toggle("on", on); } };
}
