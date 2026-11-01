#!/usr/bin/env python3
"""Extract pinned Xiaomi queue initialization for an independent userspace oracle.

Generated GPL source is test-only, outside the kernel; no hardware accesses.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sources', type=Path, default=Path('../upstream-marble/gpu'))
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    records = json.loads((a.sources / 'sources.json').read_text())
    if records['commit'] != '48952ed36228217531482b39d5bef13e7fd808ec':
        raise ValueError('unexpected Xiaomi source pin')
    inputs = {}
    for name in ('adreno_hfi.h', 'adreno_gen7_hfi.c'):
        raw = (a.sources / name).read_bytes()
        wanted = next(x['sha256'] for x in records['files'] if x['path'] == 'drivers/gpu/msm/' + name)
        if hashlib.sha256(raw).hexdigest() != wanted:
            raise ValueError('source differs: ' + name)
        inputs[name] = raw.decode()
    header, source = inputs['adreno_hfi.h'], inputs['adreno_gen7_hfi.c']
    parts = ['/* SPDX-License-Identifier: GPL-2.0-only */',
             '/* Test-only extraction from pinned Xiaomi 48952ed3; original init_queues body. */',
             '#include <stdint.h>\n#include <string.h>\ntypedef uint32_t u32;',
             '#define __packed __attribute__((packed))\n#define SZ_4K 4096\n#define SZ_16 16',
             '#define ALIGN(x,a) (((x)+(a)-1)&~((a)-1))']
    names = ('HFI_QUEUE_SIZE', 'HFI_CMD_ID', 'HFI_MSG_ID', 'HFI_DBG_ID', 'HFI_CMD_PRI',
             'HFI_MSG_PRI', 'HFI_DBG_PRI', 'HFI_QUEUE_STATUS_ENABLED', 'HFI_QUEUE_DEFAULT_CNT',
             'HFI_QUEUE_DISPATCH_MAX_CNT', 'HFI_QUEUE_HDR_MAX', 'QUEUE_HDR_TYPE',
             'HFI_QUEUE_OFFSET', 'GMU_QUEUE_START_ADDR')
    for name in names:
        match = re.search(r'^#define ' + name + r'\b[^\n]*(?:\\\n[^\n]*)*', header, re.M)
        if not match:
            raise ValueError('missing macro ' + name)
        # Read logical lines explicitly, preserving actual source continuations.
        start = match.start()
        lines = header[start:].splitlines(keepends=True)
        macro = ''
        for line in lines:
            macro += line
            if not line.rstrip().endswith('\\'):
                break
        parts.append(macro)
    for name in ('hfi_queue_table_header', 'hfi_queue_header', 'hfi_queue_table'):
        match = re.search(r'struct ' + name + r'\s*\{.*?\}\s*__packed;', header, re.S)
        if not match:
            raise ValueError('missing struct ' + name)
        parts.append(match.group())
    parts.append('''
#define HFI_QUEUE_MAX HFI_QUEUE_DEFAULT_CNT
struct kgsl_memdesc { void *hostptr; u32 gmuaddr; };
struct gen7_hfi { struct kgsl_memdesc *hfi_mem; };
struct gen7_gmu_device { struct gen7_hfi hfi; };
struct adreno_device { struct gen7_gmu_device *gmu; };
static struct gen7_gmu_device *to_gen7_gmu(struct adreno_device *d) { return d->gmu; }
''')
    start = source.index('static void init_queues(')
    body = source.index('{', start)
    depth = 1
    end = body + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    parts.append(source[start:end])
    parts.append('''
void marble_source_initialize(uint32_t *memory,uint32_t iova)
{
    struct kgsl_memdesc md={memory,iova};
    struct gen7_gmu_device gmu={{&md}};
    struct adreno_device device={&gmu};
    memset(memory,0,16384);
    init_queues(&device);
}
''')
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text('\n'.join(parts))
    print(a.output)


if __name__ == '__main__':
    main()
