#!/usr/bin/env python3
"""Validate stage12 raw clocks/command DB evidence plus stage11 regressions."""
import argparse
import importlib.util
import json
from pathlib import Path
import re
import struct

spec = importlib.util.spec_from_file_location('resources', Path(__file__).with_name('marble-gmu-resources-report.py'))
resources = importlib.util.module_from_spec(spec)
spec.loader.exec_module(resources)
fields = resources.memory.fields
OFFSETS = (0x9318, 0x931c, 0x93ec, 0x93f0, 0x9430, 0x942c, 0x913c, 0x9148)
NAMES = ('gfx.lvl', 'cx.lvl', 'mx.lvl', 'mxc.lvl', 'CN0', 'SH0', 'MC0', 'ACV')


def nominal(hub, cfg):
    if cfg & 0x3000:
        return 0
    parent, hid = (cfg >> 8) & 7, cfg & 31
    # Use the pinned parent/frequency table. Unknown selections stay unknown.
    if parent == 0 and hid <= 1:
        return 19200000
    return ({(6, 3): 150000000, (5, 4): 240000000, (5, 3): 300000000} if hub else
            {(6, 2): 200000000, (3, 3): 500000000}).get((parent, hid), 0)


def inspect(path, stages=(12,)):
    result = resources.inspect(path, stages=stages)
    errors = result['errors']
    text = path.read_bytes().decode('utf8', 'replace')
    lines = text.splitlines()
    def select(prefix):
        return [line[len(prefix):] for line in lines if line.startswith(prefix)]
    def one(prefix):
        selected = select('marble-gmu-preflight: ' + prefix)
        if len(selected) != 1:
            errors.append('missing/duplicate: ' + prefix)
            return ''
        return selected[0]
    raw = [fields(s) for s in select('marble-gmu-preflight: clock ')]
    if len(raw) != 8 or [r.get('offset') for r in raw] != list(OFFSETS) or any(set(r) != {'offset', 'value'} for r in raw):
        errors.append('clock register inventory differs')
    else:
        values = [r['value'] for r in raw]
        expected = dict(PASS=1, stable=1, guarded=1, reads=16, writes=0,
                        GMU_nominal=nominal(False, values[1]), HUB_nominal=nominal(True, values[3]),
                        AHB_div=(values[4] & 15) + 1, CX_div=(values[5] & 15) + 1)
        if fields(one('clocks ')) != expected:
            errors.append('clock stability/guard/readback decode differs')
        result['clocks'] = dict(raw=raw, **expected)
    db = one('cmd_db ')
    db_fields = fields(db.split(' sha256=')[0])
    if set(db_fields) != {'PA', 'bytes', 'stable', 'reads', 'writes', 'version', 'standalone'} or any(
            db_fields.get(k) != v for k, v in dict(PA=0x80860000, bytes=0x20000, stable=1, reads=65536, writes=0, standalone=0).items()) or not db_fields.get('version'):
        errors.append('command DB region/stability/mode differs')
    digest = re.search(r'\bsha256=([0-9a-f]{64})(?:$|\s)', db)
    if not digest:
        errors.append('missing command DB digest')
    else:
        result['cmd_db_sha256'] = digest[1]
    found = select('marble-gmu-preflight: resource ')
    expected_found = [f'name={name} found=1' for name in NAMES]
    if found != expected_found:
        errors.append('resource lookup inventory differs')
    entries = select('marble-gmu-preflight: entry ')
    records = []
    if len(entries) != 8:
        errors.append('resource entry count differs')
    for index, entry in enumerate(entries):
        if index >= 8:
            break
        match = re.fullmatch(r'name=(\S+) slave=(\d+) version=(0x[0-9a-f]+|\d+) address=(0x[0-9a-f]+|\d+) bytes=(\d+) aux=([0-9a-f]+)', entry)
        if not match:
            errors.append('malformed resource entry')
            continue
        name, slave, version, address, size, aux = match.groups()
        slave, version, address, size = int(slave), int(version, 0), int(address, 0), int(size)
        if name != NAMES[index] or slave != (3 if index < 4 else 5) or not address or address & 3 or (address >> 16) & 7 != slave or size * 2 != len(aux) or not 0 < size <= 64:
            errors.append('resource identity/bounds/address differs: ' + name)
            continue
        data = bytes.fromhex(aux)
        record = dict(name=name, slave=slave, version=version, address=address, bytes=size, aux=aux)
        if index < 4:
            if size & 1:
                errors.append('odd ARC aux size: ' + name)
            else:
                record['levels'] = list(struct.unpack('<' + 'H' * (size // 2), data))
                # This step records the rail table; it does not select OPPs or
                # certify a rail policy suitable for a real power request.
        else:
            if size != 8 or data[7]:
                errors.append('unsupported BCM aux format: ' + name)
            else:
                unit, width, vcd, _ = struct.unpack('<IHBB', data)
                expected = dict(status=0, unit=unit, width=width, vcd=vcd)
                bcm_lines = select('marble-gmu-preflight: BCM name=' + name + ' ')
                if len(bcm_lines) != 1 or fields(bcm_lines[0]) != expected:
                    errors.append('BCM decode differs: ' + name)
                record.update(expected)
        records.append(record)
    if len(select('marble-gmu-preflight: BCM ')) != 4:
        errors.append('BCM decode count differs')
    expected = dict(PASS=1, clocks_writes=0, db_writes=0, votes=0, notifications=0, reset_released=0)
    if fields(one('MGPU-MARBLE-GMU-PREFLIGHT-v1 ')) != expected:
        errors.append('preflight incomplete or writes/start attempted')
    result['resources'] = records
    result['passed'] = not errors
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('logs', type=Path, nargs='+')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    results = [inspect(path) for path in args.logs]
    # Within one loader boot the immutable database and configuration should
    # agree across repeated power transactions, including after CX collapse.
    for current in results[1:]:
        if any(current.get(k) != results[0].get(k) for k in ('clocks', 'resources', 'cmd_db_sha256')):
            current['errors'].append('prerequisite evidence differs between runs')
            current['passed'] = False
    passed = all(r['passed'] for r in results)
    args.output.write_text(json.dumps(dict(passed=passed, runs=results,
        gmu_executed=False, acceleration_enabled=False,
        limits=['Clock table values are nominal configuration, not measured effective frequency.',
                'Command DB resource addresses/aux data are read only; no RPMh/AOP/ICC request.',
                'HFI remains stopped; no actual GPU DMA or rendering.']), indent=2) + '\n')
    for result in results:
        print(result['log'], 'PASS' if result['passed'] else 'FAIL')
        for error in result['errors']:
            print(error)
    raise SystemExit(0 if passed else 1)


if __name__ == '__main__':
    main()
