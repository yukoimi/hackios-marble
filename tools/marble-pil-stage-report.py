"""Qualify native PIL substage evidence; never equate loading with working audio."""
import argparse,hashlib,json,re
from pathlib import Path

def inspect(directory):
    directory=Path(directory)
    run=json.loads((directory/'exercise.json').read_text())
    path=directory/'pil-file-probe.log';raw=path.read_bytes();text=raw.decode(errors='replace')
    stage=run.get('requested_pil_stage') or ('all-segments' if run.get('pil_all_segments_command_ok') else
           'first-segment' if run.get('pil_first_segment_command_ok') else
           'memory-setup' if run.get('pil_pas_memory_command_ok') else 'metadata')
    checks={}
    phases={}
    for phase in ('metadata-init','memory-setup','metadata-shutdown'):
        found=re.findall(r'marble-pil-PAS: phase='+phase+r' status=(\w+) transport=(\w+) secure=(\w+) elapsed_us=(\d+)([^\n]*)',text)
        if len(found)!=1:
            checks[phase]=False;continue
        status,transport,secure,elapsed,tail=found[0]
        phases[phase]=dict(efi=int(status,16),transport=int(transport,16),secure=int(secure,16),elapsed_us=int(elapsed))
        checks[phase]=status=='0' and transport=='1' and secure=='0' and int(elapsed)<1000000
        if phase=='memory-setup':checks[phase]=checks[phase] and 'PASS=1' in tail
    if stage=='metadata':checks.pop('memory-setup')
    checks['native_metadata_clear']=bool(re.search(r'marble-pil-header: complete PASS=[01] metadata_clear=1 clear_rc=10 ',text))
    checks['stage_complete']=bool(re.search(r'marble-pil-header: complete PASS=1 ',text))
    released=re.findall(r'marble-pil-ramfs: released PASS=(\d+) installed=(\d+) dirty=(\d+) reads=(\d+) file_writes=(\d+) block_reads=(\d+) block_writes=(\d+) opens=(\d+) closes=(\d+) watchdog_clear=(\w+)',text)
    checks['balanced_release']=False
    if len(released)==1:
        passed,installed,dirty,reads,writes,block_reads,block_writes,opens,closes,watchdog=released[0]
        checks['balanced_release']=(not any(int(x) for x in (installed,dirty,writes,block_reads,block_writes)) and opens==closes and watchdog=='0')
    if stage=='first-segment':
        checks['segment_readback_restore']=bool(re.search(r'marble-pil-segment: index=1 native_rc=0 bytes=6760 zero_bytes=1432 ROM_match=1 tail_zero=1 original_restored=1 file_reads=1 elapsed_us=\d+ PASS=1 auth_reset=0 dsp_start=0',text))
    if stage=='all-segments':
        checks['all_segments_readback_restore']=bool(re.search(r'marble-pil-segments: native_rc=0 files=24 ROM_matches=24 zero_regions=25 loads=25 original_restored=1 free_status=0 elapsed_us=\d+ PASS=[01] auth_reset=0 dsp_start=0',text))
        checks['display_profile_before_and_after']=text.count('exact_single_GOP_disjoint=1 scope=display_profile_only')>=3
    checks['android_recovery']=bool(run.get('android_screen_off') and 'battery_after_c' in run)
    checks['shutdown_clean']=bool(re.search(r'phase=metadata-shutdown status=0 transport=1 secure=0 .*dirty=0 PASS=[01] auth_reset=0 dsp_start=0',text))
    order=[text.find('marble-pil-PAS: phase='+phase+' ') for phase in
           (('metadata-init','metadata-shutdown') if stage=='metadata' else ('metadata-init','memory-setup','metadata-shutdown'))]
    checks['phase_order']=all(x>=0 for x in order) and order==sorted(order) and len(set(order))==len(order)
    return dict(stage=stage,passed=all(checks.values()),checks=checks,phases=phases,
        log_sha256=hashlib.sha256(raw).hexdigest(),battery_before_c=run.get('battery_before_c'),battery_after_c=run.get('battery_after_c'),
        scope='isolated native PIL stage; no authentication/reset or DSP execution',audio_acceptance=False,
        limits=['No DSP execution','No audio stream or codec validation','No Wi-Fi/Bluetooth/sensor acceptance'])

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('directory',type=Path);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    if a.out.exists():p.error('new output required')
    result=inspect(a.directory);a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
    if not result['passed']:raise SystemExit(1)

if __name__=='__main__':main()
