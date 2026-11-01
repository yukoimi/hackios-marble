"""Five-command silent TDM lifecycle; no PCM or speaker-output acceptance."""
import re

def inspect(text,pcm_graph=False,pcm_memory=False,pcm_data=False,tdm_pins=False,mfc=False):
 def rows(prefix):return [dict(re.findall(r'(\w+)=([^\s]+)',l)) for l in text.splitlines() if l.startswith(prefix)]
 def one(prefix):
  r=rows(prefix)
  if len(r)!=1:raise ValueError('missing/duplicate '+prefix)
  return r[0]
 def exact(r,**kw):return all(r.get(k)==str(v) for k,v in kw.items())
 # Expand compact constrained-EFI diagnostics before the readable validator.
 expanded=[]
 labels={'end':'complete','i':'intent','q':'query','f':'fragment','r':'reply','d':'done'}
 for line in text.splitlines():
  if line.startswith('tr: '):
   tag=line.split()[1];line='tdm: '+labels.get(tag,tag)+line[len('tr: '+tag):]
   fields={'s':'sent' if tag=='end' else 'stage','t':'token','c':'client' if tag=='r' else 'command','n':'chunk' if tag=='f' else 'bytes','l':'left','u':'used','f':'fragment' if tag=='end' else 'frame','rx':'replies','d':'done','r':'reply','bad':'rejected','p':'pins','m':'mask','e':'errors'}
   for a,b in fields.items():line=re.sub(r'\b'+a+'=',b+'=',line)
  expanded.append(line)
 text='\n'.join(expanded)
 errors=[];conformant=True
 try:
  if pcm_graph:
   if not exact(one('tdm: topology '),modules=3 if mfc else 2,links=2 if mfc else 1,rate=96000,bits=32,channels=2,buffers=int(bool(pcm_data))):raise ValueError('connected PCM topology missing')
  elif rows('tdm: topology '):raise ValueError('connected PCM topology requires explicit contract')
  if rows('tdm: rejected'):raise ValueError('rejected reply')
  if not exact(one('tdm: intent '),bytes=512):raise ValueError('local intent')
  if not exact(one('tdm: complete '),sent=5,replies=5,done=5,mask='1f',tx='1f',reply='1f',rejected=0,pins=1,fragment=0,bytes=0,errors='0,0,0,0,0'):raise ValueError('five successful retired commands required')
  q=rows('tdm: query ');r=rows('tdm: reply ');done=rows('tdm: done ')
  if len(q)!=5 or len(r)!=5 or len(done)!=5:raise ValueError('transaction counts')
  intents=rows('marble-glink-apps: remote_intent ');used=set()
  tx=rows('marble-glink-control: tx ');rx=rows('marble-glink-control: rx ')
  data=[x for x in tx if x['command']=='9']
  if len(data)!=(7+int(pcm_data) if pcm_data else 7 if pcm_memory else 5) or len([x for x in tx if x['command']=='5'])!=1 or len([x for x in tx if x['command']=='14'])!=(7+int(pcm_data) if pcm_data else 7 if pcm_memory else 5) or len([x for x in rx if x['command'] in ('6','14')])!=(7+int(pcm_data) if pcm_data else 7 if pcm_memory else 5):raise ValueError('transport counts')
  if pcm_memory:
   graph_ids={int(x['id'],16) for x in q};data=[x for x in data if int(x['param2'],16) in graph_ids]
  if len(data)!=5:raise ValueError('five graph DATA identities required')
  order=['marble-glink-apps: open_ack ','tdm: intent ','prm: guard after=0 ']
  for i in range(5):
   token=f'{0x4d545201+i:x}';n=64 if i else (608 if mfc else 528) if pcm_graph else 432
   if not exact(q[i],stage=i,token=token,bytes=n) or not exact(r[i],stage=i,token=token,error=0,bytes=32):raise ValueError('request/reply identity/status')
   iid=int(q[i]['id'],16);cap=int(q[i]['cap']);client=int(r[i]['client'],16)
   if iid in used or not n<=cap<=65536 or sum(int(x['id'],16)==iid and int(x['bytes'])==cap for x in intents)!=1:raise ValueError('distinct remote intents')
   used.add(iid)
   if not 0<=client<=255:raise ValueError('client byte')
   conformant=conformant and not bool(client&0xf0)
   d=[x for x in done if x.get('stage')==str(i)]
   if len(d)!=1 or d[0]['command'] not in ('6','14') or int(d[0]['id'],16)!=iid:raise ValueError('TX retirement')
   if not exact(data[i],param1=1,bytes=80 if i else (624 if mfc else 544) if pcm_graph else 448) or int(data[i]['param2'],16)!=iid:raise ValueError('DATA geometry')
   qp=text.find(f'tdm: query stage={i} ');rp=text.find(f'tdm: reply stage={i} ');dp=text.find(f'tdm: done stage={i} ')
   if min(qp,rp,dp)<0 or qp>=min(rp,dp):raise ValueError('reply/retirement before query')
   if i and max(text.find(f'tdm: reply stage={i-1} '),text.find(f'tdm: done stage={i-1} '))>=qp:raise ValueError('next command before previous retirement')
   order.extend([f'tdm: query stage={i} ',f'tdm: reply stage={i} '])
  fragments=rows('tdm: fragment ')
  if len([x for x in rx if x['command'] in ('9','12')])!=len(fragments)+(len(rows('pm: f ')) if pcm_memory else 0)+(len(rows('pd: f ')) if pcm_data else 0):raise ValueError('fragment count')
  packet=acc=left=0
  for f in fragments:
   cmd=int(f['command']);chunk=int(f['chunk']);remaining=int(f['left']);total=int(f['used'])
   if not 0<chunk<=512 or not 0<=remaining<=512-chunk or int(f['frame'])!=((16+chunk+7)&~7):raise ValueError('fragment geometry')
   if (not acc and cmd!=9) or (acc and (cmd!=12 or chunk>left or remaining!=left-chunk)) or total!=acc+chunk or total>512:raise ValueError('fragment continuity')
   acc=total;left=remaining
   if not left:
    if packet>=5 or acc!=32:raise ValueError('reply assembly')
    packet+=1;acc=0
  if acc or packet!=5:raise ValueError('incomplete assembly')
  guards=rows('prm: guard ');pins=rows('prm: pin ');initial={}
  if len(guards)!=2 or len(pins)!=14:raise ValueError('passive pin guard count')
  for after in (0,1):
   g=[x for x in guards if x.get('after')==str(after)]
   if len(g)!=1 or not exact(g[0],PASS=1,writes=0):raise ValueError('GPIO guard failed')
   for i in range(7):
    x=[x for x in pins if exact(x,after=after,index=i)]
    if len(x)!=1:raise ValueError('GPIO guard identity')
    cfg=int(x[0]['cfg'],16);io=int(x[0]['io'],16)
    if not 0<=cfg<=0xffffffff or not 0<=io<=0xffffffff or (i<3 and (cfg!=1 or io&3)) or (i>=3 and cfg&0x23c):raise ValueError('unsafe amp reset/switch/TDM pin')
    if not after:initial[i]=(cfg,io)
    elif cfg!=initial[i][0] or (io^initial[i][1])&2:raise ValueError('GPIO changed')
  order.extend(['marble-smccc: PAS_stop ','prm: guard after=1 ','tdm: complete '])
  pos=[text.find(x) for x in order]
  if min(pos)<0 or pos!=sorted(pos):raise ValueError('lifecycle/PAS order')
 except (KeyError,ValueError,TypeError) as e:errors.append(str(e))
 return dict(passed=not errors,errors=errors,silent_TDM_lifecycle_verified=not errors,connected_PCM_graph_lifecycle_verified=pcm_graph and not errors,header_conformant=conformant,
  graph_id='4001',clock_id='204',requested_hz=12288000,GPIO_writes=12 if tdm_pins else 0,amplifier_commands=0,PCM_buffers=int(bool(pcm_data)),audio_acceptance=False,
  scope=('connected write-shared-memory source to calibrated TDM sink' if pcm_graph else 'board-calibrated disconnected TDM sink')+' OPEN/PREPARE/START/STOP/CLOSE and passive GPIO guards; no physical waveform or audible playback acceptance')
