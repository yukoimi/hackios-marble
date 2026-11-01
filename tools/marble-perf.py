#!/usr/bin/env python3
"""Measure RAM desktop swipe presentation; works with CoreAnimation software rendering.

The upstream frames.py requires GL draw markers, which gl=0 does not emit.
This report uses actual framebuffer completion times, without inventing draw/GPU timing.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
from statistics import mean


def stats(values):
    v=sorted(values)
    return {'n':len(v),'mean':round(mean(v),2),'p50':v[math.ceil(.5*len(v))-1],
            'p95':v[math.ceil(.95*len(v))-1],'max':v[-1]} if v else None


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('log',type=Path)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    shows={}; gestures=set(); loads={}
    for line in a.log.read_bytes().decode('utf8','replace').splitlines():
        if 'tl: ' not in line: continue
        w=line[line.index('tl: '):].split()
        try:
            if w[1]=='v' and len(w)==9:
                shows[int(w[2])]=tuple(map(int,(w[4],w[6],w[8])))
            elif w[1]=='p' and len(w)==8 and w[2]=='1':
                gestures.add(int(w[7]))
            elif w[1]=='u':
                loads[int(w[3])]={k:int(v) for k,v in (x.split(':') for x in w[4:])}
        except ValueError:
            continue
    all_gaps=[]; all_copy=[]; all_refresh=[]; selected_loads={}; results=[]
    starts=sorted(gestures)
    for i,start in enumerate(starts):
        end=min(start+2500000,starts[i+1] if i+1<len(starts) else start+2500000)
        frames=sorted((v for v in shows.values() if start<=v[2]<end),key=lambda v:v[2])
        gaps=[b[2]-a[2] for a,b in zip(frames,frames[1:])]
        moving=[v for v in gaps if v<100000]
        copies=[done-shown for ready,shown,done in frames]
        refresh=[shown-ready for ready,shown,done in frames]
        selected_loads.update({t:v for t,v in loads.items() if start<=t<end})
        results.append({'start_us':start,'presentations':len(frames),
                        'moving_gap_us':stats(moving),'pauses_ge_100ms':len(gaps)-len(moving),
                        'copy_us':stats(copies),'refresh_wait_us':stats(refresh)})
        all_gaps+=moving; all_copy+=copies; all_refresh+=refresh
    result={'log_sha256':hashlib.sha256(a.log.read_bytes()).hexdigest(),
            'gestures':results,'moving_gap_us':stats(all_gaps),'copy_us':stats(all_copy),
            'refresh_wait_us':stats(all_refresh),
            'moving_presentations_per_second':round(1e6/mean(all_gaps),2) if all_gaps else None,
            'intervals_over_18ms_percent':round(100*sum(v>18000 for v in all_gaps)/len(all_gaps),2) if all_gaps else None,
            'cpu_busy_percent':{core:stats([v[core] for v in selected_loads.values() if core in v])
                                for core in ('7','6','5','4')},
            'limits':['Six synthetic horizontal gestures; this is not an application-wide FPS benchmark.',
                      'Intervals >=100 ms are excluded as pauses and reported separately.',
                      'Software renderer emits no GL draw markers; drawing and scheduling cannot be separated.',
                      'Completion means panel memory updated, not measured physical scanout.']}
    a.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
    print(json.dumps({k:v for k,v in result.items() if k not in ('gestures','limits')},indent=2))


if __name__=='__main__': main()
