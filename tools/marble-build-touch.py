"""Build a static ARM64 Darwin input bridge using portable LLVM (no Apple SDK)."""
import argparse
from pathlib import Path
import subprocess
import struct
ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--llvm',type=Path,required=True)
    a=p.parse_args(); llvm=a.llvm.resolve(); out=ROOT/'out'
    flags=['--target=arm64-apple-ios16.5','-Os','-ffreestanding','-fno-builtin',
           '-fno-stack-protector','-Wall','-Wextra','-Iuserland/libmini']
    objects=[]
    for name,source in [('touch-bridge','tools/marble-touch-bridge.c'),('touch-mini','userland/libmini/mini.c'),('touch-start','userland/libmini/start.S')]:
        obj=out/(name+'.o'); objects.append(obj)
        subprocess.run([str(llvm/'clang.exe'),*flags,'-c',source,'-o',str(obj)],cwd=ROOT,check=True)
    linked=out/'marble-touch-bridge.linked'
    subprocess.run([str(llvm/'ld.lld.exe'),'-flavor','darwin','-arch','arm64',
                    '-no_fixup_chains','-no_adhoc_codesign','-headerpad','0x1000',
                    '-e','_start','-pagezero_size','0x100000000','-platform_version','ios','16.5','16.5',
                    '-o',str(linked),*map(str,objects)],cwd=ROOT,check=True)
    # LLD does not implement Darwin -static. All references are resolved above,
    # without chained pointers. Produce the kernel's static UNIXTHREAD entry,
    # removing the dyld entry and MH_PIE; linked absolute pointers keep their VA.
    data=bytearray(linked.read_bytes()); header=list(struct.unpack_from('<8I',data))
    commands=[]; pos=32; text_base=entry=None
    for _ in range(header[4]):
        cmd,size=struct.unpack_from('<II',data,pos); raw=bytes(data[pos:pos+size]); pos+=size
        if cmd==0x19:
            commands.append(raw)
            if raw[8:24].rstrip(b'\0')==b'__TEXT': text_base=struct.unpack_from('<Q',raw,24)[0]
        elif cmd==0x80000028: entry=struct.unpack_from('<Q',raw,8)[0]
        elif cmd==2:
            symoff,nsyms,stroff,strsize=struct.unpack_from('<4I',raw,8)
            for i in range(nsyms):
                _,typ,sect,desc,value=struct.unpack_from('<IBBHQ',data,symoff+16*i)
                if typ&0xe==0 and typ&1: raise RuntimeError('unresolved external in static bridge')
            commands.append(raw)
        elif cmd in (0xc,0x18,0x8000001c): raise RuntimeError('unexpected library dependency')
    if text_base is None or entry is None: raise RuntimeError('missing linked entry')
    state=[0]*34; state[32]=text_base+entry
    commands.append(struct.pack('<4I34Q',5,288,6,68,*state))
    blob=b''.join(commands)
    header[4]=len(commands); header[5]=len(blob); header[6]=1
    data[:32]=struct.pack('<8I',*header)
    data[32:32+len(blob)]=blob
    if len(blob)<pos-32: data[32+len(blob):pos]=bytes(pos-32-len(blob))
    (out/'marble-touch-bridge').write_bytes(data)
    print(out/'marble-touch-bridge')
    # Optional process-local input scheduling shim. dyld resolves the public
    # system symbols at runtime; no SDK stub libraries or fixed cache addresses.
    subprocess.run([str(llvm/'clang.exe'),*flags,'-c','tools/marble-input-qos.c',
                    '-o',str(out/'marble-input-qos.o')],cwd=ROOT,check=True)
    subprocess.run([str(llvm/'ld.lld.exe'),'-flavor','darwin','-arch','arm64','-dylib',
                    '-undefined','dynamic_lookup','-no_fixup_chains','-no_adhoc_codesign',
                    '-install_name','/redwood/lib/marble-input-qos.dylib',
                    '-platform_version','ios','16.5','16.5',
                    '-o',str(out/'marble-input-qos.dylib'),str(out/'marble-input-qos.o')],cwd=ROOT,check=True)
if __name__=='__main__': main()
