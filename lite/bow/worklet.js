// Runs the bare bowed string (bow.wasm) in an AudioWorklet. The page can also
// load this file as a plain script and use BowHost with a ScriptProcessorNode.
class BowHost {
  async init(wasmBytes, ir, irRate, sampleRate) {
    const { instance } = await WebAssembly.instantiate(wasmBytes, {});
    const e = (this.e = instance.exports);
    e.__wasm_call_ctors();
    e.bs_prepare(sampleRate);
    // The body response is measured at irRate; resample it to the context's rate.
    const ratio = irRate / sampleRate;
    const n = Math.min(Math.floor(ir.length / ratio), e.bs_body_capacity());
    const buf = new Float32Array(e.memory.buffer, e.bs_body_buffer(), n);
    for (let i = 0; i < n; i++) {
      const x = i * ratio, k = Math.floor(x), f = x - k;
      buf[i] = ((ir[k] || 0) * (1 - f) + (ir[k + 1] || 0) * f) * ratio;
    }
    e.bs_body_load(n);
    this.bodyGain = e.bs_body_gain(); // reshaped bodies keep the real body's gain
    this.frames = 0;
  }
  handle(m) {
    const e = this.e;
    if (!e) return;
    if (m.type === 'param') e.bs_set(m.index, m.value);
    else if (m.type === 'on') e.bs_note_on(m.note);
    else if (m.type === 'off') e.bs_note_off(m.note);
    else if (m.type === 'panic') e.bs_all_off();
    else if (m.type === 'ir') {
      // A reshaped body response, already at the context's rate.
      const n = Math.min(m.ir.length, e.bs_body_capacity());
      new Float32Array(e.memory.buffer, e.bs_body_buffer(), n).set(m.ir.subarray(0, n));
      e.bs_body_load(n);
      e.bs_set_body_gain(this.bodyGain);
    }
  }
  render(left, right, n) {
    const e = this.e;
    e.bs_process(n);
    left.set(new Float32Array(e.memory.buffer, e.bs_left(), n));
    right.set(new Float32Array(e.memory.buffer, e.bs_right(), n));
    // Soft limit above 0.8, since an exaggerated body can push some notes very loud.
    for (const ch of [left, right])
      for (let i = 0; i < n; i++) {
        const a = Math.abs(ch[i]);
        if (a > 0.8) ch[i] = Math.sign(ch[i]) * (0.8 + 0.2 * Math.tanh((a - 0.8) / 0.2));
      }
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
          try { await this.host.init(ev.data.wasm, ev.data.ir, ev.data.irRate, sampleRate); this.ready = true; this.port.postMessage({ type: 'ready' }); }
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
