#!/usr/bin/env python3
"""Check measured stage7 CLI logs; never substitutes software address walks."""
import argparse
import hashlib
import json
from pathlib import Path
import re


def inspect(path):
    raw = path.read_bytes()
    text = raw.decode('utf8', 'replace')
    # Logs contain a second timestamped copy of the kernel ring. Use the
    # original unprefixed records to preserve request order without duplicates.
    records = [line for line in text.splitlines()
               if line.startswith('marble-ecats: MGPU-MARBLE-ECATS-v1 ')]
    requests = [{k: int(v, 0) for k, v in re.findall(r'(\w+)=(0x[0-9a-f]+|\d+)', line)}
                for line in records]
    good = len(requests) == 4
    for i, request in enumerate(requests):
        good &= all(request.get(k) == v for k, v in
                    dict(status=0, complete=1, restored=1, retain=0, GFSR=0, PASS=1).items())
        if i < 4:
            good &= request.get('iova') == (0x60000000, 0x60000fff, 0x60001000, 0x60000000)[i]
            negative = i == 2
            good &= request.get('fault') == int(negative)
            good &= bool(request.get('PAR', 0) & 1) == negative
            if not negative:
                good &= request.get('PA', 0) != 0 and request.get('PA') == request.get('expected')
    if len(requests) == 4:
        good &= requests[1].get('PA') == requests[0].get('PA', 0) + 4095
        good &= requests[3].get('PA') == requests[0].get('PA')
    required = [
        'context result passed=1 translation=1 released=1 fault_irq=0 writes=32 stage=7',
        'disable status=0 verified=1 tlb_sync=1 retain=0',
        'SID5 quarantined, SMR0 invalid, stream lease released',
        'CX result powered=1 clocks=1 observed=1 restored=1 skipped=0',
        '=== 36 checks, 0 failed ===',
        'hw.activecpu 4',
        *[f'returned secondary {core} PSCI state 1 (OFF=1)' for core in (600, 500, 400)],
    ]
    missing = [item for item in required if item not in text]
    faults = [line for line in text.splitlines()
              if any(key in line for key in ('panic(cpu ', 'EARLY KERNEL EXCEPTION:'))]
    return dict(log=str(path), sha256=hashlib.sha256(raw).hexdigest(),
                passed=bool(good and not missing and not faults),
                requests=requests, missing=missing, unexpected_faults=faults,
                checks=36 if not missing else None)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('logs', type=Path, nargs='+')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    tests = [inspect(path) for path in args.logs]
    report = dict(hardware_translation_proven=all(t['passed'] for t in tests),
                  acceleration_enabled=False, gmu_firmware_executed=False,
                  policy='temporary fastboot boot; RAM APFS; no Android userdata or partition writes',
                  tests=tests,
                  limits=['Only one owned 4KiB data page and SID5/CB0 tested.',
                          'ECATS proves translation, not GMU/GPU DMA or rendering.',
                          'FSR background 0x400 retained; defined fault mask is 0xc00001fe.'])
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
    print(args.output)
    for test in tests:
        print(test['log'], 'PASS' if test['passed'] else 'FAIL')
    if not report['hardware_translation_proven']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
