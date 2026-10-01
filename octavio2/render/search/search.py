import cma, json, numpy as np, sys
from concurrent.futures import ThreadPoolExecutor
sys.path.insert(0,'/tmp/o2r'); import evalreal as ER
P=[('forceSus',.2,1,.55),('forceTau',.03,.25,.08),('forceHold',0,.08,.03),('shapeIOI',.25,.8,.45),('strokeSus',.4,1,1),('strokeTau',.05,.3,.12),
   ('stopBelow',0,.6,.35),('stopForce',.2,1.2,.6),('stopTime',.01,.1,.04),('posLo',.25,.55,.38),('posRange',.2,.6,.4),('earMax',1,3,2),
   ('bite',0,.4,.1),('slipNoise',.04,.14,.09),('accel',4,15,8),('accelFF',8,30,20),('changeDip',0,.6,.25),('releaseTime',.02,.15,.05)]
def toval(z): return [lo+(hi-lo)*min(1,max(0,zi)) for (n,lo,hi,d),zi in zip(P,z)]
x0=[(d-lo)/(hi-lo) for n,lo,hi,d in P]
es=cma.CMAEstimator if False else cma.CMAEvolutionStrategy(x0,0.15,{'popsize':8,'bounds':[0,1],'seed':3,'verbose':-9})
log=open('/tmp/o2r/search.log','a'); k=0; best=(1e9,None)
def ev(args):
    i,z=args; vals=toval(z); opts=['%s=%.4g'%(n,v) for (n,*_),v in zip(P,vals)]+['seed=%d'%(1+i%3)]
    r=ER.run(opts,'c%d'%i); return r['total'],opts,r
gen=0
while gen<int(sys.argv[1]):
    Z=es.ask(); 
    with ThreadPoolExecutor(2) as ex: out=list(ex.map(ev,[(k+j,z) for j,z in enumerate(Z)]))
    k+=len(Z); es.tell(Z,[o[0] for o in out])
    for t,opts,r in out:
        log.write(json.dumps({'gen':gen,'total':t,'opts':opts,'m4':r['Mov4_Violin2'],'m1':r['Mov1_Violin1']},default=float)+'\n'); log.flush()
        if t<best[0]: best=(t,opts)
    mean=['%s=%.4g'%(n,v) for (n,*_),v in zip(P,toval(es.mean))]
    log.write(json.dumps({'gen':gen,'best':best,'mean':mean})+'\n'); log.flush(); gen+=1
