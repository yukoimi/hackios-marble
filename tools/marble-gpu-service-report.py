"""Validate userspace Freedreno shader pixels, mapped BOs, fences and release."""
import argparse,importlib.util,json,re
from pathlib import Path
s=importlib.util.spec_from_file_location('blit',Path(__file__).with_name('marble-gpu-blit-report.py'))
blit=importlib.util.module_from_spec(s);s.loader.exec_module(blit)
def inspect(path):
 r=blit.inspect(path,live='service');errors=r['errors'];text=path.read_text(encoding='utf8')
 def rows(prefix):
  return [dict((k,int(n,0)) for k,n in re.findall(r'(\w+)=(-?0x[0-9a-fA-F]+|-?\d+)',line))
   for line in text.splitlines() if line.startswith(prefix)]
 def one(prefix,want):
  a=rows(prefix)
  if a!=[want]:errors.append(prefix+' missing/duplicate/different')
 one('marble-gpu-runtime: MGPU-MARBLE-RUNTIME-v1 ',dict(ready=1,service=1,pos=871,fence=67))
 one('marble-gpu-service: MGPU-MARBLE-SERVICE-v1 ',dict(ready=1,chip=0x07030002,gmem=2097152,VA=0x4000000,size=0x1c000000,pos=871,fence=67))
 one('marble-gl-test: PASS=',dict(PASS=1,hardware=1,checked=15564,inside=3864,outside=11700,bad=0,error=0))
 if len(re.findall(r'^gl_test: freedreno; FD725; OpenGL ES 3\.2 Mesa 25\.0\.7$',text,re.M))!=1 or len(re.findall(r'^marble-gl-test: PASS=1 .* renderer=FD725$',text,re.M))!=1:errors.append('hardware renderer identity differs')
 submissions=rows('marble-gpu-service: submit ')
 if len(submissions)!=3:errors.append('triangle submission count differs')
 for i,v in enumerate(submissions,1):
  if any(v.get(k)!=n for k,n in dict(PASS=1,count=1,submissions=i,pos=871+72*i,fence=67+i).items()) or not 1<=v.get('polls',0)<=100001:errors.append('framed submission fence/position differs')
 one('marble-gpu-service: work ',dict(PASS=1,stop_requested=1,submissions=3,pos=1087,fence=70))
 one('marble-gpu-runtime: released ',dict(PASS=1,draws=0,wraps=0,active=0))
 updates=rows('marble-gpu-live-map: map=')
 if len(updates)!=175:errors.append('live allocation/unmap generation count differs')
 leaves=0
 for generation,v in enumerate(updates,1):
  if v.get('generation')!=generation or v.get('PASS')!=1 or v.get('map') not in (0,1) or v.get('tlb_removed')!=1-v.get('map',0) or v.get('published',0)<1 or v.get('ranges',0)<1:errors.append('live batch metadata differs')
  now=v.get('leaves',0)
  if (v.get('map')==1 and now<=leaves) or (v.get('map')==0 and now>=leaves):errors.append('live leaves direction differs')
  leaves=now
 if leaves!=48:errors.append('userspace BOs not all removed before hardware teardown')
 markers=['marble-gpu-service: MGPU-MARBLE-SERVICE-v1 ', 'marble-gpu-service: submit ',
  'marble-gl-test: PASS=1 ', 'marble-gpu-service: work ', 'marble-gmu-gx: stopped ',
  'marble-gpu-context: stopped ', 'marble-gpu: CX result ', 'marble-gpu-runtime: released ']
 at=[text.find(v) for v in markers]
 if -1 in at or at!=sorted(at):errors.append('shader/BO/stop/release order differs')
 if 'SYSTEM_RESET' in text or 'panic(cpu ' in text or 'completion timeout' in text:errors.append('failure/reset present')
 r.update(passed=not errors,shader_pixels=15564,submissions=submissions);return r
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('logs',nargs='+',type=Path);p.add_argument('--output',required=True,type=Path);a=p.parse_args()
 runs=[inspect(v) for v in a.logs];passed=all(v['passed'] for v in runs)
 a.output.write_text(json.dumps(dict(passed=passed,userspace_gpu_shader_verified=passed,ios_gpu_composition_enabled=False,runs=runs),indent=2)+'\n',encoding='utf8')
 for r in runs:print(r['log'],'PASS' if r['passed'] else 'FAIL',r['errors'])
 raise SystemExit(0 if passed else 1)
if __name__=='__main__':main()
