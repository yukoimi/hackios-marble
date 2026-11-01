import importlib.util,json,os,shutil,subprocess,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
s=importlib.util.spec_from_file_location('format_pack',ROOT/'tools/marble-format-pack.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
class Formats(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.temp=tempfile.TemporaryDirectory();cls.base=Path(cls.temp.name);cls.codec=cls.base/'lz4.exe';local=ROOT.parent/'toolchains/llvm-mingw-20261020-ucrt-x86_64/bin/clang.exe';cls.clang=os.environ.get('CC') or (str(local) if local.is_file() else shutil.which('clang') or shutil.which('gcc'))
  if not cls.clang:raise unittest.SkipTest('host compiler unavailable')
  subprocess.run([str(cls.clang),'-O2',str(ROOT/'tools/marble-lz4.c'),str(ROOT/'xnuloader/vendor/lz4/lz4.c'),str(ROOT/'xnuloader/vendor/lz4/lz4hc.c'),'-o',str(cls.codec)],check=True,capture_output=True)
 @classmethod
 def tearDownClass(cls):cls.temp.cleanup()
 def test_c_escapes_and_complete_literal_arguments(self):
  self.assertEqual(m.literal_bytes(r'a\n\x41\101\"'),b'a\nAA"');self.assertIsNone(m.literal_bytes(r'\u1234'));self.assertIsNone(m.literal_bytes(r'\x100'))
  fmt=b'long enough compact format value=%x\n'
  raw='print("long enough compact format value=%x\\n",n); print("long enough compact format value=%x\\012",n); print("long enough compact format value=%x\\n" "suffix",n); otherprint("long enough compact format value=%x\\n",n);'
  packed,r=m.transform(raw,[fmt]);self.assertEqual(len(r),2);self.assertEqual(packed.count(r'\002\000\000'),2);self.assertIn('" "suffix"',packed);self.assertIn('otherprint("long enough',packed)
 def test_large_table_keeps_wide_offsets(self):
  src=self.base/'wide-source';src.mkdir();formats=[(f'long format {i:03d} '+('x'*4000)).encode() for i in range(20)]
  (src/'cases.h').write_text('\n'.join('print('+json.dumps(f.decode())+');' for f in formats));ref=self.base/'wide-reference';ref.write_bytes(b'\0'.join(formats)+b'\0')
  packed=self.base/'wide-packed/xnuloader';r=m.create(src,packed,ref,self.codec)
  self.assertGreater(r['raw_bytes'],65536);self.assertIn('uint32_t marble_format_offsets',(packed/'marble_log_formats.h').read_text())
 def test_actual_firmware_prepare_and_formatter_across_256_ids(self):
  src=self.base/'source';src.mkdir();formats=[f'marble-format-test: row{i:03d} value=%x %s\n'.encode() for i in range(300)]
  calls='\n'.join('print('+json.dumps(f.decode())+',(uint64_t)0xabcULL,"ok");' for f in formats)
  (src/'cases.h').write_text(calls);ref=self.base/'reference';ref.write_bytes(b'\0'.join(formats)+b'\0')
  packed=self.base/'packed/xnuloader';r=m.create(src,packed,ref,self.codec);self.assertEqual(r['formats'],300);self.assertLess(r['compressed_bytes'],r['raw_bytes'])
  loader=(ROOT/'xnuloader/loader.c').read_text();actual=loader[loader.index('static void\nprint('):loader.index('#define READ_SYSREG')]
  def extract(signature):
   start=loader.index(signature);at=loader.index('{',start);depth=1;end=at+1
   while depth:
    if loader[end]=='{':depth+=1
    elif loader[end]=='}':depth-=1
    end+=1
   return loader[start:end]+'\n'
  memory=''.join(extract(sig) for sig in ['void *\nmemset(','int\nmemcmp(','void *\nmemcpy(','void *memmove('])

  program='''#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#define XNU_MARBLE_LOG_FORMAT_PACK 1
'''+memory+'''#include "marble_log_formats_fw.h"
static char output[65536];static unsigned used;
static void put_char(char c){assert(used<sizeof(output)-1);output[used++]=c;}
'''+actual+'''int main(int argc,char **argv){
assert(argc==2 && marble_format_prepare());
assert(sizeof(marble_format_offsets)==MARBLE_FORMAT_COUNT*2);
static char original[MARBLE_FORMAT_RAW_SIZE];FILE *f=fopen(argv[1],"rb");assert(f && fread(original,1,sizeof(original),f)==sizeof(original));fclose(f);assert(!memcmp(original,marble_format_raw,sizeof(original)));
#include "cases.h"
print("plain %s\\n","yes");
static char expected[65536];unsigned pos=0;for(unsigned i=0;i<300;i++)pos+=(unsigned)snprintf(expected+pos,sizeof(expected)-pos,"marble-format-test: row%03u value=abc ok\\n",i);
strcat(expected,"plain yes\\n");assert(!strcmp(expected,output));
char bad[MARBLE_FORMAT_RAW_SIZE];assert(LZ4_decompress_safe((const char *)marble_format_block,bad,sizeof(marble_format_block)-1,sizeof(bad))!=sizeof(bad));return 0;
}'''
  c=self.base/'formatter.c';c.write_text(program);exe=self.base/'formatter.exe'
  r=subprocess.run([str(self.clang),'-O2','-ffreestanding','-fno-builtin','-DLZ4_FREESTANDING=1','-DLZ4_FAST_DEC_LOOP=0','-DLZ4_memcpy=memcpy','-DLZ4_memmove=memmove','-DLZ4_memset=memset','-include',str(ROOT/'tools/marble-lz4-efi.h'),'-I'+str(packed),'-I'+str(ROOT/'xnuloader'),str(c),str(ROOT/'xnuloader/vendor/lz4/lz4.c'),'-o',str(exe)],capture_output=True,text=True)
  self.assertEqual(r.returncode,0,r.stdout+r.stderr)
  subprocess.run([str(exe),str(packed.parent/'formats.raw')],check=True,capture_output=True)
if __name__=='__main__':unittest.main()
