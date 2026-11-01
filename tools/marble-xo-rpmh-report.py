"""Audit retained physical XO vote; reset proof is a separate host contract."""
import argparse,json,re
from pathlib import Path
def inspect(text):
 def rows(kind):return [dict(re.findall(r'(\w+)=([^\s]+)',s)) for s in text.splitlines() if s.startswith('marble-xo-rpmh: '+kind+' ')]
 def one(k):
  r=rows(k);return r[0] if len(r)==1 else {}
 def exact(r,**v):return all(r.get(k)==str(x) for k,x in v.items())
 errors=[];b=one('binding');inv=one('inventory');c=one('client');issued=rows('issue');f=one('held')
 if not exact(b,create_offset=30,create_rva='16ec',issue_offset=98,issue_rva=1890,churn_offset=58,churn_rva='17b4',code_ranges=12):errors.append('explicit command/churn binding missing')
 if not exact(inv,stable=1,xo_cached=0,original_hardware_vote_known=0):errors.append('fresh resource ownership missing')
 if not exact(c,status=0,owned_empty=1):errors.append('client ownership missing')
 if len(issued)!=3 or any(not exact(r,index=i,set=s,status=0,data=3) for i,(r,s) in enumerate(zip(issued,(1,2,0)))):errors.append('command sequence differs')
 try:
  if [int(r['request']) for r in issued[:2]]!=[0,0] or not 0<int(issued[2]['request'])<0xffffffff:raise ValueError('completion identifiers')
  if not 0<int(f['elapsed_us'])<1000000:raise ValueError('elapsed budget')
  if not all(0<int(inv[k])<1024 for k in ('original_resources','original_votes','original_clients')):raise ValueError('original counts')
 except (KeyError,ValueError,IndexError) as e:errors.append('invalid metadata: '+str(e))
 if not exact(f,PASS=1,churn_status=0,original_owners_unchanged=1,xo_vote_acknowledged=1,queues_drained=1,thermal_ok=1,issue_calls=3,zero_vote_calls=0,raw_writes=0,retained_client=1,dirty=1,watchdog_armed=1,cold_reset_required=1,physical_release_verified=0,original_hardware_restored=0,DSP_start=0,storage_access=0,audio_acceptance=0):errors.append('retained hold/reset boundary missing')
 order=['binding','inventory','client']+['issue index='+str(i) for i in range(3)]+['held']
 pos=[text.find('marble-xo-rpmh: '+p+' ') for p in order]
 if min(pos)<0 or pos!=sorted(pos):errors.append('hold lifecycle order')
 refusal=rows('refused');clean=len(refusal)==1 and exact(refusal[0],clean=1,attempted=0,dirty=0) and not any(rows(k) for k in ('client','issue','held'))
 if refusal and not clean:errors.append('contradictory refusal')
 return dict(passed=not errors,errors=errors,reset_safe=not errors,clean_refusal=clean,
  physical_xo_hold_verified=not errors,physical_release_verified=False,original_hardware_restored=False,
  dsp_started=False,audio_acceptance=False,scope='native sleep/wake/active XO ON request and active completion; held until mandatory cold reset')
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('log',type=Path);a=p.parse_args();r=inspect(a.log.read_text(errors='replace'));print(json.dumps(r,indent=2));raise SystemExit(not r['passed'])
