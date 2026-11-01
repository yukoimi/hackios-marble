#!/usr/bin/env python3
"""Validate measured stage11 log/HFI mappings, empty queue image, and cleanup."""
import argparse
import importlib.util
import json
from pathlib import Path
import re

spec = importlib.util.spec_from_file_location('memory', Path(__file__).with_name('marble-gmu-memory-report.py'))
memory = importlib.util.module_from_spec(spec)
spec.loader.exec_module(memory)


def inspect(path, stages=(11,)):
    r = memory.inspect(path, stages=stages, resources=True)
    text = path.read_bytes().decode('utf8', 'replace')
    errors = r['errors']
    def check(prefix, expected):
        lines = [line[len(prefix):] for line in text.splitlines() if line.startswith(prefix)]
        if len(lines) != 1 or memory.fields(lines[0]) != expected:
            errors.append('missing/duplicate/incorrect resource evidence: ' + prefix)
    check('marble-gmu-resources: HFI ', dict(init_status=0, layout_ok=1, table_words=210, queue_offset=0x350, debug_priority=40, notify=0))
    check('marble-gmu-resources: MGPU-MARBLE-GMU-RESOURCES-v1 ',
          dict(ranges=8, pages=59, log_iova=0x60002000, hfi_iova=0x60006000, GENERAL8_planned=0x60002003))
    # Include the leading field; generic check() strips only the fixed label.
    lines = [line for line in text.splitlines() if line.startswith('marble-gmu-resources: images_intact=')]
    expected = dict(images_intact=1, layout_ok=1, notify=0, pending=0, sequence=0, poisoned=0)
    if len(lines) != 1 or memory.fields(lines[0]) != expected:
        errors.append('HFI/log image modified or command/notification published')
    check('marble-smmu: GMU ', dict(SYSRESET=1, FW_INIT_RESULT=0, active_banks=0, asid_conflicts=0))
    check('marble-gmu-tcm: MGPU-MARBLE-GMU-TCM-v1 ', dict(stage=9, spans=3, words=135))
    lines = [line for line in text.splitlines() if line.startswith('marble-gmu-tcm: PASS=')]
    expected = dict(PASS=1, stable=1, loaded=0, restored=0, retain=0, reads=16384, writes=0, attempted=0, phase=7, failure_offset=0)
    if len(lines) != 1 or memory.fields(lines[0]) != expected:
        errors.append('read-only TCM stability regression failed')
    if not re.search(r'^marble-gmu-tcm: original_sha256=[0-9a-f]{64} ', text, re.M):
        errors.append('missing TCM snapshot digest')
    for item in ('registers NOT written', 'GMU held reset', 'GMU not executed'):
        if item not in text:
            errors.append('missing: ' + item)
    r['passed'] = not errors
    return r


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('logs', type=Path, nargs='+')
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    results = [inspect(path) for path in a.logs]
    report = dict(passed=all(x['passed'] for x in results), runs=results,
                  gmu_firmware_executed=False, acceleration_enabled=False,
                  limits=['HFI queue image initialized/audited only; no HFI command, doorbell or ACK.',
                          'ECATS proves translations, not DMA coherence or GMU firmware execution.',
                          'Log/HFI register values are planned only, not programmed.'])
    a.output.write_text(json.dumps(report, indent=2) + '\n')
    for result in results:
        print(result['log'], 'PASS' if result['passed'] else 'FAIL', result['request_count'], 'requests')
        for error in result['errors']:
            print(error)
    raise SystemExit(0 if report['passed'] else 1)


if __name__ == '__main__':
    main()
