// The browser's sound thread for the full plugin (web/full/host.c): plays what the plugin's audio thread wrote into
// the shared ring of samples, 128 frames per call, and moves the read index on (waking that thread to write more).
// It never calls into the module: it only reads its shared memory.
class MooWonRing extends AudioWorkletProcessor {
  constructor(options) {
    super();
    const o = options.processorOptions;
    this.ring = new Float32Array(o.memory, o.ring, o.frames * 2);
    this.idx = new Int32Array(o.memory, o.info, 12);   // web_info(): widx at [4], ridx at [5], underruns at [10]
    this.mask = o.frames - 1;
    this.blocks = 0;
    this.peak = 0;
  }

  process(inputs, outputs) {
    const l = outputs[0][0], r = outputs[0][1] || outputs[0][0];
    const n = l.length;
    const w = Atomics.load(this.idx, 4), rd = Atomics.load(this.idx, 5);
    let pk = this.peak * 0.97;
    if (((w - rd) | 0) >= n) {   // (int32 counters that wrap: the difference taken as int32 too)
      for (let i = 0; i < n; i++) {
        const at = ((rd + i) & this.mask) * 2;
        const a = this.ring[at], b = this.ring[at + 1];
        l[i] = a;
        r[i] = b;
        const m = a > 0 ? a : -a;
        if (m > pk) pk = m;
      }
      Atomics.store(this.idx, 5, rd + n);
    } else {
      l.fill(0);
      r.fill(0);
      Atomics.add(this.idx, 10, 1);
    }
    Atomics.notify(this.idx, 5);
    this.peak = pk;
    if (++this.blocks % 16 === 0) this.port.postMessage(pk);
    return true;
  }
}
registerProcessor("moo-won-ring", MooWonRing);
