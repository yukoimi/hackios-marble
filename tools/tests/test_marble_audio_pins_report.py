import importlib.util,unittest
from pathlib import Path
BASE=Path(__file__).parent
def load(name,path):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
old=load('pins_data_fixture',BASE/'test_marble_pcm_data_report.py');m=load('audio_pins_report',BASE.parent/'marble-audio-pins-report.py')
def snapshot(phase):
 gpios=(1,120,52,121,123,122,124);active=(1,1,1,0x2c4,0x2c4,0xc4,0xc5)
 return '\n'.join(f'tp: pin phase={phase} index={i} gpio={gpio} cfg={active[i] if phase==1 else 1:x} io={2 if phase==1 and i in (3,4) else 0:x}' for i,gpio in enumerate(gpios))+'\n'
def fixture():
 s=old.fixture();first='tp: begin cfg=2c4,2c4,c4,c5 amps_reset=1\n'+snapshot(0)+snapshot(1)+'tp: acquired PASS=1 active=1 writes=6\n'
 at=s.index('marble-glink-control: tx command=9 param1=1 param2=8 ');s=s[:at]+first+s[at:]
 last=snapshot(2)+'tp: end PASS=1 active=0 restored=1 writes=12 PAS_stopped=0 amp_changes=0\n'
 at=s.index('marble-glink-control: tx command=9 param1=1 param2=7 ');return s[:at]+last+s[at:]
class Pins(unittest.TestCase):
 def test_bound_real_cape_and_runtime_fdt(self):
  board=load('pins_board',BASE.parent/'marble-audio-pins-board.py');r=board.inspect();self.assertTrue(r['passed']);self.assertEqual([x['cfg'] for x in r['pins']],[0x2c4,0x2c4,0xc4,0xc5])
 def test_complete_pcm_pin_lifetime(self):
  s=fixture();r=m.inspect(s);self.assertTrue(r['passed'],r);self.assertFalse(r['audio_acceptance'])
  owned=load('pins_owned',BASE.parent/'marble-adsp-owned-report.py');kw=dict(scm_peak=True,wire=True,raw_shutdown=True,held=True,control=True,apps=True,gpr=True,tdm_run=True,tdm_pcm_graph=True,audio_dma=True,pcm_memory=True,pcm_data=True)
  r=owned.inspect(s,1,tdm_pins=True,**kw);self.assertTrue(r['passed'],r);self.assertFalse(owned.inspect(s,1,**kw)['passed'])
 def test_bad_amplifier_or_gpio_or_restore(self):
  for a,b in [('gpio=121 cfg=2c4','gpio=121 cfg=c4'),('gpio=124 cfg=c5','gpio=124 cfg=c4'),('index=0 gpio=1 cfg=1 io=0','index=0 gpio=1 cfg=1 io=2'),('restored=1','restored=0'),('writes=12','writes=11'),('PAS_stopped=0 amp_changes=0','PAS_stopped=1 amp_changes=0')]:self.assertFalse(m.inspect(fixture().replace(a,b))['passed'],a)
  for l in fixture().splitlines():
   if l.startswith('tp:'):
    self.assertFalse(m.inspect(fixture().replace(l,''))['passed'],l);self.assertFalse(m.inspect(fixture()+'\n'+l)['passed'],l)
 def test_controller_and_failed_recovery(self):
  base=load('pins_controller',BASE/'test_marble_amdb_report.py').Controller()
  code,r=base.simulate(tdm_pins=True);self.assertEqual(code,0,r);self.assertTrue(r['adsp_tdm_pins_cycle']['TDM_pins_routed_and_restored'])
  for kw in (dict(bad=True),dict(changed=False)):
   code,r=base.simulate(tdm_pins=True,**kw);self.assertEqual(code,1,r)
if __name__=='__main__':unittest.main()
