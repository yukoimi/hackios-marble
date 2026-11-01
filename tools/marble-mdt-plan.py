"""Validate split Qualcomm DSP firmware and make a relocation plan, offline only.

Layout follows qcom_mdt_load/read_metadata from the pinned marble Linux source.
This tool never maps device memory, invokes PAS, or communicates with a phone.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

TYPE_MASK = 7 << 24
TYPE_HASH = 2 << 24
RELOCATABLE = 1 << 27
U32 = 1 << 32
PROFILES = {'adsp2': (0x9fd00000, 0x3100000), 'slpi2': (0x88000000, 0x1900000)}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def plan(path, base, size):
    path = Path(path)
    raw = path.read_bytes()
    if len(raw) < 52:
        raise ValueError('truncated ELF header')
    e = struct.unpack_from('<16sHHIIIIIHHHHHH', raw)
    if e[0][:7] != b'\x7fELF\x01\x01\x01' or e[1:4] != (2, 164, 1):
        raise ValueError('expected little-endian ELF32 Hexagon executable')
    phoff, ehsize, phsize, phnum = e[5], e[8], e[9], e[10]
    if ehsize != 52 or phsize != 32 or phoff != 52 or not 2 <= phnum <= 128:
        raise ValueError('unsupported ELF program-header layout')
    if phoff + phnum * phsize > len(raw):
        raise ValueError('truncated program headers')
    if base < 0 or size <= 0 or base + size > U32 or base % 4096 or size % 4096:
        raise ValueError('invalid destination carveout')
    ph = [struct.unpack_from('<8I', raw, phoff + i * phsize) for i in range(phnum)]
    if ph[0][0] == 1 or ph[0][4] < phoff + phnum * phsize or ph[0][4] > len(raw):
        raise ValueError('invalid metadata ELF segment')
    hashes = [i for i, p in enumerate(ph) if i and p[0] != 1 and p[6] & TYPE_MASK == TYPE_HASH]
    if len(hashes) != 1:
        raise ValueError('expected one non-load hash segment')
    loads = [(i, p) for i, p in enumerate(ph) if p[0] == 1 and p[6] & TYPE_MASK != TYPE_HASH and p[5]]
    if not loads:
        raise ValueError('no loadable memory')
    reloc = any(p[6] & RELOCATABLE for _, p in loads)
    # Mixed relocation flags leave the origin ambiguous. These ROM images
    # consistently mark every load segment, so reject mixed images explicitly.
    if reloc and not all(p[6] & RELOCATABLE for _, p in loads):
        raise ValueError('mixed relocation flags')
    origin = min(p[3] for _, p in loads) if reloc else base
    regions, files = [], {path.name: dict(bytes=len(raw), sha256=digest(raw))}
    for i, p in loads:
        _, offset, va, pa, file_size, mem_size, flags, align = p
        if file_size > mem_size or pa + mem_size > U32:
            raise ValueError(f'segment {i}: overflowing memory extent')
        start = pa - origin
        if start < 0 or start + mem_size > size:
            raise ValueError(f'segment {i}: outside destination carveout')
        if align and (align & (align - 1) or (va - offset) % align):
            raise ValueError(f'segment {i}: invalid ELF alignment')
        if any(start < r['offset'] + r['memory_bytes'] and r['offset'] < start + mem_size for r in regions):
            raise ValueError(f'segment {i}: overlapping load ranges')
        name = path.with_suffix(f'.b{i:02d}')
        if file_size:
            content = name.read_bytes()
            if len(content) != file_size:
                raise ValueError(f'segment {i}: split file length differs')
            files[name.name] = dict(bytes=len(content), sha256=digest(content))
        regions.append(dict(index=i, file=name.name if file_size else None,
                            offset=start, physical=base + start, source_physical=pa,
                            file_bytes=file_size, memory_bytes=mem_size,
                            zero_bytes=mem_size - file_size, flags=flags))
    hi = hashes[0]
    hash_path = path.with_suffix(f'.b{hi:02d}')
    hash_data = hash_path.read_bytes()
    if len(hash_data) != ph[hi][4] or not hash_data:
        raise ValueError('split hash segment length differs')
    files[hash_path.name] = dict(bytes=len(hash_data), sha256=digest(hash_data))
    metadata = raw[:ph[0][4]] + hash_data
    # The MDT may itself embed the hash, as both selected ROM images do.
    # If present, it must be identical to the split authentication metadata.
    if len(raw) != len(metadata) or raw != metadata:
        raise ValueError('MDT does not match packed header plus split hash metadata')
    return dict(schema='marble-mdt-plan-v1', firmware=path.name,
                carveout_physical=base, carveout_bytes=size,
                source_origin=origin, relocatable=reloc,
                used_bytes=max(r['offset'] + r['memory_bytes'] for r in regions),
                load_segments=regions, files=files,
                authentication_metadata=dict(bytes=len(metadata), sha256=digest(metadata), hash_index=hi),
                offline_validation=True, hardware_load=False, authenticated=False,
                limits=['PAS authentication not performed', 'no device memory writes',
                        'power/clock/IPCC/SMEM/GLINK lifecycle still required'])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('firmware', type=Path)
    p.add_argument('--profile', choices=PROFILES, required=True)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    if a.firmware.stem != a.profile:
        p.error('profile must match the firmware stem')
    data = plan(a.firmware, *PROFILES[a.profile])
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_text(json.dumps(data, indent=2) + '\n', encoding='utf8')
    print(f"{a.profile}: {len(data['load_segments'])} segments, {data['used_bytes']}/{data['carveout_bytes']} bytes; offline only")


if __name__ == '__main__':
    main()
