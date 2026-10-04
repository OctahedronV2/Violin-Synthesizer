# mk.py out.mid "pitch,start,dur,vel; ..."   (seconds)
import sys, mido
mf = mido.MidiFile(ticks_per_beat=480)
tr = mido.MidiTrack()
mf.tracks.append(tr)
tr.append(mido.MetaMessage('set_tempo', tempo=500000))
evs = []
for item in sys.argv[2].split(';'):
    item = item.strip()
    if not item:
        continue
    p, t0, d, v = item.split(',')
    evs.append((float(t0), 1, int(p), int(v)))
    evs.append((float(t0) + float(d), 0, int(p), 0))
evs.sort(key=lambda e: (e[0], e[1]))
last = 0.0
for t, on, p, v in evs:
    dt = int(round((t - last) * 960))
    last += dt / 960
    tr.append(mido.Message('note_on' if on else 'note_off', note=p, velocity=v, time=dt))
mf.save(sys.argv[1])
