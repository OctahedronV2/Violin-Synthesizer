"""Fits Octavio 2's passive modal bridge (M3) and writes octavio2/core/BridgeData.h.

    python3 research/scripts/fetch_cnsm.py          # CNSM admittances (CC BY 4.0), once
    python3 octavio2/data/bridge/fit_bridge.py [path/to/cnsm]

1. Modes: Y(s) = sum_k (1/m_k) s / (s^2 + s w_k/Q_k + w_k^2) with m_k, Q_k > 0 (positive
   residues: Re Y >= 0 everywhere, so the bridge is passive whatever is done to it), fitted in
   log |Y| to the mean of all six CNSM Stoppani bridge-admittance measurements (phases 1 and 2,
   1/48-octave smoothed, 180 Hz - 9 kHz). The raw CNSM phase is not used (only half its bins
   have Re Y >= 0: body-radiation FINDINGS F7). 80 modes start on the 80 most prominent peaks;
   modes that end up contributing nothing (peak |Y| < 5e-4 s/kg) are dropped. Result: 76 modes,
   0.3 dB rms. Writes modes_cnsm_stoppani.txt (f Q m).
2. Modal body: for each Violin choice's measured body (octavio2/data/bodies), weights a_k, b_k
   so that d/dt(sum a_k v_k) + sum b_k w_k v_k matches the body after its bulk delay, 150 Hz -
   9 kHz weighted by the 1.5 kHz Linkwitz-Riley low-pass it plays through (v_k: mode k's
   velocity at admScale 0.5, Hold 0). 1/6-octave error below 1.5 kHz: Stoppani 0.8 dB, Klimke
   1.2, Levaggi 1.3, Iowa 2.1 (a different violin's poles).

The string loss was then refitted against the Iowa pizzicato partial decays with this bridge
(string-physics tools/bridgefit.py, the lab's pizz replaced by the same pluck on o2::Violin, Hold
0.5): admScale 0.35 fits best (cost 43.8), 0.5 costs 46.1, 0.7 50.6, 1.0 54.7; the M0 generic
set at 0.5 costs 41.4 (it was fitted from the Iowa violin's own body IR, so it knows that
violin's resonances). Shipped: 0.5 and its losses (Violin::defaults). Measured afterwards on the
full model (plucks scored as bridgefit does): cost 48.6 against the M0 bridge's 40.1; the
largest misses are partials sitting on this violin's B1-/B1+ modes, e.g. the open A's
fundamental (440 Hz, next to B1- at 452 Hz) rings 0.8 s against Iowa's 1.6 s.
"""
import json, os, sys, glob
import numpy as np, soundfile as sf
from scipy.optimize import least_squares
from scipy.signal import find_peaks

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '../../..'))
CNSM = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'research/external/cnsm')
K, FLO, FHI, SCALE, XO = 80, 180.0, 9000.0, 0.5, 1500.0


def load(violin, phase):
    tot, n = None, 0
    grid = np.arange(0, 20000, 1.5625)
    for p in sorted(glob.glob(f'{CNSM}/admittances/Phase {phase}/{violin}/measurement_*.csv')):
        d = np.loadtxt(p, delimiter=',', skiprows=1)
        y = np.interp(grid, d[:, 0], d[:, 1]) + 1j * np.interp(grid, d[:, 0], d[:, 2])
        tot = y if tot is None else tot + y
        n += 1
    return grid, tot, n


def fit_modes(violin='Stoppani'):
    parts = [load(violin, ph) for ph in (1, 2)]
    f = parts[0][0]
    y = sum(p[1] for p in parts) / sum(p[2] for p in parts)
    P = np.abs(y) ** 2
    c = np.cumsum(P)
    lo = np.searchsorted(f, f * 2 ** (-1 / 96)); hi = np.minimum(np.maximum(np.searchsorted(f, f * 2 ** (1 / 96)), lo + 1), len(c) - 1)
    msm = np.sqrt((c[hi] - c[lo]) / (hi - lo))
    sel = (f >= FLO) & (f <= FHI)
    fg, target = f[sel][::2], msm[sel][::2]
    w = 2 * np.pi * fg
    s = 1j * w[None, :]

    def parts_of(th):
        wk = 2 * np.pi * np.exp(th[:K])[:, None]; Q = np.exp(th[K:2 * K])[:, None]; m = np.exp(th[2 * K:])[:, None]
        D = s * s + s * wk / Q + wk * wk
        return wk, Q, m, D, s / (m * D)

    pk, pr = find_peaks(20 * np.log10(target), prominence=0.5)
    pk = np.sort(pk[np.argsort(-pr['prominences'])[:K]])
    f0 = list(fg[pk])
    while len(f0) < K:
        f0.append(np.exp(np.random.default_rng(len(f0)).uniform(np.log(FLO), np.log(FHI))))
    f0 = np.sort(np.maximum(np.array(f0), 212.0))
    Q0 = np.clip(f0 / 40.0, 10, 40)
    m0 = Q0 / (np.interp(f0, fg, target) * 2 * np.pi * f0) * 2.0
    wt = (fg / 1000.0) ** -0.25
    lb = np.concatenate([np.log(np.maximum(f0 * 0.93, 205.0)), np.full(K, np.log(4.0)), np.full(K, np.log(1e-4))])
    ub = np.concatenate([np.log(f0 * 1.07), np.log(np.where(f0 < 260, 25.0, 80.0)), np.full(K, np.log(1e3))])
    th0 = np.clip(np.concatenate([np.log(f0), np.log(Q0), np.log(m0)]), lb + 1e-9, ub - 1e-9)

    def res(th):
        return wt * (np.log(np.abs(parts_of(th)[4].sum(0)) + 1e-9) - np.log(target))

    def jac(th):
        wk, Q, m, D, Yk = parts_of(th)
        Y = Yk.sum(0); cY = np.conj(Y) / np.abs(Y) ** 2
        J = np.concatenate([np.real(cY * (-Yk * (s * wk / Q + 2 * wk * wk) / D)),
                            np.real(cY * (Yk * s * wk / (Q * D))), np.real(cY * -Yk)], 0).T
        return wt[:, None] * J

    th = least_squares(res, th0, jac=jac, bounds=(lb, ub), x_scale='jac', max_nfev=100).x
    fk, Qk, mk = np.exp(th[:K]), np.exp(th[K:2 * K]), np.exp(th[2 * K:])
    e = 20 * np.log10(np.abs(parts_of(th)[4].sum(0)) / target)
    print(violin, 'modal fit: %.2f dB rms' % np.sqrt(np.mean(e ** 2)))
    rows = [(a, q, m) for a, q, m in sorted(zip(fk, Qk, mk)) if q / (m * 2 * np.pi * a) > 5e-4]
    return rows


