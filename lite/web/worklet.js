// Hosts the Octavio Lite engine (lite.wasm, built from LiteCore.h).
// Runs inside an AudioWorklet; the page can also load it as a plain script and
// drive LiteHost from a ScriptProcessorNode where AudioWorklet is missing.

class LiteHost {
  async init(wasmBytes, ir, irRate, sampleRate) {
    const { instance } = await WebAssembly.instantiate(wasmBytes, {});
    const e = (this.e = instance.exports);
    e.__wasm_call_ctors();
    e.lite_prepare(sampleRate);
    // The body response is measured at irRate; resample it to the context's rate.
    const ratio = irRate / sampleRate;
    const n = Math.min(Math.floor(ir.length / ratio), e.lite_body_capacity());
    const buf = new Float32Array(e.memory.buffer, e.lite_body_buffer(), n);
    for (let i = 0; i < n; i++) {
      const x = i * ratio, k = Math.floor(x), f = x - k;
      buf[i] = ((ir[k] || 0) * (1 - f) + (ir[k + 1] || 0) * f) * ratio;
    }
    e.lite_body_load(n);
    this.sampleRate = sampleRate;
    this.seq = null;
    this.params = [];
    for (let i = 0; i < e.lite_param_count(); i++)
      this.params.push({
        index: i, id: this.str(e.lite_param_id(i)), group: this.str(e.lite_param_group(i)),
        label: this.str(e.lite_param_label(i)), unit: this.str(e.lite_param_unit(i)),
        min: e.lite_param_min(i), max: e.lite_param_max(i), def: e.lite_param_default(i),
      });
    return this.params;
  }
  str(ptr) {
    const m = new Uint8Array(this.e.memory.buffer);
    let s = '';
    while (m[ptr]) s += String.fromCharCode(m[ptr++]);
    return s;
  }
  handle(msg) {
    const e = this.e;
    if (!e) return;
    switch (msg.type) {
      case 'param': e.lite_set_param(msg.index, msg.value); break;
      case 'on': e.lite_note_on(msg.note, msg.vel); break;
      case 'off': e.lite_note_off(msg.note); break;
      case 'panic': this.seq = null; e.lite_all_off(); break;
      case 'play':
        e.lite_all_off();
        this.seq = { events: msg.events, loop: msg.loop, pos: 0, i: 0,
          end: Math.floor((msg.events.length ? msg.events[msg.events.length - 1].t : 0) * this.sampleRate) + Math.floor(1.5 * this.sampleRate) };
        break;
      case 'loop': if (this.seq) this.seq.loop = msg.loop; break;
      case 'stop': if (this.seq) { this.seq = null; e.lite_all_off(); } break;
    }
  }
  // Render n samples into left/right, applying sequence events on their samples.
  render(left, right, n) {
    const e = this.e;
    let done = 0;
    while (done < n) {
      let chunk = n - done;
      const s = this.seq;
      if (s) {
        while (s.i < s.events.length && Math.floor(s.events[s.i].t * this.sampleRate) <= s.pos) {
          const x = s.events[s.i++];
          if (x.type === 'on') e.lite_note_on(x.note, x.vel);
          else if (x.type === 'off') e.lite_note_off(x.note);
          else e.lite_controller(x.cc, x.value);
        }
        const next = s.i < s.events.length ? Math.floor(s.events[s.i].t * this.sampleRate) : s.end;
        chunk = Math.max(1, Math.min(chunk, next - s.pos));
      }
      e.lite_process(chunk);
      left.set(new Float32Array(e.memory.buffer, e.lite_out_left(), chunk), done);
      right.set(new Float32Array(e.memory.buffer, e.lite_out_right(), chunk), done);
      done += chunk;
      if (s) {
        s.pos += chunk;
        if (s.pos >= s.end) {
          if (s.loop) { e.lite_all_off(); s.pos = 0; s.i = 0; }
          else { this.seq = null; this.ended = true; }
        }
      }
    }
  }
}

if (typeof registerProcessor === 'function') {
  class LiteProcessor extends AudioWorkletProcessor {
    constructor() {
      super();
      this.host = new LiteHost();
      this.ready = false;
      this.port.onmessage = async (ev) => {
        const msg = ev.data;
        if (msg.type === 'init') {
          try {
            const params = await this.host.init(msg.wasm, msg.ir, msg.irRate, sampleRate);
            this.ready = true;
            this.port.postMessage({ type: 'ready', params });
          } catch (err) {
            this.port.postMessage({ type: 'error', message: String(err) });
          }
        } else this.host.handle(msg);
      };
    }
    process(inputs, outputs) {
      const out = outputs[0];
      if (!this.ready || !out || out.length < 2) return true;
      this.host.render(out[0], out[1], out[0].length);
      if (this.host.ended) { this.host.ended = false; this.port.postMessage({ type: 'ended' }); }
      return true;
    }
  }
  registerProcessor('lite', LiteProcessor);
}
