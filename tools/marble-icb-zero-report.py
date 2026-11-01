"""Verify native crypto/DDR client cleanup without accepting a bandwidth lease."""
import argparse,importlib.util,json,re
from pathlib import Path
s=importlib.util.spec_from_file_location('icb_power_metadata',Path(__file__).with_name('marble-power-report.py'))
power=importlib.util.module_from_spec(s);s.loader.exec_module(power)

def inspect(text):
    metadata=power.inspect_dependencies(text);errors=list(metadata['errors'])
    def rows(label):return [dict(re.findall(r'(\w+)=([^\s]+)',s)) for s in text.splitlines() if s.startswith('marble-icb-zero: '+label+' ')]
    def one(label):
        r=rows(label);return r[0] if len(r)==1 else {}
    def exact(row,**values):return all(row.get(k)==str(v) for k,v in values.items())
    binding=one('binding');qualified=one('qualified');created=one('created');final=one('complete');lists=rows('list')
    if not exact(binding,create_offset=10,create_rva=1448,destroy_offset=78,destroy_rva='15e4',explicit=1):errors.append('explicit native client binding missing')
    if not exact(qualified,exact_abi=1,route_names=8,hops=4,vector_type=400,pair_bytes=8,explicit_requests=0,watchdog_seconds=30):errors.append('qualified native lifecycle missing')
    if not exact(created,status=0,owned=1,empty=1,lists_appended=1):errors.append('empty owned client missing')
    if not exact(final,PASS=1,destroy_status=0,attempted=1,dirty=0,lists_restored=1,clients_restored=1,resource_restored=1,RPMh_cache_restored=1,watchdog_clear=0,create_calls=1,destroy_calls=1,explicit_requests=0,DSP_start=0,storage_access=0,bandwidth_lease=0):errors.append('native cleanup missing')
    expected=['qxm_crypto','qns_a2noc_snoc','qnm_aggre2_noc','qns_gemnoc_sf','qnm_snoc_sf','qns_llcc','llcc_mc','ebi']
    if [r.get('name') for r in metadata['endpoints']]!=expected:errors.append('route names differ from marble')
    try:
        before=int(created['clients_before'])
        if not 0<=before<64 or int(created['clients_during'])!=before+1:raise ValueError('client count')
        if not 0<int(final['elapsed_us'])<1000000:raise ValueError('time budget')
        if len(lists)!=8:raise ValueError('node list count')
        for i,row in enumerate(lists):
            if not exact(row,index=i,restored=1) or not 0<=int(row['before'])<64 or row['before']!=row['after']:raise ValueError('request list closure')
            old=int(row['old_capacity']);new=int(row['native_capacity'])
            if not int(row['before'])<=old<=128 or not new<=128 or new-old not in (0,10):raise ValueError('native retained capacity')
    except (KeyError,ValueError) as exc:errors.append('invalid lifecycle metadata: '+str(exc))
    prefixes=['marble-adsp-dependencies: complete ','marble-icb-zero: binding ','marble-icb-zero: qualified ','marble-icb-zero: created ']+[
        f'marble-icb-zero: list index={i} ' for i in range(8)]+['marble-icb-zero: complete ']
    pos=[text.find(p) for p in prefixes]
    if min(pos)<0 or pos!=sorted(pos):errors.append('lifecycle order')
    refused=rows('refused')
    clean=len(refused)==1 and exact(refused[0],clean=1,attempted=0,dirty=0) and not any(rows(k) for k in ('qualified','created','list','complete'))
    if refused and not clean:errors.append('contradictory refusal')
    return dict(passed=not errors,errors=errors,cleanup_verified=not errors,clean_refusal=clean,
        lists=lists,bandwidth_lease_verified=False,dsp_started=False,audio_acceptance=False,
        scope='native zero-request client; original members, requests and RPMh cache restored; native list capacity may be retained')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('log',type=Path);a=p.parse_args()
    r=inspect(a.log.read_text(errors='replace'));print(json.dumps(r,indent=2));raise SystemExit(not r['passed'])
