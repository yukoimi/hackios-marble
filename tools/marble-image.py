#!/usr/bin/env python3
"""Repack the supplied marble firmware with a RAM-only HackiOS EFI entry.

No flash/ADB operations. Only the known input image is accepted. The original
BootShim, DTB and all firmware files except ConfigDiskDxe remain identical.
"""
import argparse
import gzip
import hashlib
import json
import lzma
from pathlib import Path
import struct
import subprocess
import tempfile
import uuid
import zlib

SOURCE_SHA256 = '77733522406161dd0c621b204d34e2b42b377f71e6474dfe4a928e3eefc5c328'
CONFIG_GUID = uuid.UUID('5b2e8c41-9d3a-4e77-a1c6-8f2d4b7e9013').bytes_le
LZMA_GUID = uuid.UUID('ee4e5898-3914-4259-9d6e-dc7bd79403cf').bytes_le

CONFIG = b'''version = 1
[boot]
timeout = 3
default = "hackios"
rotate = 2
[ui]
title = "HackiOS marble - RAM only"
[[entry]]
id = "hackios"
title = "HackiOS USB loader (RAM only)"
type = "efi"
path = "\\\\EFI\\\\HACKIOS\\\\LOADER.EFI"
[[entry]]
id = "exit"
title = "Return to firmware"
type = "action"
action = "exit"
[[entry]]
id = "restart"
title = "Restart device"
type = "action"
action = "reset"
[[entry]]
id = "shutdown"
title = "Power off"
type = "action"
action = "shutdown"
'''


def align(n, size):
    return (n + size - 1) & ~(size - 1)


def file_checksum(blob):
    """Recalculate EFI FFS normal-size file checksums; preserve its state."""
    blob = bytearray(blob)
    assert int.from_bytes(blob[20:23], 'little') == len(blob)
    blob[17] = (-sum(blob[24:])) & 255 if blob[19] & 0x40 else 0xaa
    blob[16] = 0
    header = bytearray(blob[:24]); header[17] = header[23] = 0
    blob[16] = (-sum(header)) & 255
    return bytes(blob)


