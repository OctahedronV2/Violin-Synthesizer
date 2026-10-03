import sys, numpy as np, mido
sys.path.insert(0,'/mnt/project-files/research/world-class/references/tool')
import violinscore as vs
from scipy.ndimage import median_filter
R='/mnt/project-files/research/world-class/references/corpus/phrases/anechoic-haydn/Mov4_Violin2_mic5-front-1m.flac'
def tr(p):
    x=vs.load(p); f0,ap,rms=vs.yin(x); return f0,median_filter(rms,size=3)
a,b=float(sys.argv[2]),float(sys.argv[3])
fr,rr=tr(R); fo,ro=tr(sys.argv[1])
FR=vs.FR
t=0;on={};ns=[]
for m in mido.MidiFile(sys.argv[4] if len(sys.argv)>4 else 'haydn_Mov4_Violin2.mid'):
    t+=m.time
    if m.type=='note_on' and m.velocity>0: on[m.note]=(t,m.velocity)
    elif m.type in('note_off','note_on') and m.note in on: s,v=on.pop(m.note); ns.append((s,t,m.note,v))
ns.sort()
def mm(f): return np.where(f>0,vs.midi_of(np.maximum(f,1),440.0),0)
mr,mo=mm(fr),mm(fo)
lr=rr.max(); lo=ro.max()
for i in range(int(a*FR),int(b*FR),2):
    tt=i/FR; cur=[n[2] for n in ns if n[0]<=tt<n[1]]
    print('%6.2f mid %-8s real %5.1f %5.1f  ours %5.1f %5.1f %s'%(tt,cur,mr[i],rr[i]-np.percentile(rr,98),mo[i],ro[i]-np.percentile(ro,98),'' if abs(mr[i]-mo[i])<.5 else 'X'))
