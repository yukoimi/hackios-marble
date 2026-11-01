"""Audit the isolated LPI client lifecycle; this never accepts working audio."""
import argparse,json,re,importlib.util
from pathlib import Path
_runtime_spec=importlib.util.spec_from_file_location('npa_audio_runtime',Path(__file__).with_name('marble-audio-runtime-contract.py'))
_runtime=importlib.util.module_from_spec(_runtime_spec);_runtime_spec.loader.exec_module(_runtime)

def inspect(text,expected_requests=None):
    def rows(prefix):
        result=[]
        for line in text.splitlines():
            if line.startswith(prefix):
                result.append(dict(re.findall(r'(\w+)=([^\s]+)',line)))
        return result
    def exact(row,fields):return all(row.get(k)==str(v) for k,v in fields.items())
    states=rows('marble-npa-client-state: ')
    cleanup=(len(states)==1 and states[0].get('attempted') in ('0','1') and exact(states[0],dict(active=0,dirty=0)))
    errors=[]
    if not cleanup:errors.append('missing, conflicting or dirty final client state')
    complete=rows('marble-npa-client: complete ')
    if len(complete)!=1:errors.append('one complete lifecycle record required')
    else:
        record=complete[0]
        if not exact(record,dict(PASS=1,created=2,destroyed=2,restored=2,active=0,dirty=0,watchdog_clear=0,DSP_start=0,audio_acceptance=0)):
            errors.append('lifecycle counts, cleanup, watchdog or DSP boundary failed')
        try:
            elapsed=int(record['elapsed_us']);issued=int(record['explicit_requests'])
            if not 0<=elapsed<5000000:errors.append('lifecycle exceeded five-second bound')
            if issued not in (0,2) or (expected_requests is not None and issued!=expected_requests):
                errors.append('explicit request count differs from selected stage')
        except (KeyError,ValueError):errors.append('missing/invalid elapsed time or request count');issued=-1
        creates=rows('marble-npa-client: create ');destroys=rows('marble-npa-client: destroy ')
        if len(creates)!=2 or [r.get('index') for r in creates]!=['0','1'] or any(r.get('status')!='0' for r in creates):
            errors.append('two successful ordered creates required')
        try:
            handles=[int(r['handle'],16) for r in creates]
            if len(handles)!=2 or not all(handles) or len(set(handles))!=2:errors.append('client handles are null or aliased')
        except (KeyError,ValueError):errors.append('invalid client handle')
        if len(destroys)!=2 or [r.get('index') for r in destroys]!=['1','0'] or any(not exact(r,dict(status=0,implicit_complete=1)) for r in destroys):
            errors.append('two successful reverse-order destroys required')
        restored=rows('marble-npa-client: restore ')
        if len(restored)!=2 or {r.get('index') for r in restored}!={'0','1'} or any(not exact(r,dict(PASS=1,client_head=0,state=256,max=480,aggregates=0)) for r in restored):
            errors.append('both original empty client lists and resource states must be restored')
        requests=rows('marble-npa-client: request ')
        if len(requests)!=issued or (issued==2 and ({r.get('index') for r in requests}!={'0','1'} or any(not exact(r,dict(configured_level=256,status=0,aggregate=256,actual_state=256,PASS=1,DSP_start=0)) for r in requests))):
            errors.append('missing, extra or invalid native configured-level requests')
    return dict(passed=not errors,errors=errors,cleanup_verified=cleanup,
        scope='isolated native LPI client/request lifecycle with restoration; not DSP execution or audio',audio_acceptance=False)

