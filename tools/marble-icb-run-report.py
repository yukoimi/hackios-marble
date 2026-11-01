"""Audit the published ICB post-init transition; RUN is retained until reset."""
import argparse,json,re
from pathlib import Path
def inspect(text):
 def rows(k):return [dict(re.findall(r'(\w+)=([^\s]+)',l)) for l in text.splitlines() if l.startswith('marble-icb-run: '+k+' ')]
 def one(k):
  r=rows(k);return r[0] if len(r)==1 else {}
 def exact(r,**v):return all(r.get(k)==str(x) for k,x in v.items())
 errors=[];b=one('binding');before=one('before');f=one('complete');standalone=rows('standalone')
 if not exact(b,protocol_offset=8,protocol_rva='13f4',implementation_rva='2aa4',post_init_rva=4088,finish_offset=68,finish_rva='17e4',code_ranges=10):errors.append('published post-init and completion binding missing')
 if not exact(before,state=2,direct_state_writes=0):errors.append('initial HOLD state missing')
 if not exact(f,PASS=1,status=0,state=4,rpmh_snapshot=1,queues_drained=1,NPA_clients_unchanged=1,hardware_aggregates_unchanged=1,thermal_ok=1,watchdog_armed=1,cold_reset_required=1,firmware_RUN_retained=1,original_hardware_restored=0,direct_state_writes=0,explicit_bandwidth_requests=0,DSP_start=0,storage_access=0,audio_acceptance=0):errors.append('RUN/completion/reset boundary missing')
 try:
  if not 0<int(f['elapsed_us'])<1000000 or not 0<int(f['completion_polls'])<=500:raise ValueError('transition budget')
  if not 0<int(before['hardware_nodes'])<=128 or not 0<int(before['rpmh_resources'])<=512 or not 0<int(before['rpmh_clients'])<=64:raise ValueError('native counts')
  if not 0<=int(before['standalone_nodes'])<=64 or int(before['standalone_nodes'])!=len(standalone):raise ValueError('standalone count')
  for i,r in enumerate(standalone):
   if not exact(r,index=i,native_decides_flush=1):raise ValueError('standalone sequence')
 except (KeyError,ValueError) as e:errors.append('invalid metadata: '+str(e))
 labels=['binding','before','complete'];pos=[text.find('marble-icb-run: '+k+' ') for k in labels]
 if min(pos)<0 or pos!=sorted(pos):errors.append('RUN transition order')
 refusals=rows('refused');clean=len(refusals)==1 and exact(refusals[0],clean=1,attempted=0,dirty=0) and not rows('complete')
 if refusals and not clean:errors.append('contradictory refusal')
 return dict(passed=not errors,errors=errors,reset_safe=not errors,cleanup_verified=False,clean_refusal=clean,
  native_run_transition_verified=not errors,original_hardware_restored=False,dsp_started=False,audio_acceptance=False,
  scope='published native ICB post-init, HOLD to RUN and bounded RPMh completion; global RUN/native caches retained until mandatory cold reset')
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('log',type=Path);a=p.parse_args();r=inspect(a.log.read_text(errors='replace'));print(json.dumps(r,indent=2));raise SystemExit(not r['passed'])
