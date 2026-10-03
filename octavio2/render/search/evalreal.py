# one score for "plays like the real player" on the first 40 s of the Finale and first movement
import sys, os, json, subprocess, numpy as np, soundfile as sf, scipy.signal as ss, mido, hashlib
sys.path.insert(0,'/mnt/project-files/research/world-class/references/tool')
import violinscore as vs
from scipy.ndimage import median_filter
H='/mnt/project-files/research/world-class/references/corpus/phrases/anechoic-haydn/'
PARTS=['Mov4_Violin2','Mov1_Violin1']; UP=40.0
def env5(x): h=240; x=x[:len(x)//h*h].reshape(-1,h); return 10*np.log10((x**2).mean(1)+1e-12)
def notes_of(part):
    t=0; on={}; out=[]
    for m in mido.MidiFile('/tmp/o2r/haydn_%s.mid'%part):
        t+=m.time
        if m.type=='note_on' and m.velocity>0: on[m.note]=t
        elif m.type in('note_off','note_on') and m.note in on: out.append((on.pop(m.note),t,m.note))
    return sorted(n for n in out if n[0]<UP-0.6)
def feats(x48, part):
    x=ss.resample_poly(x48,147,160)[:int(UP*vs.FS)]  # 48k -> 44.1k
    f0,ap,rms=vs.yin(x); rms=median_filter(rms,size=4)
    e=env5(x48[:int(UP*48000)]); ns=notes_of(part); dec=[]
    for i,(a,b,p) in enumerate(ns):
        nx=ns[i+1][0] if i+1<len(ns) else b+0.1
        seg=e[int((a-0.03)*200):int(min(nx,a+0.6)*200)]
        if len(seg)>8: dec.append(seg[-3:].mean()-seg.max())
    v=(f0>0)&(rms>np.percentile(rms,90)-25); hn=[]
    for i in np.where(v)[0][::6]:
        s0=i*vs.HOP; fr=x[s0:s0+2048]
        if len(fr)<2048: continue
        S=np.abs(np.fft.rfft(fr*np.hanning(2048)))**2; f=np.fft.rfftfreq(2048,1/vs.FS)
        k=f/f0[i]; near=np.abs(k-np.round(k))<0.15; band=(f>1500)&(f<8000)
        hn.append(10*np.log10(S[band&~near].sum()/S[band&near].sum()+1e-12))
    W=int(0.15*vs.FR); dd=[]
    for i in range(W,len(rms)-W,2):
        if rms[i]==rms[i-4:i+5].min():
            d=min(rms[i-W:i].max(),rms[i+1:i+W+1].max())-rms[i]
            if d>3 and (f0[i-W:i+W]>0).mean()>0.5: dd.append(d)
    return dict(f0=f0,rms=rms,dec=float(np.median(dec)),hiss=float(np.median(hn)),sep=float(np.median(dd)))
def load48(p):
    x,sr=sf.read(p); x=x if x.ndim==1 else x[:,0]
    return ss.resample_poly(x,48000,sr) if sr!=48000 else x
REAL={}
def real(part):
    if part not in REAL:
        c='/tmp/o2r/realfeat_%s.npz'%part
        if not os.path.exists(c):
            F=feats(load48(H+part+'_mic5-front-1m.flac'),part); np.savez(c,**{k:np.asarray(v) for k,v in F.items()})
        d=np.load(c); REAL[part]={k:(d[k] if d[k].ndim else float(d[k])) for k in d.files}
    return REAL[part]
def score_file(path, part):
    R=real(part); O=feats(load48(path),part)
    n=min(len(R['f0']),len(O['f0'])); v=R['f0'][:n]>0
    a4=440*2**(np.median(vs.midi_of(R['f0'][v])-np.round(vs.midi_of(R['f0'][v])))/12)
    mr=vs.midi_of(np.maximum(R['f0'][:n],1),a4); mo=vs.midi_of(np.maximum(O['f0'][:n],1))
    notes=(v&(O['f0'][:n]>0)&(np.abs(mo-mr)<.5)).sum()/v.sum()
    r=np.corrcoef(R['rms'][:n][v],O['rms'][:n][v])[0,1]
    m=dict(notes=notes,r=r,dsep=O['sep']-R['sep'],ddec=O['dec']-R['dec'],dhiss=O['hiss']-R['hiss'])
    m['score']=(1-notes)*10+(1-r)*5+abs(m['dsep'])*0.3+abs(m['ddec'])*0.3+abs(m['dhiss'])*0.2
    return m
def run(opts, tag):
    procs=[]; res={}
    for p in PARTS:
        f='/tmp/o2r/ev/%s_%s'%(tag,p)
        cmd='/tmp/o2 /tmp/o2r/haydn_%s.mid %s.f.wav length=%g %s 2>/dev/null && python3 /home/user/Violin-Synthesizer/octavio2/render/finish.py %s.f.wav %s --hall none'%(p,f,UP+0.5,' '.join(opts),f,f)
        procs.append((p,f,subprocess.Popen(cmd,shell=True)))
    for p,f,pr in procs:
        pr.wait(); res[p]=score_file(f+'.dry.wav',p)
    res['total']=sum(res[p]['score'] for p in PARTS)
    return res
if __name__=='__main__':
    os.makedirs('/tmp/o2r/ev',exist_ok=True)
    if sys.argv[1]=='file':
        print(score_file(sys.argv[2],sys.argv[3]))
    else:
        r=run(sys.argv[2:],sys.argv[1]); print(json.dumps({k:(v if k=='total' else {kk:round(float(vv),3) for kk,vv in v.items()}) for k,v in r.items()}))
