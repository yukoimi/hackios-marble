"""Require repeated live mappings, real TBU translation, GPU pixels and teardown."""
import argparse, importlib.util, json, re
from pathlib import Path
spec=importlib.util.spec_from_file_location('runtime',Path(__file__).with_name('marble-gpu-runtime-report.py'))
runtime=importlib.util.module_from_spec(spec);spec.loader.exec_module(runtime)

def inspect(path):
 r=runtime.inspect(path,live=True);errors=r['errors'];text=path.read_text(encoding='utf8')
 lines=[s for s in text.splitlines() if s.startswith('marble-gpu-live-map: ')]
 def fields(s):return dict((k,int(n,0)) for k,n in re.findall(r'(\w+)=(-?0x[0-9a-fA-F]+|-?\d+)',s))
 prepared=[fields(s) for s in lines if 'MGPU-MARBLE-LIVE-MAP-v1 ' in s]
 p=prepared[0] if len(prepared)==1 else {};pa=p.get('table_PA',0)
 if p!=dict(prepared=1,table_PA=pa,pages=258,aperture=0x20000000,leaves=0) or not pa or pa&0x3fff or pa+260*4096>1<<36:
  errors.append('empty live aperture topology evidence differs')
 events=[s for s in lines if not s.startswith('marble-gpu-live-map: MGPU-')]
 expected=[dict(map=1,ranges=5,leaves=48,generation=1,published=3,tlb_removed=0,PASS=1)]
 target=0
 for i in range(64):
  at=1+4*i
  if len(events)>at+1 and i==0:target=fields(events[at+1]).get('PA',0)
  expected.extend([dict(map=1,ranges=1,leaves=52,generation=2+2*i,published=1,tlb_removed=0,PASS=1),
   dict(iova=0x2000000,PA=target,expected=target,PASS=1),
   dict(iova=0x2003000,PA=target+0x3000,expected=target+0x3000,PASS=1),
   dict(map=0,ranges=1,leaves=48,generation=3+2*i,published=1,tlb_removed=1,PASS=1)])
 expected.append(dict(PASS=1,cycles=64,map=64,unmap=64,ecats=128,draws=64,pixels=1024,guards=3056,pos=871,fence=67,generation=129,leaves=48))
 if [fields(s) for s in events]!=expected:errors.append('live map/ECATS/unmap/draw sequence differs')
 if not target or target&0x3fff or target+0x4000>1<<36 or (target<pa+260*4096 and pa<target+0x4000):errors.append('live target RAM invalid/aliases tables')
 markers=['marble-gpu-live-map: MGPU-MARBLE-LIVE-MAP-v1 ', 'marble-gpu-context: attach ',
  'marble-gpu-live-map: map=1 ranges=5 ', 'marble-gpu-context: ready ',
  'marble-gpu-blit: MGPU-MARBLE-BLIT-v1 ', 'marble-gpu-live-map: map=1 ranges=1 ',
  'marble-gpu-live-map: cycles ', 'marble-gpu-runtime: MGPU-MARBLE-RUNTIME-v1 ']
 at=[text.find(s) for s in markers]
 if -1 in at or at!=sorted(at):errors.append('live ownership/draw/runtime order differs')
 r.update(passed=not errors,live_map=dict(cycles=64,target_PA=target,table_PA=pa,aperture_bytes=0x20000000));return r

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('logs',nargs='+',type=Path);p.add_argument('--output',required=True,type=Path);a=p.parse_args()
 runs=[inspect(v) for v in a.logs];passed=all(r['passed'] for r in runs)
 a.output.write_text(json.dumps(dict(passed=passed,live_mapping_verified=passed,acceleration_enabled=False,
  limits='Owned dynamic target and persistent worker 2D only; no userspace shader or iOS GPU composition',runs=runs),indent=2)+'\n',encoding='utf8')
 for r in runs:print(r['log'],'PASS' if r['passed'] else 'FAIL',r['errors'])
 raise SystemExit(0 if passed else 1)
if __name__=='__main__':main()
