#!/usr/bin/env python3
"""Summarize measured marble logs; missing telemetry stays explicitly unavailable."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re


def distribution(values):
    values = sorted(values)
    if not values:
        return None
    return {'samples': len(values), 'p50': values[math.ceil(len(values)*.50)-1],
            'p95': values[math.ceil(len(values)*.95)-1], 'max': values[-1]}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('logs', type=Path, nargs='+')
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    lines = list(dict.fromkeys(line for path in a.logs for line in path.read_bytes().decode('utf8', 'replace').splitlines()))
    text = '\n'.join(lines)
    source = [int(x) for x in re.findall(r'source_hold_ms=(\d+)', text)]
    posted = [int(x) for x in re.findall(r'posted_hold_ms=(\d+)', text)]
    releases = [int(x) for x in re.findall(r'release_lag_ms=(\d+)', text)]
    bridge = [{k:int(v) for k,v in re.findall(r'(\w+)=(\d+)', line)} for line in lines if 'marble-touch: delivered=' in line]
    scans = [dict(zip(('frames', 'elapsed_ms', 'fps', 'driver_us', 'gpu_wait_us'), map(int, m)))
             for m in re.findall(r'MiniIOMobileFramebuffer: (\d+) frames in (\d+) ms \((\d+) a second\), (\d+) us each in the driver, after (\d+) us', text)]
    result = {
        'logs': {str(path):hashlib.sha256(path.read_bytes()).hexdigest() for path in a.logs},
        'ram_checks': [dict(checks=int(x), failed=int(y)) for x,y in re.findall(r'=== (\d+) checks, (\d+) failed ===', text)],
        'active_cpus': sorted(set(map(int,re.findall(r'hw.activecpu[ =]+(\d+)', text)))),
        'release_lag_ms': distribution(releases),
        'source_hold_ms': distribution(source),
        'posted_hold_ms': distribution(posted),
        'bridge_last': bridge[-1] if bridge else None,
        'bridge_max_lag_ms': max((x.get('max_lag_ms',0) for x in bridge), default=None),
        'framebuffer_windows': scans,
        'cpu_load_samples': len(re.findall(r'tl: u at ',text)),
        'requested_cpu_clock': [x for x in lines if x.startswith('marble-cpu:')],
        'cpu_stress': [x for x in lines if 'marble-cpu-stress:' in x],
        'faults': [x for x in lines if any(s in x for s in ('panic(cpu ', 'EARLY KERNEL EXCEPTION:',
                  'marble-touch: overrun', 'marble-touch: producer stale'))],
        'limits': ['Touch timing traces are capped; these percentiles are of recorded samples.',
                   'Framebuffer windows include idle time and do not by themselves benchmark continuous gestures.',
                   'Input timing excludes physical sensor latency and screen scanout.',
                   'EPSS requested row is not measured effective frequency; temperature telemetry is unavailable.']}
    a.output.write_text(json.dumps(result, indent=2)+'\n', encoding='utf8')
    print(a.output)
    print(json.dumps({k:result[k] for k in ('ram_checks','active_cpus','release_lag_ms','bridge_last','bridge_max_lag_ms','cpu_load_samples','faults')}, indent=2))


if __name__ == '__main__':
    main()
