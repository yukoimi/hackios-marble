#!/usr/bin/env python3
"""Check stage13 CPU tables against pinned Xiaomi original-source output."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('preflight', Path(__file__).with_name('marble-gmu-preflight-report.py'))
preflight = importlib.util.module_from_spec(spec)
spec.loader.exec_module(preflight)


def load_oracle(path):
    oracle = json.loads(path.read_text())
    if oracle['source_commit'] != '48952ed36228217531482b39d5bef13e7fd808ec':
        raise ValueError('unexpected Xiaomi source pin')
    for key, file in (
        ('dt_sha256', ROOT.parent / 'image-inspection/marble-dtb.json'),
        ('oracle_sha256', ROOT / 'out/linux-build/marble-rpmh-xiaomi-oracle.c'),
        ('input_report_sha256', ROOT / 'out/marble-gmu-preflight-v2-report.json'),
    ):
        if hashlib.sha256(file.read_bytes()).hexdigest() != oracle[key]:
            raise ValueError('oracle input changed: ' + str(file))
    for name, count in (('performance', 58), ('bandwidth', 159)):
        words = oracle['tables'][name]
        if len(words) != count or any(type(w) is not int or not 0 <= w <= 0xffffffff for w in words):
            raise ValueError('malformed original-source table: ' + name)
    return oracle


def inspect(path, oracle, stages=(13,)):
    result = preflight.inspect(path, stages=stages)
    errors = result['errors']
    lines = path.read_bytes().decode('utf8', 'replace').splitlines()
    def select(prefix):
        return [s[len(prefix):] for s in lines if s.startswith(prefix)]
    def one(prefix):
        found = select('marble-gmu-votes: ' + prefix)
        if len(found) != 1:
            errors.append('missing/duplicate: ' + prefix)
            return ''
        return found[0]
    if result.get('resources') != oracle['resources'] or result.get('cmd_db_sha256') != oracle['cmd_db_sha256']:
        errors.append('real command DB differs from original-source oracle inputs')
    if one('catalog ') != 'gpu=8 gmu=3 bandwidth=12 enabled_OPPs=0; DT candidates only':
        errors.append('candidate catalog/policy differs')
    tables = {}
    for name, identifier in (('performance', 4), ('bandwidth', 3)):
        expected = oracle['tables'][name]
        digest = hashlib.sha256(struct.pack('<' + 'I' * len(expected), *expected)).hexdigest()
        if one('payload name=' + name + ' ') != f'id={identifier} words={len(expected)} sha256={digest}':
            errors.append('payload identity/size/digest differs: ' + name)
        chunks = select('marble-gmu-votes: words name=' + name + ' ')
        recovered = []
        if len(chunks) != (len(expected) + 7) // 8:
            errors.append('payload chunk count differs: ' + name)
        for index, chunk in enumerate(chunks):
            match = re.fullmatch(r'offset=(\d+) data=([0-9a-f]+)', chunk)
            remaining = len(expected) - index * 8
            count = min(8, remaining)
            if not match or int(match[1]) != index * 8 or count <= 0 or len(match[2]) != count * 8:
                errors.append('payload chunk bounds/order differs: ' + name)
                continue
            recovered.extend(int(match[2][i:i+8], 16) for i in range(0, len(match[2]), 8))
        if recovered != expected:
            errors.append('payload bytes differ from Xiaomi original source: ' + name)
        tables[name] = dict(id=identifier, words=len(recovered), sha256=digest, original_source_equal=recovered == expected)
    if len(select('marble-gmu-votes: payload ')) != 2 or len(select('marble-gmu-votes: words ')) != 28:
        errors.append('unexpected payload inventory')
    expected_status = dict(PASS=1, cpu_only=1, queue_writes=0, hardware_votes=0, notifications=0, reset_released=0)
    if preflight.fields(one('MGPU-MARBLE-GMU-VOTES-v1 ')) != expected_status:
        errors.append('CPU-only boundary incomplete or violated')
    result['tables'] = tables
    result['passed'] = not errors
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('logs', type=Path, nargs='+')
    parser.add_argument('--oracle', type=Path, default=ROOT / 'out/marble-gmu-votes-oracle.json')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    oracle = load_oracle(args.oracle)
    results = [inspect(path, oracle) for path in args.logs]
    for current in results[1:]:
        if any(current.get(k) != results[0].get(k) for k in ('clocks', 'resources', 'cmd_db_sha256', 'tables')):
            current['errors'].append('evidence differs between runs')
            current['passed'] = False
    passed = all(r['passed'] for r in results)
    args.output.write_text(json.dumps(dict(passed=passed, runs=results,
        oracle_sha256=hashlib.sha256(args.oracle.read_bytes()).hexdigest(),
        gmu_executed=False, acceleration_enabled=False,
        limits=['CPU-only DT candidate catalog, not enabled OPP or speed-bin policy.',
                'No HFI commands, rail/bandwidth requests, GMU execution or GPU DMA/rendering.']), indent=2) + '\n')
    for result in results:
        print(result['log'], 'PASS' if result['passed'] else 'FAIL')
        for error in result['errors']:
            print(error)
    raise SystemExit(0 if passed else 1)


if __name__ == '__main__':
    main()
