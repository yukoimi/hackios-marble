"""Verify AP-owned SMP2P preparation only; never infer DSP or audio readiness."""
import argparse,json,re
from pathlib import Path

def _inspect(text,notify,dsp_state=0):
    def rows(prefix):return [dict(re.findall(r'(\w+)=([^\s]+)',line)) for line in text.splitlines() if line.startswith(prefix)]
    errors=[]
    def one(prefix,fields):
        values=rows(prefix)
        if len(values)!=1 or any(values[0].get(k)!=str(v) for k,v in fields.items()):errors.append('invalid record: '+prefix)
        return values[0] if len(values)==1 else {}
    state=one('marble-smp2p-state: ',dict(attempted=1,dirty=1,DSP_start=dsp_state))
    qualified=one('marble-smp2p: qualified ',dict(exact_allocator=1,lock_hooks_stub=1,DSP_off_profile=1,local_item=429,remote_item=443,payload_reads=0))
    allocated=one('marble-smp2p: allocated ',dict(status=0,item=429,bytes=344,flags='40000000',DSP_start=0,ipcc_writes=0))
    one('marble-smp2p: prepared ',dict(PASS=1,magic='504d5324',version=1,local=0,remote=2,total=16,valid=1,master_value=0,free_delta=360,glink_items_unchanged=1,owned_bytes=340,payload_reads=340,payload_write_bytes=344))
    final=one('marble-smp2p: complete ',dict(PASS=1,attempted=1,dirty=1,reset_required=1,watchdog_retained=1,DSP_start=0,ipcc_writes=notify,audio_acceptance=0))
    if notify:
        one('marble-smp2p-ipcc: qualified ',dict(PASS=1,exact_table=1,resident_base='ed18000',APPS_client=8,target_client=3,signal=2,send_offset='c',protocol_calls=0,MMIO_reads=0,MMIO_writes=0))
        notified=one('marble-smp2p: notified ',dict(PASS=1,issued=1,target_client=3,signal=2,register='ed1800c',value='30002',owned_header_unchanged=1,thermal_ok=1,delivery_verified=0,DSP_start=0,IRQ_changes=0))
        try:
            if not 0<=int(notified['die_mc'])<55000:errors.append('notification temperature guard failed')
        except (KeyError,ValueError):errors.append('notification temperature unavailable')
    try:
        region=int(qualified['region'],16);size=int(qualified['region_bytes']);part=int(qualified['partition'],16);part_size=int(qualified['partition_bytes'])
        free=int(qualified['free_before']);cached=int(qualified['cached_before']);address=int(allocated['address'],16)
        if not(region==0x80900000 and 0<size<=0x300000 and region+0x1000<=part and part+part_size<=region+size-0x1000 and
               part_size>0 and 32<=free and free%8==0 and free+360<=cached<=part_size and address==part+free+16):
            errors.append('private allocation extent or exact new-item pointer differs')
        if not 0<=int(final['elapsed_us'])<1000000:errors.append('one-second preparation bound exceeded')
    except (KeyError,ValueError):errors.append('allocation geometry/time evidence unavailable')
    lines=text.splitlines();prefixes=('qualified ','allocated ','prepared ','complete ')
    indices=[next((i for i,x in enumerate(lines) if x.startswith('marble-smp2p: '+p)),-1) for p in prefixes]
    if -1 in indices or indices!=sorted(set(indices)):errors.append('allocation/preparation order is ambiguous')
    if notify:
        kicked=[i for i,x in enumerate(lines) if x.startswith('marble-smp2p: notified ')]
        if len(kicked)!=1 or not indices[2]<kicked[0]<indices[3]:errors.append('notification not after complete header publication')
    raw_states=rows('marble-smp2p-state: ')
    clean_refusal=(len(raw_states)==1 and raw_states[0].get('dirty')=='0' and not rows('marble-smp2p: allocated '))
    return dict(passed=not errors,errors=errors,clean_refusal=clean_refusal,reset_required=state.get('dirty')=='1',
        cold_reset_verified=False,dsp_started=False,ipcc_writes=bool(notify),delivery_verified=False,audio_acceptance=False,
        scope='native private SMEM allocation and APPS-owned master-kernel initialization'+('; one IPCC notification issued, delivery not verified' if notify else '; no notification')+'; no DSP execution or audio')

def inspect(text):return _inspect(text,0)
def inspect_notify(text):return _inspect(text,1)

def inspect_powered_stage(text):
    import importlib.util
    spec=importlib.util.spec_from_file_location('smp_npa',Path(__file__).with_name('marble-npa-report.py'))
    npa=importlib.util.module_from_spec(spec);spec.loader.exec_module(npa)
    result=npa.inspect_powered_stage(text);smp=inspect_notify(text)
    result['errors']+=smp['errors']
    lines=text.splitlines()
    prepared=[i for i,x in enumerate(lines) if x.startswith('marble-smp2p: complete ')]
    clients=[i for i,x in enumerate(lines) if x.startswith('marble-npa-client: create ')]
    if len(prepared)!=1 or len(clients)!=2 or prepared[0]>=min(clients):result['errors'].append('SMP2P notification must precede every power client')
    result.update(passed=not result['errors'],smp2p_report=smp,scope='APPS SMP2P preparation/notification then powered ROM stage, verified PAS stop before restore, native release and mandatory cold reset; no DSP execution/audio')
    return result

