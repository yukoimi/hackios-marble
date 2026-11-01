"""One bounded display pacing run, preserving raw evidence and settled captures."""
from marble_local import ADB as MARBLE_ADB, ANDROID_SERIAL as MARBLE_ANDROID_SERIAL, FASTBOOT as MARBLE_FASTBOOT
import argparse, json, re, shutil, subprocess, time
from pathlib import Path
from PIL import Image
from marble_android import screen_off

ACTIVE_OUTPUT=None


def main():
    global ACTIVE_OUTPUT
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--pacing',choices=('legacy','immediate','vsync'),required=True)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--kernel',type=Path,default=Path('out/linux-build/mach-marble-idle-fix'))
    p.add_argument('--ramdisk',type=Path,default=Path('out/marble-root-display-test.apfs'))
    p.add_argument('--record-seconds',type=int,default=0)
    p.add_argument('--seconds',type=int,choices=range(45,91),default=75)
    p.add_argument('--temperature-stop-mc',type=int,choices=range(40000,45001),default=45000)
    p.add_argument('--cpu-cap',action='store_true',help='matching post-CPU_ON request-cap loader and kernel required')
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
    ACTIVE_OUTPUT=a.out
    logs=a.out/'logs';logs.mkdir();captured=set();returned=False;last='';start=time.monotonic()
    gpu_ready=False;renderer=False;failure=None;dock=None;last_thermal=None;thermal_seen=start
    def run(*cmd):
        try:
            return subprocess.check_output(list(map(str,cmd)),stderr=subprocess.STDOUT,text=True,timeout=60)
        except subprocess.CalledProcessError as exc:
            print(exc.output,flush=True)
            raise
    def fb(*cmd):return run(MARBLE_FASTBOOT,'-s','xnuloader',*cmd)
    def snapshot():
        path=Path(run('python','tools/marble-run.py','snapshot').strip())
        target=logs/f'{len(list(logs.glob("*.log"))):03}.log';shutil.copyfile(path,target)
        return target.read_text(encoding='utf8',errors='replace')
    initial_status=fb('getvar','result')
    if 'result: idle' not in initial_status:
        raise RuntimeError('display test requires a fresh idle loader; preserve the prior run first')
    try:
        print(run('python','tools/marble-run.py','run','--kernel',a.kernel,'--ramdisk',a.ramdisk,
                  '--cpus','4','--seconds',str(a.seconds),'--fb-threads','2','--input-mailbox','--diagnostics',
                  '--gpu-probe','17','--gpu-runtime','--gpu-composition','--display-pacing',a.pacing,
                  '--record-seconds',a.record_seconds,'--detach',*(['--cpu-cap'] if a.cpu_cap else [])),flush=True)
        while time.monotonic()-start<a.seconds-5:
            status=fb('getvar','result')
            if 'result: returned ' in status:break
            last=snapshot()
            if 'marble-thermal: STOP requested' in last:
                failure='temperature guard stopped this run; no automatic retry'
                break
            thermal_rows=re.findall(r'marble-thermal: valid=(\d+) sensors=(\d+) maximum_mc=(-?\d+).*limit_mc=(?:45000|55000) ok=(\d+) at_us=(\d+)',last)
            if thermal_rows:
                valid,sensors,temperature,ok,stamp=map(int,thermal_rows[-1])
                if stamp!=last_thermal:thermal_seen=time.monotonic();last_thermal=stamp
                peak=max(int(row[2]) for row in thermal_rows)
                if valid!=1 or sensors!=19 or ok!=1 or peak>=a.temperature_stop_mc:
                    failure=f'conservative host temperature stop: observed peak {peak} mC; no automatic retry'
                    break
            if time.monotonic()-thermal_seen>5:
                failure='fresh runtime temperature evidence missing; no automatic retry'
                break
            gpu_ready |= 'MGPU-MARBLE-RUNTIME-v1 ready=1 service=1' in last
            renderer |= bool(re.search(r'redwood-gl: .*CoreAnimation.*; FD725;',last))
            if any(v in last for v in ('keeping software compositor','no accelerated renderer','completion timeout','panic(cpu ')):
                failure='GPU unavailable or hardware error; display experiment invalid'
                break
            if time.monotonic()-start>50 and not (gpu_ready and renderer):
                failure='hardware compositor readiness deadline exceeded'
                break
            if (gpu_ready and renderer and time.monotonic()-start>30 and 'ready' not in captured
                    and 'rwtouch: mark=panels-begin ' not in last):
                print(run('python','tools/marble-ui-regression.py','capture','ready','--out',a.out,'--settle','0').strip(),flush=True)
                captured.add('ready')
                band=Image.open(a.out/'ready.png').convert('RGB').crop((150,2110,930,2125))
                pixels=list(band.getdata())
                fraction=sum(min(rgb)>230 for rgb in pixels)/len(pixels)
                dock=dict(white_fraction=fraction,white_regression_rejected=fraction>.8,
                          scope='settled Dock band only; does not certify blur or transient frames')
                if fraction>.8:
                    failure='white Dock regression; GPU log alone is insufficient'
                    break
            marks=re.findall(r'rwtouch: mark=([\w-]+) at_us=(\d+)',last)
            for mark,_ in marks:
                if mark.endswith('-end') and mark not in captured:
                    print(run('python','tools/marble-ui-regression.py','capture',mark,'--out',a.out,'--settle','0').strip(),flush=True)
                    captured.add(mark)
            if any(mark=='complete' for mark,_ in marks):break
            time.sleep(2)
    finally:
        # Keep the observed reason even if firmware USB disappears during
        # shutdown. A device reset is not an audited GPU release.
        (a.out/'exercise.json').write_text(json.dumps(dict(
            returned=False,pacing=a.pacing,budget_seconds=a.seconds,
            host_temperature_stop_mc=a.temperature_stop_mc,failure=failure,
            gpu_ready=gpu_ready,hardware_renderer=renderer,captured=sorted(captured),
            visual_pass=None,transient_artifacts_pass=None),indent=2)+'\n')
        status=fb('getvar','result')
        if any(value in status for value in ('result: idle','result: not-started ')):
            # No kernel ran (for example, cold preflight refused). Preserve
            # the refusal and leave Android in charge of idle/power management.
            if 'result: idle' in status:fb('oem','log')
            fb('get_staged',a.out/'refused.log')
            print(fb('reboot'),flush=True)
            (a.out/'exercise.json').write_text(json.dumps(dict(
                returned=False,started=False,returned_to_android=True,
                failure='start refused; no automatic retry'),indent=2)+'\n')
            screen_off(a.out)
            raise RuntimeError('display start refused; diagnostic preserved; returned to Android')
        if 'result: running' in status:print(fb('oem','stop'),flush=True)
        for _ in range(35):
            status=fb('getvar','result')
            if 'result: returned ' in status:
                fb('get_staged',a.out/'returned.log');returned=True;break
            time.sleep(1)
        report=dict(returned=returned,pacing=a.pacing,seconds=time.monotonic()-start,captured=sorted(captured),
                    budget_seconds=a.seconds,host_temperature_stop_mc=a.temperature_stop_mc,
                    gpu_ready=gpu_ready,hardware_renderer=renderer,failure=failure,
                    dock=dock,
                    visual_pass=None,transient_artifacts_pass=None,
                    synthetic_input=True,optical_display=False,in_test_temperature=False)
        (a.out/'exercise.json').write_text(json.dumps(report,indent=2)+'\n')
        if not returned:raise RuntimeError('audited loader return not confirmed')
        try:
            if a.record_seconds:
                fb('oem','recording');fb('get_staged',a.out/'recording.bin')
            print(run('python','tools/marble-stitch-stream.py','--live-dir',logs,
                      '--returned',a.out/'returned.log','--output',a.out/'complete.log'),flush=True)
        finally:
            # Cleanup must also happen if evidence transfer/parsing raises.
            # The returned tail contains the hardware shutdown records.
            tail=(a.out/'returned.log').read_bytes().split(b'\0PMOC')[0].decode('utf8',errors='replace')
            released=bool(re.search(r'^marble-gpu-runtime: released PASS=1 draws=0 wraps=0 active=0\r?$',tail,re.M))
            gx=bool(re.search(r'^marble-gmu-gx: stopped rc=0 halted=1 .*collapsed=1 restored=1 phase=3\r?$',tail,re.M))
            context=bool(re.search(r'^marble-gpu-context: stopped PASS=1 reset=1 .*roots_zero=1 .*tlb_sync=1 irq_fault=0 ',tail,re.M))
            cap=(not a.cpu_cap or 'marble-cpucap: restored PASS=1 active=0 dirty=0 changed=0' in tail)
            if released and gx and context and cap:
                print(fb('reboot'),flush=True)
                report['returned_to_android']=True
                (a.out/'exercise.json').write_text(json.dumps(report,indent=2)+'\n')
                screen_off(a.out)
    text=(a.out/'complete.log').read_text(encoding='utf8')
    if failure:raise RuntimeError(failure)
    if not (gpu_ready and renderer):raise RuntimeError('hardware compositor not verified')
    if 'marble-gpu-runtime: released PASS=1' not in text:raise RuntimeError('GPU release not verified')
    if 'rwtouch: mark=complete ' not in text:raise RuntimeError('bounded gesture script did not complete')
    print(run('python','tools/marble-thermal-report.py',a.out/'complete.log','--out',a.out/'thermal-report.json'),flush=True)
    report['in_test_temperature']=True
    (a.out/'exercise.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report),flush=True)


if __name__=='__main__':
    try:main()
    except Exception as exc:
        if ACTIVE_OUTPUT is not None:
            file=ACTIVE_OUTPUT/'exercise.json'
            report=json.loads(file.read_text()) if file.exists() else {}
            report.update(host_error=str(exc),aborted=True)
            file.write_text(json.dumps(report,indent=2)+'\n')
            # Only if Android has independently reappeared: let its normal
            # display stack sleep. Never reboot a live/unverified GPU lease.
            state=subprocess.run([MARBLE_ADB,'-s',MARBLE_ANDROID_SERIAL,'get-state'],
                                 capture_output=True,timeout=8)
            if state.returncode==0 and state.stdout.strip()==b'device':
                screen_off(ACTIVE_OUTPUT)
                report['android_reappeared']=True
                file.write_text(json.dumps(report,indent=2)+'\n')
        raise
