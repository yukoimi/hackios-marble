"""Only one disconnected TDM graph OPEN/CLOSE; no playback acceptance."""
import re
def inspect(text):
 def rows(prefix):return [dict(re.findall(r'(\w+)=([^\s]+)',l)) for l in text.splitlines() if l.startswith(prefix)]
 def one(prefix):
  r=rows(prefix)
  if len(r)!=1:raise ValueError('missing/duplicate '+prefix)
  return r[0]
 def exact(r,**kw):return all(r.get(k)==str(v) for k,v in kw.items())
 errors=[];conformant=True;results=[]
 try:
  if rows('prm: rejected '):raise ValueError('rejected response')
  local=one('prm: intent ');end=one('prm: complete ')
  if not exact(local,channel=1,id=1,bytes=512,static=1) or not exact(end,sent=2,replies=2,tx_done=2,request_reply=1,release_reply=1,request_error=0,release_error=0,rejected=0,fragment_active=0,fragment_bytes=0,PAS_stopped=1,pins_ok=1,graph_commands=0,amp_commands=0,audio_acceptance=0):raise ValueError('graph did not open/close and retire')
  remote=int(one('marble-glink-apps: complete ')['remote_channel'])
  queries=rows('prm: query ');replies=rows('prm: reply ');done=rows('prm: tx_done ')
  if len(queries)!=2 or len(replies)!=2 or len(done)!=2:raise ValueError('exactly two matched graph transactions required')
  intents=rows('marble-glink-apps: remote_intent ');used=set()
  for i,(q,r) in enumerate(zip(queries,replies)):
   token=('4d434b01','4d434b02')[i];op=('100100f','1001010')[i];size=(76,64)[i]
   if not exact(q,stage=i,token=token,opcode=op,bytes=size) or not exact(r,stage=i,token=token,kind=1,error=0,bytes=32,chan=remote,intent=1):raise ValueError('graph request/reply identity or result')
   iid=int(q['id'],16);capacity=int(q['cap']);client=int(r['client_data'],16)
   if iid in used or not size<=capacity<=65536 or sum(int(x['id'],16)==iid and int(x['bytes'])==capacity for x in intents)!=1:raise ValueError('distinct remote intent ownership')
   used.add(iid)
   if not 0<=client<=255:raise ValueError('client byte bounds')
   conformant=conformant and not bool(client&0xf0);results.append(0)
   d=[x for x in done if x.get('stage')==str(i)]
   if len(d)!=1 or d[0]['command'] not in ('6','14') or not exact(d[0],chan=remote) or int(d[0]['id'],16)!=iid:raise ValueError('TX retirement')
  tx=rows('marble-glink-control: tx ');rx=rows('marble-glink-control: rx ')
  data=[x for x in tx if x['command']=='9'];adverts=[x for x in tx if x['command']=='5']
  if len(adverts)!=1 or not exact(adverts[0],param1=1,param2=1,bytes=16) or len(data)!=2 or len([x for x in tx if x['command']=='14'])!=2 or len([x for x in rx if x['command'] in ('6','14')])!=2:raise ValueError('transport packet/retirement counts')
  for i,(q,d) in enumerate(zip(queries,data)):
   if not exact(d,param1=1,bytes=(96,80)[i]) or int(d['param2'],16)!=int(q['id'],16):raise ValueError('outgoing DATA geometry')
  fragments=rows('prm: fragment ')
  if len([x for x in rx if x['command'] in ('9','12')])!=len(fragments):raise ValueError('fragment count')
  packet=acc=left=0
  for f in fragments:
   cmd=int(f['command']);chunk=int(f['chunk']);remaining=int(f['left']);used_bytes=int(f['used'])
   if not exact(f,channel=remote,intent=1) or not 0<chunk<=512 or not 0<=remaining<=512-chunk or int(f['frame'])!=((16+chunk+7)&~7):raise ValueError('fragment geometry')
   if (not acc and cmd!=9) or (acc and (cmd!=12 or chunk>left or remaining!=left-chunk)) or used_bytes!=acc+chunk or used_bytes>512:raise ValueError('fragment continuity')
   acc=used_bytes;left=remaining
   if not left:
    if packet>=2 or int(replies[packet]['bytes'])!=acc:raise ValueError('reply not backed by fragments')
    packet+=1;acc=0
  if acc or packet!=2:raise ValueError('incomplete assembly')
  guards=rows('prm: guard ');pins=rows('prm: pin ')
  if len(guards)!=2 or len(pins)!=14:raise ValueError('complete before/after GPIO guard required')
  initial={}
  for after in (0,1):
   g=[x for x in guards if x.get('after')==str(after)]
   if len(g)!=1 or not exact(g[0],PASS=1,writes=0):raise ValueError('guard failed')
   for i in range(7):
    x=[x for x in pins if exact(x,after=after,index=i)]
    if len(x)!=1:raise ValueError('pin count/identity')
    cfg=int(x[0]['cfg'],16);io=int(x[0]['io'],16)
    if not 0<=cfg<=0xffffffff or not 0<=io<=0xffffffff or (i<3 and (cfg!=1 or io&3)) or (i>=3 and cfg&0x23c):raise ValueError('amp reset/switch or TDM GPIO unsafe')
    if not after:initial[i]=(cfg,io)
    elif cfg!=initial[i][0] or (io^initial[i][1])&2:raise ValueError('GPIO changed across clock lease')
  order=['marble-glink-apps: open_ack ','prm: intent ','prm: guard after=0 ','prm: query stage=0 ','prm: reply stage=0 ','prm: query stage=1 ','prm: reply stage=1 ','marble-smccc: PAS_stop ','prm: guard after=1 ','prm: complete ']
  positions=[text.find(x) for x in order]
  if min(positions)<0 or positions!=sorted(positions):raise ValueError('open/close/PAS ordering')
 except (KeyError,ValueError,TypeError) as e:errors.append(str(e))
 return dict(passed=not errors,errors=errors,clock_id='204',requested_hz=12288000,attribute=4,root=0,
  clock_lease_verified=not errors,header_conformant=conformant,graph_commands=0,GPIO_writes=0,
  amplifier_commands=0,audio_acceptance=False,scope='typed PRM request/release with passive GPIO guards and PAS stop; no measured clock waveform or playback')
