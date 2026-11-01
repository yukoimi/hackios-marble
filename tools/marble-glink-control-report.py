"""Accept control traffic only with bounded queues, owned IRQs and a real stop."""
import argparse,importlib.util,json,re
from pathlib import Path
def inspect(text,apps=False,gpr=False,amdb=False,amdb_cycle=False,tdm_graph=False,tdm_config=False,audio_clock=False,tdm_run=False,tdm_pcm_graph=False,audio_dma=False,pcm_memory=False,pcm_data=False,tdm_pins=False,mfc=False):
 def rows(prefix):return [dict(re.findall(r'(\w+)=([^\s]+)',line)) for line in text.splitlines() if line.startswith(prefix)]
 def one(prefix):
  found=rows(prefix);return found[0] if len(found)==1 else {}
 def exact(row,**values):return all(row.get(k)==str(v) for k,v in values.items())
 errors=[]
 if tdm_pins and not pcm_data:errors.append("TDM pins require explicit silent PCM contract")
 if not tdm_pins and "tp: begin " in text:errors.append("TDM pin writes require explicit contract")
 if pcm_data and not pcm_memory:errors.append("PCM submission requires explicit shared-memory contract")
 if not pcm_data and "pd: q " in text:errors.append("PCM submission requires explicit contract")
 if pcm_memory and not (tdm_pcm_graph and audio_dma):errors.append("memory lifecycle requires explicit PCM graph/DMA contract")
 if not pcm_memory and "pm: lease " in text:errors.append("memory lifecycle requires explicit contract")
 if audio_dma and not tdm_pcm_graph:errors.append('DMA inventory requires explicit PCM graph contract')
 if not audio_dma and 'marble-audio-dma: begin ' in text:errors.append('DMA inventory requires explicit read-only contract')
 if tdm_pcm_graph and not tdm_run:errors.append('PCM graph requires explicit TDM run contract')
 if not tdm_pcm_graph and 'tr: topology ' in text:errors.append('PCM graph requires explicit topology contract')
 if tdm_run and (not gpr or amdb or amdb_cycle or tdm_graph or audio_clock):errors.append('TDM run requires exclusive GPR contract')
 if not tdm_run and rows('tr: end '):errors.append('TDM run requires explicit contract')
 if tdm_config and not tdm_graph:errors.append('TDM calibration requires explicit graph contract')
 if not tdm_config and rows('marble-tdm-graph: configuration '):errors.append('TDM calibration requires explicit contract')
 packet_count=1 if gpr else 0
 if tdm_run:
  try:
   packet_count=int(one('tr: end ')['s'])
   if not 1<=packet_count<=5:raise ValueError('TDM packet bound')
  except (KeyError,ValueError):errors.append('missing/invalid TDM completion')
 if pcm_memory:packet_count+=2
 if pcm_data:packet_count+=int(pcm_data)
 if audio_clock:
  if not gpr or amdb or amdb_cycle or tdm_graph or tdm_config:errors.append('audio clock requires explicit exclusive GPR contract')
  try:
   packet_count=int(one('prm: complete ')['sent'])
   if packet_count not in (1,2):raise ValueError('clock packet bound')
  except (KeyError,ValueError):errors.append('missing/invalid clock completion')
 if not audio_clock and rows('prm: complete '):errors.append('audio clock requires explicit contract')
 if tdm_graph:
  if not gpr or amdb or amdb_cycle:errors.append('TDM graph requires explicit exclusive GPR contract')
  try:
   packet_count=int(one('marble-tdm-graph: complete ')['sent'])
   if packet_count not in (1,2):raise ValueError('graph packet bound')
  except (KeyError,ValueError):errors.append('missing/invalid TDM graph completion')
 if not tdm_graph and rows('marble-tdm-graph: complete '):errors.append('TDM graph requires explicit contract')
 if amdb_cycle:
  if not gpr or amdb:errors.append('library cycle requires explicit exclusive GPR contract')
  try:
   packet_count=int(one('marble-amdb-cycle: complete ')['sent'])
   if packet_count not in (1,2):raise ValueError('cycle packet bound')
  except (KeyError,ValueError):errors.append('missing/invalid library cycle completion')
 if not amdb_cycle and rows('marble-amdb-cycle: complete '):errors.append('library cycle requires explicit contract')
 if amdb and not gpr:errors.append('AMDB requires explicit bounded GPR contract')
 if not amdb and rows('marble-amdb: complete '):errors.append('AMDB requires explicit metadata contract')
 if gpr and not apps:errors.append("GPR requires explicit application contract")
 if not gpr and rows("marble-gpr: complete "):errors.append("GPR requires explicit read-only query contract")
 if not apps and rows('marble-glink-apps: complete '):errors.append('application channel requires an explicit contract')
 begin=one('marble-glink-control: begin ');end=one('marble-glink-control: complete ')
 if rows('marble-glink-control: refused '):errors.append('control service explicitly refused')
 if not exact(begin,qualified_SMEM=1,local_head=0,local_tail=0,native_dispatch=0,allocator_calls=0,local_channels=0,DSP_start=0):errors.append('fresh qualified control context missing')
 if not exact(end,PASS=1,FIFO_seen=1,version_seen=1,version_ACK=1,native_unchanged=1,allocator_calls=0,native_dispatch=0,local_channels=0,GPR_packets=packet_count,PAS_stopped=1,DSP_start=1,audio_acceptance=0):errors.append('negotiated control link and verified stop required')
 rx=rows('marble-glink-control: rx ');tx=rows('marble-glink-control: tx ');opens=rows('marble-glink-control: remote_open ')
 try:
  q=one('marble-glink-smem: qualified ');remote=one('marble-glink-control: remote ')
  base=int(q['partition'],16);length=int(q['partition_bytes']);address=int(remote['address'],16)
  if not exact(remote,item=480,bytes=16384,NC_pages=1,nonoverlap=1,allocator_calls=0) or not base+32<=address<=base+length-16384 or address&7:raise ValueError('remote FIFO ownership')
  for row in rows('marble-glink-smem: get '):
   if row['item'] not in ('478','479'):continue
   other=int(row['address'],16);n=int(row['bytes'])
   if not (address+16384<=other or other+n<=address):raise ValueError('remote/local FIFO alias')
  if not 2<=len(rx)==int(end['RX_messages'])<=(2048 if apps else 256) or not 2<=len(tx)==int(end['TX_messages'])<=(44 if pcm_data==8 else 32) or not len(opens)==int(end['remote_opens'])<=32:raise ValueError('message/deferred counts')
  # A fresh SMP2P shared-memory ACK does not promise a separately observable
  # receive-ID. Never require the caller to clear a nonexistent notification.
  if not 1<=int(end['GLINK_clears'])<=256 or not 0<=int(end['SMP2P_clears'])<=256:raise ValueError('invalid owned notification counts')
  app=one('marble-glink-apps: complete ') if apps else {}
  remote_channel=int(app['remote_channel']) if apps else 0
  head=tail=data_index=0
  for i,row in enumerate(tx):
   command=int(row['command']);p1=int(row['param1']);p2=int(row['param2'],16);n=24 if command==2 else 16 if gpr and command==5 else (80 if amdb_cycle else 112 if amdb else 40) if gpr and command==9 else 8;head=(head+n)&16383
   if tdm_run and command==9 and pcm_memory:
    sequence=(64,624 if mfc else 544,80,80)+((88,)*int(pcm_data))+(80,80,48) if pcm_data else (64,544,80,80,80,80,48)
    if data_index>=len(sequence):raise ValueError('too many graph/memory packets')
    n=sequence[data_index];head=(head-40+n)&16383;data_index+=1
   elif tdm_run and command==9:
    if data_index>=5:raise ValueError('too many TDM packets')
    n=((624 if mfc else 544) if tdm_pcm_graph else 448) if not data_index else 80;head=(head-40+n)&16383;data_index+=1
   if audio_clock and command==9:
    if data_index>=2:raise ValueError('too many clock packets')
    n=(96,80)[data_index];head=(head-(40 if gpr else 8)+n)&16383;data_index+=1
   if tdm_graph and command==9:
    if data_index>=2:raise ValueError('too many graph packets')
    n=(416 if tdm_config else 312,80)[data_index];head=(head-(40 if gpr else 8)+n)&16383;data_index+=1
   if command not in ((0,1,2,4,5,9,11,14) if gpr else (0,1,2,4,11) if apps else (0,1,11)) or (command in (0,1) and (p1!=1 or p2>1)) or (command==11 and (not 1<=p1<=65535 or p2)) or int(row['local_head'])!=head or (i==0 and command!=0) or ('bytes' in row and int(row['bytes'])!=n):raise ValueError('TX ownership or control geometry')
   if command==2 and (p1!=1 or p2!=10):raise ValueError('wrong local adsp_apps OPEN')
   if command==4 and (p1!=remote_channel or p2):raise ValueError('wrong remote OPEN acknowledgement')
   if command==5 and (p1!=1 or p2!=1):raise ValueError('local intent identity')
   if command==9 and p1!=1:raise ValueError('outgoing DATA channel')
   if command==14 and (p1!=1 or p2!=1):raise ValueError('local RX_DONE ownership')
  for row in rx:
   command=int(row['command']);p1=int(row['param1']);p2=int(row['param2'],16);n=int(row['bytes'])
   expected=(8+p2+7)&~7 if command==2 else 8+8*p2 if command==5 else n if gpr and command in (9,12) else 8
   if command not in ((0,1,2,3,4,5,6,9,12,13,14,15) if gpr else (0,1,2,3,4,5,13,15) if apps else (0,1,2,3,13,15)) or n!=expected or (command==2 and not (1<=p1<=65535 and 2<=p2<=32)) or (command in (0,1) and p1!=1) or (command==13 and (p1 or p2)) or (command in (3,15) and not 1<=p1<=65535):raise ValueError('RX control geometry')
   if command==4 and (p1!=1 or p2):raise ValueError('wrong local OPEN acknowledgement')
   if command==5 and (p1!=remote_channel or not 1<=p2<=64):raise ValueError('remote intent metadata ownership/count')
   if command in (6,9,12,14) and (not gpr or p1!=remote_channel):raise ValueError('GPR channel ownership')
   if command in (9,12) and (p2!=1 or not 24<=n<=528 or n&7):raise ValueError('GPR DATA geometry')
   tail=(tail+n)&16383
   if int(row['local_tail'])!=tail:raise ValueError('RX tail did not advance by validated frame length')
  local=accepted=0
  for row in opens:
   accept=apps and row.get('name')=='adsp_apps'
   if accept:local=1;accepted+=1
   if not exact(row,deferred=0 if accept else 1,acknowledged=1 if accept else 0,local_channels=local) or not 1<=int(row['channel'])<=65535 or not re.fullmatch(r'[A-Za-z0-9_./-]{1,31}',row['name']):raise ValueError('unowned application channel')
   if accept and int(row['channel'])!=remote_channel:raise ValueError('adsp_apps remote identity')
  if apps:
   if accepted!=1 or not exact(app,PASS=1,local_channel=1,requested=1,open_ACK=1,intent_capacity=512,local_intents=1 if gpr else 0,GPR_packets=packet_count,PAS_stopped=1,retired_after_PAS=1,close_handshake=0,audio_acceptance=0):raise ValueError('adsp_apps metadata-only lifecycle incomplete')
   ack=one('marble-glink-apps: open_ack ');intents=rows('marble-glink-apps: remote_intent ')
   if not exact(ack,local_channel=1,remote_channel=remote_channel,local_intents=0,GPR_packets=0) or not 1<=len(intents)==int(app['remote_intents'])<=512:raise ValueError('OPEN ACK/intent count')
   seen=set()
   for i,row in enumerate(intents):
    iid=int(row['id'],16);n=int(row['bytes'])
    if not exact(row,channel=remote_channel,index=i,payload_reads=0) or row.get('local_intents') not in (('0','1') if gpr else ('0',)) or not 0<=iid<=0xffffffff or iid in seen or not 0<n<=65536:raise ValueError('remote intent geometry/identity')
    seen.add(iid)
   if sum(int(row['param2'],16) for row in rx if row['command']=='5')<len(intents):raise ValueError('intents not backed by received control frames')
   app_order=['marble-glink-control: tx command=4 ','marble-glink-control: tx command=2 ',
    'marble-glink-apps: open_ack ','marble-glink-apps: complete ','marble-glink-control: complete ']
   app_positions=[text.find(x) for x in app_order]
   if min(app_positions)<0 or app_positions!=sorted(app_positions):raise ValueError('application open/retirement ordering')
  if sum(int(row['command'])==2 for row in rx)<len(opens):raise ValueError('deferred opens exceed received requests')
  audit=one('marble-smccc: resume_audit ');samples=rows('marble-smccc: interrupted ');raw=one('marble-smccc: PAS_stop ')
  if not exact(audit,USB_progress_calls=0,foreign_clears=0,allocator_calls=0,native_dispatch=0,limit_calls=128,limit_us=1000000,thermal_ok=1) or not len(samples)==int(audit['samples'])==int(raw['calls'])-1 or not 0<=len(samples)<128:raise ValueError('bounded resume/USB audit missing')
  if int(audit['thermal_samples'])!=(int(raw['calls'])+3)//4 or not 0<int(audit['die_max_mc'])<55000:raise ValueError('runtime stop thermal sampling missing')
  services=0
  for i,row in enumerate(samples):
   before=int(row['before'],16);after=int(row['after'],16);service=int(row['service'])
   if not exact(row,index=i,USB_progress_calls=0) or before not in (0xffffffff,0x30000,0x30002) or after not in (0xffffffff,0x30000,0x30002) or service not in (0,1) or (before==0xffffffff and (service or after!=before)) or (before!=0xffffffff and service!=1):raise ValueError('foreign/unserviced interrupted notification')
   services+=service
  if services!=int(audit['owned_service_calls']):raise ValueError('interrupted service count')
 except (KeyError,ValueError) as exc:errors.append('invalid control evidence: '+str(exc))
 if gpr:
  if len(rows('marble-irq-observer: omitted '))!=1 or not exact(one('marble-irq-observer: omitted '),profile='GPR-only',IRQ_writes=0,IAR_reads=0) or rows('marble-irq-observer: qualified '):errors.append('explicit omitted GIC diagnostic contract missing')
 else:
  spec=importlib.util.spec_from_file_location('control_irq_observer',Path(__file__).with_name('marble-irq-observer-report.py'))
  irq=importlib.util.module_from_spec(spec);spec.loader.exec_module(irq);observed=irq.inspect(text)
  errors.extend('GIC observation: '+e for e in observed['errors'])
 stages=['marble-glink-control: begin ','marble-pil-boot: auth ','marble-glink-control: remote ',
   'marble-smp2p-boot: stopped ','marble-smccc: PAS_stop ','marble-glink-control: complete ',
   'marble-pil-held: shutdown ','marble-qmp-wire: send_before index=1 ','marble-pil-held: restored ']
 positions=[text.find(x) for x in stages]
 if min(positions)<0 or positions!=sorted(positions):errors.append('control service, fresh stop, secure shutdown and OFF/restore order missing')
 return dict(passed=not errors,errors=errors,control_link_verified=not errors,dsp_shutdown_verified=not errors,
  application_channel_verified=apps and not errors,local_application_channels=int(apps and not errors),gpr_packets=packet_count if not errors else 0,audio_acceptance=False,SMP2P_notification_delivery_verified=False,
  scope=('adsp_apps with one fixed local intent and one read-only GPR query; payload result requires the separate GPR report' if gpr else 'control negotiation and one adsp_apps OPEN/ACK with remote-intent metadata, retired by PAS stop; no local intents, close-handshake or GPR DATA' if apps else 'control version negotiation, recorded/unanswered remote opens and owned GLINK/SMP2P notifications')+'; no audio acceptance')
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('log',type=Path);a=p.parse_args();r=inspect(a.log.read_text(errors='replace'));print(json.dumps(r,indent=2));raise SystemExit(not r['passed'])
