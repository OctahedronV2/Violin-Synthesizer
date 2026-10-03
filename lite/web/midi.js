// Tiny Standard MIDI File reader: returns time-ordered events in seconds.
// [{t, type: 'on'|'off'|'cc', note|cc, vel|value (0..1)}]
function parseMidi(bytes) {
  const d = new Uint8Array(bytes);
  let p = 0;
  const u32 = () => { const v = (d[p] << 24 | d[p + 1] << 16 | d[p + 2] << 8 | d[p + 3]) >>> 0; p += 4; return v; };
  const u16 = () => { const v = d[p] << 8 | d[p + 1]; p += 2; return v; };
  const vlq = () => { let v = 0, c; do { c = d[p++]; v = v * 128 + (c & 127); } while (c & 128); return v; };
  p = 4; const hlen = u32(); u16(); const ntracks = u16(); const division = u16(); p = 8 + hlen;
  const raw = []; let order = 0;
  for (let t = 0; t < ntracks && p + 8 <= d.length; t++) {
    p += 4; const len = u32(), end = p + len; let tick = 0, running = 0;
    while (p < end) {
      tick += vlq();
      let st = d[p]; if (st & 128) p++; else st = running;
      if (st === 0xff) { const type = d[p++], l = vlq(); if (type === 0x51 && l === 3) raw.push({ tick, order: order++, tempo: d[p] << 16 | d[p + 1] << 8 | d[p + 2] }); p += l; }
      else if (st === 0xf0 || st === 0xf7) { p += vlq(); }
      else { running = st; const hi = st & 0xf0, n = (hi === 0xc0 || hi === 0xd0) ? 1 : 2; raw.push({ tick, order: order++, st, a: d[p], b: n === 2 ? d[p + 1] : 0 }); p += n; }
    }
    p = end;
  }
  raw.sort((x, y) => x.tick - y.tick || x.order - y.order);
  let spt = 0.5 / division, sec = 0, last = 0; const out = [];
  for (const r of raw) {
    sec += (r.tick - last) * spt; last = r.tick;
    if (r.tempo !== undefined) { spt = r.tempo / 1e6 / division; continue; }
    const hi = r.st & 0xf0;
    if (hi === 0x90 && r.b > 0) out.push({ t: sec, type: 'on', note: r.a, vel: r.b / 127 });
    else if (hi === 0x80 || hi === 0x90) out.push({ t: sec, type: 'off', note: r.a });
    else if (hi === 0xb0) out.push({ t: sec, type: 'cc', cc: r.a, value: r.b / 127 });
  }
  const rank = { off: 0, on: 1, cc: 2 };
  out.sort((x, y) => x.t - y.t || rank[x.type] - rank[y.type]);
  return out;
}
if (typeof module !== 'undefined') module.exports = { parseMidi };
