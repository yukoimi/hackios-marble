"""Require all GPU-drawn pixels/guards, fence, authentication and full teardown."""
import argparse,importlib.util,json,re
from pathlib import Path
spec=importlib.util.spec_from_file_location('secure',Path(__file__).with_name('marble-gpu-secure-report.py'))
secure=importlib.util.module_from_spec(spec);spec.loader.exec_module(secure)
def inspect(path,live=False):
 r=secure.inspect(path,1,live=live);errors=r['errors'];text=path.read_text(encoding='utf8')
 lines=[s for s in text.splitlines() if s.startswith('marble-gpu-blit: MGPU-MARBLE-BLIT-v1 ')]
 v=dict((k,int(n,0)) for k,n in re.findall(r'(\w+)=(-?0x[0-9a-fA-F]+|-?\d+)',lines[0])) if len(lines)==1 else {}
 want=dict(PASS=1,submitted=1,completed=1,words=167,rptr=167,wptr=167,shadow=167,fence=3,
  status3=0,irq=0,fault=0,pixels=1024,guards=3056,pixel0=0xffff8000,pixel1=0xfffdfbf7,pixel2=0xff0080ff,bad_index=0,bad_value=0,phase=3)
 if len(lines)!=1 or any(v.get(k)!=n for k,n in want.items()) or not 1<=v.get('polls',0)<=10001 or v.get('status',2)&~1:errors.append('2D draw/pixel/guard evidence missing/duplicate/invalid')
 markers=['marble-gpu-secure: MGPU-MARBLE-SECURE-v1 ', 'marble-gpu-cp: before-write reg=0x807 value=0xa7\n',
  'marble-gpu-cp: after-write reg=0x807\nmarble-gpu-blit: ', 'marble-gpu-cp: before-write reg=0x808 value=0x3']
 at=[text.find(s) for s in markers]
 if -1 in at or at!=sorted(at):errors.append('2D submit/completion/stop order differs')
 r.update(passed=not errors,blit=v);return r
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('logs',type=Path,nargs='+');p.add_argument('--output',required=True,type=Path);a=p.parse_args()
 runs=[inspect(v) for v in a.logs];passed=all(r['passed'] for r in runs)
 a.output.write_text(json.dumps(dict(passed=passed,gpu_2d_draw_verified=passed,acceleration_enabled=False,
  limits='Bounded startup 2D draw only; no shader or persistent iOS GPU composition',runs=runs),indent=2)+'\n',encoding='utf8')
 for r in runs:print(r['log'],'PASS' if r['passed'] else 'FAIL',r['errors'])
 raise SystemExit(0 if passed else 1)
if __name__=='__main__':main()
