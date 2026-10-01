"""Note-by-note, stroke-by-stroke comparison of a render with the real recording it copies.

    python3 notecompare.py real.flac render.dry.wav played.mid [player.log] --out notes.json [--upto s] [--realmid t.mid] [--refs r o] [--played p.json]

--realmid: the transcription the real recording is measured against, when played.mid has had its
notes moved (fitnotes.py); both files must hold the same notes in the same order.
--played: our note times as JSON ([{on, off, pitch}], fitnotes.py), for when played.mid joins
notes that the transcription has apart.
--refs: fixed levels (dB) to measure the real and our recording against, instead of each one's
98th-percentile level, so a short render reads the same levels as the whole piece. The levels
used are saved in the JSON as refs.

The MIDI is what the render played: the transcription of the real recording, so each MIDI note
is one real note. For every note, in both recordings:
  speaks    when the written pitch is first held (within 50 cents for 15 ms), searched from 60 ms
            before the note to 500 ms into it
  cents     median pitch error over the note's held part (real: against the player's own A,
            ours: against A = 440), in tune = share of held frames within 20 cents
  wrong     share of the note's sounding time on another pitch (glitches, octave slips)
  peak      loudest point, dB re the file's loud level (98th percentile)
  attack    time from speaking to the peak; end = level at the note's end re the peak
  gap       the deepest drop inside the note (between its first 40 ms and last 30 ms) under
            the louder sides around it: a hole in the middle of a note
  sep       level dip at the change into this note re the quieter of the two notes' peaks:
            how distinct the stroke or finger change is
  rough     share of loud held frames that are aperiodic (YIN aperiodicity > 0.25):
            scratch, crunch, multiple slips
  stroke    real: a new bow stroke if the change dips 5 dB or more, or the sound stops,
            otherwise a slur; ours: from the player log (stroke or slur), or the same rule
Then each note gets issues in plain words, a 0-100 score and the curves for the page.
"""
import json, re, sys
import numpy as np
import mido

sys.path.insert(0, '/mnt/project-files/research/world-class/references/tool')
import violinscore as vs

FS = vs.FS
HOP = vs.HOP
FR = vs.FR
W = 2048  # YIN window; its frames are centred W/2 after their index time


def analyse(path, upto, ref=None):
    x = vs.load(path)
    if upto:
        x = x[:int(upto * FS)]
    f0, ap, _ = vs.yin(x)
    # pitch frames are centred W/2 later: shift so index i means time i/FR
    sh = int(round(W / 2 / HOP))
    f0 = np.concatenate([np.zeros(sh), f0])
    ap = np.concatenate([np.ones(sh), ap])
    # level: 10 ms RMS every 5 ms, centred
    n = len(x) // HOP
    lv = np.full(n, -120.0)
    for i in range(n):
        a, b = max(0, i * HOP - 220), min(len(x), i * HOP + 220)
        lv[i] = 10 * np.log10(np.mean(x[a:b] ** 2) + 1e-14)
    m = min(len(f0), n)
    ref = float(np.percentile(lv[:m], 98)) if ref is None else ref
    lv = lv[:m] - ref
    return dict(f0=f0[:m], ap=ap[:m], lv=np.maximum(lv, -60.0), ref=ref)


def read_notes(mid):
    t, on, out = 0.0, {}, []
    for msg in mido.MidiFile(mid):
        t += msg.time
        if msg.type == 'note_on' and msg.velocity > 0:
            on[msg.note] = (t, msg.velocity)
        elif msg.type in ('note_off', 'note_on') and msg.note in on:
            a, v = on.pop(msg.note)
            out.append(dict(on=a, off=t, pitch=msg.note, vel=v))
    out.sort(key=lambda k: (k['on'], k['pitch']))
    # double stops: notes starting together; keep both, mark the upper one
    for i, k in enumerate(out):
        k['chord'] = any(abs(o['on'] - k['on']) < 0.02 and o is not k for o in out[max(0, i - 2):i + 3])
    return out


def read_log(path):
    ev = []
    if not path:
        return ev
    for l in open(path):
        m = re.match(r'on ([\d.]+) p(\d+) s(\d) (\w+)', l)
        if m:
            ev.append(dict(t=float(m.group(1)), pitch=int(m.group(2)), string='GDAE'[int(m.group(3))], kind=m.group(4), cap=None))
        m = re.match(r'capture ([\d.]+) s at ([\d.]+)', l)
        if m and ev:
            c, at = float(m.group(1)), float(m.group(2))
            for e in reversed(ev):
                if e['cap'] is None and abs((at - c) - e['t']) < 0.06:
                    e['cap'] = c
                    break
    return ev


def cents_of(f0, pitch, a4):
    return np.where(f0 > 0, 1200 * np.log2(np.maximum(f0, 1) / (a4 * 2 ** ((pitch - 69) / 12))), np.nan)


