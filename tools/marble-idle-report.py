"""Audit opt-in idle observations; does not certify sustained power/temperature."""
import argparse, importlib.util, json, re
from pathlib import Path

spec=importlib.util.spec_from_file_location('thermal',Path(__file__).with_name('marble-thermal-report.py'))
thermal=importlib.util.module_from_spec(spec);spec.loader.exec_module(thermal)


def inspect(path, cpus):
    report=thermal.inspect(path)
    text=path.read_bytes().split(b'\0PMOC')[0].decode(errors='replace')
    # The kernel message buffer duplicates some records. Use the explicit
    # redwood_tl stream before that buffer, keeping the raw log untouched.
    stream=text.split('--- kernel message buffer ---')[0]
    frequency=re.findall(r'marble-idle-regs: .*cntfrq=(\d+) enabled=1 signal=0',stream)
    samples={i:[] for i in range(cpus)};now=None
    for line in stream.splitlines():
        load=re.fullmatch(r'tl: u at (\d+).*',line)
        if load:now=int(load[1])
        if line.startswith('marble-idle: cpu=') and now is not None:
            row={k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',line)}
            if row['cpu'] in samples:samples[row['cpu']].append(dict(at_us=now,**row))
    errors=report['errors'];summaries=[]
    if not frequency or len(set(frequency))!=1:errors.append('consistent enabled idle/counter clock evidence missing')
    hz=int(frequency[0]) if frequency else 0
    for cpu,rows in samples.items():
        if len(rows)<5:
            errors.append(f'CPU {cpu}: five idle observations missing');continue
        a,b=rows[0],rows[-1];elapsed=b['at_us']-a['at_us']
        wait_us=(b['ticks']-a['ticks'])*1000000/hz if hz else 0
        ratio=wait_us/elapsed if elapsed>0 else 0
        if any(r['disallowed'] or r['pending'] for r in rows):errors.append(f'CPU {cpu}: disabled idle or pending WFI entry')
        if not .90<ratio<=1.01 or b['wfi']<=a['wfi']:errors.append(f'CPU {cpu}: WFI residency below 90% or invalid')
        summaries.append(dict(cpu=cpu,samples=len(rows),interval_us=elapsed,wfi_us=round(wait_us),
                              wfi_fraction=ratio,wfi_count=b['wfi']-a['wfi'],
                              longest_wfi_us=round(b['longest']*1000000/hz) if hz else None))
    if 'marble-idle: boot framebuffer black; panel power unchanged' not in text:errors.append('black framebuffer confirmation missing')
    report.update(passed=not errors,idle=summaries,cpus=cpus,
                  scope='short RAM-only sleeping-init observation; WFI instruction residence includes wake latency',
                  sustained_thermal_acceptance=False,gpu_acceptance=False,panel_power_off=False)
    return report


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('log',type=Path)
    p.add_argument('--cpus',type=int,choices=(1,2,4),required=True);p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();r=inspect(a.log,a.cpus);a.out.write_text(json.dumps(r,indent=2)+'\n')
    print(json.dumps({k:v for k,v in r.items() if k!='samples'},indent=2));raise SystemExit(0 if r['passed'] else 1)
