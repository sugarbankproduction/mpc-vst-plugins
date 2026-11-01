// The sound thread: runs the engine (engine.js, moowon.wasm) one 128-frame block per call, as the browser asks.
// The page sends notes, presets and settings as messages; nothing here allocates while sound plays.
import { loadEngine } from "./engine.js";

class MooWon extends AudioWorkletProcessor {
  constructor(options) {
    super();
    this.eng = loadEngine(new WebAssembly.Module(options.processorOptions.wasm));
    this.peak = 0;
    this.blocks = 0;
    const names = [];
    for (let i = 0; i < this.eng.presetCount(); i++) names.push(this.eng.presetName(i));
    this.port.onmessage = (e) => {
      const m = e.data;
      if (m.midi) this.eng.midi(m.midi[0], m.midi[1], m.midi[2]);
      else if (m.preset !== undefined) {
        for (let ch = 0; ch < 16; ch++) this.eng.midi(0xB0 | ch, 123, 0);   // all notes off first
        this.eng.preset(m.preset);
      } else if (m.set) this.eng.set(m.set[0], m.set[1]);
    };
    this.port.postMessage({ ready: true, presets: names });
  }

  process(inputs, outputs) {
    const out = outputs[0];
    const l = out[0], r = out[1] || out[0];
    const buf = this.eng.render();
    let pk = this.peak * 0.97;
    for (let i = 0; i < l.length; i++) {
      const a = buf[2 * i], b = buf[2 * i + 1];
      l[i] = a;
      r[i] = b;
      const m = Math.max(Math.abs(a), Math.abs(b));
      if (m > pk) pk = m;
    }
    this.peak = pk;
    if (++this.blocks % 8 === 0) this.port.postMessage({ peak: pk });
    return true;
  }
}

registerProcessor("moo-won", MooWon);
