# Beating in a ring-out: per partial, the dB envelope's wobble around a straight-line decay.
#   beat.py real              -> Iowa pizz open strings + arco release tails
#   beat.py wav f0 [t0 t1]    -> one file (mono or stereo), from t0 to t1 s
import sys, glob, numpy as np, soundfile as sf
sys.path.insert(0, '/mnt/project-files/research/world-class/string-physics/tools')
from seg import notes as segnotes
from metrics import refine_f0


def wobble(x, sr, f0, K=6, dur=1.6):
    """rms (dB) of each partial's envelope around its linear fit, over the part above floor+15 dB,
    and the dominant wobble period (s)."""
    n = int(0.06 * sr); hop = int(0.01 * sr); w = np.hanning(n); N = 1 << 15
    fr = np.fft.rfftfreq(N, 1 / sr)
    x = x[:int(dur * sr)]
    S = np.array([np.abs(np.fft.rfft(x[i:i + n] * w, N)) for i in range(0, len(x) - n, hop)])
    out = []
    for k in range(1, K + 1):
        b = np.argmin(np.abs(fr - k * f0))
        env = 20 * np.log10(S[:, b - 3:b + 4].max(1) + 1e-12)
        top = env[:10].max()
        floor = np.percentile(env, 5)
        idx = np.arange(5, len(env))
        idx = idx[env[idx] > max(floor + 15, top - 40)]
        if len(idx) < 30:
            out.append((np.nan, np.nan))
            continue
        idx = np.arange(idx[0], idx[-1] + 1)
        p = np.polyfit(idx, env[idx], 2)  # gentle curvature allowed (two-stage decay is not beating)
        r = env[idx] - np.polyval(p, idx)
        R = np.abs(np.fft.rfft(r * np.hanning(len(r)), 1024)); fq = np.fft.rfftfreq(1024, 0.01)
        m = fq > 0.3
        per = 1 / fq[m][np.argmax(R[m])]
        out.append((float(np.sqrt(np.mean(r ** 2))), float(per)))
    return out


def summary(ws):
    v = [a for a, _ in ws if np.isfinite(a)]
    return float(np.median(v)) if v else np.nan


if __name__ == '__main__':
    if sys.argv[1] == 'real':
        R = '/mnt/project-files/research/world-class/references/corpus/notes/iowa-2012/'
        f0s = {'G': 196.0, 'D': 293.66, 'A': 440.0, 'E': 659.26}
        allp, alla = [], []
        for s in 'GDAE':
            for dyn in ['ff', 'mf', 'pp']:
                f = glob.glob(R + f'Violin.pizz.{dyn}.sul{s}.{"GDAE"[["G","D","A","E"].index(s)]}*.flac')
                f = [q for q in f if q.split('.')[-3].startswith({'G': 'G3', 'D': 'D4', 'A': 'A4', 'E': 'E5'}[s])]
                if not f:
                    continue
                x, sr, segs = segnotes(f[0], thr_db=-60)
                if not segs:
                    continue
                a = segs[0][0]
                f0 = refine_f0(x[a + int(0.05 * sr):a + int(0.6 * sr)], sr, f0s[s])
                ws = wobble(x[a + int(0.05 * sr):], sr, f0)
                allp.append(summary(ws))
                print('pizz', s, dyn, 'wobble dB', [round(a, 2) for a, _ in ws], 'period s', [round(p, 2) for _, p in ws])
        print('REAL pizz median wobble %.2f dB' % np.nanmedian(allp))
        # arco release tails (iowa 1997 refs: notes separated by silence; the tail after the bow stops)
        R2 = '/mnt/project-files/research/world-class/string-physics/refs/iowa/'
        for f in sorted(glob.glob(R2 + 'Violin.arco.*.wav')):
            x, sr, segs = segnotes(f)
            s = f.split('.')[-3][-1]
            if not segs:
                continue
            a, b = segs[0]
            f0 = refine_f0(x[a + sr // 2:a + sr], sr, f0s[s])
            # the release: from where the level has dropped 6 dB below the sustain
            hop = int(0.005 * sr)
            e = np.array([np.sqrt(np.mean(x[i:i + hop * 4] ** 2)) for i in range(a, min(len(x), b + sr), hop)])
            db = 20 * np.log10(e + 1e-12); ss = np.median(db[len(db) // 4:len(db) // 2])
            k = len(db) // 2 + np.argmax(db[len(db) // 2:] < ss - 6)
            st = a + k * hop
            ws = wobble(x[st:], sr, f0, dur=1.5)
            alla.append(summary(ws))
            print('release', f.split('/')[-1], [round(a, 2) for a, _ in ws])
        print('REAL release median wobble %.2f dB' % np.nanmedian(alla))
    else:
        x, sr = sf.read(sys.argv[1])
        x = x.mean(1) if x.ndim > 1 else x
        f0 = float(sys.argv[2])
        t0 = float(sys.argv[3]) if len(sys.argv) > 3 else 0.0
        t1 = float(sys.argv[4]) if len(sys.argv) > 4 else t0 + 1.6
        ws = wobble(x[int(t0 * sr):int(t1 * sr)], sr, f0, dur=t1 - t0)
        print('wobble dB', [round(a, 2) for a, _ in ws], 'median %.2f' % summary(ws), 'period', [round(p, 2) for _, p in ws])
