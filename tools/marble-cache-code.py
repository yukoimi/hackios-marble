"""Read-only ARM64 disassembly at an unslid address in the local iOS cache."""
import argparse, mmap, struct, sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'out/host-python'))
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM
class Cache:
    def __init__(self):
        self.maps=[];self.handles=[]
        for f in sorted((ROOT/'ios165/cryptex/System/Library/Caches/com.apple.dyld').glob('dyld_shared_cache_arm64*')):
            if f.suffix=='.symbols':continue
            h=f.open('rb');m=mmap.mmap(h.fileno(),0,access=mmap.ACCESS_READ);self.handles.append((h,m))
            off,count=struct.unpack_from('<II',m,16)
            for i in range(count):
                addr,size,fo,_,_=struct.unpack_from('<QQQII',m,off+i*32)
                self.maps.append((addr,size,fo,m))
    def read(self,address,size):
        for addr,length,off,m in self.maps:
            if addr<=address<addr+length:return m[off+address-addr:off+address-addr+size]
        raise ValueError(hex(address))
    def pointer(self,address):
        # This arm64 (non-arm64e) cache encodes slide-chain deltas above bit 39.
        return struct.unpack('<Q',self.read(address,8))[0]&((1<<40)-1)
    def string(self,address):return self.read(address,1024).split(b'\0')[0].decode(errors='replace')
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('address',type=lambda x:int(x,0));p.add_argument('--size',type=lambda x:int(x,0),default=1024)
    p.add_argument('--pointer-string',action='store_true')
    a=p.parse_args()
    if a.pointer_string:
        c=Cache();ptr=c.pointer(a.address);print(hex(ptr),c.string(ptr));return
    for f in sorted((ROOT/'ios165/cryptex/System/Library/Caches/com.apple.dyld').glob('dyld_shared_cache_arm64*')):
        if f.suffix=='.symbols':continue
        with f.open('rb') as h:
            with mmap.mmap(h.fileno(),0,access=mmap.ACCESS_READ) as m:
                off,count=struct.unpack_from('<II',m,16)
                for i in range(count):
                    addr,size,fo,_,_=struct.unpack_from('<QQQII',m,off+i*32)
                    if addr<=a.address<addr+size:
                        pos=fo+a.address-addr;data=m[pos:pos+min(a.size,addr+size-a.address)]
                        print(f.name,hex(pos))
                        for ins in Cs(CS_ARCH_ARM64,CS_MODE_ARM).disasm(data,a.address):
                            print(f'{ins.address:#x}: {ins.mnemonic:9} {ins.op_str}')
                        return
    raise SystemExit('address absent')
if __name__=='__main__':main()
