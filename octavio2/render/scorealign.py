"""Fit a transcription to the printed score: one note per score note, timed by the recording.

    python3 scorealign.py played.mid score.mid score_start_s out.mid [--report report.txt]

The transcription (from the recording) has the real timing but splits some held notes and
misses or adds others; the score has the right notes. Both are aligned as note sequences
(dynamic programming over pitch: a transcribed note matches a score note of the same pitch; an
extra transcribed note on the same pitch as the score note before it is a split and joins it;
a score note with no transcribed note is put in between its neighbours). Each score note
then takes the start of its first transcribed note and the end of its last one.
Score: Haydn op. 76 no. 1, 2nd violin, Mutopia project (public domain, LilyPond MIDI).
"""
import sys
import mido
import notecompare as nc


def score_notes(path, start):
    t, on, out = 0.0, {}, []
    for m in mido.MidiFile(path):
        t += m.time
        if m.type == 'note_on' and m.velocity > 0:
            on[m.note] = t
        elif m.type in ('note_off', 'note_on') and m.note in on:
            a = on.pop(m.note)
            if a >= start - 1e-3:
                out.append(dict(on=a, off=t, pitch=m.note))
    out.sort(key=lambda k: (k['on'], k['pitch']))
    return out


def align(T, S):
    """Cost-minimal alignment; returns for each transcribed note the score index or None."""
    INF = 1e9
    n, m = len(T), len(S)
    D = [[INF] * (m + 1) for _ in range(n + 1)]
    B = [[None] * (m + 1) for _ in range(n + 1)]
    D[0][0] = 0.0
    for j in range(1, m + 1):  # the recording may start at any score note? no: it starts at S[0]
        D[0][j] = D[0][j - 1] + 1.0
        B[0][j] = 'skip'

    def pc(a, b):
        return 0.0 if a == b else 0.6 if abs(a - b) == 12 else 1.5

    for i in range(1, n + 1):
        for j in range(0, m + 1):
            best, how = INF, None
            if j:
                c = D[i - 1][j - 1] + pc(T[i - 1]['pitch'], S[j - 1]['pitch'])
                if c < best:
                    best, how = c, 'match'
                c = D[i][j - 1] + 1.0  # score note missing from the transcription
                if c < best:
                    best, how = c, 'skip'
                # an extra transcribed note: a split of the score note it follows
                c = D[i - 1][j] + (0.1 if T[i - 1]['pitch'] == S[j - 1]['pitch'] else 1.2)
                if c < best:
                    best, how = c, 'extra'
            D[i][j], B[i][j] = best, how
    # the recording may stop anywhere in the score: best end column
    j = min(range(m + 1), key=lambda k: D[n][k])
    i, path = n, []
    while i > 0 or j > 0:
        how = B[i][j]
        if how == 'match':
            path.append(('match', i - 1, j - 1)); i, j = i - 1, j - 1
        elif how == 'skip':
            path.append(('skip', None, j - 1)); j -= 1
        else:
            path.append(('extra', i - 1, j - 1)); i -= 1
    return path[::-1]


def main():
    a = sys.argv
    T = nc.read_notes(a[1])
    S = score_notes(a[2], float(a[3]))
    path = align(T, S)
    used = max(j for _, _, j in path) + 1
    S = S[:used]
    got = [[] for _ in S]
    rep = []
    for how, i, j in path:
        if how == 'match' or (how == 'extra' and T[i]['pitch'] == S[j]['pitch']):
            got[j].append(T[i])
            if how == 'extra':
                rep.append('joined %.2f s into the %s at %.2f s (score note %d)' % (T[i]['on'], nc_name(S[j]['pitch']), got[j][0]['on'], j + 1))
        elif how == 'extra':
            rep.append('dropped %.2f s %s: not in the score here' % (T[i]['on'], nc_name(T[i]['pitch'])))
        if how == 'match' and T[i]['pitch'] != S[j]['pitch']:
            rep.append('%.2f s: transcribed %s, score has %s' % (T[i]['on'], nc_name(T[i]['pitch']), nc_name(S[j]['pitch'])))
    out = []
    for j, s in enumerate(S):
        if got[j]:
            out.append(dict(on=got[j][0]['on'], off=max(k['off'] for k in got[j]), pitch=s['pitch'], vel=got[j][0]['vel']))
        else:
            out.append(None)
    # score notes the transcription missed: placed by score time between their timed neighbours
    for j, o in enumerate(out):
        if o is None:
            p = next(k for k in range(j - 1, -1, -1) if out[k] is not None) if any(out[:j]) else None
            q = next((k for k in range(j + 1, len(out)) if out[k] is not None), None)
            if p is None or q is None:
                continue
            f = (S[j]['on'] - S[p]['on']) / max(1e-6, S[q]['on'] - S[p]['on'])
            on = out[p]['on'] + f * (out[q]['on'] - out[p]['on'])
            g = (S[j]['off'] - S[j]['on']) / max(1e-6, S[q]['on'] - S[p]['on'])
            out[j] = dict(on=on, off=on + g * (out[q]['on'] - out[p]['on']), pitch=S[j]['pitch'], vel=out[p]['vel'], added=True)
            rep.append('added %s at %.2f s (score note %d, not heard in the transcription)' % (nc_name(S[j]['pitch']), on, j + 1))
    out = [o for o in out if o is not None]
    ev = sorted([(k['on'], 1, k['pitch'], k['vel']) for k in out] + [(k['off'], 0, k['pitch'], 0) for k in out])
    mf = mido.MidiFile(ticks_per_beat=480)
    tr = mido.MidiTrack()
    mf.tracks.append(tr)
    tr.append(mido.MetaMessage('set_tempo', tempo=500000))
    t = 0.0
    for tt, kind, p, v in ev:
        d = int(round((tt - t) * 960))
        t += d / 960
        tr.append(mido.Message('note_on' if kind else 'note_off', note=p, velocity=v, time=d))
    mf.save(a[4])
    print('%d transcribed notes, %d score notes (score %.1f-%.1f s) -> %d notes' % (len(T), len(S), S[0]['on'], S[-1]['on'], len(out)))
    txt = '\n'.join(rep)
    if '--report' in a:
        open(a[a.index('--report') + 1], 'w').write(txt + '\n')
    print(txt)


def nc_name(p):
    return 'C C# D D# E F F# G G# A A# B'.split()[p % 12] + str(p // 12 - 1)


if __name__ == '__main__':
    main()
