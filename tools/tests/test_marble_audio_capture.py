import importlib.util,unittest
from pathlib import Path
BASE=Path(__file__).parent
def load(name,path):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
m=load('audio_capture',BASE.parent/'marble-audio-capture.py');f=load('audio_capture_fixture',BASE/'test_marble_i2c_inventory_report.py')
def payload():
 return '\n'.join(l for l in f.Inventory().digital_fixture().splitlines() if l.startswith(('marble-aw-digital:','marble-aw-profile: complete ','marble-i2c-amp: complete ')) or (l.startswith('marble-i2c-amp: write ') and any(s in l for s in ('phase=digital-','phase=reset-assert','phase=reset-restore'))))+'\n'
def encode(data):
 raw=data.encode('ascii');h=0xcbf29ce484222325
 for byte in raw:h=((h^byte)*0x100000001b3)&0xffffffffffffffff
 return '\n'.join([f'(bootloader) ADC:{i:08x}:{raw[i:i+20].hex()}' for i in range(0,len(raw),20)]+[f'(bootloader) ADC-END:{len(raw):08x}:{h:016x}'])+'\n'
class Capture(unittest.TestCase):
 def test_exact_digital_records_and_no_PAS_claim(self):
  text=payload();self.assertEqual(m.decode(encode(text)),text);r=m.inspect(text);self.assertTrue(r['passed'],r);self.assertFalse(r['PAS_shutdown_verified']);self.assertFalse(r['audio_acceptance'])
 def test_lost_duplicate_corrupt_and_reordered_frames(self):
  lines=encode('payload\n'*30).splitlines()
  for bad in [lines[:-1],lines[1:],lines[:2]+lines[3:],lines+[lines[-1]],lines[:1]+lines,lines[1:2]+lines[:1]+lines[2:],['ADC-OVERFLOW'],[lines[0].replace(':7061796c',':7161796c')]+lines[1:]]:
   with self.assertRaises(ValueError):m.decode('\n'.join(bad))
 def test_unmute_missing_reset_and_PLL_failure_rejected(self):
  text=payload()
  for a,b in [('HMUTE_retained=1','HMUTE_retained=0'),('status=11','status=13'),('reset_restored=1','reset_restored=0'),('phase=reset-assert','phase=reset-wrong')]:
   self.assertFalse(m.inspect(text.replace(a,b))['passed'],a)
if __name__=='__main__':unittest.main()
