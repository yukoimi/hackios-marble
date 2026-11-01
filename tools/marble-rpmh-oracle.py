#!/usr/bin/env python3
"""Extract pinned Xiaomi RPMh arithmetic for userspace-only compatibility tests.

Generated GPL code stays outside the kernel. Measured inputs come from an
already-passed stage12 report; no device requests are sent by this tool.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re


def function(source, name):
    match = re.search(r'static (?:int|void) ' + name + r'\(', source)
    if not match:
        raise ValueError('missing original function: ' + name)
    start = source.index('{', match.start())
    depth = 1
    pos = start + 1
    while depth:
        depth += (source[pos] == '{') - (source[pos] == '}')
        pos += 1
    return source[match.start():pos]


def macro(source, name):
    match = re.search(r'^#define ' + name + r'(?:\s|\()', source, re.M)
    if not match:
        raise ValueError('missing original macro: ' + name)
    output = ''
    for line in source[match.start():].splitlines(keepends=True):
        output += line
        if not line.rstrip().endswith('\\'):
            break
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sources', type=Path, default=Path('../upstream-marble/gpu'))
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    metadata = json.loads((args.sources / 'sources.json').read_text())
    if metadata['commit'] != '48952ed36228217531482b39d5bef13e7fd808ec':
        raise ValueError('unexpected Xiaomi pin')
    def read(path):
        file = args.sources / path
        if not file.exists():
            file = args.sources / Path(path).name
        data = file.read_bytes()
        record = next(r for r in metadata['files'] if r['path'] == path)
        if hashlib.sha256(data).hexdigest() != record['sha256']:
            raise ValueError('pinned source changed: ' + path)
        return data.decode()
    source = read('drivers/gpu/msm/adreno_gen7_rpmh.c')
    tcs = read('include/soc/qcom/tcs.h')
    header = read('drivers/gpu/msm/adreno_hfi.h')
    core = read('drivers/gpu/msm/kgsl_gmu_core.h')
    parts = [source[:source.index('#include')].strip(),
             '/* Test-only original Xiaomi 48952ed3 functions; never part of XNU. */',
             '#include <stdint.h>\n#include <stdbool.h>\n#include <errno.h>',
             'typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;',
             '#define GENMASK(h,l) ((~0u << (l)) & (~0u >> (31-(h))))',
             '#define FIELD_PREP(mask,value) (((u32)(value) << __builtin_ctz(mask)) & (mask))',
             '#define do_div(n,d) ((n)/=(d))\n#define min_t(t,a,b) ((t)(a)<(t)(b)?(t)(a):(t)(b))']
    for name in ('rpmh_arc_vals', 'bcm', 'rpmh_bw_votes'):
        parts.append(re.search(r'struct ' + name + r' \{.*?\n\};', source, re.S)[0])
    parts.append(macro(source, 'ARC_VOTE_SET'))
    for name in ('BCM_TCS_CMD_COMMIT_SHFT', 'BCM_TCS_CMD_VALID_SHFT',
                 'BCM_TCS_CMD_VOTE_X_SHFT', 'BCM_TCS_CMD_VOTE_MASK',
                 'BCM_TCS_CMD_VOTE_Y_SHFT', 'BCM_TCS_CMD'):
        parts.append(macro(tcs, name))
    parts += [function(source, 'setup_volt_dependency_tbl'), function(source, 'tcs_cmd_data')]
    parts.append(function(source, 'to_cx_hlvl'))
    for name in ('MAX_GX_LEVELS', 'MAX_CX_LEVELS', 'MAX_CNOC_LEVELS', 'MAX_CNOC_CMDS', 'MAX_BW_CMDS'):
        parts.append(macro(core, name))
    parts.append('#define __packed __attribute__((packed))')
    for name in ('hfi_bwtable_cmd', 'opp_gx_desc', 'opp_desc', 'hfi_dcvstable_cmd'):
        parts.append(re.search(r'struct ' + name + r' \{.*?\n\} __packed;', header, re.S)[0])
    parts.append(function(source, 'build_bw_table_cmd'))
    parts.append(re.search(r'static struct bcm gen7_ddr_bcms\[\] = \{.*?\n\};', source, re.S)[0])
    report = json.loads(args.report.read_text())
    if not report['passed'] or len(report['runs']) < 2:
        raise ValueError('two passed hardware runs required')
    parts.append('/* Exact selected command DB bytes from passed hardware evidence. */')
    for entry in report['runs'][0]['resources']:
        name = entry['name'].replace('.', '_').lower()
        data = bytes.fromhex(entry['aux'])
        parts.append('static const uint8_t measured_' + name + '[] = {' + ','.join(hex(v) for v in data) + '};')
        parts.append('static const uint32_t address_' + name + '=' + hex(entry['address']) + ';')
    parts.append('/* report SHA256: ' + hashlib.sha256(args.report.read_bytes()).hexdigest() + ' */')
    args.output.write_text('\n\n'.join(parts) + '\n')
    print(args.output)


if __name__ == '__main__':
    main()
