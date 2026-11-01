"""Reproducible, single-instruction GIC stride fix for the pinned XNU prebuilt.

This is a temporary substitute for rebuilding gic_v3_irq.c. The original stays
untouched. All other bytes, including the Mach-O layout, must remain identical.
"""
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
KERNEL_SHA = '9239d4adb7a2c433d7ed5729b47b63acc5281af73d8a81640ef3c678c8afd26c'
SCAN_VA = 0xfffffff0074748b0


def patched_kernel(source):
    if hashlib.sha256(source).hexdigest() != KERNEL_SHA:
        raise ValueError('SMP patch requires the exact pinned upstream prebuilt')
    count, size = struct.unpack_from('<II', source, 16)
    pos = 32
    sites = []
    for _ in range(count):
        cmd, n = struct.unpack_from('<II', source, pos)
        if n < 8 or pos+n > 32+size:
            raise ValueError('invalid Mach-O load commands')
        if cmd == 0x19:
            va, vm, off, fs = struct.unpack_from('<4Q', source, pos+24)
            if va <= SCAN_VA and SCAN_VA-va+16 <= fs:
                sites.append(off+SCAN_VA-va)
        pos += n
    if len(sites) != 1:
        raise ValueError('GIC scan must have exactly one file-backed location')
    offset = sites[0]
    context = struct.pack('<4I', 0x9140816c, 0xeb0a019f, 0x54000a68, 0xf940056e)
    if source[offset:offset+16] != context:
        raise ValueError('unexpected GIC scan instructions')
    result = bytearray(source)
    struct.pack_into('<I', result, offset, 0x9141016c)
    return bytes(result), offset


def write_kernel():
    source = (ROOT/'prebuilt/mach.development.bcm2837').read_bytes()
    data, offset = patched_kernel(source)
    output = ROOT/'out/mach.marble-smp'
    output.parent.mkdir(exist_ok=True)
    output.write_bytes(data)
    manifest = {'source_sha256': KERNEL_SHA, 'output_sha256': hashlib.sha256(data).hexdigest(),
                'patch_va': hex(SCAN_VA), 'patch_file_offset': offset,
                'old_instruction': '9140816c', 'new_instruction': '9141016c',
                'purpose': 'GIC redistributor scan and bounds step: 128 KiB -> 256 KiB'}
    output.with_suffix('.json').write_text(json.dumps(manifest, indent=2)+'\n')
    return output, data


if __name__ == '__main__':
    output, _ = write_kernel()
    print(output)
    print(output.with_suffix('.json').read_text())
