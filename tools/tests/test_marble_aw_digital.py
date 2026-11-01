import importlib.util,unittest
from pathlib import Path
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[2]
s=importlib.util.spec_from_file_location('digital_board',ROOT/'tools/marble-aw-digital-board.py')
m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
class DigitalBoard(unittest.TestCase):
 def test_exact_oem_registers_and_safe_rom_values(self):
  r=m.inspect();self.assertTrue(r['passed']);self.assertFalse(r['audio_acceptance']);self.assertFalse(r['CCO_fallback'])
  self.assertEqual(r['polls_maximum']*r['poll_delay_us'],20000)
  self.assertEqual([(v['sysctrl_down'],v['sysctrl_digital'],v['sysctrl2']) for v in r['devices']],[(0xb443,0xb442,0xf039),(0x3443,0x3442,0xf039)])
 def test_changed_oem_bit_or_poll_count_rejected(self):
  original=Path.read_text
  for file,a,b in [('aw882xx_pid_2032_reg.h','AW_PID_2032_PWDN_START_BIT\t\t(0)','AW_PID_2032_PWDN_START_BIT\t\t(1)'),
                   ('aw882xx_device.c','#define AW_DEV_SYSST_CHECK_MAX   (10)','#define AW_DEV_SYSST_CHECK_MAX   (100)')]:
   def changed(path,*args,**kw):
    text=original(path,*args,**kw)
    if path.name==file:
     self.assertIn(a,text);text=text.replace(a,b)
    return text
   with patch.object(Path,'read_text',changed):
    with self.assertRaises(ValueError):m.inspect()
if __name__=='__main__':unittest.main()
