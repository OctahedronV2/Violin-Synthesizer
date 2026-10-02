"""Tune a performance note by note until each note matches the real recording.

    python3 fitnotes.py state.json --init played.mid          start from a transcription
    python3 fitnotes.py state.json --notes A B [--iters 3]    tune notes A..B-1 (0-based)

Each note carries what a player (or a score editor) would adjust for it: when it starts, how hard
it is played (velocity), its intonation (CC21, cents) and its level contour (CC11 expression at
its start, 25, 50, 75 and 100% of its length). Every iteration renders the piece, measures every
note with notecompare.py and moves each tuned note's settings towards the real note:
  start     by the difference in when the two notes speak
  velocity  by the difference in peak level (2.5 velocity per dB)
  cents     by our tuning error (aiming at the written pitch)
  contour   by the difference in absolute level at each contour point
  slur      changed to a new bow stroke or a slur where the real player does the other; a
            repeated pitch the real player plays in one stroke is tied
  gap       a silence before a new stroke, when ours is less distinct than the real one
  bite      more force at the start (CC23) when our note is slow to catch a clean tone
  press     bow pressure (CC22): less when ours is scratchy, more when it is very slow to catch
  vib       vibrato width (CC24), scaled towards the real note's average width
A note that scores worse than its best so far goes back to its best settings and takes half
steps from there (a line search per note); at the end every note keeps its best settings and the
result is rendered once more to confirm. The real recording is always measured against the
original transcription times, so moving our note never moves the target.
The renderer and the comparison run as in audit.sh; the state file keeps the settings, the
latest measurements and the MIDI path. Notes outside A..B keep their settings, so earlier
notes stay as tuned while later ones are worked on.
"""
import json, os, subprocess, sys
import mido

R = os.path.dirname(os.path.abspath(__file__))
REAL = '/mnt/project-files/research/world-class/references/corpus/phrases/anechoic-haydn/Mov4_Violin2_mic5-front-1m.flac'
WORK = '/tmp/o2r/x'


def read(mid):
    t, on, out = 0.0, {}, []
    for m in mido.MidiFile(mid):
        t += m.time
        if m.type == 'note_on' and m.velocity > 0:
            on[m.note] = (t, m.velocity)
        elif m.type in ('note_off', 'note_on') and m.note in on:
            a, v = on.pop(m.note)
            out.append(dict(on=a, off=t, pitch=m.note, vel=v))
    out.sort(key=lambda k: (k['on'], k['pitch']))
    return out


def played(S):
    """Our note times: start moved by dOn; a slurred note keeps an overlap into the next, a new
    stroke ends gap seconds before the next note, and a tied note joins the previous one."""
    N = S['notes']
    out = []
    for i, n in enumerate(N):
        on = n['on'] + n['dOn']
        off = n['off']
        nx = N[i + 1] if i + 1 < len(N) else None
        if nx is not None and nx['slur']:  # keep the overlap that makes the slur
            off = max(off + n['dOn'], nx['on'] + nx['dOn'] + 0.01)
        elif nx is not None and nx['on'] > n['on'] + 0.02:  # a new bow stroke: no overlap
            off = min(off, nx['on'] + nx['dOn'] - 0.001 - nx['gap'])
        later = [m['on'] + m['dOn'] for m in N[i + 1:] if m['pitch'] == n['pitch'] and not m['tie']]
        if later:  # a repeated pitch must end before it sounds again
            off = min(off, min(later) - 0.001)
        out.append(dict(on=on, off=max(off, on + 0.02), pitch=n['pitch'], vel=n['vel'] + n['dVel']))
    for i in range(len(N) - 1, 0, -1):  # a tied note's sound belongs to the note before
        if N[i]['tie']:
            out[i - 1]['off'] = max(out[i - 1]['off'], out[i]['off'])
    return out


