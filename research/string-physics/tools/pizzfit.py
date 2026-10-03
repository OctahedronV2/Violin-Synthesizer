# Fit the per-string loss filter (one-pole: T60 low, T60 at fhi) to Iowa pizzicato partial decays.
import json, os, numpy as np
from scipy.optimize import minimize
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
real = json.load(open(ROOT + '/refs/iowa_metrics.json'))
fs = 96000.0
def model_t60(f1, freqs, lo, hi, fh):
    g = 10**(-3/(lo*f1))
    r = 10**(-3/f1*(1/hi-1/lo)); cw = np.cos(2*np.pi*fh/fs); A = 1-r*r; B = 1-r*r*cw
    d = (B-np.sqrt(max(B*B-A*A, 0)))/A
    w = 2*np.pi*np.asarray(freqs)/fs
    H = g*(1-d)/np.abs(1-d*np.exp(-1j*w))
    return 60/(-20*np.log10(H)*f1)
out = {}
for s, fh in zip('GDAE', [2000, 2000, 3000, 4000]):
    P = real[f'pizz{s}']['partials']; f1 = real[f'pizz{s}']['f0']
    fr = np.array([p[1] for p in P]); t = np.array([p[2] for p in P])
    ok = np.isfinite(t) & (t < 6) & (t > 0.03)
    # log-domain fit with a robust (soft-L1) loss so body-mode outliers matter less
    def cost(x):
        lo, hi = np.exp(x)
        if hi >= lo: return 1e9
        e = np.log(model_t60(f1, fr[ok], lo, hi, fh)) - np.log(t[ok])
        return np.sum(np.sqrt(1+(e/0.3)**2)-1)
    r = minimize(cost, np.log([3, 0.3]), method='Nelder-Mead')
    lo, hi = np.exp(r.x)
    out[s] = dict(lo=lo, hi=hi, fh=fh)
    print(s, f'f1 {f1:.1f}  t60 low {lo:.2f} s, at {fh} Hz {hi:.3f} s')
    print('   real :', np.round(t, 2))
    print('   model:', np.round(model_t60(f1, fr, lo, hi, fh), 2))
json.dump(out, open(ROOT + '/results/pizz_lossfit.json', 'w'), indent=1)
