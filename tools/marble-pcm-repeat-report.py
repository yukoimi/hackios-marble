"""Two serial silent WREP requests, unique tokens, one safely recycled page.

No claim of continuous playback, measured sample clock or audible output.
"""
import re

def inspect(text,requests=2,mfc=False):
 if requests not in (2,8):return dict(passed=False,errors=['unsupported repeat count'],audio_acceptance=False)
 errors=[];handle=None
 try:
  lines=text.splitlines()
  def rows(prefix):return [(i,dict(re.findall(r'(\w+)=([^\s]+)',l))) for i,l in enumerate(lines) if l.startswith(prefix)]
  def one(prefix):
   r=rows(prefix)
   if len(r)!=1:raise ValueError('missing/duplicate '+prefix)
   return r[0]
  def exact(r,**kw):return all(r.get(k)==str(v) for k,v in kw.items())
  path=rows('mfc: path ')
  if mfc:
   if requests!=2 or len(path)!=1 or not exact(path[0][1],source_rate=48000,source_bits=16,output_rate=96000,output_bits=32,channels=2,module='7001015',instance='6003',mapped_bytes=4096,PCM_bytes=1024,conversion_verified=0,audio_acceptance=0):raise ValueError('explicit MFC input/output contract')
  elif path:raise ValueError('MFC requires explicit format contract')
  qi=rows('pd: q ');ri=rows('pd: r ');di=rows('pd: d ');fi=rows('pd: f ');retired=rows('pr: retired ')
  if any(len(r)!=requests for r in (qi,ri,di,retired)):raise ValueError('exact requested count of issued/consumed/retired PCM requests required')
  ei,end=one('pd: end ');pi,policy=one('pr: end ')
  if not exact(end,PASS=1,sent=1,tx=1,reply=1,kind=1,error=0,md=0,zero=1,faults_ok=1,bad=0,PCM_requests=requests,PAS_stopped=0):raise ValueError('repeat final DMA/fault state')
  if not exact(policy,PASS=1,submitted=requests,completed=requests,unique_tokens=requests,page_bytes=4096,reuse_after_DONE_and_TX=1,zero=1,audio_acceptance=0):raise ValueError('repeat policy')
  handle=int(qi[0][1]['handle'],16)
  if not 0<handle<=0xffffffff or int(one('pm: r s=0 ')[1]['handle'],16)!=handle:raise ValueError('mapped handle identity')
  used={int(x['id'],16) for prefix in ('tr: q ','pm: q ') for _,x in rows(prefix)}
  intents=rows('marble-glink-apps: remote_intent ')
  ids=[int(q['id'],16) for _,q in qi]
  if len(set(ids))!=1 or ids[0] in used:raise ValueError('same owned recycled intent required')
  tx=[(i,x) for i,x in rows('marble-glink-control: tx ') if x['command']=='9' and int(x['param2'],16)==ids[0]]
  if len(tx)!=requests:raise ValueError('two canonical WREP DATA frames required')
  for index in range(requests):
   qpos,q=qi[index];rpos,r=ri[index];dpos,d=di[index];retpos,ret=retired[index];token=f'{0x4d504401+index:x}'
   iid=ids[index];cap=int(q['cap'])
   if not exact(q,t=token,n=68,bytes=1024 if mfc else 4096,frames=256 if mfc else 512,rate=48000 if mfc else 96000,bits=16 if mfc else 32,channels=2,zero=1) or int(q['handle'],16)!=handle or not 68<=cap<528:raise ValueError('repeat WREP exact identity/geometry')
   if sum(int(x['id'],16)==iid and int(x['bytes'])==cap for _,x in intents)!=1:raise ValueError('owned advertised intent capacity')
   if not exact(r,t=token,error=0,md=0,kind=1,n=56) or int(r['handle'],16)!=handle:raise ValueError('repeat requires matching typed DONE56')
   if not exact(d,c=14) or int(d['id'],16)!=iid:raise ValueError('RX_DONE_W_REUSE required for recycled intent')
   if not exact(ret,index=index,token=token,faults_ok=1,zero=1) or int(ret['id'],16)!=iid:raise ValueError('typed retired record identity')
   tpos,t=tx[index]
   if not exact(t,param1=1,bytes=88) or not tpos<qpos<min(rpos,dpos)<=max(rpos,dpos)<retpos<ei<pi:raise ValueError('repeat submission/retirement ordering')
   if index and not retired[index-1][0]<tpos:raise ValueError('page/intent reused before previous DONE and TX retirement')
   fragments=[x for x in fi if qpos<x[0]<retpos]
   acc=left=packets=0
   for _,f in fragments:
    c=int(f['c']);n=int(f['n']);remain=int(f['l']);total=int(f['u'])
    if not 0<n<=512 or not 0<=remain<=512-n or int(f['f'])!=((16+n+7)&~7) or (not acc and c!=9) or (acc and (c!=12 or remain!=left-n)) or total!=acc+n:raise ValueError('repeat DONE fragment continuity')
    acc=total;left=remain
    if not left:
     if packets or acc!=56:raise ValueError('exact DONE56 assembly required')
     packets=1;acc=0
   if acc or packets!=1:raise ValueError('repeat DONE assembly incomplete')
  if any(not any(qi[i][0]<pos<retired[i][0] for i in range(requests)) for pos,_ in fi):raise ValueError('extra PCM fragments outside request')
  if len(rows('pr:'))!=requests+1:raise ValueError('unexpected repeat records')
  order=[one('pm: r s=0 ')[0],one('tr: r s=2 ')[0],*[pos for i in range(requests) for pos in (qi[i][0],retired[i][0])],ei,pi,
         one('tr: q s=3 ')[0],one('tr: r s=4 ')[0],one('pm: q s=1 ')[0],one('pm: end ')[0],one('marble-smccc: PAS_stop ')[0]]
  if order!=sorted(order) or one('tr: d s=2 ')[0]>=qi[0][0]:raise ValueError('repeat graph/MAP/START/STOP/UNMAP/PAS lifetime')
 except (KeyError,ValueError,TypeError) as e:errors.append(str(e))
 return dict(passed=not errors,errors=errors,handle=handle,PCM_requests=requests,PCM_bytes=requests*(1024 if mfc else 4096),PCM_frames=requests*(256 if mfc else 512),source_rate=48000 if mfc else 96000,source_bits=16 if mfc else 32,MFC_format_negotiation_verified=mfc and not errors,conversion_verified=False,
             physical_buffer_bytes=4096,typed_buffer_consumed=not errors,serial_buffer_recycling_verified=not errors,
             continuous_playback=False,audio_acceptance=False,
             scope=f'{requests} silent{48000 if mfc else 96000}Hz/S{16 if mfc else 32}/stereo WREPs; same4K page and reusable intent only after typed DONE and TX; no audible/continuous playback acceptance')
