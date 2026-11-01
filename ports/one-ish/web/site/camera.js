// The whole panel's camera (2026-11-02): the panel's canvas stays one element, moved and scaled by a CSS transform, so
// nothing about drawing or sound changes and a pointer's position still maps back through its on-screen rectangle.
//
// Who gets a finger:
//   two fingers, wherever they land: always the camera (pinch and pan); never a control;
//   one finger: the control under it, except with PANEL ZOOM on, when only the focused panel's controls answer and a
//   finger anywhere else taps to focus that panel, or drags to pan.
// A control hears of a finger only after HOLD_MS (or as soon as it moves or lifts), which is how a second finger landing
// just after the first stops a button being pressed; the keyboard hears at once, so notes aren't late. A mouse is never
// held back.
const HOLD_MS = 90, TAP_PX = 10;        // a press not yet a control's; a press that moved less is a tap
const MAX_ZOOM = 3, MAX_FOCUS = 2.4;    // CSS pixels per base pixel: pinching, framing a panel
const MIN_FOCUS = 0.5;                  // a panel wider than the screen at this size (the keyboard, upright) is panned
const FRAME_PAD = 8, ANIM_MS = 380;
const EDGE_PX = 44, EDGE_SPEED = 14;    // a cable dragged near the screen's edge moves the camera (pixels a frame)
const BIG_PANEL = 300000;               // base pixels: the two big blocks are framed only when tapped between panels

