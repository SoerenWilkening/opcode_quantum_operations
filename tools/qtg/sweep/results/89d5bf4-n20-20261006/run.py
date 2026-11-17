import os, subprocess, time, re, json, hashlib
from pathlib import Path
w=Path('/tmp/qtg-n20-20261006'); root=Path('/Users/sorenwilkening/Desktop/Projects/opcode_quantum_operations')
prior=json.loads(Path('/tmp/qtg-n4-20261006/measurement.json').read_text())
meta={k:prior[k] for k in ('cqops','qec','compiler')}
meta.update(configuration='Release, n=20, noiseless, serial QEC, no trace, sequential arms',date=time.strftime('%Y-%m-%d %H:%M:%S %z'),instrument='/usr/bin/time -p; Python perf_counter end-to-end',rows=[])
meta['load_at_start']=list(os.getloadavg())
for f in (1,2,4,8,16,32):
 cfg=root/f'tools/qtg/sweep/configs/qtg_n20_F{f}.json'; env=os.environ.copy()
 for k in list(env):
  if k.startswith('CQOPS_') or k.startswith('QEC_'): del env[k]
 env.update(CQOPS_SINK='qec',CQOPS_QEC_CONFIG=str(cfg),LC_ALL='C')
 print(f'F={f} start {time.strftime("%H:%M:%S")}',flush=True)
 with (w/f'logs/n20_F{f}.log').open('w') as log:
  start=time.perf_counter(); r=subprocess.run(['/usr/bin/time','-p',str(w/'n20_depth')],stdout=log,stderr=subprocess.STDOUT,env=env,stdin=subprocess.DEVNULL); elapsed=time.perf_counter()-start
 s=(w/f'logs/n20_F{f}.log').read_text(); m=re.search(r'^QEC d=.*$',s,re.M)
 row={'factories':f,'rc':r.returncode,'elapsed_seconds':elapsed,'config_sha256':hashlib.sha256(cfg.read_bytes()).hexdigest(),'row':m.group(0) if m else None}
 for label in ('real','user','sys'):
  t=re.search(r'^'+label+r'\s+([\d.]+)',s,re.M); row[label+'_seconds']=float(t[1]) if t else None
 row['rotation_checks_passed']='rotations OK: 20 controlled biased Hadamards' in s
 meta['rows'].append(row); (w/'measurement.json').write_text(json.dumps(meta,indent=2)+'\n'); print(json.dumps(row),flush=True)
 if r.returncode or not m or not row['rotation_checks_passed']: raise SystemExit('Invalid run; inspect log')
