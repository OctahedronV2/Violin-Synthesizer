# harmcheck.py wav "target:finger:start ..." : purity of each harmonic note
import sys, numpy as np, soundfile as sf
x, sr = sf.read(sys.argv[1])
x = x.mean(1) if x.ndim > 1 else x
hz = lambda p: 440 * 2 ** ((p - 69) / 12)
for item in sys.argv[2].split():
    tp, fp, t0 = item.split(':')
    tp, fp, t0 = int(tp), int(fp), float(t0)
    s = x[int((t0 + 0.4) * sr):int((t0 + 1.4) * sr)]
    s = s * np.hanning(len(s))
    X = np.abs(np.fft.rfft(s, 1 << 18)) ** 2
    fr = np.fft.rfftfreq(1 << 18, 1 / sr)
    ft, ff = hz(tp), hz(fp)
    def band(f):
        m = (fr > f * 0.985) & (fr < f * 1.015)
        return X[m].sum()
    tot = sum(band(k * ff) for k in range(1, int(12000 / ff)))
    tgt = sum(band(k * ft) for k in range(1, int(12000 / ft)))
    fund = band(ff)
    print('target %d finger %d: target-series %.1f%% of harmonic energy, finger fundamental %.1f dB re target' % (
        tp, fp, 100 * tgt / tot, 10 * np.log10(fund / band(ft) + 1e-20)))
