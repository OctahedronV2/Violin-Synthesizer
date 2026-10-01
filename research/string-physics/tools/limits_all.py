# Steady-state Schelleng limits (SGA08 sweep method) for every experiment config.
import json, glob, os, re, subprocess, sys
from concurrent.futures import ThreadPoolExecutor
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LAB = os.environ.get('LAB', '/tmp/lab3'); SGA = ROOT + '/results/strings_sga08.txt'
def lim(opts, v):
    o = [x for x in opts if not x.startswith('strings=')] + ['strings=' + SGA]
    out = subprocess.run([LAB, 'limits', 'D', '62', str(v)] + o, capture_output=True, text=True).stdout
    m = re.search(r'c_lower ([-\d.]+) g/s\s+c_upper ([-\d.]+)', out)
    return float(m.group(1)), float(m.group(2)), out
names = sys.argv[1:] or sorted(os.path.basename(f)[:-5] for f in glob.glob(ROOT + '/results/exp/*.json'))
rows = []
for n in names:
    f = ROOT + f'/results/exp/{n}.json'; d = json.load(open(f))
    with ThreadPoolExecutor(3) as ex:
        r = list(ex.map(lambda v: lim(d['opts'], v), [0.05, 0.1, 0.2]))
    d['limits'] = {str(v): dict(c_lower=a, c_upper=b, detail=o) for v, (a, b, o) in zip([0.05, 0.1, 0.2], r)}
    json.dump(d, open(f, 'w'), indent=1, default=float)
    line = f"| {n} | " + ' / '.join(f'{a:.1f}' for a, _, _ in r) + ' | ' + ' / '.join(f'{b:.2f}' for _, b, _ in r) + ' |'
    print(line); rows.append(line)
with open(ROOT + '/results/limits.md', 'a') as fo:
    fo.write('\n'.join(rows) + '\n')