def write(S, out):
    N = S['notes']
    P = played(S)
    json.dump([dict(on=p['on'], off=p['off'], pitch=p['pitch'], vel=int(p['vel'])) for p in P], open(out + '.json', 'w'))
    ev = []
    for n, p in zip(N, P):
        on = p['on']
        vel = int(max(1, min(127, round(p['vel']))))
        if not n['tie']:
            ev.append((on - 0.0005, 2, 21, int(max(0, min(127, round(64 + n['cents']))))))
            ev.append((on - 0.0005, 2, 22, int(max(0, min(127, round(64 + 64 * n['press'] / 0.3))))))
            ev.append((on - 0.0005, 2, 23, int(max(64, min(127, round(64 + 64 * n['bite']))))))
            ev.append((on - 0.0005, 2, 24, int(max(0, min(127, round(64 * n['vib'] ** 0.5))))))
        # the level curve: straight lines between its five points, sent every 10 ms
        L = max(0.0, n['off'] - on)
        steps = max(1, int(L / 0.01))
        for j in range(steps + 1):
            q = 4.0 * j / steps
            k = min(3, int(q))
            e = n['contour'][k] + (q - k) * (n['contour'][k + 1] - n['contour'][k])
            ev.append((on + j * L / steps - (0.001 if j == steps else 0), 2, 11, int(max(0, min(127, round(100 + e / 0.4))))))
        if not n['tie']:
            ev.append((on, 1, n['pitch'], vel))
            ev.append((p['off'], 0, n['pitch'], 0))
    ev.sort(key=lambda e: (e[0], {0: 0, 2: 1, 1: 2}[e[1]]))
    mf = mido.MidiFile(ticks_per_beat=480)
    tr = mido.MidiTrack()
    mf.tracks.append(tr)
    tr.append(mido.MetaMessage('set_tempo', tempo=500000))
    t = 0.0
    for tt, kind, a, b in ev:
        d = max(0, int(round((max(0.0, tt) - t) * 960)))
        t += d / 960
        if kind == 2:
            tr.append(mido.Message('control_change', control=a, value=b, time=d))
        else:
            tr.append(mido.Message('note_on' if kind == 1 else 'note_off', note=a, velocity=b, time=d))
    mf.save(out)


def measure(S, tag, opts, upto=54.0):
    mid = '%s/fit_%s.mid' % (WORK, tag)
    write(S, mid)
    f = '%s/fit_%s' % (WORK, tag)
    subprocess.run('/tmp/o2 %s %s.f.wav log=1 length=%.2f %s 2>%s.log >/dev/null' % (mid, f, upto, ' '.join(opts), f), shell=True, check=True)
    # the first full render sets the level references every later (shorter) render is measured on
    fixed = 'gain' in S
    out = subprocess.run(['python3', R + '/finish.py', f + '.f.wav', f, '--hall', 'none'] + (['--gain', str(S['gain'])] if fixed else []),
                         check=True, capture_output=True, text=True).stdout
    if not fixed:
        S['gain'] = float(out.split()[-1])
    subprocess.run(['python3', R + '/notecompare.py', REAL, f + '.dry.wav', mid, f + '.log', '--out', f + '.json', '--realmid', S['src'], '--played', mid + '.json']
                   + (['--upto', '%.2f' % upto] if upto < 54 else []) + (['--refs'] + [str(v) for v in S['refs']] if fixed else []),
                   check=True, stdout=subprocess.DEVNULL)
    J = json.load(open(f + '.json'))
    if not fixed:
        S['refs'] = J['refs']
    return J['notes'], mid


ONLY = None  # --only vib,cents: tune just these settings
SKIP = None  # --skip N: notes scoring N or more when their group starts are not changed
KEYS = ('dOn', 'dVel', 'cents', 'contour', 'slur', 'tie', 'gap', 'press', 'bite', 'vib')


