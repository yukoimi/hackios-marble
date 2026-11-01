"""Synthetic parser cases only; these are not hardware evidence."""
import importlib.util,json,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
spec=importlib.util.spec_from_file_location('stack_report',Path(__file__).parents[1]/'marble-tpl-stack-report.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

class StackReport(unittest.TestCase):
 def parse(self,change=None,drop=False,duplicate=False):
  sp=0xa0000000;at=192;words={i:0 for i in range(0,2048,8)}
  words.update({at+0xf8:sp+at+0x330,at+0xe8:sp+1040,at+0x300:0xfe001278,at+0x308:0x305,
                1040:sp+1080,1048:0xfe102008,1080:sp+1080,1088:0xfe203008,144:0xfe300000})
  if change:change(words)
  lines=['aud-tpl-stack: sample=0 sp=a0000000 words=256 raw_only=1 foreign_calls=0']
  lines += [f'aud-tpl-stack-word: sample=0 offset={i} value={v:x}' for i,v in words.items() if not(drop and i==2032)]
  if duplicate:lines.append(lines[-1])
  owners=[]
  for address,name,rva in [(0xfe001278,'delay','0x1278'),(0xfe102008,'caller','0x2008'),(0xfe203008,'parent','0x3008'),(0xfe300000,'unrelated-data','0x1000')]:
   owners.append(dict(unique_owner=True,observation=dict(callback=f'{address:x}'),owners=[dict(name=name,rva=rva,file='unused.efi')]))
  with tempfile.TemporaryDirectory() as tmp:
   p=Path(tmp);(p/'index.json').write_text(json.dumps(dict(matches=owners)));(p/'log').write_text('\n'.join(lines))
   with patch.object(m,'pe_word',return_value=0x94000000):return m.inspect(p/'log',p)
 def test_context_and_frame_links_without_unrelated_raw_words(self):
  c=self.parse()['samples'][0]['candidate_contexts'];self.assertEqual(len(c),1)
  self.assertEqual(c[0]['pc']['image'],'delay');self.assertEqual([f['return_address']['image'] for f in c[0]['frames']],['caller','parent'])
  self.assertTrue(all(f['return_address']['preceded_by_call'] for f in c[0]['frames']))
 def test_sp_or_pstate_mismatch_is_not_context(self):
  for offset,value in [(192+0xf8,0),(192+0x308,0)]:
   self.assertFalse(self.parse(lambda w:w.update({offset:value}))['samples'][0]['candidate_contexts'])
 def test_outside_snapshot_ends_frame_walk(self):
  c=self.parse(lambda w:w.update({1040:0xa0010000}))['samples'][0]['candidate_contexts'][0]
  self.assertEqual(len(c['frames']),1)
 def test_missing_and_duplicate_words_rejected(self):
  for kw in [dict(drop=True),dict(duplicate=True)]:
   with self.assertRaises(ValueError):self.parse(**kw)

if __name__=='__main__':unittest.main()
