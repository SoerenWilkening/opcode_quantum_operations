import os,subprocess,time,json,re,hashlib
from pathlib import Path
w=Path('/tmp/qtg-n4-F4-noisy-20261006'); base=json.loads(Path('/tmp/qtg-n4-20261006/measurement.json').read_text())
env=os.environ.copy()
for k in list(env):
 if k.startswith('CQOPS_') or k.startswith('QEC_'): del env[k]
env.update(CQOPS_SINK='qec',CQOPS_QEC_CONFIG=str(w/'config.json'),LC_ALL='C')
with (w/'logs/fault-verification.log').open('w') as log:
 start=time.perf_counter(); r=subprocess.run(['/usr/bin/time','-p',str(w/'n4_faults')],env=env,stdout=log,stderr=subprocess.STDOUT,stdin=subprocess.DEVNULL); elapsed=time.perf_counter()-start
s=(w/'logs/fault-verification.log').read_text(); result={k:base[k] for k in ('qec','cqops','compiler')}
result.update(configuration='Release, n=4 F=4, frame=true decoder=union_find seed=42, pX=pY=pZ=1e-3/3 pM=1e-3, serial QEC, no trace',instrument='/usr/bin/time -p',rc=r.returncode,elapsed_seconds=elapsed,date=time.strftime('%Y-%m-%d %H:%M:%S %z'),config_sha256=hashlib.sha256((w/'config.json').read_bytes()).hexdigest())
for label in ('real','user','sys'):
 m=re.search(r'^'+label+r'\s+([\d.]+)',s,re.M); result[label+'_seconds']=float(m[1]) if m else None
for label in ('QEC','PHYS'):
 m=re.search(r'^QEC d=.*$' if label=='QEC' else r'^PHYS .*$',s,re.M); result[label.lower()+'_row']=m[0] if m else None
m=re.search(r'^NOISE .*$',s,re.M); result['noise_row']=m[0] if m else None
result['rotation_checks_passed']='rotations OK: 4 controlled biased Hadamards' in s
(w/'fault-verification.json').write_text(json.dumps(result,indent=2)+'\n'); print(json.dumps(result,indent=2),flush=True)
if r.returncode or not result['qec_row'] or not result['phys_row'] or not result['rotation_checks_passed']: raise SystemExit('Invalid run; inspect log')