def step(S, i, m, g):
    """Move note i's settings towards the real note by gain g."""
    n = S['notes'][i]
    R_, O_ = m['real'], m['ours']
    if R_['speak'] is not None and O_['speak'] is not None:
        d = O_['speak'] - R_['speak']
        p = S['notes'][i - 1] if i else None
        if p is not None and abs(p['on'] - n['on']) < 0.02:
            n['dOn'] = p['dOn']  # a chord's notes move together
        else:
            lo = p['on'] + p['dOn'] + 0.03 - n['on'] if p else -n['on']
            n['dOn'] = max(lo, min(0.08, n['dOn'] - 0.7 * g * d))
    # bow stroke or slur, as the real player does it; a repeated pitch the real player joins is tied
    p = S['notes'][i - 1] if i else None
    if p is not None and n['on'] - p['on'] > 0.02 and R_['stroke'] != O_['stroke']:
        if p['pitch'] != n['pitch']:
            n['slur'] = R_['stroke'] == 'slur'
        else:
            n['tie'] = R_['stroke'] == 'slur'
    # a new stroke not distinct enough: end the note before it earlier
    if R_['stroke'] == 'stroke' and R_['sep'] is not None and O_['sep'] is not None and abs(R_['sep'] - O_['sep']) > 3:
        n['gap'] = max(0.0, min(0.12, n['gap'] + g * 0.004 * (R_['sep'] - O_['sep'])))
    # a slow catch: more force at the start of the stroke; a scratchy note: less pressure
    cap = O_.get('capture')
    if cap is not None and cap > 50:
        n['bite'] = min(1.0, n['bite'] + g * 0.004 * (cap - 40))
    elif cap is not None and cap < 30:
        n['bite'] *= 1 - 0.3 * g
    if (O_.get('rough') or 0) > (R_.get('rough') or 0) + 0.1:
        n['press'] = max(-0.3, n['press'] - g * 0.1)
    elif cap is not None and cap > 80:
        n['press'] = min(0.3, n['press'] + g * 0.05)
    # vibrato width: scale ours towards the real one's average width through the note
    if R_.get('vib') and O_.get('vib'):
        pts = [(x, y) for x, y in zip(R_['vib'], O_['vib']) if x is not None and y is not None]
        if pts:
            r = sum(x for x, _ in pts) / max(3.0, sum(y for _, y in pts))
            n['vib'] = max(0.0, min(3.9, max(0.05, n['vib']) * r ** (0.7 * g)))
    dpk = O_['peak'] - R_['peak']
    n['dVel'] = max(-60.0, min(60.0, n['dVel'] - 0.5 * g * 2.5 * dpk))
    if O_['cents'] is not None and not m['chord']:
        n['cents'] = max(-63.0, min(63.0, n['cents'] - 0.8 * g * O_['cents']))
    if R_.get('contour') and O_.get('contour'):
        e = [(O_['peak'] + O_['contour'][k]) - (R_['peak'] + R_['contour'][k]) for k in range(4)]
        # the start: when our note peaks right at the start and the real one later, the start
        # itself is too loud; otherwise it follows the first quarter
        early = O_['attack'] is not None and O_['attack'] < 60 and (R_['attack'] or 0) > 120
        e = [O_['peak'] - (R_['peak'] + R_['contour'][0]) if early else e[0]] + e
        for k in range(5):
            n['contour'][k] = max(-30.0, min(6.0, n['contour'][k] - 0.5 * g * e[k]))


