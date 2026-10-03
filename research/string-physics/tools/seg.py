# Note segmentation for Iowa MIS files (notes separated by silence).
import numpy as np, soundfile as sf
def notes(path, thr_db=-45, min_gap=0.25, min_len=0.4):
    x, sr = sf.read(path)
    if x.ndim > 1: x = x.mean(1)
    hop = int(0.01 * sr)
    e = np.array([np.sqrt(np.mean(x[i:i+hop]**2) + 1e-20) for i in range(0, len(x)-hop, hop)])
    db = 20*np.log10(e/e.max())
    on = db > thr_db
    segs, i = [], 0
    n = len(on)
    while i < n:
        if on[i]:
            j = i
            gap = 0
            while j < n and (on[j] or gap < min_gap/0.01):
                gap = 0 if on[j] else gap+1
                j += 1
            j -= gap
            if (j-i)*0.01 >= min_len: segs.append((i*hop, j*hop))
            i = j+1
        else: i += 1
    return x, sr, segs
if __name__ == '__main__':
    import sys, glob
    for f in sorted(glob.glob(sys.argv[1]+'/*.wav')):
        x, sr, s = notes(f)
        print(f.split('/')[-1], len(s), [(round(a/sr,2), round((b-a)/sr,2)) for a,b in s[:3]])
