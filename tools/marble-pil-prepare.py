"""Prepare a local RAM-only marble PILDxe with automatic image loading skipped.

The original OEM image stays untouched. This does not install or execute it.
"""
import argparse,hashlib,json,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
EXPECTED='1cac0e5c3e73c2dd756bcbe19344efa6ecea2e1f45e88fdc6255cc2e79fbff64'
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',type=Path,default=Path('out/pil-reuse-v1-plan/PILDxe.efi'))
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--header-only',action='store_true',help='replace the PAS-init stage callback with a fixed refusal; only native MDT file reading can run')
    a=p.parse_args();out=a.out.resolve()
    if not out.is_relative_to(ROOT/'out') or out.exists():p.error('new directory under out required')
    original=a.source.read_bytes()
    if len(original)!=118784 or hashlib.sha256(original).hexdigest()!=EXPECTED:raise ValueError('unqualified source PILDxe')
    pe=struct.unpack_from('<I',original,60)[0]
    if original[pe:pe+4]!=b'PE\0\0' or struct.unpack_from('<H',original,pe+4)[0]!=0xaa64:raise ValueError('ARM64 PE required')
    # Verified file .text RVA == raw offset. Skip from immediately after the
    # RETAIL configuration to protocol installation, before parsing AUTO.
    at,target=0x15d8,0x1640
    if struct.unpack_from('<I',original,at)[0]!=0xb0000080:raise ValueError('AUTO entry instruction differs')
    patched=bytearray(original);replacement=0x14000000|((target-at)//4)
    struct.pack_into('<I',patched,at,replacement)
    extra=[]
    if a.header_only:
        if struct.unpack_from('<Q',original,0x12dc0)[0]!=0x23d0:raise ValueError('native ELF PAS-init callback differs')
        reloc_rva,reloc_size=struct.unpack_from('<II',original,pe+24+112+5*8);found=False;pos=reloc_rva
        while pos<reloc_rva+reloc_size:
            page,size=struct.unpack_from('<II',original,pos)
            if size<8 or pos+size>reloc_rva+reloc_size:raise ValueError('invalid base-relocation block')
            for at_reloc in range(pos+8,pos+size,2):
                word=struct.unpack_from('<H',original,at_reloc)[0]
                if word>>12==10 and page+(word&4095)==0x12dc0:found=True
            pos+=size
        if not found:raise ValueError('PAS callback base relocation absent')
        # The AUTO block is now unreachable from entry. Use eight bytes of it
        # for `mov w0,#7; ret` and retain the callback's existing base relocation.
        # No authentication code is changed; the pipeline always aborts before it.
        for offset,value,fmt in [(0x15dc,0x528000e0,'<I'),(0x15e0,0xd65f03c0,'<I'),(0x12dc0,0x15dc,'<Q')]:
            extra.append(dict(offset=hex(offset),old=original[offset:offset+struct.calcsize(fmt)].hex(),value=hex(value)))
            struct.pack_into(fmt,patched,offset,value)
    changed=[i for i,(x,y) in enumerate(zip(original,patched)) if x!=y]
    if not changed or not all(at<=i<at+(12 if a.header_only else 4) or (a.header_only and 0x12dc0<=i<0x12dc8) for i in changed):raise ValueError('unexpected patch extent')
    fnv=0xcbf29ce484222325
    for b in patched:fnv=((fnv^b)*0x100000001b3)&0xffffffffffffffff
    out.mkdir();(out/'PILDxe-noauto.efi').write_bytes(patched)
    (out/'manifest.json').write_text(json.dumps(dict(source=str(a.source),source_sha256=EXPECTED,
        sha256=hashlib.sha256(patched).hexdigest(),bytes=len(patched),fnv1a64=hex(fnv),
        patch=dict(file_offset=hex(at),rva=hex(at),old=original[at:at+4].hex(),new=patched[at:at+4].hex(),branch_target=hex(target),extra=extra),
        scope='skip AUTO; PAS-init callback refuses before authentication, native header-read only' if a.header_only else 'skip AUTO parsing/loading; unchanged protocol implementation; RAM-only derivative, not executed',
        dsp_started=False,partition_writes=False),indent=2)+'\n')
    print((out/'manifest.json').read_text())
if __name__=='__main__':main()
