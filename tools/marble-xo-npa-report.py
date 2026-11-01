"""Audit software XO aggregation and reset; never accept a physical clock lease."""
import argparse,json,re
from pathlib import Path
def inspect(text):
    def rows(kind):return [dict(re.findall(r'(\w+)=([^\s]+)',s)) for s in text.splitlines() if s.startswith('marble-xo-npa: '+kind+' ')]
    def one(kind):
        r=rows(kind);return r[0] if len(r)==1 else {}
    def exact(row,**values):return all(row.get(k)==str(v) for k,v in values.items())
    binding=one('binding');defined=one('defined');clients=rows('client');requests=rows('request');destroyed=rows('destroy');final=one('complete');errors=[]
    if not exact(binding,define_offset=20,define_rva='148c',scalar_offset=28,scalar_rva='14a4',destroy_offset=78,code_ranges=4):errors.append('explicit provider binding missing')
    if not exact(defined,status=0,ready=1,name='/marble/clock/xo-test',resource_retained=1,hardware_calls=0):errors.append('isolated provider definition missing')
    if len(clients)!=3 or any(not exact(r,index=i,status=0,created=1) for i,r in enumerate(clients)):errors.append('client creation missing')
    states=[(1,1,0),(1,1,0),(1,1,0),(1,1,1),(1,0,1),(0,0,0)]
    if len(requests)!=6 or any(not exact(r,index=i,status=0,PASS=1,active=a,required=b,suppressible=c) for i,(r,(a,b,c)) in enumerate(zip(requests,states))):errors.append('aggregation/release sequence differs')
    if len(destroyed)!=3 or any(not exact(r,index=2-i,status=0) for i,r in enumerate(destroyed)):errors.append('client release missing')
    if not exact(final,PASS=1,attempted=1,dirty=0,clients_released=1,RPMh_cache_restored=1,watchdog_clear=0,provider_retained=1,graph_restored=0,cold_reset_required=1,hardware_calls=0,DSP_start=0,storage_access=0,lease_accepted=0):errors.append('software-only cleanup/reset boundary missing')
    try:
        if not 0<int(final['elapsed_us'])<1000000 or int(defined['driver_calls'])!=1:raise ValueError('provider init or time budget')
    except (KeyError,ValueError) as exc:errors.append('invalid metadata: '+str(exc))
    order=['binding','defined']+['client index='+str(i) for i in range(3)]+['request index='+str(i) for i in range(6)]+['destroy index='+str(i) for i in (2,1,0)]+['complete']
    pos=[text.find('marble-xo-npa: '+p+' ') for p in order]
    if min(pos)<0 or pos!=sorted(pos):errors.append('provider lifecycle order')
    refused=rows('refused');clean=len(refused)==1 and exact(refused[0],clean=1,attempted=0,dirty=0) and not any(rows(k) for k in ('defined','client','request','destroy','complete'))
    if refused and not clean:errors.append('contradictory refusal')
    return dict(passed=not errors,errors=errors,cleanup_verified=not errors,clean_refusal=clean,
        physical_xo_lease_verified=False,dsp_started=False,audio_acceptance=False,
        scope='software required/suppressible aggregation with zero hardware calls; provider remains defined until explicit cold reset')
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('log',type=Path);a=p.parse_args()
    r=inspect(a.log.read_text(errors='replace'));print(json.dumps(r,indent=2));raise SystemExit(not r['passed'])
