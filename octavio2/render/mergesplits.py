"""Join notes the transcription split but the player did not: the same pitch again within 50 ms
with no real dip between them (under 5 dB on a 10 ms level), so nothing is heard there.

    python3 mergesplits.py real.flac in.mid out.mid
"""
import sys
import mido
import notecompare as nc

FR = 200  # level frames per second (notecompare's 5 ms hop)


def main():
    real, src, out = sys.argv[1:4]
    lv = nc.analyse(real, None)['lv']
    N = nc.read_notes(src)
    keep = []
    for k in N:
        p = next((q for q in reversed(keep) if q['pitch'] == k['pitch']), None)
        if p is not None and not k['chord'] and not p['chord'] and 0 <= k['on'] - p['off'] < 0.05 and keep[-1] is p:
            j = int(k['on'] * FR)
            dip = min(lv[max(0, j - 20):j - 6].max(), lv[j + 6:j + 20].max()) - lv[j - 6:j + 6].min()
            if dip < 5.0:
                print('joined %.2f s + %.2f s (pitch %d, dip %.1f dB)' % (p['on'], k['on'], k['pitch'], dip))
                p['off'] = k['off']
                continue
        keep.append(dict(k))
    ev = sorted([(k['on'], 1, k['pitch'], k['vel']) for k in keep] + [(k['off'], 0, k['pitch'], 0) for k in keep])
    mf = mido.MidiFile(ticks_per_beat=480)
    tr = mido.MidiTrack()
    mf.tracks.append(tr)
    tr.append(mido.MetaMessage('set_tempo', tempo=500000))
    t = 0.0
    for tt, on, p, v in ev:
        d = int(round((tt - t) * 960))
        t += d / 960
        tr.append(mido.Message('note_on' if on else 'note_off', note=p, velocity=v, time=d))
    mf.save(out)
    print('%d notes -> %d' % (len(N), len(keep)))


if __name__ == '__main__':
    main()
