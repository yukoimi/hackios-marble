"""Build a bounded LZ4/SHA256 EFI memory-loader chain; no phone operations."""
import argparse,hashlib,json,shutil,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def build(efi,out,codec="lz4"):
 if codec not in ("lz4","lzma"):raise ValueError("unsupported explicit codec")
 efi=Path(efi).resolve();out=Path(out).resolve();data=efi.read_bytes()
 if not 512<=len(data)<=1048576 or data[:2]!=b'MZ':raise ValueError('bounded EFI input required')
 out.mkdir(exist_ok=False);llvm=ROOT.parent/'toolchains/llvm-mingw-20261020-ucrt-x86_64/bin'
 paths=[ROOT/p for p in ('tools/marble-efi-pack.py','tools/marble-lz4.c','tools/marble-lz4-efi.h','xnuloader/efi.h','xnuloader/marble_efi_chain.c','xnuloader/marble_efi_chain.h')]
 if codec=='lzma':paths += [ROOT/'xnuloader/marble_lzma_decode.h',ROOT/'tools/efi-libc/string.h',*list((ROOT/'xnuloader/vendor/lzma').glob('*'))]
 paths += [p for directory in ('xnuloader/vendor/lz4','xnuloader/vendor/sha256') for p in (ROOT/directory).iterdir() if p.is_file()]
 sources={p.relative_to(ROOT).as_posix():digest(p) for p in paths};commands=[]
 with (out/'build.log').open('w',encoding='utf8') as log:
  def run(args):
   args=list(map(str,args));commands.append(args);(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
   r=subprocess.run(args,cwd=ROOT,capture_output=True,text=True,errors='replace');log.write(r.stdout+r.stderr);log.flush()
   if r.returncode:raise RuntimeError(r.stdout+r.stderr)
  (out/'loader-payload.efi').write_bytes(data)
  run([llvm/'clang.exe','-O2','tools/marble-lz4.c','xnuloader/vendor/lz4/lz4.c','xnuloader/vendor/lz4/lz4hc.c','-o',out/'marble-lz4.exe'])
  run([out/'marble-lz4.exe','c',out/'loader-payload.efi',out/'loader-payload.lz4','1048576'])
  run([out/'marble-lz4.exe','d',out/'loader-payload.lz4',out/'loader-roundtrip.efi',str(len(data))])
  if (out/'loader-roundtrip.efi').read_bytes()!=data:raise ValueError('full EFI LZ4 roundtrip mismatch')
  block=(out/'loader-payload.lz4').read_bytes()
  if codec=='lzma':
   import lzma
   block=lzma.compress(data,format=lzma.FORMAT_ALONE,filters=[dict(id=lzma.FILTER_LZMA1,dict_size=1048576,lc=3,lp=0,pb=2,mode=lzma.MODE_NORMAL,nice_len=273,mf=lzma.MF_BT4)])
   if lzma.decompress(block,format=lzma.FORMAT_ALONE)!=data:raise ValueError('full EFI LZMA roundtrip mismatch')
   (out/'loader-payload.lzma').write_bytes(block)
  sha=hashlib.sha256(data).digest()
  header=f'#define MARBLE_EFI_RAW_BYTES {len(data)}u\n'
  header+='static const uint8_t marble_efi_sha256[]={'+','.join('0x%02x'%b for b in sha)+'};\n'
  header+='static const uint8_t marble_efi_block[]={\n'+'\n'.join(','.join('0x%02x'%b for b in block[i:i+24])+',' for i in range(0,len(block),24))+'\n};\n'
  (out/'marble_efi_payload.h').write_text(header,encoding='ascii')
  flags=['--target=aarch64-unknown-windows','-ffreestanding','-fno-builtin','-fno-stack-protector','-fshort-wchar','-mgeneral-regs-only','-Oz','-flto','-Wall','-Wextra']
  if codec=='lzma':flags.append('-DMARBLE_CHAIN_LZMA=1')
  run([llvm/'clang.exe',*flags,'-I'+str(out),'-c','xnuloader/marble_efi_chain.c','-o',out/'chain.obj'])
  if codec=='lzma':
   run([llvm/'clang.exe',*flags,'-Itools/efi-libc','-include','tools/marble-lz4-efi.h','-c','xnuloader/vendor/lzma/LzmaDec.c','-o',out/'lz4.obj'])
  else:
   run([llvm/'clang.exe',*flags,'-DLZ4_FREESTANDING=1','-DLZ4_FAST_DEC_LOOP=0','-DLZ4_memcpy=memcpy','-DLZ4_memmove=memmove','-DLZ4_memset=memset','-include','tools/marble-lz4-efi.h','-c','xnuloader/vendor/lz4/lz4.c','-o',out/'lz4.obj'])
  run([llvm/'clang.exe',*flags,'-DMARBLE_SHA256_FREESTANDING=1','-include','tools/marble-lz4-efi.h','-c','xnuloader/vendor/sha256/sha256.c','-o',out/'sha256.obj'])
  run([llvm/'ld.lld.exe','-flavor','link','/subsystem:efi_application','/entry:efi_main','/nodefaultlib','/base:0xA0000000',f'/out:{out}/loader-chain.efi',out/'chain.obj',out/'lz4.obj',out/'sha256.obj'])
 if efi.read_bytes()!=data or any(digest(ROOT/name)!=sha for name,sha in sources.items()):raise ValueError('input changed while building EFI chain')
 for name in sources:
  dest=out/'source'/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(ROOT/name,dest)
 (out/'sources.json').write_text(json.dumps(sources,indent=2)+'\n')
 result=dict(codec=codec,status='OFFLINE_BUILT_NOT_HARDWARE_QUALIFIED',target_bytes=len(data),target_sha256=hashlib.sha256(data).hexdigest(),compressed_bytes=len(block),wrapper_bytes=(out/'loader-chain.efi').stat().st_size,wrapper_sha256=digest(out/'loader-chain.efi'),runtime_checks=[codec.upper()+' exact output size','SHA256 complete target','bounded AArch64 EFI PE headers','UEFI LoadImage SourceBuffer','UEFI StartImage'],scope='no direct decoded-code jumps, block protocols, partition changes or phone operations')
 (out/'manifest.json').write_text(json.dumps(result,indent=2)+'\n');return result
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--efi',required=True,type=Path);p.add_argument('--out',required=True,type=Path);p.add_argument('--codec',choices=('lz4','lzma'),default='lz4');a=p.parse_args();print(json.dumps(build(a.efi,a.out,a.codec),indent=2))
if __name__=='__main__':main()
