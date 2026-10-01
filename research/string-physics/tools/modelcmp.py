# Render open-string notes with the lab and score them against the Iowa recordings.
# usage: python3 modelcmp.py label [lab options ...]
import sys, json, subprocess, os, numpy as np, soundfile as sf
from scipy.signal import fftconvolve
sys.path.insert(0, os.path.dirname(__file__))
from metrics import *
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.dirname(HERE)
LAB = os.environ.get('LAB', '/tmp/lab')
BODY = os.environ.get('BODY', '/home/user/Violin-Synthesizer/resources/bodies/iowa.wav')
real = json.load(open(ROOT + '/refs/iowa_metrics.json'))
body, _ = sf.read(BODY)
# bow settings per dynamic (force N, speed m/s, beta), after Schoonderwaldt's measured ranges
DYN = {'pp': (0.15, 0.08, 0.13), 'mf': (0.5, 0.2, 0.10), 'ff': (1.2, 0.4, 0.07)}
MIDI = {'G': 55, 'D': 62, 'A': 69, 'E': 76}
def run(label, extra, strings='GDAE', dyns=('pp', 'mf', 'ff'), dur=3.0, save=None):
    rows = {}
    for s in strings:
        for d in dyns:
            F, v, b = DYN[d]
            w = f'/tmp/mc_{label}_{s}{d}.wav'
            r = subprocess.run([LAB, 'note', s, str(MIDI[s]), str(F), str(v), str(b), str(dur), w, 'ring=2'] + extra, capture_output=True, text=True).stdout
            x, sr = sf.read(w)
            y = fftconvolve(x, body)[:len(x)]
            if save: sf.write(f'{save}/{s}{d}.wav', y/np.abs(y).max()*0.5, sr)
            f0 = real[f'{s}{d}']['f0'] * 2**(0)  # analysis guess
            f0 = refine_f0(y[int(sr*1):int(sr*2)], sr, 440*2**((MIDI[s]-69)/12))
            mid = y[int(sr*0.9):int(sr*dur*0.95)]
            hdb, noise, _ = harmonics(mid, sr, f0)
            sh, ji, dr = period_stats(mid, sr, f0)
            lvl, spec = attack(y[:int(sr*1.5)], sr, f0, hdb)
            # release: level decay after the bow lifts
            hop = int(0.005*sr); e = np.array([np.sqrt(np.mean(y[i:i+hop*4]**2)) for i in range(int(sr*(dur-0.5)), len(y)-hop*4, hop)])
            db = 20*np.log10(e+1e-12); ss = np.median(db[:80]); k = np.argmax(db < ss-6); j = k+np.argmax(db[k:] < ss-30)
            rt = -60/np.polyfit(np.arange(k, j)*0.005, db[k:j], 1)[0] if j-k > 3 else np.nan
            R = real[f'{s}{d}']
            rh = np.array([np.nan if v is None else v for v in R['h']])
            # only below 5 kHz: the Iowa body IR is unreliable above ~6 kHz
            fk = f0*np.arange(1, len(hdb)+1); K = 20
            sel = (fk[:K] < 5000)
            diff = (hdb[:K]-rh[:K])[sel]; herr = np.sqrt(np.nanmean(diff**2))
            lo = np.nanmean(diff[1:7]); hsel = (fk[:K] > 2500) & (fk[:K] < 5000)
            hi = np.nanmean((hdb[:K]-rh[:K])[hsel])
            rows[f'{s}{d}'] = dict(regime=r.strip(), herr=herr, lo=lo, hi=hi, noise=noise, rnoise=R['noise'], shim=sh, rshim=R['shimmer'], jit=ji, rjit=R['jitter'], rel=rt, rrel=R['release_t60'], cent=centroid(hdb, f0), rcent=R['centroid'])
    return rows
def summary(rows):
    a = lambda k: np.nanmean([r[k] for r in rows.values()])
    return dict(herr=a('herr'), lo=a('lo'), hi=a('hi'), dnoise=a('noise')-a('rnoise'), shim=a('shim'), rshim=a('rshim'), jit=a('jit'), rjit=a('rjit'), rel=a('rel'), rrel=a('rrel'),
                helm=sum('regime 1 ' in r['regime'] for r in rows.values()), n=len(rows))
if __name__ == '__main__':
    label = sys.argv[1]; extra = sys.argv[2:]
    rows = run(label, extra)
    for k, r in rows.items():
        print(f"{k:4s} {r['regime'][:28]:28s} herr {r['herr']:5.1f} lo {r['lo']:+5.1f} hi {r['hi']:+5.1f} noise {r['noise']:6.1f}/{r['rnoise']:6.1f} shim {r['shim']:.3f}/{r['rshim']:.3f} jit {r['jit']:.2f}/{r['rjit']:.2f} rel {r['rel']:.2f}/{r['rrel']:.2f} cent {r['cent']:5.0f}/{r['rcent']:5.0f}")
    s = summary(rows); print('SUMMARY', label, json.dumps({k: round(float(v), 3) for k, v in s.items()}))
    os.makedirs(ROOT + '/results', exist_ok=True)
    json.dump(dict(label=label, opts=extra, rows=rows, summary=s), open(ROOT + f'/results/cmp_{label}.json', 'w'), indent=1, default=float)
