"""Transcribe a monophonic violin recording to MIDI for like-for-like renders, fast passages included.

    python3 transcribe2.py in.flac out.mid [--check] [--valley dB]

The scorer's transcriber (references/tool/transcribe.py) smooths pitch over ~130 ms and needs a
pitch to hold 50 ms, so fast figures (16ths at ~100 ms) lose notes, and it slurs anything without
a 30 ms silence, so separate strokes become one legato line. Here:
  - pitch is smoothed over 45 ms and a note needs to hold 30 ms
  - a stroke ends at a level valley (VALLEY dB under the peaks on both sides, 10 by default; real
    bow changes in fast Haydn passages dip about 6 dB, slurred changes 1-3, so --valley 5 reads
    passagework as separate strokes) or at silence;
    separate strokes don't overlap, so the player changes bow there
  - a pitch change after a short unvoiced break (10 ms) is also a new stroke
  - a pitch change without a valley or break is a slur (40 ms overlap, the player keeps the bow)
  - velocity follows each note's peak level (2.5 velocity per dB, 100 at the file's loud level)
  - loud stretches the pitch tracker rejects (two strings at once) are read as double stops:
    the strongest harmonic series, then the strongest one left after removing it
--check prints, per 2 s, how much of the real pitch track the MIDI covers with the right note.
"""
import os, sys
import numpy as np, mido
from scipy.ndimage import median_filter

sys.path.insert(0, '/mnt/project-files/research/world-class/references/tool')
import violinscore as vs


VALLEY = 5.0  # dB under the louder level on both sides that marks a bow change (10 ms level)


def transcribe(path):
    x = vs.load(path)
    x = x / (np.sqrt(np.mean(x ** 2)) + 1e-12) * 0.05
    f0, ap, rms = vs.yin(x)
    FR = vs.FR
    v = f0 > 0
    a4 = 440 * 2 ** (np.median(vs.midi_of(f0[v]) - np.round(vs.midi_of(f0[v]))) / 12)
    m = np.where(v, vs.midi_of(f0, a4), np.nan)
    loud = np.percentile(rms, 98)
    gate = loud - 40
    sm = median_filter(np.nan_to_num(m, nan=-100), size=9)
    lab = np.where((sm > 0) & (rms > gate), np.round(sm), -1).astype(int)
    # level valleys on a 10 ms level (the tracker's 46 ms window smears bow-change dips away),
    # centred like the pitch frames: VALLEY dB under the louder level on both sides within 150 ms
    n = len(rms)
    c = np.arange(n) * vs.HOP + 1024
    cs = np.concatenate([[0.0], np.cumsum(x ** 2)])
    lo_, hi_ = np.clip(c - 220, 0, len(x)), np.clip(c + 220, 0, len(x))
    rs = 10 * np.log10((cs[hi_] - cs[lo_]) / np.maximum(hi_ - lo_, 1) + 1e-14)
    W = int(0.15 * FR)
    valley = np.zeros(n, bool)
    for i in range(W, n - W):
        if rs[i] == rs[i - 3:i + 4].min() and rs[i] > gate - 20:
            if rs[i - W:i].max() - rs[i] > VALLEY and rs[i + 1:i + W + 1].max() - rs[i] > VALLEY:
                valley[i] = True
    # notes: runs of a stable label, cut at valleys
    notes = []
    cur, st, joined = -1, 0, False
    hold = 6  # 30 ms
    i = 0
    def close(end, sep):
        if cur > 0 and (end - st) / FR >= 0.04:
            notes.append(dict(on=st / FR, off=end / FR, note=cur, sep_after=sep))
    while i < n:
        if valley[i] and cur > 0:
            if i - st < 8 and notes and abs(notes[-1]['off'] - st / FR) < 1e-9:
                # the dip lands just after the pitch change: it is that change's bow change
                notes[-1]['sep_after'] = True
                i += 1
                continue
            close(i, True)
            cur = -1
            i += 1
            continue
        if lab[i] <= 0:
            if cur > 0 and np.all(lab[i:i + hold] <= 0):
                close(i, True)
                cur = -1
            i += 1
            continue
        if lab[i] != cur and np.all(lab[i:i + hold] == lab[i]):
            if cur > 0:
                # a short unvoiced break before the new pitch is a bow change, not a slur
                close(i, bool(np.sum(lab[max(st, i - 8):i] <= 0) >= 2))
            cur, st = lab[i], i
        i += 1
    close(n, True)
    notes += double_stops(x, f0, rms, loud, a4)
    notes.sort(key=lambda k: k['on'])
    # velocity from each note's peak level
    for k in notes:
        a, b = int(k['on'] * FR), int(k['off'] * FR)
        pk = np.percentile(rms[a:max(b, a + 1)], 90)
        k['vel'] = int(np.clip(100 + 2.5 * (pk - loud), 25, 120))
    return notes, f0, a4


