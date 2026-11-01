"""Validate fresh PAS13 authentication and post-collapse unload, not rendering."""
import argparse,importlib.util,json,re
from pathlib import Path
spec=importlib.util.spec_from_file_location('cp',Path(__file__).with_name('marble-gpu-cp-report.py'))
cp=importlib.util.module_from_spec(spec);spec.loader.exec_module(cp)
def inspect(path,extra_writes=0,live=False):
 r=cp.inspect(path,extra_writes,live=live);errors=r['errors'];text=path.read_text(encoding='utf8')
 lines=[s for s in text.splitlines() if s.startswith('marble-gpu-zap: ')]
 def fields(s):return dict((k,int(v,0)) for k,v in re.findall(r'(\w+)=(-?0x[0-9a-fA-F]+|-?\d+)',s))
 calls=[];pending=None
 for s in lines:
  if s.startswith('marble-gpu-zap: before-scm '):
   if pending is not None:errors.append('nested PAS call')
   pending=fields(s)
  elif s.startswith('marble-gpu-zap: after-scm '):
   f=fields(s)
   if pending is None or pending.get('command')!=f.get('command'):errors.append('unpaired PAS call')
   else:calls.append((pending,f))
   pending=None
 if pending:errors.append('unfinished PAS call')
 expected=[(0x601,1,0x2000201,0,0),(0x601,1,0x2000202,0,0),
  (0x601,1,0x2000205,0,0),(0x601,1,0x2000206,0,0),
  (0x206,1,13,0,0),(0x201,0x82,13,None,0),
  (0x202,3,13,0x8b91a000,0x1000),(0x205,1,13,0,0),(0x206,1,13,0,0)]
 if len(calls)!=len(expected):errors.append('PAS call count differs')
 for i,(a,b) in enumerate(calls[:len(expected)]):
  for k,v in zip(('command','args','first','second','third'),expected[i]):
   if v is not None and a.get(k)!=v:errors.append('PAS argument differs')
  if i==5 and (not 0<a.get('second',0)<1<<36 or a['second']&0x3fff):errors.append('metadata PA invalid')
  if i!=4 and (b.get('status')!=0 or b.get('answer')!=(1 if i<4 else 0)):errors.append('PAS success missing')
  if not 0<=b.get('retries',-1)<100:errors.append('PAS retry bound differs')
 def one(prefix,want):
  found=[s for s in lines if s.startswith(prefix)]
  if len(found)!=1 or any(fields(found[0]).get(k)!=v for k,v in want.items()):errors.append(prefix+' missing/duplicate/invalid')
  return fields(found[0]) if len(found)==1 else {}
 payload=one('marble-gpu-zap: payload ',dict(PASS=1,address=0x8b91a000,bytes=4096,file_bytes=2236,segment=1,readback=1))
 auth=one('marble-gpu-zap: MGPU-MARBLE-ZAP-v1 ',dict(PASS=1,available=4,attempted=1,initialized=1,memory_ready=1,copied=1,authenticated=1,unloaded=0,retain=1,phase=5,status=0,answer=0))
 if len(calls)>4 and auth.get('old_unloaded')!=int(calls[4][1].get('status')==0 and calls[4][1].get('answer')==0):errors.append('initial unload evidence differs')
 one('marble-gpu-zap: shutdown ',dict(PASS=1,attempted=1,unloaded=1,retain=0,phase=7))
 markers=['marble-gpu-zap: after-scm command=0x201 status=0 answer=0 ',
  'marble-gpu-zap: after-scm command=0x202 status=0 answer=0 ',
  'marble-gpu-zap: payload ', 'marble-gpu-zap: after-scm command=0x205 status=0 answer=0 ',
  'marble-gpu-zap: MGPU-MARBLE-ZAP-v1 ', 'marble-gmu-gx: stopped ',
  'marble-gpu-zap: shutdown ', 'marble-gpu-cp: CX-bus-restore ', 'marble-gpu-context: stopped ']
 positions=[text.find(m) for m in markers]
 if -1 in positions or positions!=sorted(positions):errors.append('PAS copy/auth/collapse/unload/release order differs')
 gx=text.find('marble-gmu-gx: stopped ')
 final=text.find('marble-gpu-zap: before-scm command=0x206',gx)
 unload=text.find('marble-gpu-zap: shutdown ')
 if final<gx or final>unload:errors.append('final SHUTDOWN not after GX collapse')
 r.update(passed=not errors,zap=auth,payload=payload);return r
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('logs',type=Path,nargs='+');p.add_argument('--output',required=True,type=Path);a=p.parse_args()
 runs=[inspect(v) for v in a.logs];passed=all(r['passed'] for r in runs)
 a.output.write_text(json.dumps(dict(passed=passed,zap_authenticated=passed,acceleration_enabled=False,runs=runs),indent=2)+'\n',encoding='utf8')
 for r in runs:print(r['log'],'PASS' if r['passed'] else 'FAIL',r['errors'])
 raise SystemExit(0 if passed else 1)
if __name__=='__main__':main()
