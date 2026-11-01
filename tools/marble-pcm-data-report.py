"""One actual silent PCM consumption event; no physical speaker acceptance."""
import re
def inspect(text,requests=1,mfc=False):
 if requests in (2,8):
  import importlib.util
  from pathlib import Path
  spec=importlib.util.spec_from_file_location('pcm_repeat_report',Path(__file__).with_name('marble-pcm-repeat-report.py'))
  repeat=importlib.util.module_from_spec(spec);spec.loader.exec_module(repeat)
  return repeat.inspect(text,requests=requests,mfc=mfc)
 if mfc:return dict(passed=False,errors=['MFC requires exactly two retired buffers'],audio_acceptance=False)
 if requests!=1:return dict(passed=False,errors=['unsupported PCM request count'],audio_acceptance=False)
 if any(line.startswith('pr:') for line in text.splitlines()):return dict(passed=False,errors=['repeat PCM requires explicit two-request contract'],audio_acceptance=False)
 errors=[];handle=None
 try:
  def rows(prefix):return [dict(re.findall(r'(\w+)=([^\s]+)',l)) for l in text.splitlines() if l.startswith(prefix)]
  def one(prefix):
   r=rows(prefix)
   if len(r)!=1:raise ValueError('missing/duplicate '+prefix)
   return r[0]
  def exact(r,**kw):return all(r.get(k)==str(v) for k,v in kw.items())
  q=one('pd: q ');r=one('pd: r ');d=one('pd: d ');end=one('pd: end ')
  handle=int(q['handle'],16);iid=int(q['id'],16);cap=int(q['cap']);token='4d504401'
  if not 0<handle<=0xffffffff or int(one('pm: r s=0 ')['handle'],16)!=handle:raise ValueError('mapped handle mismatch')
  if not exact(q,t=token,n=68,bytes=4096,frames=512,rate=96000,bits=32,channels=2,zero=1) or not 68<=cap<528:raise ValueError('silent PCM boundary')
  if not exact(r,t=token,error=0,md=0,kind=1,n=56) or int(r['handle'],16)!=handle:raise ValueError('typed DATA_BUFFER_DONE_V2 required')
  if d['c'] not in ('6','14') or int(d['id'],16)!=iid:raise ValueError('PCM TX retirement')
  if not exact(end,PASS=1,sent=1,tx=1,reply=1,kind=1,error=0,md=0,zero=1,faults_ok=1,bad=0,PCM_requests=1,PAS_stopped=0):raise ValueError('consumption/fault status')
  used={int(x['id'],16) for prefix in ('tr: q ','pm: q ') for x in rows(prefix)}
  intents=rows('marble-glink-apps: remote_intent ')
  if iid in used or sum(int(x['id'],16)==iid and int(x['bytes'])==cap for x in intents)!=1:raise ValueError('PCM intent ownership')
  tx=[x for x in rows('marble-glink-control: tx ') if x['command']=='9' and int(x['param2'],16)==iid]
  if len(tx)!=1 or not exact(tx[0],param1=1,bytes=88):raise ValueError('WRITE68 DATA geometry')
  acc=left=packets=0
  for f in rows('pd: f '):
   c=int(f['c']);n=int(f['n']);remain=int(f['l']);total=int(f['u'])
   if not 0<n<=512 or not 0<=remain<=512-n or int(f['f'])!=((16+n+7)&~7) or (not acc and c!=9) or (acc and (c!=12 or remain!=left-n)) or total!=acc+n:raise ValueError('PCM fragment continuity')
   acc=total;left=remain
   if not left:
    if packets or acc!=56:raise ValueError('DONE56 assembly')
    packets=1;acc=0
  if acc or packets!=1:raise ValueError('DONE56 missing')
  order=['pm: r s=0 ','tr: r s=2 ','pd: q ','pd: r ','pd: end ','tr: q s=3 ','tr: r s=4 ','pm: q s=1 ','pm: end ','marble-smccc: PAS_stop ']
  p=[text.find(x) for x in order]
  if min(p)<0 or p!=sorted(p) or text.find('tr: d s=2 ')>=text.find('pd: q ') or max(text.find('pd: r '),text.find('pd: d '))>=text.find('pd: end '):raise ValueError('START/PCM/STOP/unmap/PAS ordering')
 except (KeyError,ValueError,TypeError) as e:errors.append(str(e))
 return dict(passed=not errors,errors=errors,handle=handle,PCM_requests=1,PCM_bytes=4096,PCM_frames=512,typed_buffer_consumed=not errors,amplifier_commands=0,audio_acceptance=False,scope='one zero-filled96kHz/S32/stereo page consumed by WREP with exact DONE identity and no SMMU faults; no board pinctrl, amplifier enabling or audible playback')
