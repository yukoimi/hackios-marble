"""Audit recorded TSENS samples, without claiming skin/battery temperature."""
import argparse, json, re
from pathlib import Path


def inspect(path):
    text=path.read_bytes().split(b'\0PMOC')[0].decode('utf8',errors='replace').replace('\r\n','\n')
    pattern=r'^marble-thermal: valid=(\d+) sensors=(\d+) maximum_mc=(-?\d+) controller=(\d+) sensor=(\d+) limit_mc=(\d+) ok=(\d+) at_us=(\d+)$'
    names=('valid','sensors','maximum_mc','controller','sensor','limit_mc','ok','at_us')
    samples=[dict(zip(names,map(int,m))) for m in re.findall(pattern,text,re.M)]
    profiles=set(re.findall(r'^marble-thermal-window: profile=(\S+)',text,re.M))
    window_pattern=r'^marble-thermal-window: profile=(?:short18-v1|gpu90-v1|gpu90-v2) starting=(\d+) hot50_ms=(\d+) max_hot50_ms=(\d+) reason=(\d+) ok=(\d+) at_us=(\d+)$'
    windows=[dict(zip(('starting','hot50_ms','max_hot50_ms','reason','ok','at_us'),map(int,m)))
             for m in re.findall(window_pattern,text,re.M)]
    short=bool(windows)
    running=[r for r in samples if r['limit_mc'] in (45000,55000)];cold=[r for r in samples if r['limit_mc'] in ((40000,50000) if short else (40000,))]
    errors=[]
    if len(profiles)>1 or profiles-set(('short18-v1','gpu90-v1','gpu90-v2')):errors.append('mixed or unknown thermal window profile')
    if any(r['limit_mc'] not in ((40000,45000,50000,55000) if short else (40000,45000,55000)) for r in samples):errors.append('unknown temperature threshold')
    if short:
        duration_limit=60000 if profiles=={'gpu90-v2'} else 10000
        paired={r['at_us']:r for r in samples}
        for window in windows:
            sample=paired.get(window['at_us'])
            if (not sample or window['starting'] not in (0,1) or window['reason'] or window['ok']!=1 or
                window['max_hot50_ms']>=duration_limit or window['hot50_ms']>=duration_limit or
                sample['limit_mc']!=(50000 if window['starting'] else 55000)):
                errors.append('short thermal duration guard failed or unpaired evidence');break
        if any(r['limit_mc'] in (50000,55000) and r['at_us'] not in {w['at_us'] for w in windows} for r in samples):
            errors.append('short profile is missing duration evidence')
    if not cold or len(running)<5:errors.append('cold preflight or five running samples missing')
    if any(r['valid']!=1 or r['sensors']!=19 or r['ok']!=1 or not -40000<=r['maximum_mc']<r['limit_mc'] or
           r['controller'] not in (0,1) or not 1<=r['sensor']<=(4 if r['controller'] else 15) for r in samples):
        errors.append('invalid/hot sample or channel metadata')
    gaps=[b['at_us']-a['at_us'] for a,b in zip(running,running[1:])]
    if any(not 950000<=gap<=3500000 for gap in gaps):errors.append('runtime sample interval outside 0.95..3.5 seconds')
    if running and cold and running[0]['at_us']<=cold[-1]['at_us']:errors.append('runtime samples precede final cold preflight')
    if 'marble-thermal: STOP requested' in text:errors.append('temperature stop triggered; automatic retry forbidden')
    if 'panic(cpu' in text or 'EARLY KERNEL EXCEPTION' in text:errors.append('kernel did not complete normally')
    if 'kernel returned:' not in text:errors.append('loader return not recorded')
    return dict(passed=not errors,errors=errors,log=str(path),samples=samples,
                running_samples=len(running),maximum_mc=max((r['maximum_mc'] for r in running),default=None),
                max_sample_gap_us=max(gaps,default=None),
                thermal_profile=next(iter(profiles)) if short else 'legacy',
                max_observed_consecutive_ge50_ms=max((r['max_hot50_ms'] for r in windows),default=None),
                scope='read-only TSENS CPU/GPU die samples; interval-qualified, not continuous measurement',
                battery_temperature=False,skin_temperature=False,thermal_design_certification=False)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('log',type=Path);p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();r=inspect(a.log);a.out.write_text(json.dumps(r,indent=2)+'\n')
    print(json.dumps({k:v for k,v in r.items() if k!='samples'},indent=2));raise SystemExit(0 if r['passed'] else 1)


if __name__=='__main__':main()
