"""Audit native bandwidth release with derived retained cache and mandatory reset."""
import argparse,importlib.util,json,re
from pathlib import Path
s=importlib.util.spec_from_file_location('icb_metadata',Path(__file__).with_name('marble-power-report.py'))
power=importlib.util.module_from_spec(s);s.loader.exec_module(power)
def inspect(text,run=False,mode=1,metadata=None,limit=1000000,execute=0,peak=1000000,control_execute=False):
 def rows(k):return [dict(re.findall(r'(\w+)=([^\s]+)',l)) for l in text.splitlines() if l.startswith('marble-icb-bw: '+k+' ')]
 def one(k):
  r=rows(k);return r[0] if len(r)==1 else {}
 def exact(r,**v):return all(r.get(k)==str(x) for k,x in v.items())
 metadata=power.inspect_dependencies(text) if metadata is None else metadata;errors=list(metadata['errors'])
 scm_peak=peak==0xffffffff*1000
 if control_execute and not (scm_peak and run and mode==3 and execute==1):errors.append('control execution requires the explicit SCM/RUN mode3 contract')
 if peak not in (1000000,0xffffffff*1000) or (scm_peak and (not run or mode not in (1,3) or (execute and not control_execute))):errors.append('unqualified peak/mode')
 binding=one('binding');q=one('qualified');c=one('created');f=one('complete');lists=rows('list');derived=rows('derived_cache')
 if not exact(binding,vector_offset=90,vector_rva='162c',complete_offset=18,complete_rva=1474,code_ranges=12,contract='run-request-reset-v1' if run else 'derived-cache-reset-v1'):errors.append('new explicit vector binding/reset contract missing')
 if not exact(q,exact_abi=1,route_names=8,hops=4,vector_type=400,pair_bytes=8,explicit_requests=1,request_type=4,request_words=8,peak_bytes_per_second=peak,watchdog_seconds=5 if scm_peak and mode==1 else 30):errors.append('bounded bandwidth parameters missing')
 if not exact(c,status=0,owned=1,empty=1,lists_appended=1):errors.append('owned empty client missing')
 if not exact(one('requested'),status=0,accepted=1,ib=peak,ab=0,owner_match=1):errors.append('owned bandwidth request missing')
 if not exact(one('completed'),status=0,zero=1,ib=0,ab=0,owner_match=1):errors.append('owned bandwidth completion missing')
 if not exact(f,PASS=1,destroy_status=0,attempted=1,dirty=1,lists_restored=1,clients_restored=1,resource_restored=1,RPMh_cache_restored=0,original_cache_unchanged=1,hardware_aggregate_restored=1,added_cache_derived=1,queues_drained=1,watchdog_armed=1,cold_reset_required=1,native_history_retained=1,cache_allocations_retained=1,original_hardware_restored=0,create_calls=1,destroy_calls=1,explicit_requests=1,complete_calls=1,DSP_start=execute,storage_access=0,bandwidth_request_released=1,audio_acceptance=0):errors.append('request release and retained-cache boundary missing')
 try:
  if not 0<int(f['elapsed_us'])<limit:raise ValueError('elapsed budget')
  if not 0<=int(c['clients_before'])<64 or int(c['clients_during'])!=int(c['clients_before'])+1:raise ValueError('client count')
  if len(lists)!=8:raise ValueError('list count')
  for i,row in enumerate(lists):
   if not exact(row,index=i,restored=1) or not 0<=int(row['before'])<64 or row['before']!=row['after']:raise ValueError('live endpoint restoration')
   old=int(row['old_capacity']);new=int(row['native_capacity'])
   if not int(row['before'])<=old<=128 or not new<=128 or new-old not in (0,10):raise ValueError('native capacity')
  if (run and derived) or (not run and not 0<len(derived)<=8) or len({r['address'] for r in derived})!=len(derived):raise ValueError('added resource count/identity')
  for row in derived:
   expected=int(row['expected'],16);actual=int(row['actual'],16)
   if not exact(row,PASS=1,reference='before_request_native_aggregate') or actual&~0x40000000!=expected:raise ValueError('pre-request aggregate derivation')
 except (KeyError,ValueError) as e:errors.append('invalid metadata: '+str(e))
 if run and not exact(f,mode_run=mode):errors.append('RUN ownership missing')
 if scm_peak:
  votes=rows('peak_vote');held=one('peak_hold');completions=rows('peak_completions')
  try:
   if not votes or len(votes)>128 or len({v['address'] for v in votes})!=len(votes):raise ValueError('held BCM identities')
   saturated=0
   for v in votes:
    expected=int(v['expected'],16);actual=int(v['actual'],16)
    if v.get('PASS')!='1' or actual&~0x40000000!=expected:raise ValueError('held requested/ACK BCM vote')
    if expected&0x3fff==0x3fff:saturated+=1
   if not exact(held,PASS=1,verified=len(votes),saturated=saturated,hold_us=10000,thermal_ok=1,cpu_clock_calls=0,voltage_calls=0) or not saturated or not 0<int(held['die_mc'])<55000:raise ValueError('held peak/thermal proof')
   if len(completions)!=2:raise ValueError('native completion events missing')
   for row,phase in zip(completions,('held','released')):
    if not exact(row,phase=phase,PASS=1) or not 1<=int(row['polls'])<=50:raise ValueError('pending peak completion')
   phases=['requested','peak_completions phase=held','peak_vote','peak_hold','completed','peak_completions phase=released','complete']
   positions=[text.find('marble-icb-bw: '+k+' ') for k in phases]
   if min(positions)<0 or positions!=sorted(positions):raise ValueError('peak hold/release order')
  except (KeyError,ValueError) as e:errors.append('invalid SCM peak proof: '+str(e))
 labels=['binding','qualified','created','requested','completed']+['list index='+str(i) for i in range(8)]+(['complete'] if run else ['derived_cache','complete'])
 pos=[text.find('marble-icb-bw: '+k+' ') for k in labels]
 if min(pos)<0 or pos!=sorted(pos):errors.append('bandwidth lifecycle order')
 refused=rows('refused');clean=len(refused)==1 and exact(refused[0],clean=1,attempted=0,dirty=0) and not any(rows(k) for k in ('qualified','created','requested','complete'))
 if refused and not clean:errors.append('contradictory refusal')
 return dict(passed=not errors,errors=errors,reset_safe=not errors,cleanup_verified=False,clean_refusal=clean,
  bounded_bandwidth_request_verified=not errors,bandwidth_lease_verified=False,original_hardware_restored=False,dsp_started=False,audio_acceptance=False,
  peak_bytes_per_second=peak,scm_peak_request_verified=scm_peak and not errors,
  scope=f'{peak} bytes/s logical native crypto-DDR request/complete/destroy; original aggregates restored, retained allocations/history require cold reset; no throughput or DSP acceptance')
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('log',type=Path);a=p.parse_args();r=inspect(a.log.read_text(errors='replace'));print(json.dumps(r,indent=2));raise SystemExit(not r['passed'])
