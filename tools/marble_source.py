"""Identity checks for an explicit marble source-built kernel candidate.

The manifest records local build provenance, not a cryptographic signature.
The default prebuilt path continues to require its original fixed hash.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

XNU_BASE = 'c54f35ca767986246321eb901baf8f5ff7923f6a'
MARKER = b'MGIC-DT-STRIDE-ABI-v1\0'


def verify_kernel(kernel, profile):
    if profile.get('profile') != 'marble-source-v1' or profile.get('xnu_base') != XNU_BASE:
        raise ValueError('unknown source kernel profile/base')
    if hashlib.sha256(kernel).hexdigest() != profile.get('kernel_sha256'):
        raise ValueError('source kernel differs from build manifest')
    if kernel.count(MARKER) != 1:
        raise ValueError('source kernel must declare exactly one GIC DT stride ABI')
    if len(kernel) < 32 or struct.unpack_from('<4I', kernel) != (0xfeedfacf, 0x100000c, 0, 2):
        raise ValueError('source kernel is not an ARM64 Mach-O executable')
    count, size = struct.unpack_from('<2I', kernel, 16)
    pos = 32
    entry = None
    executable = []
    for _ in range(count):
        if pos + 8 > 32 + size or pos + 8 > len(kernel):
            raise ValueError('truncated source kernel commands')
        cmd, n = struct.unpack_from('<2I', kernel, pos)
        if n < 8 or pos + n > min(len(kernel), 32 + size):
            raise ValueError('invalid source kernel command')
        if cmd == 0x19:
            if n < 72:
                raise ValueError('truncated source kernel segment')
            va, vm, off, fs, _, prot = struct.unpack_from('<4Q2I', kernel, pos+24)
            if off + fs > len(kernel) or fs > vm:
                raise ValueError('source kernel segment exceeds image')
            if prot & 4:
                executable.append((va, va+vm))
        elif cmd == 5:
            if n < 280:
                raise ValueError('truncated source kernel entry')
            entry = struct.unpack_from('<Q', kernel, pos+16+32*8)[0]
        pos += n
    if pos != 32 + size or entry is None or not any(a <= entry < b for a, b in executable):
        raise ValueError('source kernel entry is not executable')
    return profile


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('kernel', type=Path)
    p.add_argument('--build-tree', type=Path, required=True)
    a = p.parse_args()
    root = Path(__file__).resolve().parents[1]
    base = subprocess.check_output(['git', '-C', str(a.build_tree), 'rev-parse', 'HEAD'], text=True).strip()
    gic = 'osfmk/arm64/gic_v3_irq.c'
    if (a.build_tree/gic).read_bytes() != (root/'xnu/files'/gic).read_bytes():
        raise RuntimeError('exported GIC source differs from the build tree')
    data = a.kernel.read_bytes()
    profile = {'profile': 'marble-source-v1', 'xnu_base': base,
               'kernel_sha256': hashlib.sha256(data).hexdigest(),
               'gic_source_sha256': hashlib.sha256((a.build_tree/gic).read_bytes()).hexdigest(),
               'kernel_file': a.kernel.name,
               'hardware_status': 'candidate; not hardware validated'}
    verify_kernel(data, profile)
    output = a.kernel.with_suffix('.json')
    output.write_text(json.dumps(profile, indent=2)+'\n')
    print(output)


if __name__ == '__main__':
    main()