export function createCamera({ stage, canvas, ring, baseW, baseH, panels, control, isTouch, cableDragging, beforePress, onChange }) {
  let k = 1, x = 0, y = 0, fit = 1, vw = 0, vh = 0, goalK = 1;   // goalK: where a glide is heading
  let panelZoom = false, focus = null, focusAtX = null;
  let anim = 0;
  const pts = new Map();          // the fingers down: id -> latest event
  let state = "idle";             // idle, pending, held, tapwait, pan, camera
  let mainId = null, pressE = null, holdTimer = 0, edgeLoop = 0;
  let lastCx = 0, lastCy = 0, lastDist = 0, touched = false;   // touched: fingers moved the camera this time

  const cycle = panels.filter((p) => p.w * p.h < BIG_PANEL)
    .sort((a, b) => (a.y >= 600) - (b.y >= 600) || a.x - b.x || a.y - b.y);
  const nameOf = (p) => p.title || (p.y >= 600 ? "HAND CONTROLS" : "SCREEN");

  function apply() {
    canvas.style.transform = `translate(${x}px, ${y}px) scale(${k})`;
    if (focus) {
      ring.hidden = false;
      ring.style.transform = `translate(${x + focus.x * k}px, ${y + focus.y * k}px)`;
      ring.style.width = `${focus.w * k}px`;
      ring.style.height = `${focus.h * k}px`;
    } else ring.hidden = true;
  }
  function clamped(ck, cx, cy) {   // the panel never leaves: smaller than the screen it's centred, larger it covers it
    const w = baseW * ck, h = baseH * ck;
    const midY = isTouch ? (vh - h) / 2 : 0;   // (with a mouse the panel sits at the top, as it always has)
    return {
      k: ck,
      x: w <= vw ? (vw - w) / 2 : Math.min(0, Math.max(vw - w, cx)),
      y: h <= vh ? midY : Math.min(0, Math.max(vh - h, cy)),
    };
  }
  function jump(t) { stopAnim(); goalK = t.k; k = t.k; x = t.x; y = t.y; apply(); }
  function stopAnim() { if (anim) cancelAnimationFrame(anim); anim = 0; }
  function glide(t) {
    stopAnim();
    goalK = t.k;
    const still = matchMedia("(prefers-reduced-motion: reduce)").matches || document.hidden;
    if (still) return jump(t);
    // (between two framings, in steps of the same ratio and along the point at the screen's middle: an even move)
    const k0 = k, cx0 = (vw / 2 - x) / k, cy0 = (vh / 2 - y) / k, cx1 = (vw / 2 - t.x) / t.k, cy1 = (vh / 2 - t.y) / t.k;
    const t0 = performance.now();
    const step = (now) => {
      const u = Math.min(1, (now - t0) / ANIM_MS), e = u < 0.5 ? 4 * u * u * u : 1 - (2 - 2 * u) ** 3 / 2;
      k = k0 * (t.k / k0) ** e;
      x = vw / 2 - (cx0 + (cx1 - cx0) * e) * k;
      y = vh / 2 - (cy0 + (cy1 - cy0) * e) * k;
      apply();
      if (u < 1) anim = requestAnimationFrame(step);
      else { anim = 0; k = t.k; x = t.x; y = t.y; apply(); }
    };
    anim = requestAnimationFrame(step);
    return undefined;
  }
  function framing(p, nearX) {
    const room = Math.min((vw - 2 * FRAME_PAD) / p.w, (vh - 2 * FRAME_PAD) / p.h);
    const ck = Math.max(fit, Math.min(MAX_ZOOM, Math.max(MIN_FOCUS, Math.min(room, MAX_FOCUS))));
    const half = vw / 2 / ck;
    const mid = p.w * ck <= vw || nearX === null ? p.x + p.w / 2
      : Math.min(p.x + p.w - half, Math.max(p.x + half, nearX));
    return clamped(ck, vw / 2 - mid * ck, vh / 2 - (p.y + p.h / 2) * ck);
  }
  function setFocus(p, nearX = null) {
    focus = p;
    focusAtX = nearX;
    apply();
    if (p) glide(framing(p, nearX));
    onChange();
  }
  function showAll() {
    focus = null;
    glide(clamped(fit, 0, 0));
    onChange();
  }

  // the stage changed size (a rotation, the bar wrapping, sections view back to this one)
  function layout() {
    const nw = stage.clientWidth, nh = stage.clientHeight;
    if (!nw || !nh) return;
    const wasFit = !vw || k <= fit * 1.001;
    const midX = vw ? (vw / 2 - x) / k : baseW / 2, midY = vh ? (vh / 2 - y) / k : baseH / 2;
    vw = nw;
    vh = nh;
    fit = Math.min(vw / baseW, vh / baseH);
    if (focus) jump(framing(focus, focusAtX));
    else if (wasFit) jump(clamped(fit, 0, 0));
    else { const ck = Math.max(fit, k); jump(clamped(ck, vw / 2 - midX * ck, vh / 2 - midY * ck)); }
  }

  const stageXY = (e) => { const r = stage.getBoundingClientRect(); return [e.clientX - r.left, e.clientY - r.top]; };
  function baseAt(e) { const [sx, sy] = stageXY(e); return [(sx - x) / k, (sy - y) / k]; }
  function panelAt(bx, by) {
    let best = null;
    for (const p of panels)
      if (bx >= p.x && bx < p.x + p.w && by >= p.y && by < p.y + p.h && (!best || p.w * p.h < best.w * best.h)) best = p;
    return best;
  }
  const inside = (p, bx, by) => p && bx >= p.x && bx < p.x + p.w && by >= p.y && by < p.y + p.h;

  function pinchPoints() {
    const [a, b] = [...pts.values()];
    const [ax, ay] = stageXY(a), [bx, by] = stageXY(b);
    return { cx: (ax + bx) / 2, cy: (ay + by) / 2, dist: Math.hypot(ax - bx, ay - by) };
  }
  function startCamera() {
    stopAnim();
    clearTimeout(holdTimer);
    if (state === "held") control.up(pts.get(mainId));   // a control a finger was on lets go where it was
    stopEdge();
    state = "camera";
    mainId = null;
    if (pts.size >= 2) { const q = pinchPoints(); lastCx = q.cx; lastCy = q.cy; lastDist = q.dist; }
  }
  function moveCamera() {
    touched = true;
    const q = pinchPoints();
    const ck = Math.min(Math.max(MAX_ZOOM, fit), Math.max(fit, k * (lastDist > 0 ? q.dist / lastDist : 1)));
    // the base point under the fingers' middle stays under it
    const bx = (lastCx - x) / k, by = (lastCy - y) / k;
    jump(clamped(ck, q.cx - bx * ck, q.cy - by * ck));
    lastCx = q.cx;
    lastCy = q.cy;
    lastDist = q.dist;
  }
  function startPan(e) {
    stopAnim();
    state = "pan";
    mainId = e.pointerId;
    [lastCx, lastCy] = stageXY(e);
  }
  function movePan(e) {
    const [sx, sy] = stageXY(e);
    jump(clamped(k, x + sx - lastCx, y + sy - lastCy));
    lastCx = sx;
    lastCy = sy;
  }
  function deliver(first, latest) {   // the press becomes the control's
    clearTimeout(holdTimer);
    state = "held";
    if (first.pointerType !== "mouse") beforePress(first, k);
    control.down(first);
    if (latest && latest !== first) control.move(latest);
    startEdge();
  }
  // a cable being dragged near the screen's edge moves the camera, so it can reach the whole panel
  function startEdge() {
    stopEdge();
    const step = () => {
      edgeLoop = requestAnimationFrame(step);
      if (state !== "held" || !cableDragging()) return;
      const e = pts.get(mainId);
      if (!e) return;
      const [sx, sy] = stageXY(e);
      const push = (at, size) => (at < EDGE_PX ? (EDGE_PX - at) : at > size - EDGE_PX ? size - EDGE_PX - at : 0) / EDGE_PX * EDGE_SPEED;
      const dx = push(sx, vw), dy = push(sy, vh);
      if (!dx && !dy) return;
      const t = clamped(k, x + dx, y + dy);
      if (t.x !== x || t.y !== y) { jump(t); control.move(e); }
    };
    edgeLoop = requestAnimationFrame(step);
  }
  function stopEdge() { if (edgeLoop) cancelAnimationFrame(edgeLoop); edgeLoop = 0; }

  stage.addEventListener("pointerdown", (e) => {
    const mouse = e.pointerType === "mouse";
    if (mouse && e.button !== 0) {   // (the other mouse buttons are always the panel's)
      if (state === "idle" && e.target === canvas) { pts.set(e.pointerId, e); mainId = e.pointerId; state = "held"; control.down(e); }
      return;
    }
    if (state === "held" && cableDragging()) return;   // (a cable in the hand: a second finger leaves it there; the edges pan)
    pts.set(e.pointerId, e);
    try { stage.setPointerCapture(e.pointerId); } catch { /* (a pointer the browser no longer tracks) */ }
    e.preventDefault();
    if (pts.size >= 2) { startCamera(); return; }
    if (state !== "idle") return;   // (the last finger of a pinch still down: see pointerup)
    mainId = e.pointerId;
    pressE = e;
    const [bx, by] = baseAt(e);
    const on = e.target === canvas;
    if (!on || (panelZoom && !inside(focus, bx, by))) {   // nothing of a control's here: a tap, or a drag to pan
      state = "tapwait";
      return;
    }
    const p = panelAt(bx, by);
    if (mouse || (p && p.keys)) { deliver(e); return; }
    state = "pending";
    holdTimer = setTimeout(() => { if (state === "pending") deliver(pressE, pts.get(mainId)); }, HOLD_MS);
  });
  stage.addEventListener("pointermove", (e) => {
    if (!pts.has(e.pointerId)) {
      if (e.pointerType === "mouse" && state === "idle" && e.target === canvas) control.move(e);   // (hover)
      return;
    }
    pts.set(e.pointerId, e);
    if (state === "camera") {
      if (pts.size >= 2) moveCamera();
    } else if (state === "pan") {
      if (e.pointerId === mainId) movePan(e);
    } else if (e.pointerId !== mainId) {
      /* (not the finger that counts) */
    } else if (state === "held") {
      control.move(e);
    } else {
      const far = Math.hypot(e.clientX - pressE.clientX, e.clientY - pressE.clientY) >= TAP_PX;
      if (state === "tapwait" && far) startPan(e);
      else if (state === "pending" && far) deliver(pressE, e);
    }
  });
  function lift(e) {
    if (!pts.has(e.pointerId)) return;
    const wasMain = e.pointerId === mainId;
    pts.set(e.pointerId, e);
    if (state === "pending" && wasMain) {   // a quick tap: pressed and let go now
      clearTimeout(holdTimer);
      if (e.type !== "pointercancel") { beforePress(pressE, k); control.down(pressE); control.up(e); }
      state = "idle";
    } else if (state === "held" && wasMain) {
      stopEdge();
      control.up(e);
      state = "idle";
    } else if (state === "tapwait" && wasMain) {
      state = "idle";
      if (e.type !== "pointercancel" && panelZoom) {
        const [bx, by] = baseAt(e), p = panelAt(bx, by);
        if (p) setFocus(p, p.w * MIN_FOCUS > vw ? bx : null);
      }
    }
    pts.delete(e.pointerId);
    if (state === "camera" && pts.size === 1) startPan(pts.values().next().value);   // the other finger pans on
    if (pts.size === 0) {
      state = "idle";
      mainId = null;
      // pinched all the way out: nothing is framed any more
      if (touched && focus && k <= fit * 1.001 && framing(focus, null).k > fit * 1.001) setFocus(null);
      else onChange();
      touched = false;
    }
  }
  stage.addEventListener("pointerup", lift);
  stage.addEventListener("pointercancel", lift);

  canvas.style.width = `${baseW}px`;
  canvas.style.height = `${baseH}px`;
  layout();
  new ResizeObserver(layout).observe(stage);

  return {
    layout,
    showAll,
    zoomBy(f) {   // the + and - buttons: about the screen's middle; a framed panel stays framed only when it fits again
      const ck = Math.min(Math.max(MAX_ZOOM, fit), Math.max(fit, k * f));
      const bx = (vw / 2 - x) / k, by = (vh / 2 - y) / k;
      if (ck <= fit * 1.001) focus = null;
      apply();
      glide(clamped(ck, vw / 2 - bx * ck, vh / 2 - by * ck));
      onChange();
    },
    setPanelZoom(on) {
      panelZoom = on;
      if (!on) showAll(); else onChange();
    },
    step(dir) {   // the < > beside PANEL ZOOM: the panel before or after this one
      const at = focus ? cycle.indexOf(focus) : -1;
      setFocus(cycle[at < 0 ? (dir > 0 ? 0 : cycle.length - 1) : (at + dir + cycle.length) % cycle.length]);
    },
    get panelZoom() { return panelZoom; },
    get focusName() { return focus ? nameOf(focus) : ""; },
    get zoomed() { return goalK > fit * 1.001; },
    get info() { return { k, x, y, fit, vw, vh, state, focus: focus ? nameOf(focus) : null }; },   // (for checks)
  };
}
