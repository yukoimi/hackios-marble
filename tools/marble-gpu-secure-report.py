"""Require authenticated CP nonsecure command, fence and enclosing shutdown."""
import argparse,importlib.util,json,re
from pathlib import Path
spec=importlib.util.spec_from_file_location('zap',Path(__file__).with_name('marble-gpu-zap-report.py'))
zap=importlib.util.module_from_spec(spec);spec.loader.exec_module(zap)
def inspect(path,extra_writes=0,live=False):
 r=zap.inspect(path,1+extra_writes,live=live);errors=r['errors'];text=path.read_text(encoding='utf8')
 lines=[s for s in text.splitlines() if s.startswith('marble-gpu-secure: MGPU-MARBLE-SECURE-v1 ')]
 v=dict((k,int(n,0)) for k,n in re.findall(r'(\w+)=(-?0x[0-9a-fA-F]+|-?\d+)',lines[0])) if len(lines)==1 else {}
 want=dict(PASS=1,after=0,submitted=1,completed=1,rptr=28,wptr=28,shadow=28,fence=2,status3=0,irq=0,fault=0,phase=2)
 if len(lines)!=1 or any(v.get(k)!=n for k,n in want.items()) or v.get('before',2) not in (0,1) or not 1<=v.get('polls',0)<=10001 or v.get('status',2)&~1:errors.append('secure transition/fence evidence missing/duplicate/invalid')
 markers=['marble-gpu-zap: MGPU-MARBLE-ZAP-v1 ', 'marble-gpu-cp: before-write reg=0x807 value=0x1c\n',
  'marble-gpu-cp: after-write reg=0x807\nmarble-gpu-secure: ',
  'marble-gpu-cp: before-write reg=0x808 value=0x3', 'marble-gmu-gx: stopped ']
 pos=[text.find(m) for m in markers]
 if -1 in pos or pos!=sorted(pos):errors.append('authenticated secure submission/stop order differs')
 r.update(passed=not errors,secure=v,fresh_transition=v.get('before')==1 and v.get('after')==0);return r
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('logs',type=Path,nargs='+');p.add_argument('--output',required=True,type=Path);a=p.parse_args()
 runs=[inspect(v) for v in a.logs];passed=all(r['passed'] for r in runs)
 a.output.write_text(json.dumps(dict(passed=passed,nonsecure_cp_verified=passed,acceleration_enabled=False,runs=runs),indent=2)+'\n',encoding='utf8')
 for r in runs:print(r['log'],'PASS' if r['passed'] else 'FAIL',r['errors'])
 raise SystemExit(0 if passed else 1)
if __name__=='__main__':main()