def measure(A, k, nxt_on, prev, a4):
    f0, ap, lv = A['f0'], A['ap'], A['lv']
    n = len(lv)
    i_on, i_end = int(k['on'] * FR), min(n - 1, int(min(k['off'], nxt_on) * FR))
    c = cents_of(f0, k['pitch'], a4)
    near = np.abs(c) < 50
    # speaks: first 3 frames in a row on the pitch, from 60 ms before to 150 ms after the start
    speak = None
    for i in range(max(0, i_on - 12), min(n - 3, max(i_on + 30, min(i_end - 2, i_on + 100)))):
        if near[i] and near[i + 1] and near[i + 2]:
            speak = i
            break
    r = dict(speak=None if speak is None else round(speak / FR, 3))
    lo = speak if speak is not None else i_on
    hi = max(lo + 2, i_end)
    seg = slice(lo, hi)
    held = near[seg]
    snd = lv[seg] > -40
    r['cents'] = None if held.sum() < 2 else round(float(np.nanmedian(c[seg][held])), 1)
    r['intune'] = None if held.sum() < 2 else round(float(np.mean(np.abs(c[seg][held]) < 20)), 2)
    voiced = (f0[seg] > 0) & snd
    r['wrong'] = round(float(np.mean(voiced & ~near[seg])) if len(voiced) else 0.0, 2)
    seg2 = slice(i_on, hi)
    off = (f0[seg2] > 0) & ~near[seg2] & (lv[seg2] > -40)
    r['wrongBy'] = round(float(np.nanmedian(c[seg2][off])) / 100, 1) if off.sum() >= 3 else None
    # level shape
    # the peak is looked for from the note's start (or, once it speaks, from there), so a louder
    # note ringing before it does not count
    a = max(0, i_on - 4, (speak or 0) - 2)
    w = lv[a:hi]
    pk = int(np.argmax(w)) + a
    r['peak'] = round(float(lv[pk]), 1)
    r['attack'] = None if speak is None else round(max(0, pk - speak) / FR * 1000)
    r['end'] = round(float(lv[max(a, hi - 2)] - lv[pk]), 1)
    # holes inside the note: how far each point sits under the louder of the 120 ms on either side
    g0, g1 = lo + 8, hi - 6
    r['gap'], r['gapAt'], r['holes'] = 0.0, None, []
    if g1 - g0 > 4:
        hl = []
        for i in range(g0, g1):
            hl.append(min(lv[max(lo, i - 24):i].max(), lv[i + 1:min(hi, i + 25)].max()) - lv[i])
        hl = np.maximum(np.array(hl), 0)
        r['holes'] = [round(float(v), 1) for v in hl]
        r['holeFrom'] = round(g0 / FR - k['on'], 3)
        j = int(np.argmax(hl))
        r['gap'], r['gapAt'] = round(float(hl[j]), 1), round((g0 + j) / FR - k['on'], 3)
    # level at 25, 50, 75 and 100% of the held part, re the peak
    # (each the median of 25 ms around the point, so one frame's flicker does not count)
    at = lambda j: float(np.median(lv[max(lo, j - 2):min(hi, j + 3)])) if hi > lo else float(lv[j])
    r['contour'] = [round(at(min(n - 1, lo + int(q * (hi - lo)) - (1 if q == 1 else 0))) - float(lv[pk]), 1) for q in (0.25, 0.5, 0.75, 1.0)]
    # separation from the previous note
    r['sep'] = None
    if prev is not None and k['on'] - prev['off'] < 0.4:
        p0 = int(prev['on'] * FR)
        b0 = max(p0 + 1, min(i_on, int(prev['off'] * FR)) - 6)
        b1 = min(n - 1, i_on + 10)
        valley = lv[b0:b1].min() if b1 > b0 else lv[i_on]
        r['sep'] = round(float(min(lv[p0:i_on + 1].max() if i_on > p0 else lv[p0], lv[pk]) - valley), 1)
    loud = snd & (lv[seg] > r['peak'] - 15)
    r['rough'] = round(float(np.mean(ap[seg][loud] > 0.25)) if loud.sum() else 0.0, 2)
    return r


def stroke_of(m, k, prev):
    if prev is None or k['on'] - prev['off'] > 0.06:
        return 'stroke'
    return 'stroke' if (m['sep'] or 0) >= 5 else 'slur'


