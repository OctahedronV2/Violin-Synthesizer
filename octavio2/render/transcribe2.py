"""Transcribe a monophonic violin recording to MIDI for like-for-like renders, fast passages included.

    python3 transcribe2.py in.flac out.mid [--check]

The scorer's transcriber (references/tool/transcribe.py) smooths pitch over ~130 ms and needs a
pitch to hold 50 ms, so fast figures (16ths at ~100 ms) lose notes, and it slurs anything without
a 30 ms silence, so separate strokes become one legato line. Here:
  - pitch is smoothed over 45 ms and a note needs to hold 30 ms
  - a stroke ends at a level valley (10 dB under the peaks on both sides) or at silence;
    separate strokes don't overlap, so the player changes bow there
  - a pitch change after a short unvoiced break (10 ms) is also a new stroke
  - a pitch change without a valley or break is a slur (40 ms overlap, the player keeps the bow)
  - velocity follows each note's peak level (2.5 velocity per dB, 100 at the file's loud level)
--check prints, per 2 s, how much of the real pitch track the MIDI covers with the right note.
"""
import os, sys
import numpy as np, mido
from scipy.ndimage import median_filter

sys.path.insert(0, '/mnt/project-files/research/world-class/references/tool')
import violinscore as vs


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
    # level valleys: frames that are 10 dB under the peak level on both sides (within 150 ms)
    rs = median_filter(rms, size=3)
    W = int(0.15 * FR)
    n = len(rs)
    valley = np.zeros(n, bool)
    for i in range(W, n - W):
        if rs[i] == rs[i - 3:i + 4].min():
            if rs[i - W:i].max() - rs[i] > 10 and rs[i + 1:i + W + 1].max() - rs[i] > 10:
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
    # velocity from each note's peak level
    for k in notes:
        a, b = int(k['on'] * FR), int(k['off'] * FR)
        pk = np.percentile(rms[a:max(b, a + 1)], 90)
        k['vel'] = int(np.clip(100 + 2.5 * (pk - loud), 25, 120))
    return notes, f0, a4


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
        ev.append((on, 1, k['note'], k['vel']))
        ev.append((max(off, on + 0.03), 0, k['note'], 0))
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
