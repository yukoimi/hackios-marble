"""Audit one TDM library acquire/release, independently of PAS recovery."""
import re

def inspect(text,module_id='700100e'):
 if module_id not in ('700100e','7001015'):return dict(passed=False,errors=['unsupported AMDB module'],audio_acceptance=False)
 def rows(prefix):return [dict(re.findall(r'(\w+)=([^\s]+)',l)) for l in text.splitlines() if l.startswith(prefix)]
 def one(prefix):
  r=rows(prefix)
  if len(r)!=1:raise ValueError('missing/duplicate '+prefix)
  return r[0]
 def exact(r,**kw):return all(r.get(k)==str(v) for k,v in kw.items())
 errors=[];handle=0;load_error=None;released=False;conformant=True
 try:
  if rows('marble-amdb-cycle: rejected '):raise ValueError('rejected response')
  local=one('marble-amdb-cycle: local_intent ');end=one('marble-amdb-cycle: complete ')
  if not exact(local,channel=1,id=1,bytes=512,static=1) or not exact(end,rejected=0,fragment_active=0,fragment_bytes=0,PAS_stopped=1,graph_commands=0,audio_acceptance=0):raise ValueError('local intent/retirement')
  app=one('marble-glink-apps: complete ');remote=int(app['remote_channel'])
  queries=rows('marble-amdb-cycle: query ');replies=rows('marble-amdb-cycle: reply ');done=rows('marble-amdb-cycle: tx_done ')
  n=len(queries)
  if n not in (1,2) or len(replies)!=n or len(done)!=n or not exact(end,sent=n,replies=n,tx_done=n,load_reply=1,unload_reply=n-1):raise ValueError('bounded matched requests/replies/completions')
  intents=rows('marble-glink-apps: remote_intent ');used=set()
  for i,(q,r) in enumerate(zip(queries,replies)):
   token='4d4c4401' if i==0 else '4d4c4402';opcode='1001020' if i==0 else '1001022'
   if not exact(q,stage=i,token=token,opcode=opcode,bytes=64,module=module_id,graph_commands=0,audio_commands=0) or not exact(r,stage=i,token=token,remote_channel=remote,local_intent=1):raise ValueError('request/reply identity')
   iid=int(q['remote_id'],16);size=int(q['remote_bytes'])
   if iid in used or not 64<=size<=65536 or sum(int(x['id'],16)==iid and int(x['bytes'])==size for x in intents)!=1:raise ValueError('distinct remote intent ownership')
   used.add(iid)
   d=[x for x in done if x.get('stage')==str(i)]
   if len(d)!=1 or d[0]['command'] not in ('6','14') or not exact(d[0],remote_channel=remote) or int(d[0]['remote_id'],16)!=iid:raise ValueError('TX retirement')
   kind=int(r['kind']);err=int(r['error'],16);client=int(r['client_data'],16);rh=int(r['handle'],16)
   if not 0<=err<=0xffffffff or not 0<=client<=255 or not 0<=rh<=0xffffffffffffffff:raise ValueError('reply bounds')
   conformant=conformant and not bool(client&0xf0)
   if i==0:
    if int(q['handle'],16) or kind not in (1,2) or int(r['bytes'])!=(48 if kind==1 else 32):raise ValueError('load geometry')
    if kind==1 and (client or (not err and not rh)):raise ValueError('load handle/result')
    if kind==2 and (not err or rh):raise ValueError('basic load error cannot grant a handle')
    handle=rh;load_error=err
   else:
    if not handle or int(q['handle'],16)!=handle or kind!=2 or int(r['bytes'])!=32 or rh or err:raise ValueError('unload did not release the acquired handle')
    released=True
   if int(end['load_error' if i==0 else 'unload_error'],16)!=err:raise ValueError('summary error mismatch')
  if int(end['handle'],16)!=handle or bool(handle)!=(n==2):raise ValueError('acquired handle requires exactly one release')
  tx=rows('marble-glink-control: tx ');rx=rows('marble-glink-control: rx ')
  data=[x for x in tx if x['command']=='9'];adverts=[x for x in tx if x['command']=='5']
  if len(adverts)!=1 or not exact(adverts[0],param1=1,param2=1,bytes=16) or len(data)!=n or len([x for x in tx if x['command']=='14'])!=n or len([x for x in rx if x['command'] in ('6','14')])!=n:raise ValueError('transport packet/retirement counts')
  for q,d in zip(queries,data):
   if not exact(d,param1=1,bytes=80) or int(d['param2'],16)!=int(q['remote_id'],16):raise ValueError('outgoing DATA geometry')
  fragments=rows('marble-amdb-cycle: fragment ')
  if len([x for x in rx if x['command'] in ('9','12')])!=len(fragments):raise ValueError('fragment count')
  packet=acc=left=0
  for f in fragments:
   cmd=int(f['command']);chunk=int(f['chunk']);remaining=int(f['left']);used_bytes=int(f['accumulated'])
   if not exact(f,channel=remote,intent=1) or not 0<chunk<=512 or not 0<=remaining<=512-chunk or int(f['frame_bytes'])!=((16+chunk+7)&~7):raise ValueError('fragment geometry')
   if (not acc and cmd!=9) or (acc and (cmd!=12 or chunk>left or remaining!=left-chunk)) or used_bytes!=acc+chunk or used_bytes>512:raise ValueError('fragment continuity')
   acc=used_bytes;left=remaining
   if not left:
    if packet>=n or int(replies[packet]['bytes'])!=acc:raise ValueError('reply not backed by fragments')
    packet+=1;acc=0
  if acc or packet!=n:raise ValueError('incomplete assembly')
  order=['marble-glink-apps: open_ack ','marble-amdb-cycle: local_intent ','marble-amdb-cycle: query stage=0 ','marble-amdb-cycle: reply stage=0 ']
  if handle:order+=['marble-amdb-cycle: query stage=1 ','marble-amdb-cycle: reply stage=1 ']
  order+=['marble-smccc: PAS_stop ','marble-amdb-cycle: complete ']
  positions=[text.find(x) for x in order]
  if min(positions)<0 or positions!=sorted(positions):raise ValueError('acquire/release/PAS ordering')
 except (KeyError,ValueError,TypeError) as e:errors.append(str(e))
 return dict(passed=not errors,errors=errors,module_id=f'{int(module_id,16):08x}',load_error=load_error,
  module_acquired=bool(handle) and not errors,module_released=released and not errors,
  header_conformant=conformant,graph_commands=0,audio_acceptance=False)
