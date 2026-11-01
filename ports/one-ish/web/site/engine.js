// The engine module (moowon.wasm, from web/build.sh) wrapped for JavaScript. Used by the sound worklet (worklet.js)
// and by the offline check (web/test_node.mjs). The module asks for a few system calls (printing, closing files);
// none matter here, so each one does nothing.
export function loadEngine(module) {
  const imports = {};
  for (const imp of WebAssembly.Module.imports(module)) {
    (imports[imp.module] ??= {})[imp.name] = () => 0;
  }
  const inst = new WebAssembly.Instance(module, imports);
  const x = inst.exports;
  x._initialize?.();
  if (!x.mw_create()) throw new Error("the engine could not start");
  const mem = x.memory;
  const outPtr = x.mw_out();
  const bufPtr = x.mw_buf();
  let out = null;
  const view = () => {   // memory may have grown since (while presets load): views go stale then
    if (!out || out.buffer !== mem.buffer) out = new Float32Array(mem.buffer, outPtr, 256);
    return out;
  };
  // (no TextEncoder/TextDecoder in the sound thread: names and keys are plain ASCII, read and written byte by byte)
  const readStr = (ptr) => {
    const b = new Uint8Array(mem.buffer, ptr);
    let s = "";
    for (let e = 0; b[e]; e++) s += String.fromCharCode(b[e]);
    return s;
  };
  return {
    render() { x.mw_render(); return view(); },          // 128 frames, interleaved L R
    midi(s, d1 = 0, d2 = 0) { x.mw_midi(s, d1, d2); },
    presetCount() { return x.mw_presets(); },
    presetName(i) { return readStr(x.mw_preset_name(i)); },
    preset(i) { x.mw_preset(i); },
    set(key, value) {
      const s = `${key}\0${value}\0`;
      if (s.length > 255) return;
      const b = new Uint8Array(mem.buffer, bufPtr, s.length);
      for (let i = 0; i < s.length; i++) b[i] = s.charCodeAt(i) & 0x7F;
      x.mw_set();
    },
  };
}
