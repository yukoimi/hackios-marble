import gzip
import os
import importlib.util
from pathlib import Path
import struct
import tomllib
import unittest
import zlib
import lzma

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('marble_image', ROOT/'tools/marble-image.py')
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)

def disk_files(disk):
    base = struct.unpack_from('<I', disk, 454)[0]*512
    bps, spc, reserved, fats, roots = struct.unpack_from('<HBHBH', disk, base+11)
    spf = struct.unpack_from('<H', disk, base+22)[0]
    fat = disk[base+reserved*bps:base+(reserved+spf)*bps]
    root = base+(reserved+fats*spf)*bps
    data = root + m.align(roots*32,bps)
    def chain(c):
        out = b''; seen = set()
        while 2 <= c < 0xff8:
            if c in seen: raise ValueError('FAT cycle')
            seen.add(c)
            out += disk[data+(c-2)*bps:data+(c-1)*bps]
            v = int.from_bytes(fat[c*3//2:c*3//2+2], 'little')
            c = (v >> 4 if c&1 else v&0xfff)
        return out
    files = {}
    def walk(buf, prefix=''):
        for i in range(0,len(buf),32):
            e=buf[i:i+32]
            if not e or not e[0]: break
            if e[0] in (46,229) or e[11]==15: continue
            n=e[:8].decode().strip(); ext=e[8:11].decode().strip()
            name=prefix+n+('.'+ext if ext else '')
            c=struct.unpack_from('<H',e,26)[0]
            content=chain(c)
            if e[11]&16: walk(content,name+'/')
            else: files[name]=content[:struct.unpack_from('<I',e,28)[0]]
    walk(disk[root:root+roots*32])
    return files

class ImageTests(unittest.TestCase):
    def test_compact_root_preserves_disk_geometry_and_adds_six_clusters(self):
        # Extract the real immutable embedded FAT12 template, independently
        # read the rebuilt file chains, including a formerly oversized EFI.
        mu=Path(os.environ.get('MARBLE_MU_IMAGE','Mu-marble.img'))
        if not mu.is_file():self.skipTest('locally obtained Mu image unavailable')
        source=mu.read_bytes()
        ks=struct.unpack_from('<I',source,8)[0];raw=zlib.decompress(source[4096:4096+ks],31);fv=raw[0x70:]
        p=next(p for p,n,g in m.files_in_fv(fv,0) if fv[p+18]==11);sec=p+24
        n=int.from_bytes(fv[sec:sec+3],'little');off=struct.unpack_from('<H',fv,sec+20)[0]
        inner=lzma.decompress(fv[sec+off:sec+n]);marker=inner.index(b'UNIXNURAMDKFAT12');template=inner[marker-43-4096:marker-43-4096+0x20000]
        loader=bytes((i*13)&255 for i in range(122880))
        with self.assertRaises(ValueError):m.fat_disk(template,loader)
        disk=m.fat_disk(template,loader,True);self.assertEqual(len(disk),len(template));self.assertEqual(disk[:512],template[:512])
        base=struct.unpack_from('<I',disk,454)[0]*512
        self.assertEqual(struct.unpack_from('<H',disk,base+17)[0],16)
        self.assertEqual(disk[base:base+17],template[base:base+17]);self.assertEqual(disk[base+19:base+512],template[base+19:base+512])
        files=disk_files(disk);self.assertEqual(files,{'EFI/HACKIOS/LOADER.EFI':loader,'EFI/UNIXNU/UNIXNU~1.TOM':m.CONFIG})
        with self.assertRaises(ValueError):m.fat_disk(template,loader+bytes(512),True)
    def test_packaged_disks_and_firmware_checksums(self):
        if not (ROOT/'out/marble-userspace-boot.img').is_file() or not (ROOT/'out/loader-marble.efi').is_file():
            self.skipTest('locally rebuilt boot image and EFI unavailable')
        image=(ROOT/'out/marble-userspace-boot.img').read_bytes()
        loader=(ROOT/'out/loader-marble.efi').read_bytes()
        ks,rs=struct.unpack_from('<II',image,8)
        ro=m.align(4096+ks,4096)
        external=disk_files(image[ro:ro+rs])
        self.assertEqual(external['EFI/HACKIOS/LOADER.EFI'],loader)
        self.assertEqual(external['EFI/UNIXNU/UNIXNU~1.TOM'],m.CONFIG)
        self.assertEqual(tomllib.loads(m.CONFIG.decode())['entry'][0]['path'],r'\EFI\HACKIOS\LOADER.EFI')
        z=zlib.decompressobj(31); raw=z.decompress(image[4096:4096+ks])+z.flush()
        self.assertTrue(z.unused_data.startswith(b'\xd0\x0d\xfe\xed'))
        fv=raw[0x70:]
        cp=[(p,n) for p,n,_ in m.files_in_fv(fv,0) if fv[p+18]==11][0][0]
        s=cp+24; n=int.from_bytes(fv[s:s+3],'little'); off=struct.unpack_from('<H',fv,s+20)[0]
        inner=lzma.decompress(fv[s+off:s+n])
        for p,n,g in m.files_in_fv(inner,8):
            h=bytearray(inner[p:p+24]); h[17]=h[23]=0
            self.assertEqual(sum(h)&255,0)
            if inner[p+19]&64: self.assertEqual((sum(inner[p+24:p+n])+inner[p+17])&255,0)
            if g==m.CONFIG_GUID:
                marker=inner.index(b'UNIXNURAMDKFAT12',p,p+n)
                embedded=disk_files(inner[marker-43-4096:marker-43-4096+0x20000])
                self.assertEqual(embedded,external)
        self.assertEqual(len(image),ro+rs+4096)

if __name__=='__main__': unittest.main()