def inspect_boot(text):
    import importlib.util
    spec=importlib.util.spec_from_file_location('smp_npa_boot',Path(__file__).with_name('marble-npa-report.py'))
    npa=importlib.util.module_from_spec(spec);spec.loader.exec_module(npa)
    result=npa.inspect_bounded_boot(text);prepared=_inspect(text,1,1)
    result['errors']+=prepared['errors'];errors=result['errors'];lines=text.splitlines()
    def record(prefix,fields):
        found=[(i,dict(re.findall(r'(\w+)=([^\s]+)',x))) for i,x in enumerate(lines) if x.startswith(prefix)]
        if len(found)!=1 or any(found[0][1].get(k)!=str(v) for k,v in fields.items()):errors.append('missing/conflicting handshake: '+prefix);return -1,{}
        return found[0]
    remote,_=record('marble-smp2p-boot: remote ',dict(item=443,bytes=344,local=2,remote=0,stable_copy=1,bounded_reads=340,allocator_calls=0))
    ready,r=record('marble-smp2p-boot: ready ',dict(PASS=1,ready_seen=1,fatal=0,stale_ack=0))
    ack,a=record('marble-smp2p-boot: stop-ack ',dict(PASS=1,ready_seen=1,stop_ack_seen=1))
    stopped,s=record('marble-smp2p-boot: stopped ',dict(PASS=1,ready_seen=1,stop_ack_seen=1,stop_clear=1,PAS_shutdown_allowed=1,payload_access=0))
    pas=[i for i,x in enumerate(lines) if x.startswith('marble-pil-held: shutdown ')]
    power=[i for i,x in enumerate(lines) if x.startswith('marble-npa-client: create ')]
    initial=[i for i,x in enumerate(lines) if x.startswith('marble-smp2p: complete ')]
    observed=[i for i,x in enumerate(lines) if x.startswith('marble-pil-boot: observed ')]
    if len(pas)!=1 or len(observed)!=1 or not(0<=remote<=ready<ack<stopped<pas[0]) or not observed[0]<ready:
        errors.append('fresh ready/stop ACK/clear must precede PAS shutdown')
    if len(initial)!=1 or len(power)!=2 or initial[0]>=min(power):errors.append('SMP2P preparation must precede power clients and DSP startup')
    try:
        rv=int(r['value'],16);av=int(a['value'],16)
        if not(rv&2) or rv&9 or not(av&8) or av&1:errors.append('handshake state bits invalid or stale')
        if not 0<=int(r['wait_us'])<2000000 or not 0<=int(a['wait_us'])<5000000 or not 3<=int(s['notifications'])<=8:
            errors.append('handshake exceeded event/notification bounds')
    except (KeyError,ValueError):errors.append('handshake values/timing unavailable')
    result.update(passed=not errors,smp2p_ready_and_stop_verified=not errors,dsp_service_ready=False,audio_acceptance=False,
        scope='bounded secure DSP auth, fresh SMP2P ready and stop ACK, verified PAS shutdown before memory restore, native release and mandatory cold reset; no audio/channel acceptance')
    return result

