"""Qualify initialized native SMEM transport geometry without queue writes."""
import argparse,json,re
from pathlib import Path
def inspect(text):
 def rows(prefix):return [dict(re.findall(r'(\w+)=([^\s]+)',l)) for l in text.splitlines() if l.startswith(prefix)]
 def one(prefix):
  found=rows(prefix);return found[0] if len(found)==1 else {}
 def exact(r,**v):return all(r.get(k)==str(x) for k,x in v.items())
 errors=[];q=one('marble-glink-smem: qualified ');g=one('marble-glink-smem: geometry ');end=one('marble-glink-smem: complete ')
 if rows('marble-glink-smem: refused '):errors.append('firmware qualification explicitly refused')
 if not exact(q,native_ranges=4,transport_slots=17,transport_count=5,stride=170,index=0,config_rva='d2a8',remote_pid=2,FIFO_config=16384,MTU=4096,IPCC_client=3,signal=0,allocator_calls=0,RX_dispatch=0):errors.append('native transport/configuration qualification missing')
 if not exact(g,descriptor=1,FIFO_binding=1,TX_size=16384,RX_size=0,RX_missing=1,descriptor_stable=1,own_indices_zero=1,cached_indices=1,cached_TX_head=0,cached_RX_tail=0,transport_unchanged=1,listeners=1,runtime_RX_qualified=0):errors.append('preallocated descriptor/FIFO state not qualified')
 if not exact(end,PASS=1,initialized_SMEM=1,private_GET_calls=3,allocator_calls=0,descriptor_reads=64,FIFO_reads=0,RX_dispatch=0,channel_open=0,TX_calls=0,IRQ_writes=0,PAS_calls=0,DSP_start=0,storage_access=0,audio_acceptance=0):errors.append('read-only/no-execution contract incomplete')
 if not exact(one('marble-qmp-native: listeners '),PASS=1,total=1,matched=1,unknown=0,terminated=1,callback_calls=0):errors.append('unknown native listener or callback dispatch')
 items=rows('marble-glink-smem: get ')
 try:
  part=int(q['partition'],16);length=int(q['partition_bytes'])
  indices=[int(s,16) for s in g['indices'].split(',')]
  if len(indices)!=4 or any(not 0<=n<=0xffffffff for n in indices) or indices[1] or indices[2] or g.get('remote_indices_zero')!=str(int(not indices[0] and not indices[3])):raise ValueError('descriptor ownership or history')
  if not 0x80901000<=part<=0x80aff000 or length!=196608 or part+length>0x80aff000 or len(items)!=3:raise ValueError('private partition geometry')
  for i,row in enumerate(items):
   size=(32,16384,0)[i];status='800000000000000e' if i==2 else '0';addr=int(row['address'],16)
   if not exact(row,item=478+i,status=status,bytes=size,PASS=1,flags=40000000,allocator_calls=0,FIFO_reads=0):raise ValueError('wrong GET result')
   if (i==2 and addr) or (i<2 and not (part+32<=addr<=part+length-size and not addr&7)):raise ValueError('unowned item address')
  desc=int(items[0]['address'],16);fifo=int(items[1]['address'],16)
  if not (desc+32<=fifo or fifo+16384<=desc):raise ValueError('overlapping allocations')
 except (KeyError,ValueError) as exc:errors.append('invalid item provenance: '+str(exc))
 stages=['qualified','get item=478','get item=479','get item=480','geometry','complete']
 positions=[text.find('marble-glink-smem: '+s+' ') for s in stages]
 if min(positions)<0 or positions!=sorted(positions):errors.append('inventory stage ordering missing')
 return dict(passed=not errors,errors=errors,preallocated_transport_verified=not errors,runtime_RX_qualified=False,allocator_called=False,rx_dispatched=False,dsp_started=False,audio_acceptance=False,
  scope='initialized lpass transport, descriptor478 and local FIFO479 only; remote FIFO480 absent; native geometry and private GET/NC mappings; no queue or IRQ writes')
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('log',type=Path);a=p.parse_args();r=inspect(a.log.read_text(errors='replace'));print(json.dumps(r,indent=2));raise SystemExit(not r['passed'])
