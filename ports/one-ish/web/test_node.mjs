// Offline check of the web build (web/build.sh): loads site/moowon.wasm as the sound worklet does, plays a chord on
// a few factory presets and reports level and speed (how many times faster than real time; the browser needs > 1).
// Run: node ports/one-ish/web/test_node.mjs
import { readFileSync } from "node:fs";
import { loadEngine } from "./site/engine.js";

const bytes = readFileSync(new URL("./site/moowon.wasm", import.meta.url));
const eng = loadEngine(new WebAssembly.Module(bytes));
let fails = 0;
const n = eng.presetCount();
console.log(`${n} presets`);
if (n < 1) fails++;
for (const idx of [0, 1, 6, 37, 271]) {
  eng.preset(idx);
  for (const note of [48, 55, 60, 64]) eng.midi(0x90, note, 100);
  const blocks = Math.round(3 * 44100 / 128);   // 3 seconds
  let peak = 0, bad = 0;
  const t0 = performance.now();
  for (let b = 0; b < blocks; b++) {
    const out = eng.render();
    for (let i = 0; i < out.length; i++) {
      const v = out[i];
      if (!Number.isFinite(v)) bad++;
      else if (Math.abs(v) > peak) peak = Math.abs(v);
    }
    if (b === blocks / 2 | 0) for (const note of [48, 55, 60, 64]) eng.midi(0x80, note, 0);
  }
  const ms = performance.now() - t0;
  const speed = 3000 / ms;
  const ok = peak > 0.001 && bad === 0;
  if (!ok) fails++;
  console.log(`${ok ? "ok  " : "FAIL"} ${eng.presetName(idx).padEnd(24)} peak ${peak.toFixed(3)}  ${speed.toFixed(1)}x real time${bad ? `  ${bad} bad samples` : ""}`);
}
console.log(fails ? `FAILED (${fails})` : "PASSED");
process.exit(fails ? 1 : 0);
