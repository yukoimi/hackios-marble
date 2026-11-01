#!/usr/bin/env python3
"""Validate real stage8 RAM/ECATS logs; no software translation substitutes."""
import argparse
import hashlib
import json
from pathlib import Path
import re


def fields(line):
    return {k: int(v, 0) for k, v in re.findall(r'(\w+)=(0x[0-9a-f]+|\d+)', line)}


def inspect(path, stages=(8,), resources=False):
    raw = path.read_bytes()
    text = raw.decode('utf8', 'replace')
    lines = text.splitlines()
    ranges = [fields(line) for line in lines if line.startswith('marble-gmu-memory: range=')]
    if resources:
        ranges += [fields(line) for line in lines if line.startswith('marble-gmu-resources: range=')]
    requests = [fields(line) for line in lines
                if line.startswith('marble-ecats: MGPU-MARBLE-ECATS-v1 ')]
    errors = []
    wanted_iova = (0x4000, 0x100000, 0x200000, 0x300000, 0x60000000, 0x60001000)
    wanted_bytes = (0xe000, 0x9000, 0x13000, 0x7000, 0x1000, 0x1000)
    if resources:
        wanted_iova += (0x60002000, 0x60006000)
        wanted_bytes += (0x4000, 0x4000)
    wanted_count = len(wanted_iova)
    pages = 59 if resources else 51
    if len(ranges) != wanted_count:
        errors.append('missing/extra ranges')
    for i, r in enumerate(ranges):
        if i >= wanted_count or r.get('iova') != wanted_iova[i] or r.get('bytes') != wanted_bytes[i] or r.get('permissions') != (15 if i < 4 else 11):
            errors.append('unexpected range: ' + str(r))
        if not r.get('PA') or r['PA'] & 4095 or r['PA'] + r.get('bytes', 0) > 1 << 36:
            errors.append('invalid physical range')
        if 0 < i < 6 and r.get('PA') != ranges[i-1].get('PA', 0) + ranges[i-1].get('bytes', 0):
            errors.append('backing is not the verified contiguous reservation')
        if i >= 6 and r.get('PA', 0) & 0x3fff:
            errors.append('auxiliary allocation not aligned to host page size')
        for previous in ranges[:i]:
            if r.get('PA', 0) < previous.get('PA', 0) + previous.get('bytes', 0) and previous.get('PA', 0) < r.get('PA', 0) + r.get('bytes', 0):
                errors.append('physical reservations overlap')
    retries = 0
    successes = []
    for i, r in enumerate(requests):
        if any(r.get(k) != v for k, v in dict(status=0, complete=1, restored=1, retain=0, GFSR=0).items()):
            errors.append('request or cleanup failed: ' + str(r))
        if not r.get('PASS'):
            # Locked Xiaomi quirk permits at most two retries for a positive
            # request after a prior negative result, never a timeout or PA mismatch.
            nxt = requests[i+1] if i+1 < len(requests) else {}
            if r.get('fault') or not r.get('PAR', 0) & 1 or r.get('attempt', 3) >= 3 or any(
                nxt.get(k) != r.get(k) for k in ('iova', 'access', 'expected')) or nxt.get('attempt') != r.get('attempt', 0) + 1:
                errors.append('unresolved/unsupported failed request: ' + str(r))
            retries += 1
            continue
        successes.append(r)
        if bool(r.get('PAR', 0) & 1) != bool(r.get('fault')) or r.get('FSR', 0) & 0xc00001fe != r.get('expected_fsr'):
            errors.append('fault type differs from expected: ' + str(r))
        if not r.get('fault') and (r.get('PA') != r.get('expected') or r.get('PA') != (r.get('PAR', 0) >> 12) & 0xfffffffff):
            errors.append('actual translated PA mismatch')
    def seen(iova, access, fault, pa=None):
        return any(r.get('iova') == iova and r.get('access') == access and r.get('fault') == fault
                   and (pa is None or r.get('PA') == pa) for r in successes)
    for i, r in enumerate(ranges):
        for offset in range(0, r.get('bytes', 0), 4096):
            for edge in (0, 4095):
                if not seen(r['iova']+offset+edge, 1, 0, r['PA']+offset+edge):
                    errors.append('missing page boundary translation')
        for access, fault in ((3, 0), (5, int(i >= 4)), (0, 1)):
            if not seen(r['iova'], access, fault):
                errors.append('missing access permission test')
        end = r['iova']+r['bytes']
        if (i+1 == len(ranges) or end < ranges[i+1]['iova']) and not seen(end, 1, 1):
            errors.append('missing unmapped guard test')
    if not seen(0, 1, 1):
        errors.append('missing unmapped TCM-address test')
    if len(successes) != (157 if resources else 133):
        errors.append('wrong number of successful measured translations')
    required = [
        f'inactive 4K tables 6 pages/{pages} leaves',
        f'active physical-table audit status=0 reads=3072 leaves={pages}',
        f'ECATS pages={pages} boundaries={pages*2} writes={wanted_count} fetches={wanted_count} denied={wanted_count} guards=6 recovered={wanted_count} PASS=1',
        'context result passed=1 translation=1 released=1 fault_irq=0',
        'disable status=0 verified=1 tlb_sync=1 retain=0',
        'SID5 quarantined, SMR0 invalid, stream lease released',
        'CX result powered=1 clocks=1 observed=1 restored=1 skipped=0',
        'RAM_image_intact=1;',
        '=== 36 checks, 0 failed ===', 'hw.activecpu 4',
        *[f'returned secondary {core} PSCI state 1 (OFF=1)' for core in (600, 500, 400)],
    ]
    errors += ['missing: ' + item for item in required if item not in text]
    if not any(re.fullmatch(r'marble-smmu: context result passed=1 translation=1 released=1 fault_irq=0 writes=\d+ stage=(' + '|'.join(map(str, stages)) + ')', line) for line in lines):
        errors.append('missing successful context lifecycle for required stage')
    errors += [line for line in lines if any(key in line for key in ('panic(cpu ', 'EARLY KERNEL EXCEPTION:'))]
    return dict(log=str(path), sha256=hashlib.sha256(raw).hexdigest(), passed=not errors,
                ranges=ranges, request_count=len(requests), quirk_retries=retries,
                requests=requests, errors=errors)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('logs', type=Path, nargs='+')
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    tests = [inspect(path) for path in args.logs]
    report = dict(hardware_memory_translation_proven=all(t['passed'] for t in tests),
                  gmu_firmware_executed=False, acceleration_enabled=False, tests=tests,
                  limits=['ECATS performs address translation, not a memory read/write or instruction execution.',
                          'RAM firmware copied and cache cleaned; three TCM payloads remain deferred.',
                          'No GMU log/HFI buffers or firmware startup yet; DMA coherence untested.'])
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
    for test in tests:
        print(test['log'], 'PASS' if test['passed'] else 'FAIL', 'requests:', test['request_count'])
        for error in test['errors'][:10]:
            print(error)
    if not report['hardware_memory_translation_proven']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
