"""Builds the violin bodies and microphone direction filters Octavio 2 ships (M1, radiation).

    python3 make_bodies.py

bodies/<violin>-48k.wav  The full-band all-directions body of each measured violin
                         (resources/bodies): its measured resonances below 8 kHz, a random
                         modal tail shaped to real violins above it (body-radiation FINDINGS F2).
                         Levels match Stoppani's at 1-4 kHz.
mics/<position>-48k.wav  Six minimum-phase direction filters per microphone position, applied
                         after the body: left, right, then two more directions for each side
                         that the player's sway turns towards (left 2, left 3, right 2, right 3).
                         Each is a random direction field with the TU Berlin violin's statistics
                         (FINDINGS F4), the position's tilt (F5) and the brighter body the
                         microphones heard before M1 (TU Berlin target over the balanced one).

The front position's left and right are the directions Octavio 2 shipped before M1
(body-directional-left/right-48k.wav, seeds 133 and 100), so its default sound is unchanged.
Only statistics of the measured directivity are used, so no measured data ships here.
"""
import os
import numpy as np, soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '../../..'))
FS = 48000
NF = 1 << 15
f = np.fft.rfftfreq(NF, 1 / FS)

# smooth real targets above ~4 kHz, dB relative to 1-4 kHz (body-radiation scripts/bodies.py)
TGT_F = np.array([4000, 5040, 6350, 8000, 10079, 12699, 16000, 20159, 24000])
TGT_DB = np.array([-1.3, -7.4, -12.3, -16.7, -20.6, -23.6, -25.7, -24.7, -27])
TGT_BRIGHT = np.array([-2.1, -8.4, -11.6, -14.2, -16.5, -20.0, -20.4, -18.5, -21])


def smooth_db(H, oct=1 / 3):
    P = np.abs(H) ** 2
    c = np.cumsum(P)
    lo = np.searchsorted(f, f * 2 ** (-oct / 2))
    hi = np.maximum(np.searchsorted(f, f * 2 ** (oct / 2)), lo + 1)
    hi = np.minimum(hi, len(c) - 1)
    return 10 * np.log10((c[hi] - c[lo] + 1e-30) / (hi - lo + 1e-9) + 1e-30)


