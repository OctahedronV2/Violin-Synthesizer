# M5 (2026-10-04) pizzicato scorer. Scratch dir /tmp/m5 (refs.json, rendered MIDI); Iowa/Philharmonia from research/world-class.
# M5 pizzicato scorer: real plucked notes (Iowa MIS 2012, Philharmonia) vs Octavio renders.
#   python3 pizzscore.py refs            -> /tmp/m5/refs.json (real note list + metrics) and /tmp/m5/pizz_notes.mid
#   python3 pizzscore.py score out.wav [label]  -> metrics of the render (notes as in pizz_notes.mid), comparison
import sys, os, re, json, glob
import numpy as np, soundfile as sf
sys.path.insert(0, '/mnt/project-files/research/world-class/string-physics/tools')
from metrics import partial_decays, refine_f0
from seg import notes as segnotes

COR = '/mnt/project-files/research/world-class/references/corpus/'
NAMES = {'C': 0, 'Db': 1, 'Cs': 1, 'D': 2, 'Eb': 3, 'Ds': 3, 'E': 4, 'F': 5, 'Gb': 6, 'Fs': 6, 'G': 7, 'Ab': 8,
         'Gs': 8, 'A': 9, 'Bb': 10, 'As': 10, 'B': 11}
DYNV = {'pp': 45, 'mf': 85, 'ff': 120, 'pianissimo': 40, 'piano': 55, 'mezzo-piano': 70, 'mezzo-forte': 85,
        'forte': 105, 'fortissimo': 120}
SPACING = 3.5
DUR = 2.5


def midi_of(name):
    m = re.match(r'([A-G](?:b|s)?)(\d)', name)
    return 12 * (int(m.group(2)) + 1) + NAMES[m.group(1)]


def hz(p):
    return 440.0 * 2 ** ((p - 69) / 12)


def onset_of(x, sr, a=0):
    """first sample where the 1 ms envelope reaches -30 dB of the note's peak (searching from a)."""
    hop = int(0.001 * sr)
    seg = x[a:a + int(0.6 * sr)]
    e = np.array([np.max(np.abs(seg[i:i + hop])) for i in range(0, len(seg) - hop, hop)])
    pk = e.max()
    i = np.argmax(e > pk * 10 ** (-30 / 20))
    return a + i * hop


def analyse(x, sr, f0g, B=1.5e-5):
    """x starts at the onset; returns metrics of one plucked note."""
    x = np.asarray(x, float)
    out = {}
    hop = int(0.0005 * sr)
    env = np.array([np.max(np.abs(x[i:i + hop])) for i in range(0, int(0.15 * sr), hop)])
    out['attack_ms'] = float(np.argmax(env) * 0.5)
    try:
        f0 = refine_f0(x[int(0.05 * sr):int(0.8 * sr)], sr, f0g)
    except Exception:
        f0 = f0g
    out['f0'] = float(f0)
    pd = partial_decays(x[:int(2.4 * sr)], sr, f0, K=10, B=B)
    out['t60'] = [float(t) if np.isfinite(t) else None for _, _, t in pd]

    def cent(a, b):
        s = x[int(a * sr):int(b * sr)]
        s = s * np.hanning(len(s))
        X = np.abs(np.fft.rfft(s, 1 << 16)) ** 2
        fr = np.fft.rfftfreq(1 << 16, 1 / sr)
        m = (fr > 80) & (fr < 12000)
        return float((fr[m] * X[m]).sum() / X[m].sum())

    out['cent_early'] = cent(0.0, 0.06)
    out['cent_late'] = cent(0.3, 0.8)
    # broadband decay: -6 dB to -26 dB of the 10 ms rms envelope after the peak (x3 = T60)
    h2 = int(0.01 * sr)
    e = np.array([np.sqrt(np.mean(x[i:i + h2] ** 2)) for i in range(0, min(len(x), int(2.4 * sr)) - h2, h2)])
    db = 20 * np.log10(e / e.max() + 1e-12)
    pk = int(np.argmax(db))
    i6 = pk + np.argmax(db[pk:] < -6)
    i26 = pk + np.argmax(db[pk:] < -26)
    out['t60_bb'] = float(3 * (i26 - i6) * 0.01) if i26 > i6 else None
    # harmonic levels h1..h8 in the first 100 ms (dB re strongest)
    s = x[:int(0.1 * sr)] * np.hanning(int(0.1 * sr))
    X = np.abs(np.fft.rfft(s, 1 << 16))
    fr = np.fft.rfftfreq(1 << 16, 1 / sr)
    h = []
    for k in range(1, 9):
        m = (fr > k * f0 * 0.97) & (fr < k * f0 * 1.03)
        h.append(X[m].max() if m.any() else 1e-12)
    h = 20 * np.log10(np.array(h) / max(h))
    out['h'] = [float(v) for v in h]
    return out


def real_notes():
    items = []
    for f in sorted(glob.glob(COR + 'notes/iowa-2012/Violin.pizz.*.flac')):
        b = os.path.basename(f).split('.')
        dyn, rng = b[2], b[4]
        items.append(dict(src='iowa', file=f, dyn=dyn, pitch=midi_of(rng), string=b[3][-1]))
    for f in sorted(glob.glob(COR + 'philharmonia/violin_*_025_*_pizz-normal.mp3') +
                    glob.glob(COR + 'philharmonia/violin_*_025_*_snap-pizz.mp3')):
        b = os.path.basename(f)[:-4].split('_')
        items.append(dict(src='phil-snap' if 'snap' in b[4] else 'phil', file=f, dyn=b[3], pitch=midi_of(b[1])))
    return items