def _inspect_native_release(text,limit,dsp_start=0):
    """Separate contract: known native OFF cache state requires cold reset.

    This deliberately never changes the original-state restoration verdict.
    Hardware voltage is not measured; both driver and framework caches must
    agree, handles must be gone, and the host must still verify Android reset.
    """
    def rows(prefix):return [dict(re.findall(r'(\w+)=([^\s]+)',line)) for line in text.splitlines() if line.startswith(prefix)]
    def exact(row,fields):return all(row.get(k)==str(v) for k,v in fields.items())
    errors=[]
    def pair(prefix,fields,order=('0','1')):
        values=rows(prefix)
        if len(values)!=2 or [r.get('index') for r in values]!=list(order) or any(not exact(r,fields) for r in values):
            errors.append('invalid pair: '+prefix)
        return values
    pair('marble-npa-vcs: index=',dict(exact_binding=1,rail_cached_corner=256,resource_cached_state=256,corner_config=0,min=0,max=480,init=256,physical_voltage_unmeasured=1,protocol_calls=0,DSP_start=0))
    creates=pair('marble-npa-client: create ',dict(status=0))
    try:
        handles=[int(r['handle'],16) for r in creates]
        if len(handles)!=2 or not all(handles) or len(set(handles))!=2:errors.append('null/aliased native client handles')
    except (KeyError,ValueError):errors.append('invalid native client handles')
    pair('marble-npa-client: request ',dict(configured_level=256,status=0,aggregate=256,actual_state=256,PASS=1,DSP_start=0))
    pair('marble-npa-client: destroy ',dict(status=0,implicit_complete=1),('1','0'))
    pair('marble-npa-client: restore ',dict(PASS=0,client_head=0,state=0,max=480,aggregates=0,read_mask='f'))
    pair('marble-npa-native-release: index=',dict(PASS=1,rail_read=1,rail_cached_corner=0,resource_cached_state=0,empty_clients=1,aggregate_zero=1,physical_voltage_unmeasured=1))
    complete=rows('marble-npa-client: complete ')
    if len(complete)!=1 or not exact(complete[0],dict(PASS=0,created=2,destroyed=2,restored=0,active=0,dirty=1,watchdog_clear='8000000000000007',explicit_requests=2,DSP_start=dsp_start,audio_acceptance=0)):
        errors.append('original restoration failure and retained watchdog must remain explicit')
    else:
        try:
            if not 0<=int(complete[0]['elapsed_us'])<limit:errors.append('native cycle exceeded selected bound')
        except (KeyError,ValueError):errors.append('native elapsed time unavailable')
    states=rows('marble-npa-client-state: ')
    if len(states)!=1 or not exact(states[0],dict(attempted=1,active=0,dirty=1)):errors.append('native reset state missing/conflicting')
    final=rows('marble-npa-native-release: complete ')
    if len(final)!=1 or not exact(final[0],dict(PASS=1,released=2,original_restore=0,reset_required=1,watchdog_retained=1,active=0,dirty=1,DSP_start=dsp_start,audio_acceptance=0)):
        errors.append('native release/reset contract incomplete')
    return dict(passed=not errors,errors=errors,cleanup_verified=False,original_restoration=False,
        reset_required=True,cold_reset_verified=False,physical_voltage_measured=False,audio_acceptance=False,
        scope='native zero-request release with mandatory subsequent firmware cold reset; not original-state restoration, DSP execution or audio')

def inspect_native_release(text):
    return _inspect_native_release(text,5000000)