def main():
    a = sys.argv
    path = a[1]
    opts = a[a.index('--opts') + 1].split() if '--opts' in a else []
    if '--init' in a:
        notes = read(a[a.index('--init') + 1])
        for i, n in enumerate(notes):
            n.update(dOn=0.0, dVel=0.0, cents=0.0, contour=[0.0] * 5, tie=False, gap=0.0, press=0.0, bite=0.0, vib=1.0,
                     slur=i > 0 and notes[i - 1]['off'] > n['on'] and n['on'] - notes[i - 1]['on'] > 0.02)
        json.dump(dict(notes=notes, src=os.path.abspath(a[a.index('--init') + 1])), open(path, 'w'))
        return
    S = json.load(open(path))
    for n in S['notes']:
        n.setdefault('tie', False)
        n.setdefault('gap', 0.0)
        n.setdefault('press', 0.0)
        n.setdefault('bite', 0.0)
        n.setdefault('vib', 1.0)
    A, B = int(a[a.index('--notes') + 1]), int(a[a.index('--notes') + 2])
    iters = int(a[a.index('--iters') + 1]) if '--iters' in a else 3
    group = int(a[a.index('--group') + 1]) if '--group' in a else 3
    global SKIP
    SKIP = int(a[a.index('--skip') + 1]) if '--skip' in a else None
    global ONLY
    ONLY = a[a.index('--only') + 1].split(',') if '--only' in a else None
    if 'gain' not in S:
        measure(S, 'full', opts)
    for g0 in range(A, B, group):
        fit(S, g0, min(B, g0 + group), iters, opts)
        json.dump(S, open(path, 'w'))
    if '--full' in a:
        meas, S['mid'] = measure(S, 'full', opts)
        print('piece score %.1f' % (sum(m['score'] for m in meas) / len(meas)))
        json.dump(S, open(path, 'w'))


def fit(S, A, B, iters, opts):
    """Tune notes A..B-1 with every earlier note frozen. The player only looks back, so rendering
    stops a little after the group: later notes cannot change these."""
    N = S['notes']
    C = min(len(N), B + 3)  # the next notes count too, so a group is never tuned at their cost
    upto = max(n['off'] for n in N[A:C]) + 0.7
    w = lambda m: sum(m[i]['score'] for i in range(A, C)) / (C - A)
    best = {}  # note -> (score, settings)
    gain = {i: 1.0 for i in range(A, B)}
    whole = None  # best whole iteration (score, settings)
    skip = set()
    for it in range(iters + 1):
        meas, mid = measure(S, 'g', opts, upto)
        if it == 0 and SKIP is not None:  # notes already good enough stay as they are
            skip = {i for i in range(A, B) if meas[i]['score'] >= SKIP}
            if len(skip) == B - A and not any(meas[i]['real'].get('vib') for i in range(A, B)):
                print('notes %d-%d: %s  (all %d+, left as they are)' % (A + 1, B, ' '.join('%d:%d' % (i + 1, meas[i]['score']) for i in range(A, B)), SKIP), flush=True)
                return
        if whole is None or w(meas) > whole[0]:
            whole = (w(meas), [json.loads(json.dumps({k: N[i][k] for k in KEYS})) for i in range(A, B)])
        for i in range(A, B):
            n = N[i]
            if i not in best or meas[i]['score'] > best[i][0]:
                best[i] = (meas[i]['score'], {k: json.loads(json.dumps(n[k])) for k in KEYS})
                gain[i] = min(1.0, gain[i] * 1.3)
            elif meas[i]['score'] < best[i][0] - 2:
                n.update(json.loads(json.dumps(best[i][1])))
                gain[i] *= 0.5
                continue
            if it < iters:
                only = ['vib'] if i in skip else ONLY  # a note already good still gets its vibrato matched
                keep = {k: json.loads(json.dumps(n[k])) for k in KEYS if only and k not in only}
                step(S, i, meas[i], gain[i])
                n.update(keep)  # --only: every other setting stays as it was
        if all(best[i][0] >= 97 for i in range(A, B)):
            break
    for i in range(A, B):
        N[i].update(best[i][1])
    meas, mid = measure(S, 'g', opts, upto)
    if w(meas) < whole[0]:
        for i in range(A, B):
            N[i].update(whole[1][i - A])
        meas, mid = measure(S, 'g', opts, upto)
    print('notes %d-%d: %s' % (A + 1, B, ' '.join('%d:%d' % (i + 1, meas[i]['score']) for i in range(A, B))) +
          '  (window %.0f)' % w(meas), flush=True)


if __name__ == '__main__':
    main()
