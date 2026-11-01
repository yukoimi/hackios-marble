"""One CPU-capped iOS startup, optionally with GPU; no automatic retry.

Upload the system APFS with marble-run.py ramdisk first. A fresh idle loader
and matching post-CPU_ON CPU-cap kernel are required. This measures recorded
die temperatures; it is not battery, rendering or sustained-power acceptance.
"""
from marble_local import ADB as MARBLE_ADB, ANDROID_SERIAL as MARBLE_ANDROID_SERIAL, FASTBOOT as MARBLE_FASTBOOT
import argparse,importlib.util,json,re,shutil,subprocess,time
from pathlib import Path
from marble_android import screen_off


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--kernel',type=Path,default=Path('out/linux-build/mach-marble-cpucap-v3'))
    p.add_argument('--ramdisk',type=Path,default=Path('out/marble-root-panels-scissor-short.apfs'))
    p.add_argument('--seconds',type=int,choices=range(30,76),default=60)
    p.add_argument('--gpu',action='store_true')
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
    logs=a.out/'logs';logs.mkdir()
    report=dict(gpu_enabled=a.gpu,cpu_cap_required=True,budget_seconds=a.seconds,
                returned=False,visual_acceptance=False,battery_temperature=False,
                scope='one bounded iOS startup, TSENS die temperatures only')
    def save(): (a.out/'exercise.json').write_text(json.dumps(report,indent=2)+'\n')
    def run(*cmd,timeout=12):
        r=subprocess.run(list(map(str,cmd)),capture_output=True,timeout=timeout)
        text=(r.stdout+r.stderr).decode(errors='replace')
        if r.returncode:raise RuntimeError(text)
        return text
    def fb(*cmd,timeout=12):return run(MARBLE_FASTBOOT,'-s','xnuloader',*cmd,timeout=timeout)
    def snapshot():
        path=Path(run('python','tools/marble-run.py','snapshot',timeout=20).strip())
        dest=logs/f'{len(list(logs.glob("*.log"))):03}.log';shutil.copyfile(path,dest)
        return dest.read_bytes().split(b'\0PMOC')[0].decode(errors='replace')
    save()
    try:
        if 'result: idle' not in fb('getvar','result'):
            raise RuntimeError('requires fresh idle loader')
        cmd=['python','tools/marble-run.py','run','--kernel',a.kernel,'--ramdisk',a.ramdisk,
             '--cpus','4','--seconds',str(a.seconds),'--fb-threads','2','--input-mailbox',
             '--diagnostics','--cpu-cap','--display-pacing','immediate','--detach']
        if a.gpu:cmd+=['--gpu-probe','17','--gpu-runtime','--gpu-composition']
        report['command']=list(map(str,cmd));save()
        start=time.monotonic();stamp=None;fresh=start;failure=None
        try:
            print(run(*cmd,timeout=30),flush=True)
            while time.monotonic()-start<a.seconds+5:
                if 'result: returned ' in fb('getvar','result'):break
                text=snapshot()
                rows=re.findall(r'marble-thermal: valid=(\d+) sensors=(\d+) maximum_mc=(-?\d+).*limit_mc=45000 ok=(\d+) at_us=(\d+)',text)
                if rows:
                    valid,sensors,temp,ok,latest=map(int,rows[-1])
                    if latest!=stamp:fresh=time.monotonic();stamp=latest
                    peak=max(int(r[2]) for r in rows)
                    report['observed_peak_mc']=max(report.get('observed_peak_mc',-40000),peak)
                    if valid!=1 or sensors!=19 or ok!=1 or peak>=45000:
                        failure=f'die temperature guard: observed {peak} mC';break
                if time.monotonic()-fresh>5:failure='fresh temperature evidence missing';break
                if any(s in text for s in ('marble-thermal: STOP requested','panic(cpu ',
                    'completion timeout','marble-cpucap: runtime request changed')):
                    failure='kernel/hardware/temperature error in live log';break
                report['gpu_ready']='MGPU-MARBLE-RUNTIME-v1 ready=1 service=1' in text
                report['hardware_renderer']=bool(re.search(r'redwood-gl: .*CoreAnimation.*; FD725;',text))
                save();time.sleep(1)
            report['failure']=failure;save()
        finally:
            state=fb('getvar','result')
            if any(s in state for s in ('result: idle','result: not-started ')):
                if 'result: idle' in state:fb('oem','log')
                fb('get_staged',a.out/'refused.log')
                print(fb('reboot'),flush=True);screen_off(a.out)
                report.update(started=False,returned_to_android=True,loader_state=state,
                              failure='start refused; no automatic retry')
                save();raise RuntimeError('start refused; evidence saved; Android screen OFF')
            if 'result: running' in state:fb('oem','stop')
            deadline=time.monotonic()+35
            while 'result: running' in state and time.monotonic()<deadline:
                time.sleep(1);state=fb('getvar','result')
            report['loader_state']=state
            if 'result: returned ' not in state:raise RuntimeError('normal loader return not confirmed')
            fb('get_staged',a.out/'returned.log')
            report['returned']=True;save()
            tail=(a.out/'returned.log').read_bytes().split(b'\0PMOC')[0].decode(errors='replace')
            cap=bool(re.search(r'marble-cpucap: restored PASS=1 active=0 dirty=0 changed=0',tail))
            report['cpu_requests_restored']=cap
            released=(not a.gpu or all(re.search(s,tail,re.M) for s in (
                r'^marble-gpu-runtime: released PASS=1 draws=0 wraps=0 active=0\r?$',
                r'^marble-gmu-gx: stopped rc=0 halted=1 .*collapsed=1 restored=1 phase=3\r?$',
                r'^marble-gpu-context: stopped PASS=1 reset=1 .*roots_zero=1 .*tlb_sync=1 irq_fault=0 ')))
            report['gpu_release_audited']=bool(released) if a.gpu else None
            save()
            if not(cap and released):raise RuntimeError('request/GPU release incomplete; preserve loader for diagnosis')
            print(fb('reboot'),flush=True);screen_off(a.out)
            report['returned_to_android']=True;save()
        print(run('python','tools/marble-stitch-stream.py','--live-dir',logs,
                  '--returned',a.out/'returned.log','--output',a.out/'complete.log',timeout=20),flush=True)
        spec=importlib.util.spec_from_file_location('thermal',Path(__file__).with_name('marble-thermal-report.py'))
        thermal=importlib.util.module_from_spec(spec);spec.loader.exec_module(thermal)
        checked=thermal.inspect(a.out/'complete.log')
        (a.out/'thermal-report.json').write_text(json.dumps(checked,indent=2)+'\n')
        text=(a.out/'complete.log').read_text(errors='replace')
        report['thermal_samples_pass']=checked['passed']
        report['complete_log_peak_mc']=checked['maximum_mc']
        report['thermal_errors']=checked['errors']
        if checked['errors'] and not report.get('failure'):
            report['failure']='final log: '+'; '.join(checked['errors'])
        report['startup_observed']=any(s in text for s in ('SpringBoard','backboardd'))
        report['passed']=checked['passed'] and not report.get('failure') and report['startup_observed']
        if a.gpu:report['passed'] &= report.get('gpu_ready',False) and report.get('hardware_renderer',False)
        save();print(json.dumps(report,indent=2),flush=True)
        return 0 if report['passed'] else 1
    except Exception as exc:
        report.update(aborted=True,error=str(exc));save()
        state=subprocess.run([MARBLE_ADB,'-s',MARBLE_ANDROID_SERIAL,'get-state'],capture_output=True,timeout=8)
        if state.returncode==0 and state.stdout.strip()==b'device':
            screen_off(a.out);report['android_reappeared']=True;save()
        raise


if __name__=='__main__':raise SystemExit(main())
