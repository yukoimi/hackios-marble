"""Bounded GPU-off brightness bridge integration; temporary RAM boot only."""
from marble_local import ADB as MARBLE_ADB, ANDROID_SERIAL as MARBLE_ANDROID_SERIAL, FASTBOOT as MARBLE_FASTBOOT
import argparse,importlib.util,json,re,subprocess,time
from pathlib import Path
from marble_android import battery_snapshot,verify_battery_preflight,screen_off
ROOT=Path(__file__).resolve().parents[1]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--plan',type=Path,required=True)
    p.add_argument('--host-clock',action='store_true')
    a=p.parse_args();a.out.mkdir(exist_ok=False);report={}
    def save():(a.out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    def run(*args,timeout=30):
        r=subprocess.run(list(map(str,args)),capture_output=True,timeout=timeout,cwd=ROOT)
        output=(r.stdout+r.stderr).decode(errors='replace')
        print(output,flush=True)
        if r.returncode:raise RuntimeError(output)
        return output
    def fb(*args,timeout=15):return run(MARBLE_FASTBOOT,'-s','xnuloader',*args,timeout=timeout)
    try:
        _,before=battery_snapshot(a.out,'battery-before');verify_battery_preflight(a.out/'battery-before.json')
        report['battery_before_c']=before['temperature_c'];save();screen_off(a.out)
        run(MARBLE_ADB,'-s',MARBLE_ANDROID_SERIAL,'reboot','bootloader')
        deadline=time.monotonic()+45
        while MARBLE_ANDROID_SERIAL not in run(MARBLE_FASTBOOT,'devices') and time.monotonic()<deadline:time.sleep(1)
        run('python','tools/marble-run.py','--serial',MARBLE_ANDROID_SERIAL,'boot','--image',a.plan/'loader-boot.img',timeout=70)
        deadline=time.monotonic()+90
        while 'xnuloader' not in run(MARBLE_FASTBOOT,'devices') and time.monotonic()<deadline:time.sleep(1)
        if 'result: idle' not in fb('getvar','result'):raise RuntimeError('fresh loader not idle')
        fb('oem','clearscreen');fb('oem','idle-probe')
        fb('oem','log');fb('get_staged',a.out/'idle-probe.log')
        command=['python','tools/marble-run.py','run','--kernel','out/linux-build/mach-marble-vsync-v17',
            '--ramdisk',a.plan/'test.apfs','--seconds','18','--cpus','4','--fb-threads','4',
            '--input-mailbox','--cpu-cap','--efi-idle','--display-pacing','hardware','--brightness-backend','--detach']
        if a.host_clock:command.append('--host-clock')
        report['command']=list(map(str,command));save()
        run(*command,timeout=40)
        deadline=time.monotonic()+30
        while time.monotonic()<deadline:
            state=fb('getvar','result')
            if 'result: running' not in state:break
            time.sleep(1)
    except Exception as exc:
        report['error']=str(exc);save()
    finally:
        devices=run(MARBLE_FASTBOOT,'devices')
        if 'xnuloader' in devices:
            state=fb('getvar','result')
            if 'result: running' in state:
                fb('oem','stop');deadline=time.monotonic()+35
                while time.monotonic()<deadline:
                    state=fb('getvar','result')
                    if 'result: running' not in state:break
                    time.sleep(1)
            report['state']=state;save()
            fb('oem','log');fb('get_staged',a.out/'returned.log')
            text=(a.out/'returned.log').read_bytes().split(b'\0PMOC')[0].decode(errors='replace')
            if 'result: returned ' in state:
                checks={
                    'init_pass':'marble-brightness-test: complete PASS=1',
                    'reject_invalid':'marble-brightness-test: malformed_rejected PASS=1',
                    'cpu_restore':'marble-cpucap: restored PASS=1 active=0 dirty=0 changed=0',
                    'event_restore':'marble-efi-idle: restored PASS=1 active=0 dirty=0 failed=0',
                    'brightness_restore':'marble-brightness: restored PASS=1',
                    'display_bus_restore':'marble-display-bus: restored PASS=1 active=0 held=0 uncertain=0 dirty=0'}
                report['checks']={k:v in text for k,v in checks.items()}
                if a.host_clock:
                    epoch=re.search(r'\bepoch=(\d+)',text)
                    calendar=re.search(r'marble-brightness-test: calendar_seconds=(\d+)',text)
                    report['clock_epoch']=int(epoch[1]) if epoch else None
                    report['clock_observed']=int(calendar[1]) if calendar else None
                    report['checks']['clock_readback']=bool(epoch and calendar and
                        0<=int(calendar[1])-int(epoch[1])<=20)
                report['requests']=re.findall(r'marble-brightness-mailbox: request=.*',text)
                spec=importlib.util.spec_from_file_location('thermal',ROOT/'tools/marble-thermal-report.py')
                thermal=importlib.util.module_from_spec(spec);spec.loader.exec_module(thermal)
                audit=thermal.inspect(a.out/'returned.log')
                (a.out/'thermal-report.json').write_text(json.dumps(audit,indent=2)+'\n')
                report['thermal_passed']=audit['passed']
                report['passed']=all(report['checks'].values()) and len(report['requests'])==3 and audit['passed']
                save()
                if not all(report['checks'][k] for k in ('cpu_restore','event_restore','brightness_restore','display_bus_restore')):
                    raise RuntimeError('restore not verified; loader retained for diagnosis')
            elif not any(s in state for s in ('result: idle','result: not-started ')):
                raise RuntimeError('normal return unavailable; no blind reboot')
            fb('reboot')
        screen_off(a.out)
        _,after=battery_snapshot(a.out,'battery-after');report['battery_after_c']=after['temperature_c']
        report['android_screen_off']=True;save()
    if not report.get('passed'):raise SystemExit('brightness integration did not pass; see report')
    print(json.dumps(report,indent=2))
if __name__=='__main__':main()
