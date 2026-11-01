"""Read-only ARM64 PE/COFF disassembly for pinned firmware driver audits."""
import argparse,struct,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'out/host-python'))
from capstone import Cs,CS_ARCH_ARM64,CS_MODE_ARM
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('file',type=Path);p.add_argument('rva',type=lambda x:int(x,0));p.add_argument('--size',type=lambda x:int(x,0),default=0x400);a=p.parse_args()
    d=a.file.read_bytes();pe=struct.unpack_from('<I',d,60)[0]
    if d[pe:pe+4]!=b'PE\0\0' or struct.unpack_from('<H',d,pe+4)[0]!=0xaa64:raise ValueError('not ARM64 PE')
    count=struct.unpack_from('<H',d,pe+6)[0];opt=pe+24;sections=opt+struct.unpack_from('<H',d,pe+20)[0]
    if struct.unpack_from('<H',d,opt)[0]!=0x20b:raise ValueError('not PE32+')
    print('entry=',hex(struct.unpack_from('<I',d,opt+16)[0]),'base=',hex(struct.unpack_from('<Q',d,opt+24)[0]))
    for i in range(count):
        at=sections+i*40;vs,va,size,offset=struct.unpack_from('<IIII',d,at+8)
        if va<=a.rva<va+min(vs,size):
            start=offset+a.rva-va;end=min(start+a.size,offset+size)
            if end>len(d):raise ValueError('truncated section')
            for x in Cs(CS_ARCH_ARM64,CS_MODE_ARM).disasm(d[start:end],a.rva):print(f'{x.address:#x}: {x.mnemonic:9} {x.op_str}')
            return
    raise ValueError('RVA absent from initialized sections')
if __name__=='__main__':main()