def minphase(mag_db):
    n = 2 * (len(mag_db) - 1)
    cep = np.fft.irfft(np.log(10 ** (mag_db / 20) + 1e-9), n)
    fold = np.zeros(n)
    fold[0] = cep[0]
    fold[1:n // 2] = 2 * cep[1:n // 2]
    fold[n // 2] = cep[n // 2]
    return np.exp(np.fft.rfft(fold))


def target_hf(t):
    return np.interp(np.log(np.maximum(f, 1)), np.log(TGT_F), t)


def rel14(mdb):
    m = (f > 1000) & (f < 4000)
    return mdb - 10 * np.log10(np.mean(10 ** (mdb[m] / 10)))


def modal_tail(seed, f_lo=5000, f_hi=23500, spacing=60.0, q=40.0):
    rng = np.random.default_rng(seed)
    fm = np.arange(f_lo, f_hi, spacing) + rng.uniform(-0.5, 0.5, int(np.ceil((f_hi - f_lo) / spacing))) * spacing
    s = 2j * np.pi * f
    H = np.zeros(len(f), complex)
    for fk in fm:
        w = 2 * np.pi * fk
        a = rng.normal() + 1j * rng.normal()
        H += a * (s * w / q) / (s * s + s * w / q + w * w)
    return H


def hybrid(base='stoppani', seed=1, xo=(8000, 10000), tgt=TGT_DB, tilt_from=None):
    h = sf.read(f'{ROOT}/resources/bodies/{base}.wav')[0]
    H = np.fft.rfft(h, NF)
    T = modal_tail(seed)
    T = T * 10 ** ((target_hf(tgt) - smooth_db(T)) / 20)
    m = (f > 1000) & (f < 4000)
    T = T * 10 ** (10 * np.log10(np.mean(np.abs(H[m]) ** 2)) / 20)
    w = np.clip((f - xo[0]) / (xo[1] - xo[0]), 0, 1)
    w = 0.5 - 0.5 * np.cos(np.pi * w)
    Hh = H * (1 - w) + T * w
    if tilt_from is not None:
        cur = rel14(smooth_db(Hh))
        corr = np.where(f > tilt_from, target_hf(tgt) - cur, 0.0)
        Hh = Hh * minphase(np.convolve(corr, np.ones(64) / 64, 'same'))
    return Hh


def to_ir(H, n, fade=480):
    h = np.fft.irfft(H, NF)[:n]
    h[-fade:] *= 0.5 + 0.5 * np.cos(np.pi * np.arange(fade) / fade)
    return h


def dir_field(seed, corr=(50, 15), share=0.7, broad=1200.0):
    """random directional deviation in dB over f, with the TU Berlin violin's statistics (32 mics)"""
    rng = np.random.default_rng(seed)
    lf = 1200 * np.log2(np.maximum(f, 20) / 20)
    c = np.exp(np.interp(np.log(np.maximum(f, 20)), np.log([500, 8000]), np.log(corr)))
    u = np.concatenate([[0], np.cumsum(np.diff(lf) / c[1:])])

    def gp(axis, unit):
        grid = np.arange(0, axis[-1] + unit, unit / 4)
        v = rng.normal(size=len(grid))
        k = np.exp(-0.5 * (np.arange(-12, 13) / 4) ** 2)
        v = np.convolve(v, k / np.sqrt((k * k).sum()), 'same')
        return np.interp(axis, grid, v)

    sigma = np.interp(np.log(np.maximum(f, 1)),
                      np.log([212, 387, 632, 980, 1549, 2449, 3674, 5408, 7649, 10817, 16125]),
                      [1.63, 1.96, 3.03, 5.43, 5.22, 5.48, 6.67, 5.14, 5.16, 4.18, 5.53])
    return sigma * (np.sqrt(1 - share) * gp(lf, broad) + np.sqrt(share) * gp(u, 1.0))


def balance(seed):
    """how far a direction's third-octave balance strays from the all-directions body (dB rms)"""
    d = dir_field(seed)
    sm = []
    for a in 1000 * 2 ** (np.arange(-7, 13) / 3):
        m = (f >= a * 2 ** (-1 / 6)) & (f < a * 2 ** (1 / 6))
        sm.append(10 * np.log10(np.mean(10 ** (d[m] / 10))))
    return np.sqrt(np.mean(np.array(sm) ** 2))


def tilt(points):
    fr, db = zip(*points)
    return np.interp(np.log(np.maximum(f, 1)), np.log(fr), db)


# Position tilts relative to the front pair. Above and the player's ear follow the Expressive Solo
# Violin perspectives (F5: above-player and close mics against the 1 m pair) at 70 %, since part
# of those differences is the room; the ear also gets the near field's extra low end. The side
# loses the top plate's forward treble.
POSITIONS = {
    'front': None,
    'above': [(1000, 0), (2500, 0), (4000, 2.6), (5000, 1.3), (6300, -2.6), (8000, -5.2), (10000, -5.3),
              (12700, -6.8), (16000, -4.4), (24000, -4.4)],
    'ear': [(100, 2.5), (400, 1.5), (800, 0), (2500, 0.5), (4000, -1.0), (5000, -0.6), (6300, -4.0),
            (8000, -4.2), (10000, -5.0), (12700, -6.7), (16000, -5.3), (24000, -5.3)],
    'side': [(800, 0), (2000, -1.5), (4000, -4), (8000, -6), (16000, -7), (24000, -7)],
}


def main():
    os.makedirs(f'{HERE}/../mics', exist_ok=True)
    m14 = (f > 1000) & (f < 4000)
    ref = None
    for name in ['stoppani', 'klimke', 'levaggi', 'iowa']:
        H = hybrid(name)
        lvl = np.mean(np.abs(H[m14]) ** 2)
        ref = ref or lvl
        H *= np.sqrt(ref / lvl)
        sf.write(f'{HERE}/{name}-48k.wav', to_ir(H, 9600).astype(np.float32), FS, subtype='FLOAT')
        print(name)

    # the microphones heard the brighter body before M1: keep that as part of every direction
    bright = smooth_db(hybrid(tgt=TGT_BRIGHT, tilt_from=4500)) - smooth_db(hybrid())
    # the 40 directions the pre-M1 pair was picked from, best balanced first, then 40 more
    ranked = [s for _, s in sorted((balance(s), s) for s in range(100, 140))]
    assert ranked[:2] == [133, 100], ranked[:2]
    ranked += [s for _, s in sorted((balance(s), s) for s in range(140, 180))]
    seeds = {
        'front': [133, 100] + ranked[2:6],
        'above': ranked[6:12],
        'ear': ranked[12:18],
        'side': list(range(200, 206)),  # not chosen for balance: the side is a coloured place
    }
    for pos, pts in POSITIONS.items():
        extra = bright + (tilt(pts) if pts else 0)
        irs = [to_ir(minphase(dir_field(s) + extra), 2048, 480) for s in seeds[pos]]
        # order: left, right, left 2, left 3, right 2, right 3
        sf.write(f'{HERE}/../mics/{pos}-48k.wav', np.stack(irs, 1).astype(np.float32), FS, subtype='FLOAT')
        print(pos, seeds[pos])


if __name__ == '__main__':
    main()