def salience(S, f, a4, exclude=()):
    """Harmonic-sum strength of each MIDI note 55..100 in a magnitude spectrum."""
    out = {}
    for m in range(55, 101):
        f1 = a4 * 2 ** ((m - 69) / 12)
        tot = 0.0
        for h in range(1, 7):
            fh = h * f1
            if fh > f[-1] or any(abs(fh / fe - round(fh / fe)) < 0.02 for fe in exclude):
                continue
            b = (f > fh * 0.985) & (f < fh * 1.015)
            if b.any():
                tot += S[b].max() / h ** 0.5
        out[m] = tot
    return out


def double_stops(x, f0, rms, loud, a4):
    """Loud runs (>= 60 ms) without a single pitch: find the two notes of a double stop."""
    FR, FS = vs.FR, vs.FS
    bad = (f0 == 0) & (rms > loud - 20)
    out, i, n = [], 0, len(bad)
    while i < n:
        if not bad[i]:
            i += 1
            continue
        j = i
        while j < n and bad[j]:
            j += 1
        if (j - i) / FR >= 0.06:
            picks = []
            for k in range(i, j, 4):
                s0 = int(k / FR * FS)
                fr = x[s0:s0 + 4096]
                if len(fr) < 4096:
                    break
                S = np.abs(np.fft.rfft(fr * np.hanning(4096)))
                f = np.fft.rfftfreq(4096, 1 / FS)
                s1 = salience(S, f, a4)
                m1 = max(s1, key=s1.get)
                f1 = a4 * 2 ** ((m1 - 69) / 12)
                s2 = salience(S, f, a4, exclude=(f1,))
                s2 = {m: v for m, v in s2.items() if 2 <= abs(m - m1) <= 16}
                m2 = max(s2, key=s2.get)
                picks.append((m1, m2 if s2[m2] > 0.35 * s1[m1] else -1))
            if picks:
                lo = int(np.median([min(a, b) if b > 0 else a for a, b in picks]))
                hi = int(np.median([max(a, b) if b > 0 else a for a, b in picks]))
                d = dict(on=i / FR, off=j / FR, note=lo, sep_after=True)
                if hi != lo and np.mean([b > 0 for a, b in picks]) > 0.5:
                    d['extra'] = hi
                out.append(d)
        i = j
    return out


def write_mid(notes, out):
    ev = []
    for j, k in enumerate(notes):
        on, off = k['on'], k['off']
        nxt = notes[j + 1] if j + 1 < len(notes) else None
        if nxt is not None:
            if not k['sep_after'] and nxt['on'] - off < 0.03:
                # slur: overlap so the player keeps the bow (same pitch can't overlap)
                off = nxt['on'] + 0.04 if nxt['note'] != k['note'] else nxt['on'] - 0.012
            else:
                off = min(off, nxt['on'] - 0.012)  # separate stroke: let go before the next
        for p in [k['note']] + ([k['extra']] if 'extra' in k else []):
            ev.append((on, 1, p, k['vel']))
            ev.append((max(off, on + 0.03), 0, p, 0))
    ev.sort(key=lambda e: (e[0], e[1]))
    mf = mido.MidiFile(ticks_per_beat=480)
    tr = mido.MidiTrack()
    mf.tracks.append(tr)
    tr.append(mido.MetaMessage('set_tempo', tempo=500000))
    t = 0.0
    for tt, kind, note, vel in ev:
        d = int(round((tt - t) * 960))
        t += d / 960
        tr.append(mido.Message('note_on' if kind else 'note_off', note=note, velocity=vel, time=max(d, 0)))
    mf.save(out)


def coverage(mid, f0, a4=440.0, upto=None):
    """Share of the real voiced frames where a sounding MIDI note has the real pitch (±0.5 st)."""
    FR = vs.FR
    t, on, notes = 0.0, {}, []
    for msg in mido.MidiFile(mid):
        t += msg.time
        if msg.type == 'note_on' and msg.velocity > 0:
            on[msg.note] = t
        elif msg.type in ('note_off', 'note_on') and msg.note in on:
            notes.append((on.pop(msg.note), t, msg.note))
    n = len(f0) if upto is None else min(len(f0), int(upto * FR))
    act = [[] for _ in range(n)]
    for a, b, p in notes:
        for i in range(int(a * FR), min(n, int(b * FR) + 1)):
            act[i].append(p)
    m = np.where(f0 > 0, vs.midi_of(np.maximum(f0, 1), a4), 0)
    good = np.array([f0[i] > 0 and any(abs(m[i] - p) < 0.5 for p in act[i]) for i in range(n)])
    voiced = f0[:n] > 0
    return good, voiced


if __name__ == '__main__':
    src, out = sys.argv[1], sys.argv[2]
    if '--valley' in sys.argv:
        VALLEY = float(sys.argv[sys.argv.index('--valley') + 1])
    notes, f0, a4 = transcribe(src)
    write_mid(notes, out)
    print(out, len(notes), 'notes, a4 %.1f' % a4)
    if '--check' in sys.argv:
        good, voiced = coverage(out, f0, a4)
        print('pitch agreement %.1f%% of voiced frames' % (100 * good.sum() / voiced.sum()))
        FR = vs.FR
        for w in range(0, int(len(f0) / FR), 4):
            s = slice(int(w * FR), int((w + 4) * FR))
            print('  %3d-%3d s  %3.0f%%' % (w, w + 4, 100 * good[s].sum() / max(1, voiced[s].sum())))
