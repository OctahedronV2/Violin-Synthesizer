#!/usr/bin/env python3
"""Measure plucked violin notes: real recordings against the synth (docs/PIZZICATO.md).

    python scripts/measure_pizzicato.py                  # download and measure the recordings
    python scripts/measure_pizzicato.py <render folder>  # ... and the synth's renders

The recordings are the University of Iowa MIS violin (2012) pizzicato runs:
anechoic, every note on every string at pp, mf and ff (Lawrence Fritts,
https://theremin.music.uiowa.edu/MIS.html; free to use without restrictions).
They are downloaded into research/external/iowa_pizz (about 105 MB).

A render folder holds pizz_pp.wav, pizz_mf.wav and pizz_ff.wav written by
`ViolinSynthTests "[.pizzrender]"`: G3 to B5, one note every 3 s.

For notes up to a fifth above each open string it prints, open and stopped
separately:
  decay   dB/s of the fundamental, harmonics 2-3 and harmonics 4-8, over the
          first 80 ms and from 120 to 600 ms (the fast, then slow decay)
  H       harmonic levels 1-8 in the first 40 ms, dB re the fundamental
"""

from __future__ import annotations

import re
import sys
import urllib.parse
import urllib.request
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy.signal import butter, sosfiltfilt, stft

ROOT = Path(__file__).resolve().parent.parent
TARGET = ROOT / "external" / "iowa_pizz"
BASE = "https://theremin.music.uiowa.edu/"
INDEX = BASE + "MISviolin2012.html"
OPEN = {"sulG": 55, "sulD": 62, "sulA": 69, "sulE": 76}
NAMES = {"C": 0, "Db": 1, "D": 2, "Eb": 3, "E": 4, "F": 5, "Gb": 6, "G": 7, "Ab": 8, "A": 9, "Bb": 10, "B": 11}
RENDER_STRIDE = 144128  # samples per note in a render: 3 s rounded up to 256-sample blocks


def fetch():
    TARGET.mkdir(parents=True, exist_ok=True)
    page = urllib.request.urlopen(INDEX).read().decode("utf-8", "replace")
    for link in sorted(set(re.findall(r'href="([^"]*Violin\.pizz\.[^"]*\.mono\.aif)"', page))):
        dest = TARGET / re.sub(r"\s+", "", link.rsplit("/", 1)[-1])
        if not dest.exists():
            print("downloading", dest.name)
            urllib.request.urlretrieve(BASE + urllib.parse.quote(link), dest)


def midi(name):
    m = re.match(r"([A-G]b?)(\d)", name)
    return NAMES[m.group(1)] + 12 * (int(m.group(2)) + 1)


def hz(note):
    return 440.0 * 2.0 ** ((note - 69.0) / 12.0)


def onsets(x, fs):
    """Plucks: the level jumps by 18 dB within 30 ms."""
    h = int(0.005 * fs)
    db = 20 * np.log10(np.sqrt(np.convolve(x * x, np.ones(h) / h, "same")) + 1e-10)
    d, peak, found, k = int(0.015 * fs), db.max(), [], 0
    rise = np.full(len(x), -99.0)
    rise[d:-d] = db[2 * d :] - db[: -2 * d]
    while k < len(x):
        if rise[k] > 18 and db[min(len(x) - 1, k + d)] > peak - 40:
            k += int(np.argmax(rise[k : k + d]))
            found.append(k)
            k += int(0.8 * fs)
        else:
            k += 1
    return found


def f0_of(x, fs, lo, hi):
    """Autocorrelation pitch, refined on the spectral peaks of the first harmonics."""
    x = x - x.mean()
    ac = np.fft.irfft(np.abs(np.fft.rfft(x * np.hanning(len(x)), 4 * len(x))) ** 2)[: len(x)]
    ac /= ac[0] + 1e-20
    k = int(fs / hi) + int(np.argmax(ac[int(fs / hi) : int(fs / lo)]))
    if ac[k] < 0.5:
        return None
    f0 = fs / k
    n = 1 << int(np.ceil(np.log2(len(x) * 8)))
    spectrum = np.abs(np.fft.rfft(x * np.hanning(len(x)), n))
    freqs = np.fft.rfftfreq(n, 1 / fs)
    est, weight = [], []
    for h in range(1, 7):
        band = (freqs > h * f0 * 0.97) & (freqs < h * f0 * 1.03)
        i = np.where(band)[0][np.argmax(spectrum[band])]
        est.append(freqs[i] / h)
        weight.append(spectrum[i])
    return float(np.average(est, weights=weight))


