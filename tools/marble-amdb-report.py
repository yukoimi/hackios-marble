"""One bounded AMDB metadata exchange; module presence is separate from transport success."""
import re

def inspect(text,module_id='700100e'):
 if module_id not in ('700100e','7001015'):return dict(passed=False,errors=['unsupported AMDB module'],audio_acceptance=False)
 def rows(prefix):return [dict(re.findall(r'(\w+)=([^\s]+)',l)) for l in text.splitlines() if l.startswith(prefix)]
 def one(prefix):
  found=rows(prefix)
  if len(found)!=1:raise ValueError('missing/duplicate '+prefix)
  return found[0]
 def exact(row,**kw):return all(row.get(k)==str(v) for k,v in kw.items())
 errors=[];available=False;state=None;metadata=None;basic_error=None;client_data=0
 try:
  if rows('marble-amdb: rejected '):raise ValueError('DSP response rejected; metadata unavailable')
  local=one('marble-amdb: local_intent ');q=one('marble-amdb: query ');end=one('marble-amdb: complete ')
  app=one('marble-glink-apps: complete ');remote=int(app['remote_channel'])
  if not exact(local,channel=1,id=1,bytes=512,static=1) or not exact(q,token='4d414d01',opcode='1001007',bytes=96,module=module_id,param='80013d6',graph_commands=0,local_intents=1,audio_commands=0):raise ValueError('not the single bounded read-only command')
  iid=int(q['remote_id'],16);size=int(q['remote_bytes'])
  intents=rows('marble-glink-apps: remote_intent ')
  if not 96<=size<=65536 or sum(int(r['id'],16)==iid and int(r['bytes'])==size for r in intents)!=1:raise ValueError('outgoing intent ownership')
  tx=rows('marble-glink-control: tx ');rx=rows('marble-glink-control: rx ')
  data=[r for r in tx if r['command']=='9'];advert=[r for r in tx if r['command']=='5'];done=rows('marble-amdb: tx_done ')
  if len(data)!=1 or not exact(data[0],param1=1,bytes=112) or int(data[0]['param2'],16)!=iid or len(advert)!=1 or not exact(advert[0],param1=1,param2=1,bytes=16):raise ValueError('single DATA/local INTENT geometry')
  if len(done)!=1 or done[0]['command'] not in ('6','14') or int(done[0]['remote_id'],16)!=iid or int(done[0]['remote_channel'])!=remote:raise ValueError('remote TX completion missing')
  replies=rows('marble-amdb: reply ');fragments=rows('marble-amdb: fragment ')
  if not 1<=len(replies)<=2 or len(replies)!=int(end['replies']):raise ValueError('no matched GPR reply')
  if not exact(end,sent=1,tx_done=1,fragment_active=0,fragment_bytes=0,PAS_stopped=1,graph_commands=0,audio_acceptance=0):raise ValueError('exchange did not retire after PAS stop')
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
   if kind in kinds or kind not in (1,2) or not exact(r,token='4d414d01',remote_channel=remote,local_intent=1) or int(r['bytes'])!=(84 if kind==1 else 32):raise ValueError('reply identity/header')
   kinds.add(kind)
   error=int(r['error'],16);param_error=int(r['param_error'],16);module_error=int(r['module_error'],16)
   present=int(r['present']);major=int(r['major']);minor=int(r['minor'])
   client_data=int(r['client_data'],16)
   if not 0<=client_data<=255 or (client_data and (kind!=2 or not error)):raise ValueError('nonzero client-data only allowed on quarantined basic error')
   build=[int(x) for x in r['build'].split(',')]
   if present not in (0,1) or not all(0<=x<=0xffffffff for x in (error,param_error,module_error,major,minor)) or len(build)!=6 or not all(0<=x<=65535 for x in build) or build[1]>12 or build[2]>31 or build[3]>23 or build[4]>59 or build[5]>60:raise ValueError('invalid module metadata')
   if kind==1:
    state=present+1;metadata=dict(present=bool(present),error=error,param_error=param_error,module_error=module_error,major=major,minor=minor,build=build)
    available=bool(present) and not (error or param_error or module_error)
   else:
    basic_error=error
    if present or param_error or module_error or major or minor or any(build):raise ValueError('basic response carries metadata')
  if int(end['module_state'])!=(0 if state is None else state):raise ValueError('module result disagrees with reply')
  order=['marble-glink-apps: open_ack ','marble-amdb: local_intent ','marble-amdb: query ','marble-amdb: reply ','marble-smccc: PAS_stop ','marble-amdb: complete ']
  positions=[text.find(x) for x in order]
  if min(positions)<0 or positions!=sorted(positions):raise ValueError('query/response/stop ordering')
 except (KeyError,ValueError,TypeError) as e:errors.append(str(e))
 return dict(passed=not errors,errors=errors,exchange_verified=not errors,module_id=f'{int(module_id,16):08x}',module_available=available and not errors,metadata=metadata,basic_error=basic_error,client_data=client_data,header_conformant=not bool(client_data&0xf0),metadata_exchange=metadata is not None and not errors,graph_commands=0,audio_acceptance=False)
