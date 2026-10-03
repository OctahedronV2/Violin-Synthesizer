import sys, json, numpy as np
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from seg import notes
from metrics import *
R = __file__.rsplit('/', 2)[0] + '/refs/iowa/'
open_f = {'G': 196.0, 'D': 293.66, 'A': 440.0, 'E': 659.26}
def release_t60(x, sr, a, b):
    hop = int(0.005*sr)
    e = np.array([np.sqrt(np.mean(x[i:i+hop*4]**2)) for i in range(a, min(len(x), b+sr), hop)])
    db = 20*np.log10(e+1e-12); ss = np.median(db[len(db)//4:len(db)//2])
    k = len(db)//2 + np.argmax(db[len(db)//2:] < ss-6)
    j = k + np.argmax(db[k:] < ss-30)
    if j-k < 3: return np.nan
    sl = np.polyfit(np.arange(k, j)*0.005, db[k:j], 1)[0]
    return -60/sl
out = {}
for dyn in ['pp', 'mf', 'ff']:
    for s in 'GDAE':
        import glob
        f = glob.glob(R+f'Violin.arco.{dyn}.sul{s}.*.wav')[0]
        x, sr, segs = notes(f)
        a, b = next((a, b) for a, b in segs if (b-a)/sr > 2.0)
        f0 = refine_f0(x[a+sr:a+2*sr], sr, open_f[s])
        mid = x[a+int(0.3*(b-a)): a+int(0.7*(b-a))]
        hdb, noise, _ = harmonics(mid, sr, f0)
        sh, ji, dr = period_stats(mid, sr, f0)
        lvl, spec = attack(x[a-int(0.2*sr):a+int(1.5*sr)], sr, f0, hdb)
        rt = release_t60(x, sr, a, b)
        out[f'{s}{dyn}'] = dict(f0=f0, h=[None if np.isnan(v) else round(float(v),1) for v in hdb], noise=noise, centroid=centroid(hdb, f0), shimmer=sh, jitter=ji, drift=dr, attack_level_ms=lvl, attack_spec_ms=spec, release_t60=rt)
        print(f'{s} {dyn} f0 {f0:7.2f} noise {noise:6.1f} cent {centroid(hdb,f0):6.0f} shim {sh:.3f} dB jit {ji:.2f} c drift {dr:.1f} c att {lvl} / {spec} ms rel {rt:.2f}s  h2-8:', np.round(hdb[1:8]).astype(int))
for s in 'GDAE':
    import glob
    f = glob.glob(R+f'Violin.pizz.ff.sul{s}.*.wav')[0]
    x, sr, segs = notes(f, thr_db=-60)
    a, b = segs[0]
    f0 = refine_f0(x[a+int(0.05*sr):a+int(0.6*sr)], sr, open_f[s])
    seg = x[a:a+int(2.5*sr)]
    # inharmonicity: fit f_n = n f0 sqrt(1+B n^2) from measured partials
    pd = partial_decays(seg, sr, f0, K=14)
    ns = np.array([p[0] for p in pd]); fs = np.array([p[1] for p in pd])
    r = (fs/(ns*f0))**2 - 1
    good = ns >= 2
    B = np.polyfit(ns[good]**2, r[good], 1)[0]
    out[f'pizz{s}'] = dict(f0=f0, partials=[(int(k), float(fm), float(t)) for k, fm, t in pd], B=B)
    print(f'pizz {s} f0 {f0:.2f} B {B:.2e}  T60:', [round(t, 2) for _, _, t in pd])
json.dump(out, open(__file__.rsplit('/', 2)[0] + '/refs/iowa_metrics.json', 'w'), indent=1, default=float)
