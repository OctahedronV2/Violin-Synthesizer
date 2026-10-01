# Physics-facing metrics for one bowed or plucked note (real recording or model render).
import numpy as np
from scipy.signal import stft, butter, sosfiltfilt, hilbert

def refine_f0(x, sr, f_guess):
    n = min(len(x), int(sr*1.0)); seg = x[:n]*np.hanning(n)
    N = 1 << 20
    X = np.abs(np.fft.rfft(seg, N)); fr = np.fft.rfftfreq(N, 1/sr)
    # harmonic sum over candidates within +-1 semitone
    cands = f_guess * 2**(np.linspace(-1, 1, 401)/12)
    best = max(cands, key=lambda f: sum(X[int(round(k*f*N/sr))] for k in range(1, 6)))
    m = (fr > best*0.98) & (fr < best*1.02)
    return fr[m][np.argmax(X[m])]

def harmonics(x, sr, f0, K=30, win=0.4):
    """Average magnitude (dB re h1) of harmonics 1..K and noise between them."""
    n = int(win*sr); hop = n//2
    w = np.hanning(n); N = 1 << int(np.ceil(np.log2(n*4)))
    acc = None; cnt = 0
    for i in range(0, len(x)-n, hop):
        X = np.abs(np.fft.rfft(x[i:i+n]*w, N))**2
        acc = X if acc is None else acc+X; cnt += 1
    P = acc/cnt; fr = np.fft.rfftfreq(N, 1/sr)
    hk, nk = [], []
    for k in range(1, K+1):
        f = k*f0
        if f > sr/2*0.95: hk.append(np.nan); nk.append(np.nan); continue
        m = (fr > f-0.15*f0) & (fr < f+0.15*f0); hk.append(P[m].max())
        if k <= 12:
            m2 = (fr > f+0.35*f0) & (fr < f+0.65*f0); nk.append(np.median(P[m2]))
    hk = np.array(hk); ref = np.nansum(hk)
    noise = 10*np.log10(np.nanmean(nk)/ref)
    return 10*np.log10(hk/ref), noise, hk

def centroid(hdb, f0):
    a = 10**(np.nan_to_num(hdb, nan=-200)/20); k = np.arange(1, len(a)+1)
    return (k*a).sum()/a.sum()*f0

def period_stats(x, sr, f0):
    """Cycle-to-cycle shimmer (dB) and jitter (cents) from the fundamental's analytic signal."""
    # track the strongest of harmonics 1..4 (the G string's fundamental is weak)
    X = np.abs(np.fft.rfft(x[:int(sr*0.5)]*np.hanning(int(sr*0.5)))); fr = np.fft.rfftfreq(int(sr*0.5), 1/sr)
    h = max(range(1, 5), key=lambda k: X[np.argmin(np.abs(fr-k*f0))])
    sos = butter(4, [h*f0-0.3*f0, h*f0+0.3*f0], 'bandpass', fs=sr, output='sos')
    y = sosfiltfilt(sos, x); a = hilbert(y)
    ph = np.unwrap(np.angle(a)); inst = np.diff(ph)*sr/(2*np.pi)
    P = int(round(sr/f0))
    nper = len(inst)//P
    fper = inst[:nper*P].reshape(nper, P).mean(1)/h
    cents = 1200*np.log2(fper/np.median(fper))
    mad = lambda v: 1.4826*np.median(np.abs(v-np.median(v)))
    jitter = mad(np.diff(cents))/np.sqrt(2)
    # shimmer on the full waveform's per-period rms
    r = np.sqrt((x[:nper*P].reshape(nper, P)**2).mean(1))
    db = 20*np.log10(r+1e-12)
    shimmer = mad(np.diff(db))/np.sqrt(2)
    drift = mad(cents)
    return shimmer, jitter, drift

def attack(x, sr, f0, steady):
    """Times (ms) from onset to the level within 3 dB of steady, and to the
    spectral shape (h1..h8 dB) within 3 dB rms of the steady shape."""
    hop = int(0.005*sr); n = int(max(4/f0, 0.02)*sr)
    e = np.array([np.sqrt(np.mean(x[i:i+n]**2)) for i in range(0, len(x)-n, hop)])
    ss = np.median(e[len(e)//3: 2*len(e)//3])
    on = np.argmax(e > ss*10**(-40/20))
    lvl = np.argmax(e[on:] > ss*10**(-3/20))
    # spectral settle
    n2 = int(max(6/f0, 0.03)*sr); w = np.hanning(n2)
    k = np.arange(1, 9); t = None
    for j in range(on, len(e)):
        i = j*hop
        if i+n2 > len(x): break
        X = np.abs(np.fft.rfft(x[i:i+n2]*w, 1 << 15)); fr = np.fft.rfftfreq(1 << 15, 1/sr)
        h = np.array([X[np.argmin(np.abs(fr-kk*f0))] for kk in k])
        hdb = 20*np.log10(h/np.sqrt((h**2).sum())+1e-9)
        hdb = hdb - hdb.max()
        st = steady[:8]-np.max(steady[:8])
        if np.sqrt(np.mean((hdb-st)**2)) < 3.0:
            t = (j-on)*5; break
    return lvl*5, t

def partial_decays(x, sr, f0, K=12, B=0.0):
    """Pluck: T60 (s) and measured frequency of partials 1..K."""
    out = []
    n = int(0.08*sr); hop = int(0.01*sr); w = np.hanning(n); N = 1 << 16
    fr = np.fft.rfftfreq(N, 1/sr)
    frames = [np.abs(np.fft.rfft(x[i:i+n]*w, N)) for i in range(0, len(x)-n, hop)]
    S = np.array(frames)
    long = np.abs(np.fft.rfft(x[:int(sr*0.6)]*np.hanning(int(sr*0.6)), 1 << 20)); frl = np.fft.rfftfreq(1 << 20, 1/sr)
    for k in range(1, K+1):
        fk = k*f0*np.sqrt(1+B*k*k)
        m = (frl > fk-0.2*f0) & (frl < fk+0.2*f0)
        fmeas = frl[m][np.argmax(long[m])]
        b = np.argmin(np.abs(fr-fmeas))
        env = 20*np.log10(S[:, b-2:b+3].max(1)+1e-12)
        pk = np.argmax(env[:30]); top = env[pk]
        # fit from peak+30 ms over the region above floor+10dB, max 1.5 s
        floor = np.percentile(env, 5)
        idx = np.arange(pk+3, min(len(env), pk+150))
        idx = idx[env[idx] > max(floor+10, top-45)]
        if len(idx) < 8: out.append((k, fmeas, np.nan)); continue
        sl = np.polyfit(idx*0.01, env[idx], 1)[0]
        out.append((k, fmeas, -60/sl if sl < 0 else np.inf))
    return out
