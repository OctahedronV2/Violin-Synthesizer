"""Build the note audit page.

    python3 build.py notes.json out.html [--prev prev.json] [--history history.json --desc "what changed"]

notes.json comes from notecompare.py. --prev adds each note's previous score (matched by start
time and pitch); --history appends this step to a fix log shown on the page. Publish with
clips/finale-real.mp3 and clips/finale-ours.mp3 (level-matched, full length, same start)."""
import json, os, sys
a = sys.argv
t = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'template.html')).read()
d = json.load(open(a[1]))
if '--prev' in a and os.path.exists(a[a.index('--prev') + 1]):
    P = json.load(open(a[a.index('--prev') + 1]))['notes']
    for n in d['notes']:
        best = min(P, key=lambda p: abs(p['on'] - n['on']) + (0 if p['pitch'] == n['pitch'] else 1))
        n['prev'] = best['score'] if abs(best['on'] - n['on']) < 0.05 and best['pitch'] == n['pitch'] else None
if '--history' in a:
    hp = a[a.index('--history') + 1]
    H = json.load(open(hp)) if os.path.exists(hp) else []
    sc = [n['score'] for n in d['notes']]
    H.append(dict(desc=a[a.index('--desc') + 1] if '--desc' in a else '', mean=round(sum(sc) / len(sc)), under50=sum(s < 50 for s in sc),
                  strokes=d['notes'][-1]['ours']['strokeNo']))
    json.dump(H, open(hp, 'w'))
    d['history'] = H
open(a[2], 'w').write(t.replace('/*DATA*/null', json.dumps(d)))
