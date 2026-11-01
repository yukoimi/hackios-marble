#!/usr/bin/env python3
"""Validate measured reset-held TCM diagnostics plus the full stage8 regression."""
import argparse
import importlib.util
import json
from pathlib import Path
import re

spec = importlib.util.spec_from_file_location('memory_report', Path(__file__).with_name('marble-gmu-memory-report.py'))
memory = importlib.util.module_from_spec(spec)
spec.loader.exec_module(memory)


def inspect(path):
    result = memory.inspect(path, stages=(9, 10))
    text = path.read_bytes().decode('utf8', 'replace')
    errors = result['errors']
    def one(pattern):
        rows = re.findall(pattern, text, re.M)
        if len(rows) != 1:
            errors.append('missing/duplicate TCM record: ' + pattern)
        return rows[0] if len(rows) == 1 else ''
    header = memory.fields(one(r'^marble-gmu-tcm: MGPU-MARBLE-GMU-TCM-v1 (.*)$'))
    master = memory.fields(one(r'^marble-smmu: GMU (.*)$'))
    if master != dict(SYSRESET=1, FW_INIT_RESULT=0, active_banks=0, asid_conflicts=0):
        errors.append('GMU/context initial quiescence evidence differs: ' + str(master))
    state = memory.fields('PASS=' + one(r'^marble-gmu-tcm: PASS=(.*)$'))
    stage = header.get('stage')
    expected = dict(PASS=1, stable=1, loaded=int(stage == 10), restored=int(stage == 10),
                    retain=0, reads=32768 if stage == 10 else 16384,
                    writes=270 if stage == 10 else 0, attempted=135 if stage == 10 else 0,
                    phase=7, failure_offset=0)
    if stage not in (9, 10) or header.get('spans') != 3 or header.get('words') != 135:
        errors.append('wrong TCM stage/layout')
    if state != expected:
        errors.append('TCM transaction counters/state differ: ' + str(state))
    snapshot = one(r'^marble-gmu-tcm: original_sha256=([0-9a-f]{64}) .*')
    if text.find('marble-smmu: context result') > text.find('marble-gmu-tcm: MGPU-'):
        errors.append('TCM started before context release')
    if 'GMU held reset' not in text or 'GMU not executed' not in text:
        errors.append('reset/nonexecution evidence missing')
    result.update(stage=stage, tcm=state, snapshot_sha256=snapshot, passed=not errors)
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('logs', nargs='+', type=Path)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    results = [inspect(path) for path in args.logs]
    passed = all(r['passed'] for r in results)
    # CX collapses between invocations: uninitialized TCM need not retain bits.
    # Each transaction independently proves its own original image was restored.
    hashes = {r['snapshot_sha256'] for r in results}
    report = dict(passed=passed, snapshot_consistent=len(hashes) == 1, runs=results,
                  scope='TCM MMIO load/readback/restore while reset; no GMU execution or rendering')
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    raise SystemExit(0 if passed else 1)


if __name__ == '__main__':
    main()