def fit_bodies(rows):
    fk, Qk, mk = map(np.array, zip(*rows))
    mk = mk / SCALE
    N, fs = 1 << 15, 48000
    f = np.fft.rfftfreq(N, 1 / fs); s = 1j * 2 * np.pi * f
    den = [m * (s * s + s * (2 * np.pi * a) / q + (2 * np.pi * a) ** 2) for a, q, m in zip(fk, Qk, mk)]
    A = np.array([s * s / d for d in den] + [2 * np.pi * a * s / d for a, d in zip(fk, den)])
    band = (f > 150) & (f < 9000)
    wt = 1.0 / (1.0 + (f[band] / XO) ** 4)
    out = {}
    for name in ['stoppani', 'klimke', 'levaggi', 'iowa']:
        h, _ = sf.read(os.path.join(ROOT, 'octavio2/data/bodies', name + '-48k.wav'))
        h = h if h.ndim == 1 else h[:, 0]
        tau = max(0, int(np.argmax(np.abs(h[:400]))) - 8)
        B = np.fft.rfft(h, N) * np.exp(s * tau / fs)
        Ab, Bb = A[:, band] * wt, B[band] * wt
        M = np.concatenate([Ab.real, Ab.imag], 1).T; yv = np.concatenate([Bb.real, Bb.imag])
        lam = 1e-4 * np.trace(M.T @ M) / M.shape[1]
        r = np.linalg.solve(M.T @ M + lam * np.eye(M.shape[1]), M.T @ yv)
        out[name] = dict(tau=tau, r=r.tolist())
        print(name, 'modal body delay', tau)
    return out


def write_header(rows, rad):
    K = len(rows)
    L = ['// Octavio 2 bridge data (M3), generated by octavio2/data/bridge/fit_bridge.py: do not edit.',
         '//',
         "// kCnsmModes: %d positive-residue modes fitted to the CNSM Stoppani violin's measured" % K,
         '// driving-point bridge admittance |Y| (all six measurements, phases 1 and 2, 200 Hz - 9 kHz,',
         '// 0.3 dB rms; Pauget Ballesteros 2026, CC BY 4.0, research/scripts/fetch_cnsm.py). f (Hz), Q,',
         '// modal mass (kg), as measured on the hanging violin (Hold 0).',
         '//',
         '// kModalBody: per Violin choice (Stoppani, Klimke, Levaggi, Iowa), how the same modes radiate',
         '// below the crossover: radiated = d/dt (sum_k a_k v_k) + sum_k b_k w_k v_k, v_k the velocity of',
         "// mode k (bridge modes at admScale 0.5), fitted to that body's measured response from 150 Hz to",
         '// 9 kHz through the 1.5 kHz crossover, after its bulk delay (delay, in samples at 48 kHz).',
         '#pragma once', '', 'namespace o2', '{', 'struct ModeData', '{', '    double f, Q, m;', '};', '',
         'static constexpr int kCnsmModeCount = %d;' % K,
         'static const ModeData kCnsmModes[kCnsmModeCount] = {']
    for i in range(0, K, 4):
        L.append('    ' + ' '.join('{ %.1f, %.1f, %.6f },' % r for r in rows[i:i + 4]))
    L += ['};', '', 'struct ModalBody', '{', '    int delay; // samples at 48 kHz',
          '    double a[kCnsmModeCount], b[kCnsmModeCount];', '};', 'static const ModalBody kModalBody[4] = {']
    for name in ['stoppani', 'klimke', 'levaggi', 'iowa']:
        r = rad[name]['r']
        L.append('    { %d, // %s' % (rad[name]['tau'], name.capitalize()))
        for arr in (r[:K], r[K:]):
            L.append('      {')
            for i in range(0, K, 6):
                L.append('          ' + ' '.join('%.6e,' % x for x in arr[i:i + 6]))
            L.append('      },')
        L.append('    },')
    L += ['};', '} // namespace o2']
    open(os.path.join(ROOT, 'octavio2/core/BridgeData.h'), 'w').write('\n'.join(L) + '\n')


if __name__ == '__main__':
    rows = fit_modes()
    open(os.path.join(HERE, 'modes_cnsm_stoppani.txt'), 'w').write(''.join('%.1f %.1f %.6f\n' % r for r in rows))
    write_header(rows, fit_bodies(rows))
