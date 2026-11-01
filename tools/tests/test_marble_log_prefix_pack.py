import importlib.util,tempfile,unittest,subprocess,os,shutil
from pathlib import Path
spec=importlib.util.spec_from_file_location('log_prefix_pack',Path(__file__).parents[1]/'marble-log-prefix-pack.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class Pack(unittest.TestCase):
 def test_actual_loader_formatter_keeps_arguments_and_zero_id(self):
  root=Path(__file__).resolve().parents[2];s=(root/'xnuloader/loader.c').read_text()
  actual=s[s.index('static void\nprint('):s.index('#define READ_SYSREG')]
  program='''#include <stdint.h>
#include <string.h>
#include <assert.h>
#define XNU_MARBLE_LOG_PREFIX_PACK 1
#define MARBLE_LOG_PREFIX_COUNT 3
static const char *const marble_log_prefixes[]={"marble-a: ","marble-b: "," PASS=1"};
static char output[1024];static unsigned used;
static void put_char(char c){output[used++]=c;}
'''+actual+'''int main(void){
print("\\001\\000n=%d hex=%x %s %%\\n",(uint64_t)1234567890123ULL,(uint64_t)0xabcdef1234567890ULL,"hello");
print("plain=%d\\n",(uint64_t)7);print("\\001\\001ok\\001\\002\\n");
assert(!strcmp(output,"marble-a: n=1234567890123 hex=abcdef1234567890 hello %\\nplain=7\\nmarble-b: ok PASS=1\\n"));return 0;
}'''
  with tempfile.TemporaryDirectory() as tmp:
   p=Path(tmp);(p/'formatter.c').write_text(program)
   local=root.parent/'toolchains/llvm-mingw-20261020-ucrt-x86_64/bin/clang.exe'
   llvm=os.environ.get('CC') or (str(local) if local.is_file() else shutil.which('clang') or shutil.which('gcc'))
   if not llvm:self.skipTest('host compiler unavailable')
   subprocess.run([str(llvm),str(p/'formatter.c'),'-O2','-o',str(p/'formatter.exe')],check=True,capture_output=True)
   subprocess.run([str(p/'formatter.exe')],check=True,capture_output=True)
 def test_only_constant_print_prefixes_change_and_round_trip(self):
  s='print("marble-a: value=%x\\n",n); print("marble-a: %s",p); const char *s="marble-a: "; print(fmt,n); otherprint("marble-a: ");'
  r=m.transform(s,['marble-a: ']);self.assertEqual(r.count('\\001\\000'),2);self.assertIn('const char *s="marble-a: "',r);self.assertIn('print(fmt,n)',r);self.assertIn('otherprint("marble-a: ")',r)
 def test_exact_source_bytes_and_new_tree(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp);src=root/'source';src.mkdir();original=b'print("marble-b: n=%d\\n", n);\r\nprint("marble-a: ok\\n");\r\n'
   (src/'test.h').write_bytes(original);out=root/'packed';r=m.create(src,out)
   self.assertEqual((src/'test.h').read_bytes(),original);self.assertIn(b'\r\n',(out/'test.h').read_bytes());self.assertEqual(r['prefixes'],['marble-a: ','marble-b: ']);self.assertIn('MARBLE_LOG_PREFIX_COUNT 2',(out/'marble_log_prefixes.h').read_text())
   with self.assertRaises(ValueError):m.create(src,out)
if __name__=='__main__':unittest.main()
