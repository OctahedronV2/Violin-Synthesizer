# polsweep.py : run the polarisation spike over a few settings, measure wobble and decay shape
import subprocess, sys, numpy as np, soundfile as sf
sys.path.insert(0, '/tmp/m5')
sys.path.insert(0, '/mnt/project-files/research/world-class/string-physics/tools')
from beat import wobble, summary
import pizzscore as P

f0s = [196.0, 293.66, 440.0, 659.26]


def run(mode, s, dual, theta, kappa, admV, det, wv):
    out = '/tmp/m5/ps.wav'
    r = subprocess.run(['/tmp/m5/pol', mode, str(s), str(dual), str(theta), str(kappa), str(admV), str(det), str(wv), out],
                       capture_output=True, text=True)
    cpu = float(r.stderr.split()[1])
    x, sr = sf.read(out)
    f0 = f0s[s] * 2 ** (6 / 1200)
    if mode == 'pizz':
        on = P.onset_of(x, sr, 0)
        m = P.analyse(x[on:on + int(2.5 * sr)], sr, f0)
        w = summary(wobble(x[on + int(0.05 * sr):], sr, f0))
        t = [v for v in m['t60'][:6] if v]
        return w, m['t60_bb'], float(np.median(t)), cpu
    else:
        a = int(1.52 * sr)
        w = summary(wobble(x[a:], sr, f0, dur=1.5))
        return w, None, None, cpu


settings = [
    (0, 30, 0.0, 0.25, 0, 0.5),
    (1, 30, 0.0, 0.25, 1, 0.5),
    (1, 30, 0.1, 0.25, 1, 0.5),
    (1, 45, 0.1, 0.25, 2, 0.5),
    (1, 45, 0.2, 0.25, 2, 0.5),
    (1, 45, 0.1, 0.1, 2, 0.5),
    (1, 45, 0.1, 0.5, 2, 0.5),
    (1, 45, 0.1, 0.25, 4, 0.5),
    (1, 45, 0.1, 0.25, 2, 1.0),
    (1, 60, 0.2, 0.25, 3, 1.0),
]
mode = sys.argv[1] if len(sys.argv) > 1 else 'pizz'
for st in settings:
    res = [run(mode, s, *st) for s in range(4)]
    w = np.nanmedian([r[0] for r in res])
    cpu = np.mean([r[3] for r in res])
    if mode == 'pizz':
        bb = np.median([r[1] for r in res if r[1]]); pt = np.median([r[2] for r in res])
        print('dual %d theta %d kappa %.2f admV %.2f detune %.0f wv %.1f | wobble %.2f dB  bbT60 %.2f s  partialT60 %.2f s  ratio %.2f  cpu %.2f s/4s' % (
            *st, w, bb, pt, pt / bb, cpu))
    else:
        print('dual %d theta %d kappa %.2f admV %.2f detune %.0f wv %.1f | release wobble %.2f dB cpu %.2f' % (*st, w, cpu))