def diagnose(k, R, O, ours_kind, real_kind, log, midi_slur=False):
    iss = []
    sev = 0.0

    def add(s, w):
        nonlocal sev
        iss.append(s)
        sev += w
    if k['pitch'] < 55:
        add('Below the violin\'s lowest note (G3): almost certainly a transcription octave error, the real note is probably an octave higher.', 30)
    if O['speak'] is None:
        wb = O.get('wrongBy')
        what = ''
        if wb is not None:
            what = ' It sounds %s instead%s.' % ('an octave lower' if abs(wb + 12) < 0.6 else 'an octave higher' if abs(wb - 12) < 0.6 else '%+.1f semitones off' % wb,
                                              ' (probably the previous string still ringing over it)' if wb < -0.6 else '')
        add('Never settles on the written pitch.' + what, 60)
    elif R['speak'] is not None:
        dt = (O['speak'] - R['speak']) * 1000
        if abs(dt) > 30:
            add('Speaks %d ms %s than the real player.' % (abs(dt), 'later' if dt > 0 else 'earlier'), min(40, abs(dt) / 3))
    if O['cents'] is not None and abs(O['cents']) > 12:
        add('Out of tune: %+.0f cents (real %s).' % (O['cents'], 'n/a' if R['cents'] is None else '%+.0f' % R['cents']), min(40, abs(O['cents'])))
    if O['intune'] is not None and O['intune'] < 0.6 and (R['intune'] or 0) - O['intune'] > 0.15:
        add('Pitch wanders: only %d%% of the note within 20 cents (real %d%%).' % (100 * O['intune'], 100 * (R['intune'] or 0)), 15)
    if O['wrong'] - R['wrong'] > 0.15 and O['speak'] is not None:
        wb = O.get('wrongBy')
        add('%d%% of the note sounds on another pitch (real %d%%)%s.' % (100 * O['wrong'], 100 * R['wrong'], '' if wb is None else ', mostly %+.1f semitones' % wb), 40 * O['wrong'])
    if O['gap'] >= 5:
        # the real player's dip around the same moment
        rd = 0.0
        if R.get('holes') and R.get('holeFrom') is not None:
            j = int(round((O['gapAt'] - R['holeFrom']) * FR))
            rd = max(R['holes'][max(0, j - 8):j + 9] or [0.0])
        if O['gap'] - rd > 4:
            add('Volume hole inside the note: drops %.0f dB at +%d ms, the real player %.0f dB there.' % (O['gap'], 1000 * (O['gapAt'] or 0), rd), 20)
    if real_kind == 'stroke' and ours_kind == 'slur':
        add('Blends into the previous note: the real player changes bow here (dip %.0f dB), ours slurs (%s dB). Cause: %s.' % (R['sep'] or 0, '%.0f' % O['sep'] if O['sep'] is not None else 'n/a',
            'the transcription marked a slur here' if midi_slur else 'the player slurred notes the MIDI separates'), 20)
    elif real_kind == 'slur' and ours_kind == 'stroke':
        add('Separate bow stroke where the real player slurs.', 8)
    if R['sep'] is not None and O['sep'] is not None and real_kind == 'stroke' and R['sep'] - O['sep'] > 6:
        add('Not distinct enough: the change into it dips %.0f dB, real %.0f dB.' % (O['sep'], R['sep']), 10)
    if O['rough'] - R['rough'] > 0.2:
        add('Rough or scratchy: %d%% of its loud part is noisy (real %d%%).' % (100 * O['rough'], 100 * R['rough']), 25 * O['rough'])
    dpk = O['peak'] - R['peak']
    if abs(dpk) > 6:
        add('%s than the real note by %.0f dB.' % ('Louder' if dpk > 0 else 'Softer', abs(dpk)), 8)
    if O['end'] - R['end'] > 10:
        add('Holds its volume to the end (%.0f dB under its peak); the real note fades to %.0f dB.' % (-O['end'], -R['end']), 10)
    elif R['end'] - O['end'] > 10:
        add('Dies away too early: ends %.0f dB under its peak, the real note %.0f dB.' % (-O['end'], -R['end']), 10)
    if log and log.get('cap') is not None and log['cap'] > 0.05:
        add('Took %d ms to reach a clean (Helmholtz) tone.' % (1000 * log['cap']), min(20, 200 * (log['cap'] - 0.05)))
    if R['speak'] is None:
        iss.append('Real note: pitch not clearly held (fast or chordal), timing and tuning checks are rough here.')
    return iss, max(0, round(100 - sev))


def curves(A, a, b, pitch, a4):
    i0, i1 = max(0, int(a * FR)), min(len(A['lv']), int(b * FR))
    c = cents_of(A['f0'][i0:i1], pitch, a4)
    c = np.where(np.abs(c) > 300, np.sign(c) * 300, c)
    return dict(c=[None if np.isnan(v) else round(float(v)) for v in c[::2]], l=[round(float(v), 1) for v in A['lv'][i0:i1:2]])


