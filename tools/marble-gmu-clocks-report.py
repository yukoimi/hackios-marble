#!/usr/bin/env python3
"""Verify reversible stage14 clock configuration and raw hardware counts."""
import argparse
import importlib.util
import json
from pathlib import Path

spec = importlib.util.spec_from_file_location('votes', Path(__file__).with_name('marble-gmu-votes-report.py'))
votes = importlib.util.module_from_spec(spec)
spec.loader.exec_module(votes)
REGS = ((0,0x62000),(1,0x9318),(1,0x931c),(1,0x93ec),(1,0x93f0),
        (0,0x72000),(0,0x72004),(0,0x72008),(0,0x7200c),(0,0x72038),
        (1,0x9564),(1,0x9270),(1,0x9274))


def inspect(path, oracle, stages=(14,)):
    result = votes.inspect(path, oracle, stages=stages)
    errors = result['errors']
    lines = path.read_bytes().decode('utf8', 'replace').splitlines()
    def select(prefix):
        return [votes.preflight.fields(s[len(prefix):]) for s in lines if s.startswith(prefix)]
    registers = select('marble-gmu-clocks: register ')
    if len(registers) != 13:
        errors.append('missing/duplicate clock register evidence')
    for i, r in enumerate(registers):
        if i >= 13 or set(r) != {'block','offset','before','after'} or (r.get('block'),r.get('offset')) != REGS[i]:
            errors.append('clock register inventory differs')
            continue
        mask = 0x7fffffff if i in (1,3) else 1 if i in (7,8,12) else 0xffffffff
        if (r['before'] ^ r['after']) & mask:
            errors.append('clock register was not restored: ' + hex(r['offset']))
    rates = select('marble-gmu-clocks: measurement ')
    if len(rates) != 2:
        errors.append('missing/duplicate measured rates')
    for index, r in enumerate(rates):
        if index > 1 or set(r) != {'index','short','full','hz'} or r.get('index') != index:
            errors.append('measurement inventory differs')
            continue
        hz = ((r['full']*10+15)*4800000*4)//(0x20000*10+35)
        target = 150000000 if index else 200000000
        if r['short'] == r['full'] or r['hz'] != hz or not target*95//100 <= hz <= target*105//100:
            errors.append('raw counts/rate differs or outside 5%: ' + str(index))
    status = select('marble-gmu-clocks: MGPU-MARBLE-GMU-CLOCKS-v1 ')
    if status != [dict(PASS=1,configured=1,measured=1,restored=1,retain=0,refused=0,phase=5,reset_released=0)]:
        errors.append('clock transaction incomplete or cleanup uncertain')
    result['clock_transaction'] = dict(registers=registers, rates=rates)
    result['passed'] = not errors
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('logs', type=Path, nargs='+')
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    oracle = votes.load_oracle(votes.ROOT / 'out/marble-gmu-votes-oracle.json')
    runs = [inspect(path, oracle) for path in a.logs]
    passed = all(r['passed'] for r in runs)
    a.output.write_text(json.dumps(dict(passed=passed,runs=runs,gmu_executed=False,
        acceleration_enabled=False,limits=['Measured GMU/HUB clocks while GMU held reset and GX off.',
        'No rail/ICC requests or active firmware/DMA proof.']),indent=2)+'\n')
    for run in runs:
        print(run['log'], 'PASS' if run['passed'] else 'FAIL')
        for error in run['errors']:print(error)
    raise SystemExit(0 if passed else 1)


if __name__ == '__main__':main()
