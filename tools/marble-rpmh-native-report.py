"""Review native zero-command client closure; never accept a clock or audio lease."""
import argparse,json,re
from pathlib import Path

def inspect(text):
    prefixes=('qualified','inventory','queries','created','released','complete')
    rows={};errors=[];positions=[]
    for label in prefixes:
        prefix='marble-rpmh-native: '+label+' '
        found=[dict(re.findall(r'(\w+)=([^\s]+)',s)) for s in text.splitlines() if s.startswith(prefix)]
        rows[label]=found[0] if len(found)==1 else {}
        positions.append(text.find(prefix))
        if len(found)!=1:errors.append('missing or duplicate '+label)
    def exact(label,**values):
        if any(rows[label].get(k)!=str(v) for k,v in values.items()):errors.append('invalid '+label)
    exact('qualified',exact=1,slots=24,drv=2,hardware_drv=2,commands=16,Init_calls=0)
    exact('inventory',stable=1,pool=0,pending_issue_calls=0)
    exact('queries',valid_status=0,valid=1,num_status=0,commands=16,find_status=0,lookup_match=1)
    exact('created',status=0,captured=1,appended=1,empty=1,resources_unchanged=1,pool=0)
    exact('released',status=0,snapshot=1,clients_restored=1,resources_restored=1,native_pool_retained=1,pool=1)
    exact('complete',PASS=1,attempted=1,dirty=0,create_calls=1,delete_calls=1,query_calls=3,watchdog_clear=0,
          issue_calls=0,votes=0,raw_writes=0,NPA_calls=0,QMP_calls=0,PAS_calls=0,DSP_start=0,storage_access=0,lease_accepted=0)
    try:
        inv=rows['inventory'];count=int(inv['active'])
        if not 0<=count<64 or not 0<=int(inv['resources'])<=512 or not 0<=int(inv['votes'])<=1024:raise ValueError('snapshot limits')
        if int(rows['created']['active'])!=count+1 or int(rows['released']['active'])!=count:raise ValueError('client count closure')
        if not 0<int(rows['released']['elapsed_us'])<1000000:raise ValueError('time limit')
        if int(rows['qualified']['code_words'])!=sum((0x7c,0x44,0x54,0xf4,0xbc,0x140,0x44,0xc4,0xc4,0x28))//4:raise ValueError('instruction extent')
        if rows['queries']['xo_cached'] not in ('0','1'):raise ValueError('cache presence')
    except (KeyError,ValueError) as exc:errors.append('invalid metadata: '+str(exc))
    if min(positions)<0 or positions!=sorted(positions):errors.append('sequence missing or reordered')
    refusals=[s for s in text.splitlines() if s.startswith('marble-rpmh-native: refused ')]
    clean_refusal=len(refusals)==1 and 'clean=1 attempted=0' in refusals[0] and not any(
        'marble-rpmh-native: '+x+' ' in text for x in ('queries','created','released','complete'))
    if refusals and not clean_refusal:errors.append('contradictory refusal')
    return dict(passed=not errors,errors=errors,clean_refusal=clean_refusal,
        cleanup_verified=not errors,resource_leases_verified=False,dsp_started=False,audio_acceptance=False,
        inventory=rows['inventory'],released=rows['released'],
        scope='zero-command regular client; exact active-client and resource/vote cache closure; one native pooled allocation retained; no physical clock or bandwidth proof')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('log',type=Path);a=p.parse_args()
    r=inspect(a.log.read_text(errors='replace'));print(json.dumps(r,indent=2));raise SystemExit(not r['passed'])
