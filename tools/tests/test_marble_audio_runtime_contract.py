import importlib.util,unittest
from pathlib import Path
BASE=Path(__file__).parent
def load(name,path):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
m=load('runtime_contract_test',BASE.parent/'marble-audio-runtime-contract.py');f=load('runtime_contract_fixture',BASE/'test_marble_i2c_inventory_report.py')
BUDGET='aud-budget: version=1 maximum_us=600000 poll_rounds=60 thermal_samples=6 IRQ_masks_unchanged=1 frequency_requests=0 voltage_requests=0 audio_acceptance=0\n'
def fixture():
 text=f.Inventory().digital_fixture().replace('hold_limit_ms=500','hold_limit_ms=600').replace('hold_us=501000','hold_us=601000').replace('thermal_samples=5','thermal_samples=6').replace('deadline_us=500000 poll_rounds=50','deadline_us=600000 poll_rounds=60')
 at=text.index('marble-pil-boot: auth ');return text[:at]+BUDGET+text[at:]
class Runtime(unittest.TestCase):
 def test_typed_complete_budget_and_variable_final_thermal(self):
  text=f.Inventory().digital_fixture().replace('hold_limit_ms=500','hold_limit_ms=600').replace('deadline_us=500000 poll_rounds=50','deadline_us=600000 poll_rounds=60')
  at=text.index('marble-pil-boot: auth ');text=text[:at]+'aud-typed-budget: version=1 minimum_us=300000 maximum_us=600000 poll_rounds=60 completion_required=1 final_thermal_required=1 audio_acceptance=0\n'+text[at:]
  early='aud-early: typed_complete=1 round=43 minimum_us=300000 maximum_us=750000 thermal_samples=5 final_thermal_ok=1 idle_dwell_omitted=1 PAS_required=1 audio_acceptance=0\n'
  at=text.index('marble-pil-boot: observed ');text=text[:at]+early+text[at:]
  r=m.contract(text);self.assertEqual(r['hold_limit_ms'],600);self.assertEqual(r['thermal_samples'],5)
  r=f.m.inspect_audio_window(text,amp_profile=True,runtime_deadline=True,digital_pll=True,digital_early_stop=True);self.assertTrue(r['passed'],r)
  for a,b in [('round=43','round=63'),('typed_complete=1','typed_complete=0'),('final_thermal_ok=1','final_thermal_ok=0'),('pm: end PASS=1','pm: end PASS=0')]:
   with self.assertRaises(ValueError,msg=a):m.contract(text.replace(a,b))
  policy='aud-stop-policy: poll_us=100 timeout_us=5000000 thermal_interval_us=100000 counter_read_only=1 IRQ_masks_unchanged=1 fresh_ACK_required=1\n'
  at=text.index('marble-smp2p-boot: stop-ack ');text=text[:at]+policy+text[at:]
  r=f.m.inspect_audio_window(text,amp_profile=True,runtime_deadline=True,digital_pll=True,digital_early_stop=True,fast_stop=True);self.assertTrue(r['passed'],r)
  for a,b in [('poll_us=100','poll_us=10000'),('timeout_us=5000000','timeout_us=50000000'),('fresh_ACK_required=1','fresh_ACK_required=0'),('thermal_interval_us=100000','thermal_interval_us=1000000')]:
   self.assertFalse(f.m.inspect_audio_window(text.replace(a,b),amp_profile=True,runtime_deadline=True,digital_pll=True,digital_early_stop=True,fast_stop=True)['passed'],a)
 def test_explicit_600_not_relabelled_as500(self):
  text=fixture();r=m.contract(text);self.assertEqual(r['hold_limit_ms'],600);self.assertEqual(r['thermal_samples'],6)
  r=f.m.inspect_audio_window(text,amp_profile=True,runtime_deadline=True,digital_pll=True,runtime_600ms=True);self.assertTrue(r['passed'],r)
  self.assertFalse(f.m.inspect_audio_window(text,amp_profile=True,runtime_deadline=True,digital_pll=True)['passed'])
 def test_default_500_unchanged(self):self.assertEqual(m.contract('')['hold_limit_ms'],500)
 def test_budget_pll_mute_and_ownership_mutations(self):
  for a,b in [('maximum_us=600000','maximum_us=900000'),('thermal_samples=6','thermal_samples=5'),('IRQ_masks_unchanged=1','IRQ_masks_unchanged=0'),('AMPPD_retained=1','AMPPD_retained=0'),('restored=1','restored=0')]:
   with self.assertRaises(ValueError,msg=a):m.contract(fixture().replace(a,b))
  for more in (BUDGET,'aud-early: x=1\n','aud-capture: x=1\n'):
   with self.assertRaises(ValueError):m.contract(fixture()+more)
