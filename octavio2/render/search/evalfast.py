# "plays the quick notes like the real player": Finale (full) + first movement (first 40 s), quick notes only
import sys, os, json, subprocess, numpy as np, mido, pickle
sys.path.insert(0,'/mnt/project-files/research/world-class/references/tool')
import violinscore as vs
H='/mnt/project-files/research/world-class/references/corpus/phrases/anechoic-haydn/'
PARTS={'Mov4_Violin2':54.0,'Mov1_Violin1':40.0}
def notes_of(part,up):
    t=0;on={};ns=[]
    for m in mido.MidiFile('/tmp/o2r/%s_%s.mid'%(os.environ.get('MIDI','haydn4'),part)):
        t+=m.time
        if m.type=='note_on' and m.velocity>0: on[m.note]=t
        elif m.type in('note_off','note_on') and m.note in on: ns.append((on.pop(m.note),t,m.note))
    return sorted(n for n in ns if n[1]<up)
def feats(path,part,real):
    up=PARTS[part]; x=vs.load(path)[:int(up*vs.FS)]
    f0,ap,rms=vs.yin(x); FR=vs.FR; v=f0>0
    a4=440*2**(np.median(vs.midi_of(f0[v])-np.round(vs.midi_of(f0[v])))/12) if real else 440.0
    m=np.where(v,vs.midi_of(np.maximum(f0,1),a4),0)
    ns=notes_of(part,up); q=[n for n in ns if 0.06<n[1]-n[0]<0.35]
    up_,hn,=[],[]
    N=8192
    for a,b,n in q:
        fr=x[int((a+0.02)*vs.FS):int(b*vs.FS)]
        if len(fr)<1024: continue
        S=np.abs(np.fft.rfft(fr*np.hanning(len(fr)),N))**2; f=np.fft.rfftfreq(N,1/vs.FS); f1=a4*2**((n-69)/12)
        e1=S[(f>f1*0.94)&(f<f1*1.06)].sum()
        up_.append(10*np.log10(sum(S[(f>h*f1*0.96)&(f<h*f1*1.04)].sum() for h in range(2,9))/e1+1e-12))
        k=f/f1; near=np.abs(k-np.round(k))<0.15; band=(f>1500)&(f<8000)
        hn.append(10*np.log10(S[band&~near].sum()/S[band&near].sum()+1e-12))
    pairs=[(ns[i-1],ns[i]) for i in range(1,len(ns)) if ns[i][0]-ns[i-1][0]<0.25 and ns[i-1][2]!=ns[i][2]]
    wrong=[];dip=[]
    for a,b in pairs:
        i=int(b[0]*FR)
        if i+12>=len(m): continue
        mm=m[i-4:i+8]; good=(np.abs(mm-a[2])<0.5)|(np.abs(mm-b[2])<0.5); wrong.append(((mm>0)&~good).mean())
        dip.append(max(rms[i-12:i-2].max(),rms[i+2:i+12].max())-rms[i-4:i+4].min())
    mask=np.zeros(len(rms),bool)
    for a,b,n in q: mask[max(0,int((a-0.05)*FR)):int((b+0.1)*FR)]=True
    return dict(f0=f0,m=m,rms=rms,mask=mask,bright=np.median(up_),hiss=np.median(hn),wrong=np.mean(wrong),dip=np.median(dip),a4=a4)
def real(part):
    c='/tmp/o2r/realfast_%s.pkl'%part
    if not os.path.exists(c): pickle.dump(feats(H+part+'_mic5-front-1m.flac',part,True),open(c,'wb'))
    return pickle.load(open(c,'rb'))
def score_file(path,part):
    R=real(part); O=feats(path,part,False)
    n=min(len(R['rms']),len(O['rms'])); a=np.maximum(R['rms'][:n]-np.percentile(R['rms'],98),-40); b=np.maximum(O['rms'][:n]-np.percentile(O['rms'],98),-40)
    mk=R['mask'][:n]&((a>-40)|(b>-40)); shape=np.abs(a-b)[mk].mean()
    v=R['f0'][:n]>0; mr=R['m'][:n]; mo=O['m'][:n]
    notes=(v&(np.abs(mo-mr)<.5)).sum()/v.sum()
    d=dict(notes=notes,shape=shape,dbright=O['bright']-R['bright'],dhiss=O['hiss']-R['hiss'],dwrong=O['wrong']-R['wrong'],ddip=O['dip']-R['dip'])
    d['score']=(1-notes)*10+shape*1.0+abs(d['dbright'])*0.5+abs(d['dhiss'])*0.2+max(0,d['dwrong'])*20+abs(d['ddip'])*0.3
    return d
def run(opts,tag):
    procs=[];res={}
    for p,up in PARTS.items():
        f='/tmp/o2r/ev/f_%s_%s'%(tag,p)
        cmd='/tmp/o2 /tmp/o2r/'+os.environ.get('MIDI','haydn4')+'_%s.mid %s.f.wav length=%g %s 2>%s.log && python3 /home/user/Violin-Synthesizer/octavio2/render/finish.py %s.f.wav %s --hall none >/dev/null'%(p,f,up+0.5,' '.join(opts),f,f,f)
        procs.append((p,f,subprocess.Popen(cmd,shell=True)))
    for p,f,pr in procs:
        pr.wait(); res[p]=score_file(f+'.dry.wav',p)
        import re
        m=re.search(r'capture: \d+ notes, mean ([\d.]+) ms',open(f+'.log').read()); cap=float(m.group(1)) if m else 200.0
        res[p]['capture']=cap; res[p]['score']+=0.05*cap
    res['total']=sum(res[p]['score'] for p in PARTS)
    return res
if __name__=='__main__':
    os.makedirs('/tmp/o2r/ev',exist_ok=True)
    r=run(sys.argv[2:],sys.argv[1]); print(json.dumps({k:(round(v,2) if k=='total' else {kk:round(float(vv),3) for kk,vv in v.items()}) for k,v in r.items()}))