def files_in_fv(data, base):
    assert data[base + 40:base + 44] == b'_FVH'
    size = struct.unpack_from('<Q', data, base + 32)[0]
    header = struct.unpack_from('<H', data, base + 48)[0]
    assert sum(struct.unpack_from('<' + str(header // 2) + 'H', data, base)) & 0xffff == 0
    pos = align(base + header, 8)
    while pos + 24 <= base + size:
        h = data[pos:pos + 24]
        if h in (b'\xff' * 24, b'\0' * 24):
            break
        n = int.from_bytes(h[20:23], 'little')
        assert 24 <= n < 0xffffff and pos + n <= base + size
        yield pos, n, h[:16]
        pos = align(pos + n, 8)


def fat_disk(template, loader, compact_root=False):
    """Rebuild the existing small MBR/FAT12 disk, including the TOML long name."""
    disk = bytearray(len(template))
    disk[:512] = template[:512]
    start, sectors = struct.unpack_from('<II', disk, 454)
    base = start * 512
    boot = bytearray(template[base:base + 512])
    bps, spc, reserved, fats, roots = struct.unpack_from('<HBHBH', boot, 11)
    spf = struct.unpack_from('<H', boot, 22)[0]
    assert bps == 512 and spc == 1 and fats == 2 and spf > 0
    assert base + sectors * 512 == len(disk)
    if compact_root:
        # One EFI directory is the only root entry. Six unused root sectors
        # in the embedded disk become data without changing MBR/disk length,
        # partition start/length, FAT count or FAT size. Standard FAT12 BPB.
        assert roots>=16
        roots=16;struct.pack_into('<H',boot,17,roots)
    disk[base:base + 512] = boot
    root = base + (reserved + fats * spf) * bps
    data = root + align(roots * 32, bps)
    limit = (len(disk) - data) // bps + 2
    assert limit<0xff0 and (limit*3+1)//2<=spf*bps
    fat = bytearray(spf * bps); fat[:3] = bytes((boot[21], 255, 255))
    next_cluster = 2

    def set_fat(c, value):
        p = c * 3 // 2
        old = int.from_bytes(fat[p:p + 2], 'little')
        val = ((old & 15) | value << 4) if c & 1 else ((old & 0xf000) | value)
        fat[p:p + 2] = val.to_bytes(2, 'little')

    def allocate(content):
        nonlocal next_cluster
        count = max(1, align(len(content), bps) // bps)
        first = next_cluster
        if first + count > limit:
            raise ValueError('EFI loader does not fit the embedded configuration disk')
        for c in range(first, first + count):
            set_fat(c, c + 1 if c + 1 < first + count else 0xfff)
        pos = data + (first - 2) * bps
        disk[pos:pos + len(content)] = content
        next_cluster += count
        return first

    def entry(alias, cluster, size=0, directory=False):
        e = bytearray(32); e[:11] = alias
        e[11] = 16 if directory else 32
        struct.pack_into('<H', e, 26, cluster)
        struct.pack_into('<I', e, 28, size)
        return bytes(e)

    def dot(name, cluster):
        return entry(name.ljust(11, b' '), cluster, directory=True)

    def lfn(name, alias):
        checksum = 0
        for c in alias:
            checksum = (((checksum & 1) << 7) + (checksum >> 1) + c) & 255
        chars = name.encode('utf-16le') + b'\0\0'
        chars += b'\xff\xff' * ((13 - len(chars) // 2 % 13) % 13)
        entries = []
        for i in range(len(chars) // 26, 0, -1):
            e = bytearray(32); e[0] = i | (0x40 if i == len(chars) // 26 else 0)
            e[11] = 15; e[13] = checksum
            chunk = chars[(i - 1) * 26:i * 26]
            e[1:11], e[14:26], e[28:32] = chunk[:10], chunk[10:22], chunk[22:]
            entries.append(bytes(e))
        return b''.join(entries)

    efi, unixnu, hackios = allocate(bytes(512)), allocate(bytes(512)), allocate(bytes(512))
    config_cluster, loader_cluster = allocate(CONFIG), allocate(loader)
    config_alias = b'UNIXNU~1TOM'
    dirs = {
        efi: dot(b'.', efi) + dot(b'..', 0) + entry(b'UNIXNU     ', unixnu, directory=True) + entry(b'HACKIOS    ', hackios, directory=True),
        unixnu: dot(b'.', unixnu) + dot(b'..', efi) + lfn('unixnu.toml', config_alias) + entry(config_alias, config_cluster, len(CONFIG)),
        hackios: dot(b'.', hackios) + dot(b'..', efi) + entry(b'LOADER  EFI', loader_cluster, len(loader)),
    }
    for cluster, content in dirs.items():
        pos = data + (cluster - 2) * 512
        disk[pos:pos + len(content)] = content
    disk[root:root + 32] = entry(b'EFI        ', efi, directory=True)
    for i in range(fats):
        pos = base + (reserved + i * spf) * bps
        disk[pos:pos + len(fat)] = fat
    return bytes(disk)


def repack(source, loader, lzma_encoder, compact_root=False):
    assert hashlib.sha256(source).hexdigest() == SOURCE_SHA256, 'unexpected source firmware'
    assert source[:8] == b'ANDROID!' and struct.unpack_from('<I', source, 40)[0] == 4
    assert loader[:2] == b'MZ'
    pe = struct.unpack_from('<I', loader, 60)[0]
    assert loader[pe:pe + 4] == b'PE\0\0'
    assert struct.unpack_from('<H', loader, pe + 4)[0] == 0xaa64
    assert struct.unpack_from('<H', loader, pe + 24 + 68)[0] == 10
    ks, rs = struct.unpack_from('<II', source, 8)
    ro = align(4096 + ks, 4096)
    z = zlib.decompressobj(31)
    raw = z.decompress(source[4096:4096 + ks]) + z.flush()
    dtb = z.unused_data
    assert raw[0x98:0x9c] == b'_FVH' and len(raw) == 0x300070
    shim, fv = raw[:0x70], bytearray(raw[0x70:])
    outer_files = list(files_in_fv(fv, 0))
    compressed = [(p, n) for p, n, _ in outer_files if fv[p + 18] == 0x0b]
    assert len(compressed) == 1
    pos, size = compressed[0]
    assert pos + size == max(p + n for p, n, _ in outer_files)
    section = pos + 24
    assert fv[section + 3] == 2 and fv[section + 4:section + 20] == LZMA_GUID
    section_size = int.from_bytes(fv[section:section + 3], 'little')
    data_offset = struct.unpack_from('<H', fv, section + 20)[0]
    inner = bytearray(lzma.decompress(fv[section + data_offset:section + section_size]))
    original_inner = bytes(inner)
    files = list(files_in_fv(inner, 8))
    matches = [(p, n) for p, n, guid in files if guid == CONFIG_GUID]
    assert len(matches) == 1
    cp, cn = matches[0]
    # The disk is embedded in the PE's data section. Infer its MBR from the FAT BPB.
    marker = inner.index(b'UNIXNURAMDKFAT12', cp, cp + cn)
    volume = marker - 43
    disk_start = volume - 4096
    embedded = bytes(inner[disk_start:disk_start + 0x20000])
    assert embedded[510:512] == b'\x55\xaa'
    assert struct.unpack_from('<II', embedded, 454) == (8, 248)
    inner[disk_start:disk_start + len(embedded)] = fat_disk(embedded, loader,compact_root)
    inner[cp:cp + cn] = file_checksum(inner[cp:cp + cn])
    for p, n, guid in files:
        if guid != CONFIG_GUID:
            assert inner[p:p + n] == original_inner[p:p + n], 'unrelated firmware file changed'
    with tempfile.TemporaryDirectory(prefix='marble-lzma-') as temp:
        src, dst = Path(temp)/'inner.bin', Path(temp)/'inner.lzma'
        src.write_bytes(inner)
        subprocess.run([str(lzma_encoder), str(src), str(dst)], check=True)
        packed = dst.read_bytes()
    assert struct.unpack_from('<Q', packed, 5)[0] == len(inner)
    assert lzma.decompress(packed) == inner
    sec_header = bytearray(fv[section:section + data_offset])
    sec_header[:3] = (len(sec_header) + len(packed)).to_bytes(3, 'little')
    contents = bytes(sec_header) + packed
    ffs = bytearray(fv[pos:pos + 24]) + contents
    ffs[20:23] = len(ffs).to_bytes(3, 'little')
    ffs = file_checksum(ffs)
    assert pos + len(ffs) <= len(fv)
    fv[pos:] = ffs + b'\xff' * (len(fv) - pos - len(ffs))
    kernel = gzip.compress(shim + fv, compresslevel=6, mtime=0) + dtb
    ramdisk = fat_disk(source[ro:ro + rs], loader,compact_root)
    header = bytearray(source[:4096])
    struct.pack_into('<I', header, 8, len(kernel))
    signature = source[ro + rs:]
    image = bytes(header) + kernel + bytes(align(len(kernel), 4096) - len(kernel)) + ramdisk + signature
    return image, dict(compact_fat_root=compact_root,source_sha256=SOURCE_SHA256, loader_sha256=hashlib.sha256(loader).hexdigest(),
                       output_sha256=hashlib.sha256(image).hexdigest(), size=len(image),
                       changed_firmware_file=str(uuid.UUID(bytes_le=CONFIG_GUID)),
                       embedded_disk_offset=disk_start, unchanged_firmware_files=len(files) - 1,
                       kernel='not embedded; uploaded through USB into allocated RAM', storage_policy='RAM only')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--source', required=True, type=Path)
    ap.add_argument('--loader', required=True, type=Path)
    ap.add_argument('--output', required=True, type=Path)
    ap.add_argument('--lzma-encoder', required=True, type=Path)
    ap.add_argument('--compact-fat-root',action='store_true',help='16-entry FAT12 root within the unchanged existing disk and partition extents')
    args = ap.parse_args()
    image, manifest = repack(args.source.read_bytes(), args.loader.read_bytes(), args.lzma_encoder.resolve(),args.compact_fat_root)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(image)
    args.output.with_suffix('.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf8')
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    main()
