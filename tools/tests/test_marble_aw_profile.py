import importlib.util,unittest
from pathlib import Path
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[2]
s=importlib.util.spec_from_file_location('aw_profile',ROOT/'tools/marble-aw-profile.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)

class Profile(unittest.TestCase):
 def test_pinned_board_source_and_generated_table(self):
  r=m.qualify();self.assertTrue(r['passed']);self.assertFalse(r['audio_acceptance'])
  self.assertEqual([d['address'] for d in r['devices']],[0x34,0x35])
  self.assertEqual(m.render(r),(ROOT/'xnuloader/marble_aw2032_music.h').read_text())
  for d in r['devices']:
   pairs=d['registers'];self.assertEqual(len(pairs),44)
   self.assertFalse(any(p['address']>=0x70 for p in pairs))
   for p in pairs:
    v=m.target(p['address'],p['value'],0x4003,0xf038)
    if p['address']==4:self.assertEqual(v&3,3)
    if p['address']==5:self.assertEqual(v&0xffd0,0xf010)
    if p['address']==3:self.assertEqual(v,65535)
 def test_vcal_signed_bits_real_chips_and_boundaries(self):
  for efrh,efrm1 in ((0xc3ff,0x4015),(0xc3f7,0x401e),(0,0),(511,512),(512,511),(1023,1023)):
   a=(efrh&1023)-(1024 if efrh&512 else 0);b=(efrm1&1023)-(1024 if efrm1&512 else 0)
   self.assertEqual(m.vcalb(efrh,efrm1),8192*(1000+b)//(1000+a))
  for v in (-1,65536):
   with self.assertRaises(ValueError):m.vcalb(v,0)
 def test_active_or_unmuted_rejected(self):
  for ctrl in range(4):
   for mute in (0,16):
    if ctrl==3 and mute:continue
    with self.assertRaises(ValueError):m.target(4,0xb440,ctrl,mute)
 def test_changed_pinned_input_is_rejected(self):
  original=Path.read_bytes
  def bad(path):
   raw=original(path)
   return bytes([raw[0]^1])+raw[1:] if path.name=='rom-aw882xx-acf.bin' else raw
  with patch.object(Path,'read_bytes',bad):
   with self.assertRaisesRegex(ValueError,'pinned board inputs'):m.qualify()
if __name__=='__main__':unittest.main()
