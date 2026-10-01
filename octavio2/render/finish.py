"""Octavio 2 finishing step (until M1 moves it into the core): bridge force -> sound.

    python3 finish.py force.wav out_base [--hall arvedi-near-seat|detmold|church|none] [--mp3]

Writes
  out_base.dry.wav   mono, full-band balanced body, -20 dB RMS: what the scorer measures
  out_base.wav       stereo: direct sound through the two directional bodies (left/right mic),
                     hall tail fed by the all-direction body (the body-radiation plan, F8)
  out_base.mp3       the stereo file as 192 kbps mp3 (with --mp3)
Bodies are the body-radiation thread's full-band and directional IRs (octavio2/data).
"""
import os, sys, subprocess
import numpy as np, soundfile as sf, scipy.signal as ss

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, '..', 'data')
IRS = '/mnt/project-files/research/final-tune-up-render/irs/'
FS = 48000


def mono(p):
    x, sr = sf.read(p)
    assert sr == FS, (p, sr)
    return x[:, 0] if x.ndim > 1 else x


def pair(l, r=None):
    if r is None:
        d, _ = sf.read(IRS + l)
        L, R = d[:, 0], d[:, 1]
    else:
        L, R = mono(IRS + l), mono(IRS + r)
    n = max(len(L), len(R))
    return np.stack([np.pad(L, (0, n - len(L))), np.pad(R, (0, n - len(R)))], 1)


A = 'arvedi/arvedi_auditorium_dataset/rirs/rir-S0-'
HALLS = {
    'arvedi-near-seat': lambda: pair(A + 'A206.wav', A + 'A209.wav'),
    'arvedi-far-seat': lambda: pair(A + 'A505.wav', A + 'A510.wav'),
    'detmold': lambda: pair('detmold/SetA_SingleSources/Data/Brahmssaal/DummyHead/C1S1R3.wav'),
    'church': lambda: pair('church/OMNI/SC_ML_OMNI_2.wav', 'church/OMNI/SC_MR_OMNI_2.wav'),
}


def split(ir):
    """Direct sound (3.5 ms around the first arrival per channel) and the tail."""
    D, T = [], np.zeros_like(ir)
    for c in range(2):
        x = ir[:, c]
        p = int(np.argmax(np.abs(x)))
        a, b = max(p - 48, 0), p + 120
        w = np.zeros(len(x))
        w[a:b] = 1
        w[b:b + 48] = np.linspace(1, 0, 48)
        D.append((p, np.sqrt(np.sum((x * w) ** 2))))
        T[:, c] = x * (1 - w)
    p0 = min(d[0] for d in D)
    T = T[max(p0 - 48, 0):]
    D = [(p - p0 + min(p0, 48), g) for p, g in D]
    return D, T


def rms_norm(x, db=-20.0):
    return x * (10 ** (db / 20) / max(np.sqrt(np.mean(x ** 2)), 1e-12))


def main():
    src, base = sys.argv[1], sys.argv[2]
    hall = 'arvedi-near-seat'
    if '--hall' in sys.argv:
        hall = sys.argv[sys.argv.index('--hall') + 1]
    F = mono(src)
    body = mono(os.path.join(DATA, 'body-fullband-balanced-48k.wav'))
    bl = mono(os.path.join(DATA, 'body-directional-left-48k.wav'))
    br = mono(os.path.join(DATA, 'body-directional-right-48k.wav'))
    n = len(F)
    diffuse = ss.fftconvolve(F, body)[:n]
    dry = rms_norm(diffuse)
    sf.write(base + '.dry.wav', dry.astype(np.float32), FS)
    direct = np.stack([ss.fftconvolve(F, bl)[:n], ss.fftconvolve(F, br)[:n]], 1)
    if hall == 'none':
        out = direct
    else:
        D, T = split(HALLS[hall]())
        out = np.stack([ss.fftconvolve(diffuse, T[:, c])[:n] for c in range(2)], 1)
        for c, (p, g) in enumerate(D):
            out[p:, c] += g * direct[:n - p, c]
    out = rms_norm(out)
    out /= max(np.abs(out).max() / 0.97, 1.0)
    fade = int(0.3 * FS)
    out[-fade:] *= np.linspace(1, 0, fade)[:, None]
    sf.write(base + '.wav', out.astype(np.float32), FS)
    if '--mp3' in sys.argv:
        subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', base + '.wav', '-b:a', '192k', base + '.mp3'], check=True)


if __name__ == '__main__':
    main()
