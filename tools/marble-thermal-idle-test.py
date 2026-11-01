"""One GPU-disabled, sleeping-init run, with evidence and return to Android."""
from marble_local import FASTBOOT as MARBLE_FASTBOOT
import importlib.util, json, re, subprocess, time
from pathlib import Path
import argparse
from marble_android import screen_off,battery_snapshot,verify_battery_preflight

spec=importlib.util.spec_from_file_location('thermal',Path(__file__).with_name('marble-thermal-report.py'))
thermal=importlib.util.module_from_spec(spec);spec.loader.exec_module(thermal)
spec=importlib.util.spec_from_file_location('efi_idle',Path(__file__).with_name('marble-efi-idle-report.py'))
efi_idle=importlib.util.module_from_spec(spec);spec.loader.exec_module(efi_idle)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--kernel',type=Path,default=Path('out/linux-build/mach-marble-idle-fix'))
    p.add_argument('--cpus',type=int,choices=(1,2,4),default=1)
    p.add_argument('--cpu-cap',action='store_true')
    p.add_argument('--efi-idle',action='store_true',help='first run a loader-only event-stream probe, then the small idle image')
    p.add_argument('--short-thermal',action='store_true')
    p.add_argument('--battery-preflight',type=Path)
    p.add_argument('--hardware-timing',action='store_true',help='qualify the mapped read-only DSI counter in the explicit source kernel')
    p.add_argument('--brightness-lease',action='store_true',help='test bounded brightness while sleeping XNU is active; restore before releasing display clocks')
    p.add_argument('--snapshot',action='store_true',help='validate a kernel-mutex black frame snapshot with GPU disabled')
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
    if a.efi_idle and not a.cpu_cap:p.error('EFI idle qualification requires --cpu-cap')
    if (a.brightness_lease or a.snapshot) and not(a.hardware_timing and a.short_thermal and a.cpus==4):
        p.error('brightness lease requires hardware timing, short thermal and four cores')
    if a.short_thermal:
        if not(a.cpu_cap and a.efi_idle and a.battery_preflight):p.error('short thermal test needs both leases and --battery-preflight')
        pre=verify_battery_preflight(a.battery_preflight)
        (a.out/'battery-before.json').write_text(json.dumps(pre,indent=2)+'\n',encoding='utf8')
    def fb(*args):
        r=subprocess.run([MARBLE_FASTBOOT,'-s','xnuloader',*map(str,args)],capture_output=True,timeout=30)
        if r.returncode:raise RuntimeError((r.stdout+r.stderr).decode(errors='replace'))
        return (r.stdout+r.stderr).decode(errors='replace')
    if 'result: idle' not in fb('getvar','result'):
        raise RuntimeError('requires a fresh idle loader without a resident system disk')
    cmd=['python','tools/marble-run.py','run','--kernel',str(a.kernel),
         '--ramdisk','out/marble-thermal-idle-minimal.apfs','--seconds','18','--cpus',str(a.cpus),
         '--diagnostics','--blank-display']
    if a.cpu_cap:cmd.append('--cpu-cap')
    if a.efi_idle:cmd.append('--efi-idle')
    if a.hardware_timing:cmd+=['--display-pacing','hardware']
    if a.short_thermal:cmd+=['--short-thermal','--battery-preflight',str(a.battery_preflight)]
    code=None
    try:
        if a.efi_idle:
            fb('oem','clearscreen')
            result=subprocess.run([MARBLE_FASTBOOT,'-s','xnuloader','oem','idle-probe'],capture_output=True,timeout=10)
            (a.out/'probe-console.txt').write_bytes(result.stdout+result.stderr)
            fb('oem','log');fb('get_staged',a.out/'probe.log')
            probe=efi_idle.inspect((a.out/'probe.log').read_text(errors='replace'),'probe')
            (a.out/'probe-report.json').write_text(json.dumps(probe,indent=2)+'\n')
            if result.returncode or not probe['passed']:
                raise RuntimeError('loader-only event-stream probe failed; no XNU/GPU start')
        if a.brightness_lease or a.snapshot:cmd.append('--detach')
        r=subprocess.run(cmd,capture_output=True,timeout=85);code=r.returncode
        (a.out/'host-console.txt').write_bytes(r.stdout+r.stderr)
        if (a.brightness_lease or a.snapshot) and code==0:
            deadline=time.monotonic()+12;ready=False
            while time.monotonic()<deadline and 'result: running' in fb('getvar','result'):
                fb('oem','log');fb('get_staged',a.out/'live.log')
                text=(a.out/'live.log').read_bytes().decode(errors='replace')
                if 'marble-thermal-idle: sleeping 20 seconds, GPU disabled' in text:
                    ready=True;break
                time.sleep(.25)
            if not ready:raise RuntimeError('sleeping init not ready; no brightness write sent')
            if a.brightness_lease:
                (a.out/'dim-console.txt').write_text(fb('oem','brightness-dim'))
                fb('oem','log');fb('get_staged',a.out/'dimmed.log')
            if a.snapshot:
                (a.out/'snapshot-console.txt').write_text(fb('oem','screenshot'))
                fb('get_staged',a.out/'snapshot.bgra')
                from PIL import Image
                data=(a.out/'snapshot.bgra').read_bytes()
                if len(data)!=1080*2400*4:raise RuntimeError('snapshot has incorrect geometry')
                im=Image.frombytes('RGBA',(1080,2400),data,'raw','BGRA',4320).convert('RGB')
                im.save(a.out/'snapshot.png')
                if im.getbbox() is not None:raise RuntimeError('idle snapshot is not black')
            deadline=time.monotonic()+30
            while time.monotonic()<deadline and 'result: running' in fb('getvar','result'):time.sleep(1)
    finally:
        state=fb('getvar','result')
        if 'result: running' in state:
            fb('oem','stop')
            for _ in range(35):
                time.sleep(1);state=fb('getvar','result')
                if 'result: running' not in state:break
        returned='result: returned ' in state
        if returned:
            fb('oem','log')
            fb('get_staged',a.out/'returned.log')
            if a.cpu_cap:
                text=(a.out/'returned.log').read_bytes().split(b'\0PMOC')[0].decode(errors='replace')
                if not re.search(r'marble-cpucap: restored PASS=1 active=0 dirty=0 changed=0',text):
                    raise RuntimeError('CPU request restore not verified; keep loader for diagnosis')
            if a.efi_idle and not re.search(r'marble-efi-idle: restored PASS=1 active=0 dirty=0',
                    (a.out/'returned.log').read_bytes().split(b'\0PMOC')[0].decode(errors='replace')):
                raise RuntimeError('EFI control restore not verified; keep loader for diagnosis')
            if a.hardware_timing and not re.search(r'marble-display-bus: restored PASS=1 active=0 held=0 uncertain=0 dirty=0',
                    (a.out/'returned.log').read_bytes().split(b'\0PMOC')[0].decode(errors='replace')):
                raise RuntimeError('display-bus clock restoration not verified; keep loader for diagnosis')
            if a.brightness_lease and not re.search(r'marble-brightness: restored PASS=1 original=\d+ level=\d+ active=0 dirty=0 changes=1',
                    (a.out/'returned.log').read_bytes().decode(errors='replace')):
                raise RuntimeError('brightness restoration not verified; keep loader for diagnosis')
        elif any(value in state for value in ('result: idle','result: not-started ')):
            if 'result: idle' in state:fb('oem','log')
            fb('get_staged',a.out/'refused.log')
        else:raise RuntimeError('normal loader return not confirmed: '+state)
        print(fb('reboot'),flush=True)
        (a.out/'host.json').write_text(json.dumps(dict(command=cmd,exit_code=code,state=state,
            returned=returned,returned_to_android=True),indent=2)+'\n')
        screen_off(a.out)
        if a.short_thermal:
            _,post=battery_snapshot(a.out,'battery-after')
    if not returned:raise RuntimeError('start refused; no automatic retry; Android restored')
    report=thermal.inspect(a.out/'returned.log')
    if a.short_thermal:
        report['post_return_battery_c']=post['temperature_c']
        report['battery_temperature']=False
        if post['temperature_decic']>=400:report['errors'].append('post-return battery temperature reached user 40 C target')
    text=(a.out/'returned.log').read_bytes().split(b'\0PMOC')[0].decode('utf8',errors='replace')
    if 'marble-thermal-idle: sleeping 20 seconds, GPU disabled' not in text:
        report['errors'].append('sleeping init did not start')
    if code!=0:report['errors'].append('host run command failed')
    if a.hardware_timing and 'marble-display: MDISPLAY-MARBLE-COUNTER-v2 ready=1' not in text:
        report['errors'].append('XNU did not qualify the firmware-leased display counter')
    if a.brightness_lease and not re.search(r'marble-brightness: dimmed PASS=1 original=\d+ level=\d+ active=1 dirty=0 changes=1',text):
        report['errors'].append('runtime brightness reduction was not verified')
    if a.snapshot and not re.search(r'marble-snapshot: PASS=1 request=1 sequence=0 bytes=10368000 kernel_mutex=1 private_staging=1',text):
        report['errors'].append('kernel-mutex snapshot was not confirmed')
    if 'marble-idle: boot framebuffer black; panel power unchanged' not in text:
        report['errors'].append('black framebuffer was not confirmed')
    if a.cpu_cap:
        if 'marble-cpucap: acquired PASS=1 active=1 dirty=0' not in text:
            report['errors'].append('CPU request lease not acquired')
        for d in (1,2):
            if not re.search(r'MCPU-MARBLE-CAP-CHECK-v2 domain='+str(d)+r' row=\d+ requested_khz=\d+ PASS=1',text):
                report['errors'].append(f'CPU domain {d} lease not verified by XNU')
    if a.efi_idle:
        event=efi_idle.inspect(text)
        (a.out/'efi-idle-report.json').write_text(json.dumps(event,indent=2)+'\n')
        report['errors'].extend(event['errors'])
    report['passed']=not report['errors']
    (a.out/'thermal-report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='samples'},indent=2))
    raise SystemExit(0 if report['passed'] else 1)


if __name__=='__main__':main()
