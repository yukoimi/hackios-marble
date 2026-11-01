"""Only one disconnected TDM graph OPEN/CLOSE; no playback acceptance."""
import re
def inspect(text,configure=False):
 def rows(prefix):return [dict(re.findall(r'(\w+)=([^\s]+)',l)) for l in text.splitlines() if l.startswith(prefix)]
 def one(prefix):
  r=rows(prefix)
  if len(r)!=1:raise ValueError('missing/duplicate '+prefix)
  return r[0]
 def exact(r,**kw):return all(r.get(k)==str(v) for k,v in kw.items())
 errors=[];conformant=True;results=[]
 try:
  if rows('marble-tdm-graph: rejected '):raise ValueError('rejected response')
  configs=rows('marble-tdm-graph: configuration ')
  if configure:
   if len(configs)!=1 or not exact(configs[0],params=3):raise ValueError('qualified TDM configuration missing')
  elif configs:raise ValueError('TDM configuration requires explicit contract')
  local=one('marble-tdm-graph: local_intent ');end=one('marble-tdm-graph: complete ')
  if not exact(local,channel=1,id=1,bytes=512,static=1) or not exact(end,sent=2,replies=2,tx_done=2,open_reply=1,close_reply=1,open_error=0,close_error=0,rejected=0,fragment_active=0,fragment_bytes=0,PAS_stopped=1,graph='4001',module='700100e',prepare_commands=0,start_commands=0,clock_commands=0,amp_commands=0,audio_acceptance=0):raise ValueError('graph did not open/close and retire')
  remote=int(one('marble-glink-apps: complete ')['remote_channel'])
  queries=rows('marble-tdm-graph: query ');replies=rows('marble-tdm-graph: reply ');done=rows('marble-tdm-graph: tx_done ')
  if len(queries)!=2 or len(replies)!=2 or len(done)!=2:raise ValueError('exactly two matched graph transactions required')
  intents=rows('marble-glink-apps: remote_intent ');used=set()
  for i,(q,r) in enumerate(zip(queries,replies)):
   token=('4d475201','4d475202')[i];op=('1001000','1001004')[i];size=(400 if configure else 296,64)[i]
   if not exact(q,stage=i,token=token,opcode=op,bytes=size) or not exact(r,stage=i,token=token,error=0,bytes=32,remote_channel=remote,local_intent=1):raise ValueError('graph request/reply identity or result')
   iid=int(q['remote_id'],16);capacity=int(q['remote_bytes']);client=int(r['client_data'],16)
   if iid in used or not size<=capacity<=65536 or sum(int(x['id'],16)==iid and int(x['bytes'])==capacity for x in intents)!=1:raise ValueError('distinct remote intent ownership')
   used.add(iid)
   if not 0<=client<=255:raise ValueError('client byte bounds')
   conformant=conformant and not bool(client&0xf0);results.append(0)
   d=[x for x in done if x.get('stage')==str(i)]
   if len(d)!=1 or d[0]['command'] not in ('6','14') or not exact(d[0],remote_channel=remote) or int(d[0]['remote_id'],16)!=iid:raise ValueError('TX retirement')
  tx=rows('marble-glink-control: tx ');rx=rows('marble-glink-control: rx ')
  data=[x for x in tx if x['command']=='9'];adverts=[x for x in tx if x['command']=='5']
  if len(adverts)!=1 or not exact(adverts[0],param1=1,param2=1,bytes=16) or len(data)!=2 or len([x for x in tx if x['command']=='14'])!=2 or len([x for x in rx if x['command'] in ('6','14')])!=2:raise ValueError('transport packet/retirement counts')
  for i,(q,d) in enumerate(zip(queries,data)):
   if not exact(d,param1=1,bytes=(416 if configure else 312,80)[i]) or int(d['param2'],16)!=int(q['remote_id'],16):raise ValueError('outgoing DATA geometry')
  fragments=rows('marble-tdm-graph: fragment ')
  if len([x for x in rx if x['command'] in ('9','12')])!=len(fragments):raise ValueError('fragment count')
  packet=acc=left=0
  for f in fragments:
   cmd=int(f['command']);chunk=int(f['chunk']);remaining=int(f['left']);used_bytes=int(f['accumulated'])
   if not exact(f,channel=remote,intent=1) or not 0<chunk<=512 or not 0<=remaining<=512-chunk or int(f['frame_bytes'])!=((16+chunk+7)&~7):raise ValueError('fragment geometry')
   if (not acc and cmd!=9) or (acc and (cmd!=12 or chunk>left or remaining!=left-chunk)) or used_bytes!=acc+chunk or used_bytes>512:raise ValueError('fragment continuity')
   acc=used_bytes;left=remaining
   if not left:
    if packet>=2 or int(replies[packet]['bytes'])!=acc:raise ValueError('reply not backed by fragments')
    packet+=1;acc=0
  if acc or packet!=2:raise ValueError('incomplete assembly')
  order=['marble-glink-apps: open_ack ','marble-tdm-graph: local_intent ','marble-tdm-graph: query stage=0 ','marble-tdm-graph: reply stage=0 ','marble-tdm-graph: query stage=1 ','marble-tdm-graph: reply stage=1 ','marble-smccc: PAS_stop ','marble-tdm-graph: complete ']
  positions=[text.find(x) for x in order]
  if min(positions)<0 or positions!=sorted(positions):raise ValueError('open/close/PAS ordering')
 except (KeyError,ValueError,TypeError) as e:errors.append(str(e))
 return dict(passed=not errors,errors=errors,graph_id='4001',module_id='0700100e',
  graph_created_and_closed=not errors,board_configuration_accepted=configure and not errors,header_conformant=conformant,prepare_commands=0,start_commands=0,
  explicit_clock_commands=0,explicit_amplifier_commands=0,audio_acceptance=False)