def main():
    args = [a for a in sys.argv[1:]]
    out = args[args.index('--out') + 1] if '--out' in args else 'notes.json'
    upto = float(args[args.index('--upto') + 1]) if '--upto' in args else None
    pos = [a for i, a in enumerate(args) if not a.startswith('--') and (i == 0 or not args[i - 1].startswith('--'))
           and (i < 2 or args[i - 2] != '--refs')]
    real, ours, mid = pos[:3]
    logp = pos[3] if len(pos) > 3 else None
    refs = [float(v) for v in args[args.index('--refs') + 1:args.index('--refs') + 3]] if '--refs' in args else [None, None]
    R, O = analyse(real, upto, refs[0]), analyse(ours, upto, refs[1])
    v = R['f0'] > 0
    a4r = 440 * 2 ** (np.median(vs.midi_of(R['f0'][v]) - np.round(vs.midi_of(R['f0'][v]))) / 12)
    notes = [k for k in read_notes(mid) if upto is None or k['on'] < upto - 0.1]
    if '--played' in args:
        notes = [dict(k, chord=False) for k in json.load(open(args[args.index('--played') + 1]))]
        for i in range(1, len(notes)):
            if abs(notes[i]['on'] - notes[i - 1]['on']) < 0.02:
                notes[i]['chord'] = notes[i - 1]['chord'] = True
        notes = [k for k in notes if upto is None or k['on'] < upto - 0.1]
    rnotes = notes
    if '--realmid' in args:
        rnotes = read_notes(args[args.index('--realmid') + 1])[:len(notes)]
        assert len(rnotes) == len(notes) and all(a['pitch'] == b['pitch'] for a, b in zip(rnotes, notes))
    log = read_log(logp)
    res = []
    for i, (k, ko) in enumerate(zip(rnotes, notes)):
        nxt = next((o['on'] for o in rnotes[i + 1:] if o['on'] > k['on'] + 0.02), k['off'] + 0.3)
        prev = next((o for o in reversed(rnotes[:i]) if o['on'] < k['on'] - 0.02), None)
        nxto = next((o['on'] for o in notes[i + 1:] if o['on'] > ko['on'] + 0.02), ko['off'] + 0.3)
        prevo = next((o for o in reversed(notes[:i]) if o['on'] < ko['on'] - 0.02), None)
        mr = measure(R, k, nxt, prev, a4r)
        mo = measure(O, ko, nxto, prevo, 440.0)
        lg = min(log, key=lambda e: abs(e['t'] - ko['on']) + (0 if e['pitch'] == ko['pitch'] else 1)) if log else None
        if lg is not None and abs(lg['t'] - ko['on']) > 0.03:
            lg = None
        rk = stroke_of(mr, k, prev)
        midi_slur = prevo is not None and prevo['off'] >= ko['on'] - 0.001
        ok = lg['kind'] if lg else stroke_of(mo, ko, prevo)
        iss, score = diagnose(k, mr, mo, ok, rk, lg, midi_slur)
        a, b = k['on'] - 0.15, min(k['off'], nxt) + 0.15
        res.append(dict(i=i, on=round(k['on'], 3), off=round(k['off'], 3), pitch=k['pitch'], vel=k['vel'], chord=k['chord'],
                        name='C C# D D# E F F# G G# A A# B'.split()[k['pitch'] % 12] + str(k['pitch'] // 12 - 1),
                        real=dict(mr, stroke=rk), ours=dict(mo, stroke=ok, string=lg['string'] if lg else None,
                                                              capture=None if not lg or lg['cap'] is None else round(1000 * lg['cap'])),
                        issues=iss, score=score, win=[round(a, 3), round(b, 3)],
                        rc=curves(R, a, b, k['pitch'], a4r), oc=curves(O, a, b, k['pitch'], 440.0)))
    # bow strokes: number them in both recordings (a chord's notes share one)
    for who in ('real', 'ours'):
        n_ = 0
        for j, r in enumerate(res):
            if j and r['chord'] and abs(r['on'] - res[j - 1]['on']) < 0.02:
                r[who]['strokeNo'] = res[j - 1][who]['strokeNo']
                continue
            if r[who]['stroke'] == 'stroke' or j == 0:
                n_ += 1
            r[who]['strokeNo'] = n_
    json.dump(dict(a4real=round(a4r, 1), refs=[round(R['ref'], 2), round(O['ref'], 2)], hop=2 / FR, notes=res), open(out, 'w'))
    sc = np.array([r['score'] for r in res])
    print('%d notes, mean score %.0f, %d with issues, %d below 60' % (len(res), sc.mean(), sum(1 for r in res if r['issues']), (sc < 60).sum()))
    kinds = {}
    for r in res:
        for s in r['issues']:
            key = s.split(':')[0].split(' by ')[0][:40]
            kinds[key] = kinds.get(key, 0) + 1
    for k_, v_ in sorted(kinds.items(), key=lambda kv: -kv[1])[:14]:
        print('  %3d  %s' % (v_, k_))


if __name__ == '__main__':
    main()
