import importlib.util,subprocess,sys,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def load(name,path):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
board=load('qup_board',ROOT/'tools/marble-qup-board.py');report=load('qup_report',ROOT/'tools/marble-i2c-inventory-report.py')
def fixture():
 return '\n'.join((
 'marble-xo-rpmh: held PASS=1',
 'qk: begin name=QUP2 stable=1 qualified=1 address=50000 unit=1000 width=4 free_status=0 data=60004001 sets=2,0 zero_votes=0 sleep_votes=0',
 'qk: baseline mode=1 exact_idle_zero=0 votes=0',
 'qk: client status=0 owned_empty=1',
 'qk: issue set=2 status=0 id=0 address=50000 data=60004001',
 'qk: issue set=0 status=0 id=20 address=50000 data=60004001',
 'qk: held PASS=1 churn_status=0 other_resources_clients_unchanged=1 vote_acknowledged=1 owned_drained=1 retained_client=1 data=60004001 issue_calls=2 zero_votes=0 sleep_votes=0 cold_reset_required=1 CPU_GPU_frequency_requests=0 voltage_requests=0 audio_acceptance=0',
 'marble-adsp-owned: phase=bandwidth baseline=RUN_XO_LPI_held',
 'qk: peak address=50000 logical=0 expected=20004001 retained_floor_verified=1'))+'\n'
class Qup(unittest.TestCase):
 def test_actual_vendor_board_and_minimum_command(self):
  r=board.inspect();self.assertTrue(r['passed']);self.assertEqual(r['command'],0x60004001)
  self.assertEqual(r['sets'],[2,0]);self.assertFalse(r['initial_16000_votes_used']);self.assertEqual(r['sleep_votes'],0)
 def test_typed_native_ownership_and_retained_completion(self):
  text=fixture();r=report.inspect_qup_core_hold(text);self.assertTrue(r['passed'],r);self.assertFalse(r['audio_acceptance'])
  for a,b in [('name=QUP2','name=QUP1'),('address=50000','address=300b0'),('data=60004001','data=6fa03e80'),('sets=2,0','sets=1,0'),('set=2','set=1'),('owned_drained=1','owned_drained=0'),('other_resources_clients_unchanged=1','other_resources_clients_unchanged=0'),('sleep_votes=0','sleep_votes=1'),('voltage_requests=0','voltage_requests=1'),('cold_reset_required=1','cold_reset_required=0')]:
   self.assertFalse(report.inspect_qup_core_hold(text.replace(a,b))['passed'],a)
  for line in text.splitlines():
   if line.startswith('qk:'):
    self.assertFalse(report.inspect_qup_core_hold(text.replace(line,''))['passed']);self.assertFalse(report.inspect_qup_core_hold(text+line)['passed'])
 def test_no_combined_mode_without_explicit_window(self):
  with tempfile.TemporaryDirectory() as tmp:
   out=Path(tmp)/'live';r=subprocess.run([sys.executable,'-X','utf8','tools/marble-gpu-preview.py','--out',str(out),'--discovery-only','--qup-core-keepalive'],cwd=ROOT,capture_output=True,text=True,timeout=8)
   self.assertEqual(r.returncode,2,r.stdout+r.stderr);self.assertIn('--qup-core-keepalive requires --audio-i2c-window',r.stderr);self.assertFalse(out.exists())
 def test_existing_zero_drv_cache(self):
  text=fixture().replace('qk: baseline mode=1 exact_idle_zero=0 votes=0','\n'.join((
   'qk: cached address=50000 header=50000 pointer=fa8b519c pending=0 resources=45 votes=45 clients=6 read_only=1',
   'qk: cached_vote depth=0 pointer=fa8b519c words=2,0,1,100000000,0,0,0,4,0 read_only=1',
   'qk: baseline mode=2 exact_idle_zero=1 votes=0')))
  self.assertTrue(report.inspect_qup_core_hold(text)['passed'])
  for a,b in [('words=2,0,1,','words=2,0,2,'),('pending=0','pending=1'),('header=50000','header=50004'),('exact_idle_zero=1','exact_idle_zero=0'),('depth=0','depth=1')]:
   self.assertFalse(report.inspect_qup_core_hold(text.replace(a,b))['passed'],a)
 def test_floor_is_not_icb_bandwidth(self):
  for a,b in [('logical=0','logical=1'),('expected=20004001','expected=20004002'),('retained_floor_verified=1','retained_floor_verified=0')]:
   self.assertFalse(report.inspect_qup_core_hold(fixture().replace(a,b))['passed'],a)
if __name__=='__main__':unittest.main()
