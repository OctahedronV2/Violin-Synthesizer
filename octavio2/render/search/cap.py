# capture time (to Helmholtz) of each note: quick-run notes vs the rest, from a log=1 render
import re,sys,numpy as np
L=open(sys.argv[1]).read().splitlines()
ons=[];caps=[]
for l in L:
    m=re.match(r'on ([\d.]+) p(\d+) s(\d) (\w+)(.*)',l)
    if m: ons.append([float(m.group(1)),m.group(4),'quick' in m.group(5),None])
    m=re.match(r'capture ([\d.]+) s at ([\d.]+)',l)
    if m: caps.append((float(m.group(2))-float(m.group(1)),float(m.group(1))))
for st,c in caps:
    best=min(ons,key=lambda o:abs(o[0]-st)); 
    if abs(best[0]-st)<0.06 and best[3] is None: best[3]=c
on_t=[o[0] for o in ons]
def stat(sel,name):
    c=np.array([o[3] if o[3] is not None else 0.2 for o in sel])*1000
    print('%-22s n=%3d mean %4.0f ms  median %3.0f  >50 ms %3.0f%%  never %3.0f%%'%(name,len(c),c.mean(),np.median(c),100*(c>50).mean(),100*np.mean([o[3] is None for o in sel])))
fast=[o for i,o in enumerate(ons) if i>0 and o[0]-ons[i-1][0]<0.2]
slow=[o for i,o in enumerate(ons) if not (i>0 and o[0]-ons[i-1][0]<0.2)]
stat(fast,'fast (IOI<0.2)'); stat(slow,'other')
stat([o for o in fast if o[1]=='stroke'],' fast new strokes'); stat([o for o in fast if o[1]=='slur'],' fast slurred')
