"""Check persistent real 2D submissions and their complete enclosing teardown."""
import argparse, importlib.util, json, re
from pathlib import Path
spec=importlib.util.spec_from_file_location('blit',Path(__file__).with_name('marble-gpu-blit-report.py'))
blit=importlib.util.module_from_spec(spec);spec.loader.exec_module(blit)

def inspect(path,live=False):
 r=blit.inspect(path,live=live);errors=r['errors'];text=path.read_text(encoding='utf8')
 pos,fence=(871,67) if live else (167,3)
 def rows(prefix):
  return [dict((k,int(n,0)) for k,n in re.findall(r'(\w+)=(-?0x[0-9a-fA-F]+|-?\d+)',s))
   for s in text.splitlines() if s.startswith('marble-gpu-runtime: '+prefix)]
 ready=rows('MGPU-MARBLE-RUNTIME-v1 ');work=rows('work ');released=rows('released ')
 if ready!=[dict(ready=1,service=0,pos=pos,fence=fence)]:errors.append('unique runtime ready evidence missing')
 w=work[0] if len(work)==1 else {};n=w.get('draws',0)
 expected=dict(PASS=1,stop_requested=1,draws=n,wraps=(pos+n*139)//4096,
  pos=(pos+n*139)%4096,fence=n+fence,pixels=1024,guards=3056)
 if w!=expected or n<128 or len(work)!=1:errors.append('runtime draw/fence/wrap/pixel/stop evidence differs')
 if released!=[dict(PASS=1,draws=n,wraps=(pos+n*139)//4096,active=0)]:errors.append('runtime release evidence differs')
 progress=rows('progress ')
 expected_progress=[dict(draws=i,wraps=(pos+i*139)//4096,pos=(pos+i*139)%4096,
  fence=i+fence,pixels=1024,guards=3056,fault=0,irq=0) for i in range(32,n+1,32)]
 if progress!=expected_progress:errors.append('runtime progress sequence differs')
 markers=['marble-gpu-blit: MGPU-MARBLE-BLIT-v1 ', 'marble-gpu-runtime: MGPU-MARBLE-RUNTIME-v1 ',
  'marble-gpu-runtime: progress ','marble-gpu-runtime: work ',
  'marble-gpu-cp: before-write reg=0x808 value=0x3',
  'marble-gpu-context: stopped ','marble-gpu: CX result ', 'marble-gpu-runtime: released ']
 at=[text.find(s) for s in markers]
 if -1 in at or at!=sorted(at):errors.append('runtime ownership/stop/root/CX release order differs')
 if 'marble-gpu-runtime: draw FAIL' in text or 'SYSTEM_RESET' in text:errors.append('runtime failure/reset present')
 r.update(passed=not errors,runtime=w);return r

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('logs',nargs='+',type=Path);p.add_argument('--output',required=True,type=Path);a=p.parse_args()
 runs=[inspect(v) for v in a.logs];passed=all(r['passed'] for r in runs)
 a.output.write_text(json.dumps(dict(passed=passed,persistent_gpu_2d_verified=passed,acceleration_enabled=False,
  limits='Worker 2D draw/fence/ring-wrap and teardown only; no userspace Mesa/shader or iOS GPU composition',runs=runs),indent=2)+'\n',encoding='utf8')
 for r in runs:print(r['log'],'PASS' if r['passed'] else 'FAIL',r['errors'])
 raise SystemExit(0 if passed else 1)
if __name__=='__main__':main()