def inspect_reset_boot(text):
    """Separate contract: retain *all* DSP resources until whole cold reset.

    This never accepts PAS shutdown, in-place restoration or native release.
    The host must independently confirm a changed Android boot ID afterward.
    """
    result=_inspect(text,1,1);errors=result['errors'];lines=text.splitlines()
    def rows(prefix):return [(i,dict(re.findall(r'(\w+)=([^\s]+)',x))) for i,x in enumerate(lines) if x.startswith(prefix)]
    def exact(r,fields):return all(r.get(k)==str(v) for k,v in fields.items())
    def one(prefix,fields):
        found=rows(prefix)
        if len(found)!=1 or not exact(found[0][1],fields):errors.append('invalid reset boundary: '+prefix);return -1,{}
        return found[0]
    def pair(prefix,fields):
        found=rows(prefix)
        if len(found)!=2 or [r.get('index') for _,r in found]!=['0','1'] or any(not exact(r,fields) for _,r in found):errors.append('invalid retained clients: '+prefix)
        return found
    pair('marble-npa-vcs: index=',dict(exact_binding=1,rail_cached_corner=256,resource_cached_state=256,corner_config=0,min=0,max=480,init=256,protocol_calls=0,DSP_start=0))
    creates=pair('marble-npa-client: create ',dict(status=0,required_type=40,explicit_requests=0))
    requests=pair('marble-npa-client: request ',dict(configured_level=256,status=0,aggregate=256,actual_state=256,PASS=1,DSP_start=0))
    try:
        handles=[int(r['handle'],16) for _,r in creates]
        if len(set(handles))!=2 or not all(handles):errors.append('retained native handles null/aliased')
    except (KeyError,ValueError):errors.append('retained native handles unavailable')
    initial,_=one('marble-smp2p: complete ',dict(PASS=1,DSP_start=0,ipcc_writes=1))
    begin,_=one('marble-npa-held: begin ',dict(active=2,requests=2,watchdog_seconds=30,limit_us=15000000,DSP_start=0))
    loaded,_=one('marble-pil-held: loaded ',dict(PASS=1,native_rc=0,files=24,ROM_matches=24,zero_regions=25,loads=25,backup_retained=1,original_restored=0,auth_reset=0,dsp_start=0))
    auth,auth_r=one('marble-pil-boot: auth ',dict(status=0,transport=1,secure=0,accepted=1,hold_limit_ms=500,payload_access=0))
    observed,obs=one('marble-pil-boot: observed ',dict(thermal_samples=5,thermal_ok=1,link_invalid=0,service_acceptance=0,audio_acceptance=0))
    remote,_=one('marble-smp2p-boot: remote ',dict(item=443,bytes=344,local=2,remote=0,stable_copy=1,bounded_reads=340,allocator_calls=0))
    ready,r=one('marble-smp2p-boot: ready ',dict(PASS=1,ready_seen=1,fatal=0,stale_ack=0))
    ack,a=one('marble-smp2p-boot: stop-ack ',dict(PASS=1,ready_seen=1,stop_ack_seen=1))
    stopped,s=one('marble-smp2p-boot: stopped ',dict(PASS=1,ready_seen=1,stop_ack_seen=1,stop_clear=1,PAS_shutdown_allowed=1,payload_access=0))
    final,_=one('marble-pil-reset-boot: complete ',dict(PASS=1,auth_accepted=1,ready_stop_ack=1,PAS_shutdown_calls=0,payload_restore=0,metadata_retained=1,backup_retained=1,power_clients=2,observer_active=1,watchdog_retained=1,reset_required=1,original_restore=0,audio_acceptance=0))
    retained,_=one('marble-pil-ramfs: retained ',dict(installed=15,dirty=0,metadata_retained=1,backup_retained=1,watchdog_retained=1,dsp_start=1,reset_required=1))
    returned,ret=one('marble-npa-held: returned ',dict(outcome=-1,release_allowed=0,active=2,watchdog_retained=1,limit_us=15000000,limit_ok=1))
    one('marble-npa-client-state: ',dict(attempted=1,active=2,dirty=1))
    sequence=[initial,*[i for i,_ in creates],*[i for i,_ in requests],begin,loaded,auth,observed,remote,ready,ack,stopped,final,retained,returned]
    if -1 in sequence or sequence!=sorted(set(sequence)):errors.append('retained reset boundary ordering is ambiguous')
    forbidden=('marble-pil-held: shutdown ','marble-pil-held: restore-gate ','marble-pil-held: restored ',
        'marble-pil-header: complete ','marble-pil-ramfs: released ','marble-glink: released ',
        'marble-npa-client: destroy ','marble-npa-client: restore ','marble-npa-client: complete ',
        'marble-npa-native-release: complete ')
    if any(rows(prefix) for prefix in forbidden) or 'PAS-metadata-shutdown-before' in text:
        errors.append('planned reset must not enter PAS shutdown, local restore or client/observer release')
    try:
        rv=int(r['value'],16);av=int(a['value'],16)
        if not(rv&2) or rv&9 or not(av&8) or av&1:errors.append('reset boot ready/ACK bits invalid or stale')
        if not(0<=int(auth_r['elapsed_us'])<1000000 and 500000<=int(obs['hold_us'])<750000 and 0<=int(obs['die_mc'])<55000 and
               0<=int(r['wait_us'])<2000000 and 0<=int(a['wait_us'])<5000000 and 3<=int(s['notifications'])<=8 and
               0<=int(ret['elapsed_us'])<15000000):errors.append('reset boot exceeded thermal/time/notification bound')
    except (KeyError,ValueError):errors.append('reset boot bound evidence unavailable')
    result.update(passed=not errors,reset_required=True,cold_reset_verified=False,original_restoration=False,local_cleanup_verified=False,
        native_release_verified=False,dsp_secure_auth_accepted=not errors,smp2p_ready_and_stop_verified=not errors,dsp_service_ready=False,
        audio_acceptance=False,scope='secure DSP auth and SMP2P ready/stop ACK only; every DSP resource retained for mandatory whole-firmware cold reset; no PAS shutdown/local restoration/native release/audio acceptance')
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('log',type=Path);p.add_argument('--notify',action='store_true');p.add_argument('--powered-stage',action='store_true');p.add_argument('--boot',action='store_true');p.add_argument('--reset-boot',action='store_true');a=p.parse_args()
    result=(inspect_reset_boot if a.reset_boot else inspect_boot if a.boot else inspect_powered_stage if a.powered_stage else inspect_notify if a.notify else inspect)(a.log.read_text(errors='replace'));print(json.dumps(result,indent=2));raise SystemExit(not result['passed'])
if __name__=='__main__':main()
