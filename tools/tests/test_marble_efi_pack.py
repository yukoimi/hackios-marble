import importlib.util,subprocess,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('efi_pack',ROOT/'tools/marble-efi-pack.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class Chain(unittest.TestCase):
 def test_real_decoder_sha_pe_and_firmware_abi(self):
  self.exercise("lz4")
 def test_real_lzma_decoder_sha_pe_and_firmware_abi(self):
  self.exercise("lzma")
 def exercise(self,codec):
  with tempfile.TemporaryDirectory() as tmp:
   base=Path(tmp);out=base/'pack';source=ROOT/'out/daily-adsp-pcm-data-v146-plan/loader-marble.efi';r=m.build(source,out,codec)
   self.assertLess(r['wrapper_bytes'],119808);self.assertEqual((out/'loader-payload.efi').read_bytes(),source.read_bytes())
   program='''#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "'''+(ROOT/'xnuloader/marble_efi_chain.c').as_posix()+'''"
static _Alignas(4096) uint8_t memory[1048576],expected[1048576],scratch[1048576];
static unsigned allocated,freed,loads,starts,unloads,watchdog,fail_alloc,fail_load,fail_start,security_child,fail_unload,fail_watchdog;
static efi_status allocate(int type,int mem,uint64_t pages,uint64_t *address){assert(type==AllocateAnyPages && mem==EfiLoaderData && pages==(MARBLE_EFI_RAW_BYTES+4095)/4096);allocated++;if(fail_alloc)return EFI_ERROR_BIT|9;*address=(uint64_t)(uintptr_t)memory;return 0;}
static efi_status release(uint64_t address,uint64_t pages){assert(address==(uint64_t)(uintptr_t)memory && pages==(MARBLE_EFI_RAW_BYTES+4095)/4096);freed++;return 0;}
static efi_status timer(uint64_t sec,uint64_t code,uint64_t size,char16 *data){assert(sec==30 && !code && !size && !data);watchdog=30;return fail_watchdog?EFI_ERROR_BIT|7:0;}
static efi_status load(uint8_t policy,efi_handle parent,void *path,void *raw,uint64_t n,efi_handle *child){assert(!policy && parent==(void *)0x1234 && !path && raw==memory && n==MARBLE_EFI_RAW_BYTES && !memcmp(raw,expected,n));loads++;*child=(!fail_load || security_child)?(void *)0x5678:0;return fail_load?EFI_ERROR_BIT|26:0;}
static efi_status start(efi_handle child,uint64_t *bytes,char16 **data){assert(child==(void *)0x5678 && bytes && !*bytes && !data && watchdog==30);starts++;watchdog=0;return fail_start?EFI_ERROR_BIT|7:0;}
static efi_status unload(efi_handle child){assert(child==(void *)0x5678);unloads++;return fail_unload?EFI_ERROR_BIT|7:0;}
static void reset(void){allocated=freed=loads=starts=unloads=watchdog=fail_alloc=fail_load=fail_start=security_child=fail_unload=fail_watchdog=0;}
int main(int argc,char **argv){
assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f && fread(expected,1,MARBLE_EFI_RAW_BYTES,f)==MARBLE_EFI_RAW_BYTES);fclose(f);
assert(marble_chain_pe(expected,MARBLE_EFI_RAW_BYTES));
assert(marble_chain_unpack(marble_efi_block,sizeof(marble_efi_block),scratch,MARBLE_EFI_RAW_BYTES,marble_efi_sha256) && !memcmp(expected,scratch,MARBLE_EFI_RAW_BYTES));
#ifdef MARBLE_CHAIN_LZMA
static uint8_t corrupt[1048576];
memcpy(corrupt,marble_efi_block,sizeof(marble_efi_block));corrupt[0]^=1;
assert(!marble_lzma_decode(corrupt,sizeof(marble_efi_block),scratch,MARBLE_EFI_RAW_BYTES));
memcpy(corrupt,marble_efi_block,sizeof(marble_efi_block));corrupt[3]=0x20;
assert(!marble_lzma_decode(corrupt,sizeof(marble_efi_block),scratch,MARBLE_EFI_RAW_BYTES));
memcpy(corrupt,marble_efi_block,sizeof(marble_efi_block));memset(corrupt+5,0,8);
assert(!marble_lzma_decode(corrupt,sizeof(marble_efi_block),scratch,MARBLE_EFI_RAW_BYTES));
memcpy(corrupt,marble_efi_block,sizeof(marble_efi_block));corrupt[sizeof(marble_efi_block)]=0;
assert(!marble_lzma_decode(corrupt,sizeof(marble_efi_block)+1,scratch,MARBLE_EFI_RAW_BYTES));
assert(!marble_lzma_decode(marble_efi_block,sizeof(marble_efi_block),scratch,511));
marble_lzma_allocated=1;
assert(!marble_lzma_decode(marble_efi_block,sizeof(marble_efi_block),scratch,MARBLE_EFI_RAW_BYTES));
marble_lzma_allocated=0;
assert(!marble_lzma_allocate(0,sizeof(marble_lzma_probabilities)+1));
assert(marble_lzma_decode(marble_efi_block,sizeof(marble_efi_block),scratch,MARBLE_EFI_RAW_BYTES));
#endif
uint8_t badhash[32];memcpy(badhash,marble_efi_sha256,32);badhash[31]^=1;
assert(!marble_chain_unpack(marble_efi_block,sizeof(marble_efi_block),scratch,MARBLE_EFI_RAW_BYTES,badhash));
assert(!marble_chain_unpack(marble_efi_block,sizeof(marble_efi_block)-1,scratch,MARBLE_EFI_RAW_BYTES,marble_efi_sha256));
assert(!marble_chain_unpack(marble_efi_block,sizeof(marble_efi_block),scratch,1048577,marble_efi_sha256));
memcpy(scratch,expected,MARBLE_EFI_RAW_BYTES);scratch[0]=0;assert(!marble_chain_pe(scratch,MARBLE_EFI_RAW_BYTES));
memcpy(scratch,expected,MARBLE_EFI_RAW_BYTES);memset(scratch+60,255,4);assert(!marble_chain_pe(scratch,MARBLE_EFI_RAW_BYTES));
unsigned pe=marble_chain_u32(expected+60);memcpy(scratch,expected,MARBLE_EFI_RAW_BYTES);scratch[pe+4]=0;assert(!marble_chain_pe(scratch,MARBLE_EFI_RAW_BYTES));
memcpy(scratch,expected,MARBLE_EFI_RAW_BYTES);scratch[pe+24+68]=0;assert(!marble_chain_pe(scratch,MARBLE_EFI_RAW_BYTES));
efi_boot_services bs={0};bs.hdr.signature=0x56524553544f4f42ULL;bs.hdr.header_size=sizeof(bs);bs.allocate_pages=allocate;bs.free_pages=release;bs.set_watchdog_timer=timer;bs.load_image=(void *)load;bs.start_image=(void *)start;bs.unload_image=(void *)unload;
efi_system_table st={0};st.hdr.signature=0x5453595320494249ULL;st.bs=&bs;
reset();assert(!efi_main((void *)0x1234,&st) && allocated==1 && freed==1 && loads==1 && starts==1 && !unloads && !watchdog);
reset();fail_alloc=1;assert(EFI_ERROR(efi_main((void *)0x1234,&st)) && allocated==1 && !freed && !loads && watchdog==30);
reset();fail_load=1;assert(EFI_ERROR(efi_main((void *)0x1234,&st)) && freed==1 && loads==1 && !starts && watchdog==30);
reset();fail_load=security_child=1;assert(EFI_ERROR(efi_main((void *)0x1234,&st)) && freed==1 && unloads==1 && !starts && watchdog==30);
reset();fail_load=security_child=fail_unload=1;assert(EFI_ERROR(efi_main((void *)0x1234,&st)) && !freed && unloads==1 && !starts && watchdog==30);
reset();fail_start=1;assert(EFI_ERROR(efi_main((void *)0x1234,&st)) && starts==1 && freed==1);
reset();fail_watchdog=1;assert(EFI_ERROR(efi_main((void *)0x1234,&st)) && !allocated && !loads);
reset();st.hdr.signature=0;assert(EFI_ERROR(efi_main((void *)0x1234,&st)) && !allocated);
puts("PASS actual full EFI chain: LZ4/SHA/PE, exact LoadImage/StartImage ABI/source, watchdog, failure cleanup and security denial never starts");return 0;
}'''
   c=base/'chain-test.c';c.write_text(program);exe=base/'chain-test.exe';cc=ROOT.parent/'toolchains/llvm-mingw-20261020-ucrt-x86_64/bin/clang.exe'
   args=[str(cc),'-O2','-ffreestanding','-fno-builtin','-DMARBLE_SHA256_FREESTANDING=1','-DLZ4_FREESTANDING=1','-DLZ4_FAST_DEC_LOOP=0','-DLZ4_memcpy=memcpy','-DLZ4_memmove=memmove','-DLZ4_memset=memset','-include',str(ROOT/'tools/marble-lz4-efi.h'),'-I'+str(out),str(c),str(ROOT/'xnuloader/vendor/lz4/lz4.c'),str(ROOT/'xnuloader/vendor/sha256/sha256.c'),'-o',str(exe)]
   if codec=='lzma':
    args[args.index(str(ROOT/'xnuloader/vendor/lz4/lz4.c'))]=str(ROOT/'xnuloader/vendor/lzma/LzmaDec.c');args.append('-DMARBLE_CHAIN_LZMA=1')
   r=subprocess.run(args,capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
   r=subprocess.run([str(exe),str(source)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
