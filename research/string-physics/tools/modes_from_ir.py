# Bridge-admittance mode set for the lab: frequencies and Q from a matrix-pencil fit of the
# body IR (same body modes the listener hears), masses from a generic violin admittance level.
# usage: python3 modes_from_ir.py ir.wav out.txt [order]
import sys, numpy as np, soundfile as sf
x, sr = sf.read(sys.argv[1]); order = int(sys.argv[3]) if len(sys.argv) > 3 else 120
# resample down to 24k for the low/mid modes we need (< 8 kHz)
from scipy.signal import resample_poly
y = resample_poly(x, 1, 2); fs = sr/2
n = len(y); L = n//2
H = np.array([y[i:i+L] for i in range(n-L)])
Y0, Y1 = H[:-1], H[1:]
U, s, Vt = np.linalg.svd(Y0, full_matrices=False)
U, s, Vt = U[:, :order], s[:order], Vt[:order]
A = np.diag(1/s) @ U.T @ Y1 @ Vt.T
z = np.linalg.eigvals(A)
f = np.angle(z)*fs/(2*np.pi); d = -np.log(np.abs(z))*fs  # decay rate 1/s
keep = (f > 150) & (f < 9000) & (d > 0)
f, d = f[keep], d[keep]
# amplitudes by least squares
t = np.arange(n)/fs
Vm = np.exp(np.outer(t, -d+2j*np.pi*f))
amp = np.abs(np.linalg.lstsq(np.hstack([Vm, Vm.conj()]), y.astype(complex), rcond=None)[0][:len(f)])
Q = np.pi*f/d
o = np.argsort(f)
rows = [(f[i], Q[i], amp[i]) for i in o if 3 < Q[i] < 300]
# keep the strongest per 1/12-octave region
rows.sort(key=lambda r: -r[2]); kept = []
for r in rows:
    if all(abs(np.log2(r[0]/k[0])) > 1/24 for k in kept): kept.append(r)
kept.sort()
print(len(kept), 'modes')
amax = max(r[2] for r in kept)
with open(sys.argv[2], 'w') as fo:
    for fr, q, a in kept:
        # peak admittance Y_k = Q/(m w): scale so the strongest IR mode has Ypeak = 0.1 s/kg (x admScale)
        ypk = 0.1*(a/amax)
        m = q/(2*np.pi*fr*ypk)
        fo.write(f'{fr:.1f} {q:.1f} {m:.5f}\n')
        print(f'{fr:7.1f} Hz  Q {q:5.1f}  Ypeak {ypk:.4f}')
