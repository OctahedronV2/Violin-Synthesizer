# Schelleng-diagram limits from the lab: for each beta the Helmholtz force range,
# fitted to Schelleng's forms Fmax = c_upper / beta and Fmin = c_lower / beta^2.
import subprocess, re, sys, numpy as np, os
LAB = os.environ.get('LAB', '/tmp/lab')
def schelleng(s, midi, speed, extra, nf=16, fmin=0.01, fmax=5.0):
    out = subprocess.run([LAB, 'schelleng', s, str(midi), str(speed), f'nf={nf}', f'fmin={fmin}', f'fmax={fmax}'] + extra, capture_output=True, text=True).stdout.splitlines()
    forces = [float(v) for v in out[0].split(':')[1].split()]
    rows = []
    for line in out[1:-1]:
        b, cells = line.split(':'); rows.append((float(b), cells.split()))
    return forces, rows, int(re.search(r'helmholtz (\d+)', out[-1]).group(1))
def limits(forces, rows):
    up, lo = [], []
    for b, cells in rows:
        H = [i for i, c in enumerate(cells) if c == 'H']
        if not H: continue
        # longest contiguous run of H
        runs, cur = [], [H[0]]
        for i in H[1:]:
            if i == cur[-1]+1: cur.append(i)
            else: runs.append(cur); cur = [i]
        runs.append(cur); r = max(runs, key=len)
        if r[-1] < len(forces)-1: up.append((b, np.sqrt(forces[r[-1]]*forces[r[-1]+1])))
        if r[0] > 0: lo.append((b, np.sqrt(forces[r[0]]*forces[r[0]-1])))
    cu = np.exp(np.mean([np.log(F*b) for b, F in up])) if up else np.nan
    cl = np.exp(np.mean([np.log(F*b*b) for b, F in lo])) if lo else np.nan
    return cu, cl
if __name__ == '__main__':
    s, midi = sys.argv[1], sys.argv[2]; extra = sys.argv[3:]
    # SGA08 (steel D, Z0 0.25): c_upper ~0.75 kg/s (Fmax = c_upper vB / beta),
    # c_lower 7.0/4.2/2.3/1.8 g/s at 5/10/15/20 cm/s (Fmin = c_lower vB / beta^2) -> Fmin ~ 0.04 N at beta 0.1
    from concurrent.futures import ThreadPoolExecutor
    speeds = [0.05, 0.1, 0.2]
    with ThreadPoolExecutor(4) as ex:
        res = list(ex.map(lambda v: schelleng(s, midi, v, extra), speeds))
    tot = 0
    for v, (f, rows, h) in zip(speeds, res):
        cu, cl = limits(f, rows); tot += h
        print(f'v {v:4.2f}  Helmholtz {h:3d}/128  c_upper {cu/v:5.2f} kg/s  c_lower {cl/v*1000:6.1f} g/s  Fmax(b=.1) {cu/0.1:5.2f} N  Fmin(b=.1) {cl/0.01:6.3f} N')
    print('TOTAL helmholtz', tot)
