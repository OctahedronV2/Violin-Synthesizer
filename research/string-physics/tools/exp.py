# One experiment = one lab configuration, scored on playability (Schelleng vs SGA08, Guettler)
# and realism against the Iowa recordings. usage: python3 exp.py NAME [lab options...]
import sys, os, json, re, subprocess, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import maps, modelcmp
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LAB = os.environ.get('LAB', '/tmp/lab')
SGA = ROOT + '/results/strings_sga08.txt'
def guettler(extra, s='D', midi=62, beta=0.09):
    out = subprocess.run([LAB, 'guettler', s, str(midi), str(beta), 'nf=10', 'na=10', 'fmin=0.05', 'fmax=2.5', 'amin=0.2', 'amax=5', 'vmax=0.3'] + extra, capture_output=True, text=True).stdout
    m = re.search(r'perfect (\d+)\s+under-50ms (\d+)\s+of (\d+)', out)
    return out, int(m.group(1)), int(m.group(2))
def run(name, extra):
    t0 = time.time()
    res = {'name': name, 'opts': extra}
    # Schelleng against SGA08 (steel D string); the string file is replaced, other options kept
    ex2 = [o for o in extra if not o.startswith('strings=')] + ['strings=' + SGA]
    from concurrent.futures import ThreadPoolExecutor
    speeds = [0.05, 0.1, 0.2]
    with ThreadPoolExecutor(4) as ex:
        sch = list(ex.map(lambda v: maps.schelleng('D', 62, v, ex2), speeds))
        g = ex.submit(guettler, extra)
        res['schelleng'] = {}
        for v, (f, rows, h) in zip(speeds, sch):
            cu, cl = maps.limits(f, rows)
            res['schelleng'][v] = dict(helm=h, c_upper=cu/v, c_lower=cl/v*1000, fmin_b01=cl/0.01, fmax_b01=cu/0.1)
        gout, perf, ok = g.result()
    res['schelleng_map_v0.1'] = sch[1]
    res['guettler'] = dict(perfect=perf, under50=ok, map=gout)
    rows = modelcmp.run(name, extra)
    res['realism'] = modelcmp.summary(rows); res['realism_rows'] = rows
    b = subprocess.run([LAB, 'bench'] + extra, capture_output=True, text=True).stdout
    res['cpu_ms_per_s'] = float(b.split()[0])
    res['secs'] = time.time()-t0
    os.makedirs(ROOT + '/results/exp', exist_ok=True)
    json.dump(res, open(ROOT + f'/results/exp/{name}.json', 'w'), indent=1, default=float)
    S = res['schelleng']; R = res['realism']
    line = (f"| {name} | " + ' / '.join(f"{S[v]['c_lower']:.1f}" for v in speeds) + " | " + ' / '.join(f"{S[v]['c_upper']:.2f}" for v in speeds)
            + f" | {sum(S[v]['helm'] for v in speeds)} | {perf} / {ok} | {R['herr']:.1f} | {R['dnoise']:+.1f} | {R['shim']:.3f} | {R['jit']:.2f} | {R['rel']:.2f} | {R['helm']}/{R['n']} | {res['cpu_ms_per_s']:.0f} |")
    print(line)
    with open(ROOT + '/results/experiments.md', 'a') as f: f.write(line + '\n')
    return res
if __name__ == '__main__':
    if not os.path.exists(ROOT + '/results/experiments.md'):
        with open(ROOT + '/results/experiments.md', 'w') as f:
            f.write('Targets: c_lower 7.0 / 4.2 / 1.8 g/s at 5/10/20 cm/s (SGA08 steel D), c_upper ~0.75 kg/s; realism vs Iowa: shimmer 0.085 dB, jitter 2.1 cents (E inflates it), release T60 2.9 s.\n\n')
            f.write('| config | c_lower g/s (5/10/20 cm/s) | c_upper kg/s | Schelleng H cells /384 | Guettler perfect / <50 ms (of 100) | harm err dB | noise vs real dB | shimmer dB | jitter c | release T60 s | Iowa notes in Helmholtz | CPU ms/s |\n|---|---|---|---|---|---|---|---|---|---|---|---|\n')
    run(sys.argv[1], sys.argv[2:])