def _inspect_powered_stage(text,execute):
    """Accept a longer lease only with the complete no-execution PIL closure.

    Restore may happen only after PAS shutdown and fresh display ownership.
    Missing, duplicated or reordered evidence cannot authorize an early reset
    instead of the retained recovery watchdog.
    """
    result=_inspect_native_release(text,15000000,execute)
    errors=result['errors']
    def row(prefix,fields):
        values=[(i,dict(re.findall(r'(\w+)=([^\s]+)',line))) for i,line in enumerate(text.splitlines()) if line.startswith(prefix)]
        if len(values)!=1 or any(values[0][1].get(k)!=str(v) for k,v in fields.items()):
            errors.append('missing/conflicting powered stage: '+prefix);return -1
        return values[0][0]
    order=[
        row('marble-npa-held: begin ',dict(active=2,requests=2,watchdog_seconds=30,limit_us=15000000,DSP_start=0)),
        row('marble-pil-held: loaded ',dict(PASS=1,native_rc=0,files=24,ROM_matches=24,zero_regions=25,loads=25,backup_retained=1,original_restored=0,auth_reset=0,dsp_start=0)),
        row('marble-pil-held: shutdown ',dict(status=0,transport=1,secure=0,verified=1,target_access=0)),
        row('marble-pil-held: restore-gate ',dict(shutdown_verified=1,display_disjoint=1,target_access=1)),
        row('marble-pil-held: restored ',dict(PASS=1,original_restored=1,free_status=0,backup_retained=0,shutdown_before_restore=1,DSP_start=execute)),
        row('marble-pil-header: complete ',dict(PASS=1,metadata_clear=1,PAS_metadata_requested=1,auth_reset=execute,dsp_start=execute)),
        row('marble-pil-ramfs: released ',dict(PASS=1,installed=0,dirty=0,file_writes=0,block_reads=0,block_writes=0,watchdog_clear='8000000000000007',dsp_start=execute)),
        row('marble-npa-held: returned ',dict(outcome=1,release_allowed=1,active=2,watchdog_retained=1))]
    if -1 in order or order!=sorted(set(order)):errors.append('shutdown/restore/metadata/release ordering not proven')
    lines=text.splitlines()
    requests=[i for i,line in enumerate(lines) if line.startswith('marble-npa-client: request ')]
    destroys=[i for i,line in enumerate(lines) if line.startswith('marble-npa-client: destroy ')]
    if len(requests)!=2 or len(destroys)!=2 or not(order and max(requests,default=999999)<order[0] and order[-1]<min(destroys,default=-1)):
        errors.append('power requests did not enclose the entire PIL action')
    releases=[dict(re.findall(r'(\w+)=([^\s]+)',line)) for line in lines if line.startswith('marble-pil-ramfs: released ')]
    try:
        if len(releases)!=1 or int(releases[0]['opens'])!=int(releases[0]['closes']):errors.append('native firmware file handles not balanced')
    except (KeyError,ValueError):errors.append('native firmware file closure unavailable')
    if execute:
        budget=dict(hold_limit_ms=500,thermal_samples=5,minimum_us=500000,maximum_us=750000)
        try:budget=_runtime.contract(text)
        except ValueError as e:errors.append(str(e))
        auth=row('marble-pil-boot: auth ',dict(status=0,transport=1,secure=0,accepted=1,hold_limit_ms=budget['hold_limit_ms'],payload_access=0))
        observed=row('marble-pil-boot: observed ',dict(thermal_samples=budget['thermal_samples'],thermal_ok=1,link_invalid=0,service_acceptance=0,audio_acceptance=0))
        released=row('marble-glink: released ',dict(PASS=1,active=0,dirty=0,invalid=0,open=0,tx=0,dsp_start_attempted=1,pmic_writes=0))
        if not(order[1]<auth<observed<order[2]<released<order[3]):errors.append('DSP auth/observation/stop/observer-release ordering incomplete')
        try:
            record=dict(re.findall(r'(\w+)=([^\s]+)',text.splitlines()[observed]))
            auth_record=dict(re.findall(r'(\w+)=([^\s]+)',text.splitlines()[auth]))
            if not budget['minimum_us']<=int(record['hold_us'])<budget['maximum_us'] or not 0<=int(record['die_mc'])<55000 or not 0<=int(auth_record['elapsed_us'])<1000000:
                errors.append('DSP hold/auth/temperature bound failed')
        except (KeyError,ValueError,IndexError):errors.append('DSP bound evidence unavailable')
    result.update(passed=not errors,scope='powered ROM load/readback, verified stop-before-restore, native power release and mandatory cold reset; DSP execution/audio not tested')
    if execute:result.update(dsp_secure_auth_accepted=not errors,dsp_service_ready=False,scope='bounded secure PAS auth/500ms observation/shutdown and stop-before-restore with mandatory cold reset; no DSP channel/audio acceptance')
    return result

def inspect_powered_stage(text):return _inspect_powered_stage(text,0)

def inspect_bounded_boot(text):return _inspect_powered_stage(text,1)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('log',type=Path);p.add_argument('--requests',type=int,choices=(0,2));p.add_argument('--native-release',action='store_true');p.add_argument('--powered-stage',action='store_true');p.add_argument('--bounded-boot',action='store_true');a=p.parse_args()
    text=a.log.read_text(errors='replace');report=inspect_bounded_boot(text) if a.bounded_boot else inspect_powered_stage(text) if a.powered_stage else inspect_native_release(text) if a.native_release else inspect(text,a.requests);print(json.dumps(report,indent=2))
    raise SystemExit(0 if report['passed'] else 1)
if __name__=='__main__':main()
