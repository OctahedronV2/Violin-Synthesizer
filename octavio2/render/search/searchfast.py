import cma, json, numpy as np, sys
from concurrent.futures import ThreadPoolExecutor
sys.path.insert(0,'/tmp/o2r'); import evalfast as ER
P=[('qForceSus',.1,1,.4883),('qForceTau',.02,.25,.07482),('qForceHold',0,.06,.0159),('qStrokeSus',.05,1,.7078),('qStrokeTau',.02,.3,.1498),
   ('qStopForce',.2,1.2,.5138),('qStopTime',.01,.3,.035),('qStopAccel',8,60,25),('quickP',-.1,.5,.35),('quickContact',.7,1.4,1.3)]
FIX=['quickIOI=0.3','quickRun=3']
def toval(z): return [lo+(hi-lo)*min(1,max(0,zi)) for (n,lo,hi,d),zi in zip(P,z)]
x0=[(d-lo)/(hi-lo) for n,lo,hi,d in P]
es=cma.CMAEstimator if False else cma.CMAEvolutionStrategy(x0,0.2,{'popsize':8,'bounds':[0,1],'seed':3,'verbose':-9})
log=open('/tmp/o2r/searchfast.log','a'); k=0; best=(1e9,None)
def ev(args):
    i,z=args; vals=toval(z); opts=['%s=%.4g'%(n,v) for (n,*_),v in zip(P,vals)]+FIX+['seed=%d'%(1+i%3)]
    r=ER.run(opts,'q%d'%i); return r['total'],opts,r
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
