"""Does the render play the same music as the real recording? (The scorer only compares
statistics of notes, so a render with the wrong notes or phrasing can still score well.)

    python3 compare_real.py real.flac render.wav [render2.wav ...] [--upto s]

Per 4 s and overall:
  notes  share of the real player's voiced frames where the render sounds the same pitch (±0.5 st)
  level  correlation of the two level envelopes (dB, 20 ms, where the real player sounds):
         the shape of the phrasing, accents and note separations
  sep    level dip between strokes, median, real vs render (dB under the surrounding peaks)
"""
import sys
import numpy as np
from scipy.ndimage import median_filter

sys.path.insert(0, '/mnt/project-files/research/world-class/references/tool')
import violinscore as vs


def track(path, upto):
    x = vs.load(path)
    if upto:
        x = x[:int(upto * vs.FS)]
    f0, ap, rms = vs.yin(x)
    return f0, median_filter(rms, size=4)


def dips(rms, voiced):
    W = int(0.15 * vs.FR)
    out = []
    for i in range(W, len(rms) - W, 2):
        if rms[i] == rms[i - 4:i + 5].min():
            d = min(rms[i - W:i].max(), rms[i + 1:i + W + 1].max()) - rms[i]
            if d > 3 and voiced[i - W:i + W].mean() > 0.5:
                out.append(d)
    return np.median(out) if out else 0.0


def main():
    upto = float(sys.argv[sys.argv.index('--upto') + 1]) if '--upto' in sys.argv else None
    files = [a for a in sys.argv[1:] if not a.startswith('--') and a != str(upto) and not a.replace('.', '').isdigit()]
    real, renders = files[0], files[1:]
    f0r, rr = track(real, upto)
    v = f0r > 0
    a4 = 440 * 2 ** (np.median(vs.midi_of(f0r[v]) - np.round(vs.midi_of(f0r[v]))) / 12)
    mr = vs.midi_of(np.maximum(f0r, 1), a4)
    print('real sep %.1f dB' % dips(rr, v))
    for path in renders:
        f0o, ro = track(path, upto)
        n = min(len(f0r), len(f0o))
        mo = vs.midi_of(np.maximum(f0o[:n], 1), 440.0)
        good = v[:n] & (f0o[:n] > 0) & (np.abs(mo - mr[:n]) < 0.5)
        rows = []
        for w in range(0, int(n / vs.FR), 4):
            s = slice(int(w * vs.FR), int((w + 4) * vs.FR))
            vv = v[:n][s]
            nt = 100 * good[s].sum() / max(1, vv.sum())
            c = np.corrcoef(rr[:n][s][vv], ro[:n][s][vv])[0, 1] if vv.sum() > 20 else np.nan
            rows.append('%3d-%3ds notes %3.0f%% level r %.2f' % (w, w + 4, nt, c))
        c = np.corrcoef(rr[:n][v[:n]], ro[:n][v[:n]])[0, 1]
        print('%s: notes %.0f%%  level r %.2f  sep %.1f dB' % (path.split('/')[-1], 100 * good.sum() / v[:n].sum(), c, dips(ro[:n], v[:n])))
        if '--detail' in sys.argv:
            print('\n'.join('    ' + r for r in rows))


if __name__ == '__main__':
    main()
