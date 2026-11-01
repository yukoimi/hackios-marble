"""One explicitly bounded read-only GPR SPF exchange; no audio acceptance."""
import re

def inspect(text):
 def rows(prefix):return [dict(re.findall(r'(\w+)=([^\s]+)',l)) for l in text.splitlines() if l.startswith(prefix)]
 def one(prefix):
  found=rows(prefix)
  if len(found)!=1:raise ValueError('missing/duplicate '+prefix)
  return found[0]
 def exact(row,**kw):return all(row.get(k)==str(v) for k,v in kw.items())
 errors=[];ready=False;state=None
 try:
  local=one('marble-gpr: local_intent ');q=one('marble-gpr: query ');end=one('marble-gpr: complete ')
  app=one('marble-glink-apps: complete ');remote=int(app['remote_channel'])
  if not exact(local,channel=1,id=1,bytes=512,static=1) or not exact(q,token='4d535001',opcode='1001021',bytes=24,local_intents=1,audio_commands=0):raise ValueError('not the single bounded read-only command')
  iid=int(q['remote_id'],16);size=int(q['remote_bytes'])
  intents=rows('marble-glink-apps: remote_intent ')
  if not 24<=size<=65536 or sum(int(r['id'],16)==iid and int(r['bytes'])==size for r in intents)!=1:raise ValueError('outgoing intent ownership')
  tx=rows('marble-glink-control: tx ');rx=rows('marble-glink-control: rx ')
  data=[r for r in tx if r['command']=='9'];advert=[r for r in tx if r['command']=='5'];done=rows('marble-gpr: tx_done ')
  if len(data)!=1 or not exact(data[0],param1=1,bytes=40) or int(data[0]['param2'],16)!=iid or len(advert)!=1 or not exact(advert[0],param1=1,param2=1,bytes=16):raise ValueError('single DATA/local INTENT geometry')
  if len(done)!=1 or done[0]['command'] not in ('6','14') or int(done[0]['remote_id'],16)!=iid or int(done[0]['remote_channel'])!=remote:raise ValueError('remote TX completion missing')
  replies=rows('marble-gpr: reply ');fragments=rows('marble-gpr: fragment ')
  if not 1<=len(replies)<=2 or len(replies)!=int(end['replies']):raise ValueError('no matched GPR reply')
  if not exact(end,sent=1,tx_done=1,fragment_active=0,fragment_bytes=0,PAS_stopped=1,audio_acceptance=0):raise ValueError('exchange did not retire after PAS stop')
  if len([r for r in tx if r['command']=='14'])!=len(replies) or len([r for r in rx if r['command'] in ('9','12')])!=len(fragments):raise ValueError('DATA/RX_DONE count')
  if len([r for r in rx if r['command'] in ('6','14')])!=1:raise ValueError('TX_DONE count')
  packet=acc=left=0
  for f in fragments:
   cmd=int(f['command']);n=int(f['chunk']);remaining=int(f['left']);used=int(f['accumulated'])
   if not exact(f,channel=remote,intent=1) or not 0<n<=512 or not 0<=remaining<=512-n or int(f['frame_bytes'])!=(16+n+7)&~7:raise ValueError('fragment geometry')
   if (not acc and cmd!=9) or (acc and (cmd!=12 or n>left or remaining!=left-n)) or used!=acc+n or used>512:raise ValueError('fragment continuity')
   acc=used;left=remaining
   if not left:
    if packet>=len(replies) or int(replies[packet]['bytes'])!=acc:raise ValueError('reply length not backed by fragments')
    packet+=1;acc=0
  if acc or packet!=len(replies):raise ValueError('unfinished packet')
  kinds=set()
  for r in replies:
   kind=int(r['kind'])
   if kind in kinds or kind not in (1,2) or not exact(r,token='4d535001',remote_channel=remote,local_intent=1) or int(r['bytes'])!=(28 if kind==1 else 32):raise ValueError('reply identity/header')
   kinds.add(kind)
   if kind==1:
    state=int(r['state'])
    if state not in (0,1) or int(r['error'],16):raise ValueError('invalid SPF state')
   elif int(r['error'],16):raise ValueError('DSP basic response reports error')
  if int(end['SPF_state'])!=(0 if state is None else state+1):raise ValueError('SPF result disagrees with reply')
  ready=state==1
  order=['marble-glink-apps: open_ack ','marble-gpr: local_intent ','marble-gpr: query ','marble-gpr: reply ','marble-smccc: PAS_stop ','marble-gpr: complete ']
  positions=[text.find(x) for x in order]
  if min(positions)<0 or positions!=sorted(positions):raise ValueError('query/response/stop ordering')
 except (KeyError,ValueError,TypeError) as e:errors.append(str(e))
 return dict(passed=not errors,errors=errors,exchange_verified=not errors,spf_state=state,spf_ready=ready and not errors,audio_acceptance=False)