def measure(x, fs, f0):
    """Early and late decay (dB/s) of three harmonic groups, and harmonic levels."""
    x = sosfiltfilt(butter(4, 0.7 * f0, "high", fs=fs, output="sos"), x)  # the recordings have rumble
    n = int(0.02 * fs)
    f, t, z = stft(x[: int(1.2 * fs)], fs, nperseg=n, noverlap=n * 3 // 4, nfft=8 * n)
    a = 20 * np.log10(np.abs(z) + 1e-12)
    track = lambda h: a[(f > h * f0 * 0.97) & (f < h * f0 * 1.03)].max(0)
    decay = []
    for group in [(1,), (2, 3), (4, 5, 6, 7, 8)]:
        tr = np.max([track(h) for h in group if h * f0 < 0.45 * fs], 0)
        pk = int(np.argmax(tr[:10]))
        tr, tt = tr - tr[pk], t - t[pk]
        for lo, hi in [(0.0, 0.08), (0.12, 0.6)]:
            sel = (tt >= lo) & (tt <= hi) & (tr > -45)
            decay.append(np.polyfit(tt[sel], tr[sel], 1)[0] if sel.sum() > 2 else np.nan)
    first = [track(h)[int(np.argmax(track(1)[:10]))] for h in range(1, 9) if h * f0 < 0.45 * fs]
    levels = np.full(8, np.nan)
    levels[: len(first)] = np.array(first) - first[0]
    return np.array(decay), levels


def recordings():
    for path in sorted(TARGET.glob("Violin.pizz.*.aif")):
        dyn, string, span = path.name.split(".")[2:5]
        lo, hi = (midi(n) for n in re.findall(r"[A-G]b?\d", span))
        x, fs = sf.read(path)
        x = x.mean(1) if x.ndim > 1 else x
        for k in onsets(x, fs):
            note = x[max(0, k - int(0.01 * fs)) : k + int(1.5 * fs)]
            if len(note) < fs:
                continue
            f0 = f0_of(note[int(0.04 * fs) : int(0.2 * fs)], fs, hz(lo - 1), hz(hi + 1))
            if f0 is None or not hz(lo - 0.6) < f0 < hz(hi + 0.6):
                continue
            yield string, 12 * np.log2(f0 / 440) + 69, note, fs, f0


def renders(folder):
    for dyn in ("pp", "mf", "ff"):
        x, fs = sf.read(Path(folder) / f"pizz_{dyn}.wav")
        x = x[:, 0] if x.ndim > 1 else x
        for i, note in enumerate(range(55, 84)):
            string = [s for s, o in OPEN.items() if o <= note][-1]
            yield string, note, x[i * RENDER_STRIDE : i * RENDER_STRIDE + int(1.5 * fs)], fs, hz(note)


def report(title, notes):
    groups = {}
    for string, note, x, fs, f0 in notes:
        above = note - OPEN[string]
        if above > 6.6:
            continue
        kind = "open" if abs(above) < 0.5 else "stopped"
        for key in (kind, f"{string} {kind}"):
            groups.setdefault(key, []).append(measure(x, fs, f0))
    print(f"\n{title}")
    print(f"{'':14s}{'n':>4s}   decay dB/s: h1 early/late, h2-3 early/late, h4-8 early/late   H dB, h1-h8")
    for key in ["stopped", "open"] + [f"{s} {k}" for s in OPEN for k in ("open", "stopped")]:
        if key in groups:
            d = np.nanmedian([g[0] for g in groups[key]], 0)
            h = np.nanmedian([g[1] for g in groups[key]], 0)
            print(f"{key:14s}{len(groups[key]):4d}   {' '.join(f'{v:5.0f}' for v in d)}   {' '.join(f'{v:4.0f}' for v in h)}")


def main():
    fetch()
    report("Recordings (Iowa MIS)", recordings())
    for folder in sys.argv[1:]:
        report(f"Synth ({folder})", renders(folder))


if __name__ == "__main__":
    main()
