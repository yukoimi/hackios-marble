#!/usr/bin/env python3
"""Preserve changing live init/MMIO boundaries before an unattended reset."""
from marble_local import FASTBOOT as MARBLE_FASTBOOT
import argparse,json,subprocess,time
from pathlib import Path

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--kernel',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--runtime',action='store_true');p.add_argument('--seconds',type=int,default=20)
    p.add_argument('--ramdisk',type=Path,default=Path('out/marble-root-gpu-cli.apfs'));a=p.parse_args()
    a.out.mkdir(parents=True,exist_ok=True);fb=[MARBLE_FASTBOOT,'-s','xnuloader']
    def call(*args):
        r=subprocess.run([*fb,*map(str,args)],capture_output=True,timeout=3,check=True)
        return (r.stdout+r.stderr).decode(errors='replace')
    subprocess.run(['python','tools/marble-run.py','run','--kernel',str(a.kernel),'--ramdisk',str(a.ramdisk),'--cpus','4','--seconds',str(a.seconds),'--gpu-probe','17','--fb-threads','2','--detach']+(['--gpu-runtime'] if a.runtime else []),check=True)
    start=time.monotonic();last=None;records=[];returned=False;probe_done=False;probe_passed=False
    for i in range(max(300,a.seconds*12)):
        try:
            status=call('getvar','result');returned='result: returned ' in status
            if probe_done and not returned:
                time.sleep(.05)
                if time.monotonic()-start>a.seconds+5:break
                continue
            if not returned:call('oem','log')
            current=a.out/'current.log';call('get_staged',current);data=current.read_bytes()
            steps=[s for s in data.decode('utf8','replace').splitlines() if s.startswith(('marble-gmu-step:','marble-gpu-cp:','marble-gpu-zap:','marble-gpu-secure:','marble-gpu-blit:','marble-gpu-runtime:','marble-gpu-live-map:','marble-gpu-service:','marble-gl-test:','gl_test:','RedwoodGPU:','marble-gpu-context: HEALTH_FAIL','marble-gpu-context: IRQ_SNAPSHOT'))]
            # After an app has completed its verified draw the CLI begins
            # shutdown immediately. Stop requesting live logs now: old loaders
            # can overwrite a just-finished final log with an empty live one.
            probe_done=(b'marble-gpu: probe complete;' in data or b'marble-gl-test: PASS=1 ' in data or
                b'marble-gpu-service: work PASS=' in data)
            probe_passed=(b'marble-gmu-boot: MGPU-MARBLE-GMU-BOOT-v1 PASS=1 ' in data and
                b'=== 36 checks, 0 failed ===' in data and b'panic(cpu ' not in data and
                b'HEALTH_FAIL' not in data)
            state=steps[-1] if steps else str(len(data))
            if state!=last or returned:
                q=a.out/(f'{i:03d}.log' if not returned else 'returned.log');q.write_bytes(data)
                record=dict(seconds=round(time.monotonic()-start,3),state=state,file=q.name)
                print(json.dumps(record),flush=True);records.append(record);last=state
            if returned:break
        except (subprocess.TimeoutExpired,subprocess.CalledProcessError) as e:
            print(type(e).__name__,str(e),flush=True);break
        if time.monotonic()-start>a.seconds+5:break
    (a.out/'capture.json').write_text(json.dumps(dict(returned=returned,probe_passed=probe_passed,records=records),indent=2)+'\n')
    raise SystemExit(0 if returned and probe_passed else 1)
if __name__=='__main__':main()
