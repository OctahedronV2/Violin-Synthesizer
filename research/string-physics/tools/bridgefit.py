# Calibrate the bridge admittance level and the intrinsic string loss together on the Iowa
# pizzicato partial decays. Decay rates add: 1/T60 = intrinsic(f) + s * bridge(f), where bridge(f)
# is measured with the lab (admittance at scale 1, nearly lossless string).
import sys, os, json, subprocess, numpy as np, soundfile as sf
from scipy.optimize import minimize
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from metrics import partial_decays
import importlib.util, io, contextlib
with contextlib.redirect_stdout(io.StringIO()):
    from pizzfit import model_t60
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LAB = os.environ.get('LAB', '/tmp/lab2')
modes = sys.argv[1] if len(sys.argv) > 1 else ROOT + '/results/modes_generic.txt'
real = json.load(open(ROOT + '/refs/iowa_metrics.json'))
Z = {'G': 0.350, 'D': 0.303, 'A': 0.203, 'E': 0.173}; B = {'G': 1.6e-5, 'D': 1.4e-5, 'A': 1.3e-5, 'E': 4.7e-5}
F0 = {'G': 196, 'D': 293.66, 'A': 440, 'E': 659.26}; FH = {'G': 2000, 'D': 2000, 'A': 3000, 'E': 4000}
lossless = '/tmp/lossless.txt'
open(lossless, 'w').write(''.join(f'{s} {F0[s]} {Z[s]} {B[s]} 200 100 {FH[s]}\n' for s in 'GDAE'))
bridge = {}
for s in 'GDAE':
    subprocess.run([LAB, 'pizz', s, '/tmp/bp.wav', 'strings=' + lossless, 'admittance=1', 'modes=' + modes, 'admScale=1'], check=True)
    x, sr = sf.read('/tmp/bp.wav')
    f1 = real[f'pizz{s}']['f0']
    pd = partial_decays(x, sr, F0[s], K=14)
    bridge[s] = np.array([1/t if np.isfinite(t) and t > 0 else 0 for _, _, t in pd])
def rates(s, lo, hi, sc):
    P = real[f'pizz{s}']['partials']; f1 = real[f'pizz{s}']['f0']
    fr = np.array([p[1] for p in P])
    return 1/model_t60(f1, fr, lo, hi, FH[s]) + sc*bridge[s][:len(fr)]
FIX = float(os.environ.get('FIXSCALE', 0))
def cost(x):
    sc = FIX if FIX > 0 else np.exp(x[0]); c = 0
    for i, s in enumerate('GDAE'):
        lo, hi = np.exp(x[1+2*i]), np.exp(x[2+2*i])
        if hi >= lo: return 1e9
        t = np.array([p[2] for p in real[f'pizz{s}']['partials']])
        ok = np.isfinite(t) & (t < 6) & (t > 0.03)
        e = np.log(1/rates(s, lo, hi, sc)[ok]) - np.log(t[ok])
        c += np.sum(np.sqrt(1+(e/0.3)**2)-1)
    return c
x0 = np.log([1.0] + [8, 0.5]*4)
r = minimize(cost, x0, method='Nelder-Mead', options=dict(maxiter=6000, maxfev=6000, xatol=1e-3, fatol=1e-4))
sc = FIX if FIX > 0 else np.exp(r.x[0]); print('admScale', round(sc, 3), 'cost', round(r.fun, 2), 'rigid-bridge fit cost for comparison:')
out = {'admScale': sc}
for i, s in enumerate('GDAE'):
    lo, hi = np.exp(r.x[1+2*i]), np.exp(r.x[2+2*i]); out[s] = (lo, hi)
    t = np.array([p[2] for p in real[f'pizz{s}']['partials']])
    print(s, f'intrinsic T60 {lo:.2f} s low, {hi:.3f} s at {FH[s]} Hz')
    print('   real   ', np.round(t, 2)); print('   model  ', np.round(1/rates(s, lo, hi, sc), 2)); print('   bridge ', np.round(1/np.maximum(sc*bridge[s], 1e-3), 2)[:len(t)])
json.dump(out, open(ROOT + '/results/bridge_fit.json', 'w'), indent=1)
with open(ROOT + f'/results/strings_bridge{FIX if FIX > 0 else ""}.txt', 'w') as f:
    for s in 'GDAE': f.write(f'{s} {F0[s]} {Z[s]} {B[s]} {out[s][0]:.3f} {out[s][1]:.3f} {FH[s]}\n')