def refs():
    import mido
    res = []
    for it in real_notes():
        if it['src'] == 'iowa':
            x, sr, segs = segnotes(it['file'], thr_db=-60)
            a = segs[0][0] if segs else 0
        else:
            x, sr = sf.read(it['file'])
            if x.ndim > 1:
                x = x.mean(1)
            a = 0
        on = onset_of(x, sr, max(0, a - int(0.05 * sr)))
        seg = x[on:on + int(2.5 * sr)]
        if len(seg) < int(0.85 * sr):
            continue
        pass
        try:
            m = analyse(seg, sr, hz(it['pitch']))
        except Exception as ex:
            print('skip', it['file'], ex)
            continue
        if m["attack_ms"] > 40:
            continue
        it.update(m)
        res.append(it)
        print(it['src'], it['dyn'], it['pitch'], 'att %.1f ms  cent %.0f/%.0f  t60bb %s  t60 %s' % (
            m['attack_ms'], m['cent_early'], m['cent_late'], m['t60_bb'], [None if t is None else round(t, 2) for t in m['t60'][:6]]))
    json.dump(res, open('/tmp/m5/refs.json', 'w'), indent=0)
    # the same notes for the renderer (snap notes in their own file)
    for name, srcs in (('pizz_notes', ('iowa', 'phil')), ('snap_notes', ('phil-snap',))):
        mf = mido.MidiFile(ticks_per_beat=480)
        tr = mido.MidiTrack()
        mf.tracks.append(tr)
        tr.append(mido.MetaMessage('set_tempo', tempo=500000))
        tpb = 960  # ticks per second at 120 bpm
        t = 0
        evs = []
        k = 0
        for it in res:
            if it['src'] not in srcs:
                continue
            t0 = 0.5 + k * SPACING
            evs.append((t0, 'on', it['pitch'], DYNV[it['dyn']]))
            evs.append((t0 + DUR, 'off', it['pitch'], 0))
            k += 1
        evs.sort(key=lambda e: (e[0], e[1] == 'on'))
        last = 0
        for tt, kind, p, v in evs:
            dt = int(round((tt - last) * tpb))
            last += dt / tpb
            tr.append(mido.Message('note_on' if kind == 'on' else 'note_off', note=p, velocity=v, time=dt))
        mf.save('/tmp/m5/%s.mid' % name)


def lr(a, b):
    return abs(np.log(a / b))


def score(wav, label='', srcs=('iowa', 'phil')):
    x, sr = sf.read(wav)
    if x.ndim > 1:
        x = x.mean(1)
    res = [r for r in json.load(open('/tmp/m5/refs.json')) if r['src'] in srcs]
    rows = []
    for k, it in enumerate(res):
        t0 = 0.5 + k * SPACING
        a = int((t0 - 0.01) * sr)
        on = onset_of(x, sr, a)
        seg = x[on:on + int(2.5 * sr)]
        m = analyse(seg, sr, hz(it['pitch']))
        # partial decays: median |log ratio| over partials 1..8 measured in both
        r = [lr(p, q) for p, q in zip(m['t60'][:8], it['t60'][:8]) if p and q and 0.03 < q < 8 and 0.03 < p < 8]
        sign = [np.log(p / q) for p, q in zip(m['t60'][:8], it['t60'][:8]) if p and q and 0.03 < q < 8 and 0.03 < p < 8]
        hd = np.sqrt(np.mean((np.array(m['h'][:8]) - np.array(it['h'][:8])) ** 2))
        rows.append(dict(src=it['src'], dyn=it['dyn'], pitch=it['pitch'],
                         t60_err=float(np.median(r)) if r else np.nan,
                         t60_bias=float(np.median(sign)) if sign else np.nan,
                         ce=np.log2(m['cent_early'] / it['cent_early']),
                         cl=np.log2(m['cent_late'] / it['cent_late']),
                         att=m['attack_ms'] - it['attack_ms'], att_ours=m['attack_ms'], att_real=it['attack_ms'],
                         bb=np.log(m['t60_bb'] / it['t60_bb']) if m['t60_bb'] and it['t60_bb'] else np.nan,
                         hd=hd, cent_ours=m['cent_early'], cent_real=it['cent_early']))
    summary = {}
    for grp in sorted(set(r['src'] for r in rows)) + ['all']:
        R = [r for r in rows if grp == 'all' or r['src'] == grp]
        f = lambda key, fn=np.nanmedian: float(fn([r[key] for r in R]))
        summary[grp] = dict(n=len(R),
                            partialT60_err=f('t60_err'), partialT60_bias=f('t60_bias'),
                            centEarly_oct=f('ce'), centEarly_absoct=float(np.nanmedian([abs(r['ce']) for r in R])),
                            centLate_oct=f('cl'), centLate_absoct=float(np.nanmedian([abs(r['cl']) for r in R])),
                            attack_ms_ours=f('att_ours'), attack_ms_real=f('att_real'),
                            bbT60_logratio=f('bb'), harm_rms_db=f('hd'))
    for g, s in summary.items():
        print(label, g, ' '.join('%s %.3g' % (k, v) for k, v in s.items()))
    json.dump(dict(summary=summary, rows=rows), open(wav[:-4] + '.score.json', 'w'), indent=0, default=float)
    return summary


if __name__ == '__main__':
    if sys.argv[1] == 'refs':
        refs()
    else:
        srcs = ('phil-snap',) if 'snap' in sys.argv[2] else ('iowa', 'phil')
        score(sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else '', srcs)
