#!/usr/bin/env python3
"""Read a local ZIP/zstd raw-super ROM; extract selected LP images to host only.

Never runs flash scripts or any program from the archive, and never contacts a device.
Needs zstandard (installed in the workspace toolchain's private Python directory).
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
import zipfile

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'toolchains/python-libs'))
import zstandard


def checked(data, offset):
    return hashlib.sha256(data[:offset]+bytes(32)+data[offset+32:]).digest()==data[offset:offset+32]


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('rom',type=Path)
    p.add_argument('--out',type=Path,required=True)
    a=p.parse_args()
    a.out.mkdir(parents=True,exist_ok=True)
    report={'rom':str(a.rom.resolve()),'rom_bytes':a.rom.stat().st_size,'partitions':[],'files':{}}
    with zipfile.ZipFile(a.rom) as z, z.open('images/super.img.zst') as packed, zstandard.ZstdDecompressor().stream_reader(packed) as s:
        prefix=s.read(131072)
        magic,size=struct.unpack_from('<II',prefix,4096)
        if magic!=0x616c4467 or not 52<=size<=4096 or not checked(prefix[4096:4096+size],8):
            raise ValueError('invalid raw-super geometry')
        h=prefix[12288:]
        magic,major,minor,hs=struct.unpack_from('<IHHI',h)
        ts=struct.unpack_from('<I',h,44)[0]
        if magic!=0x414c5030 or major!=10 or not 128<=hs<=4096 or hs+ts>len(h) or not checked(h[:hs],12):
            raise ValueError('invalid LP header')
        tables=h[hs:hs+ts]
        if hashlib.sha256(tables).digest()!=h[48:80]: raise ValueError('invalid LP tables checksum')
        po,pn,pe=struct.unpack_from('<III',h,80)
        eo,en,ee=struct.unpack_from('<III',h,92)
        if pe<52 or ee<24 or po+pn*pe>ts or eo+en*ee>ts: raise ValueError('invalid LP descriptors')
        selected=[]
        for i in range(pn):
            raw,attrs,first,count,group=struct.unpack_from('<36sIIII',tables,po+i*pe)
            name=raw.split(b'\0')[0].decode('ascii')
            if not re.fullmatch(r'[a-zA-Z0-9_]+',name) or first+count>en: raise ValueError('invalid LP partition')
            extents=[struct.unpack_from('<QIQI',tables,eo+j*ee) for j in range(first,first+count)]
            length=sum(x[0]*512 for x in extents)
            report['partitions'].append({'name':name,'bytes':length,'extents':extents})
            print(name,length,flush=True)
            if name not in ('vendor_a','vendor_dlkm_a','odm_a','system_dlkm_a') or not length: continue
            logical=0
            for sectors,kind,target,source in extents:
                if kind!=0 or source!=0: raise ValueError('selected partition needs unsupported extent mapping')
                selected.append((target*512,target*512+sectors*512,name,logical))
                logical+=sectors*512
        selected.sort()
        for left,right in zip(selected,selected[1:]):
            if left[1]>right[0]: raise ValueError('overlapping selected extents')
        at=0; block=prefix; handles={}
        try:
            while block:
                end=at+len(block)
                for start,stop,name,logical in selected:
                    lo=max(start,at); hi=min(stop,end)
                    if lo>=hi: continue
                    if name not in handles: handles[name]=(a.out/(name+'.img')).open('wb')
                    f=handles[name]; f.seek(logical+lo-start); f.write(block[lo-at:hi-at])
                at=end
                block=s.read(4*1024*1024)
            if any(stop>at for start,stop,name,logical in selected): raise ValueError('truncated super data')
        finally:
            for f in handles.values(): f.close()
        report['super_uncompressed_bytes']=at
    for image in sorted(a.out.glob('*.img')):
        digest=hashlib.sha256()
        with image.open('rb') as f:
            for block in iter(lambda:f.read(4*1024*1024),b''): digest.update(block)
        report['files'][image.name]={'bytes':image.stat().st_size,'sha256':digest.hexdigest()}
    (a.out/'partitions.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
    print('Read-only partition extraction complete:',a.out,flush=True)


if __name__=='__main__': main()
