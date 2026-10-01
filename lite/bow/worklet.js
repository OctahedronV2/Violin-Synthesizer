// Runs the bare bowed string (bow.wasm) in an AudioWorklet. The page can also
// load this file as a plain script and use BowHost with a ScriptProcessorNode.
class BowHost {
  async init(wasmBytes, sampleRate) {
    const { instance } = await WebAssembly.instantiate(wasmBytes, {});
    const e = (this.e = instance.exports);
    e.__wasm_call_ctors();
    e.bs_prepare(sampleRate);
    this.frames = 0;
  }
  handle(m) {
    const e = this.e;
    if (!e) return;
    if (m.type === 'param') e.bs_set(m.index, m.value);
    else if (m.type === 'on') e.bs_note_on(m.note);
    else if (m.type === 'off') e.bs_note_off(m.note);
    else if (m.type === 'panic') e.bs_all_off();
  }
  render(left, right, n) {
    const e = this.e;
    e.bs_process(n);
    left.set(new Float32Array(e.memory.buffer, e.bs_left(), n));
    right.set(new Float32Array(e.memory.buffer, e.bs_right(), n));
    this.frames += n;
  }
  snapshot() {
    const e = this.e;
    return {
      type: 'scope',
      scope: new Float32Array(e.memory.buffer, e.bs_scope(), e.bs_scope_size()).slice(),
      slips: e.bs_slips(), irregularity: e.bs_irregularity(), bowing: e.bs_bowing(),
      period: e.bs_period(), fmax: e.bs_fmax(),
    };
  }
}

if (typeof registerProcessor === 'function') {
  class BowProcessor extends AudioWorkletProcessor {
    constructor() {
      super();
      this.host = new BowHost();
      this.ready = false;
      this.next = 0;
      this.port.onmessage = async (ev) => {
        if (ev.data.type === 'init') {
          try { await this.host.init(ev.data.wasm, sampleRate); this.ready = true; this.port.postMessage({ type: 'ready' }); }
          catch (err) { this.port.postMessage({ type: 'error', message: String(err) }); }
        } else this.host.handle(ev.data);
      };
    }
    process(inputs, outputs) {
      const out = outputs[0];
      if (!this.ready || !out || out.length < 2) return true;
      this.host.render(out[0], out[1], out[0].length);
      if (this.host.frames >= this.next) { this.next = this.host.frames + sampleRate / 30; this.port.postMessage(this.host.snapshot()); }
      return true;
    }
  }
  registerProcessor('bow', BowProcessor);
}
