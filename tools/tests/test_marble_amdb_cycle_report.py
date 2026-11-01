import importlib.util,re,unittest
from pathlib import Path
BASE=Path(__file__).parent
def load(name,path):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
old=load('cycle_gpr_fixture',BASE/'test_marble_gpr_state_report.py')
m=load('cycle_report',BASE.parent/'marble-amdb-cycle-report.py')
def fixture(acquire=True):
 s=old.fixture().replace('marble-gpr:','marble-amdb-cycle:').replace('4d535001','4d4c4401')
 s=s.replace('query token=','query stage=0 token=').replace('opcode=1001021 bytes=24','opcode=1001020 bytes=64 module=700100e handle=0 graph_commands=0')
 s=s.replace('tx_done command=','tx_done stage=0 command=').replace('bytes=40','bytes=80')
 s=s.replace('chunk=28 left=0 accumulated=28 frame_bytes=48','chunk=48 left=0 accumulated=48 frame_bytes=64')
 s=s.replace('reply token=4d4c4401 kind=1 state=1 error=0 bytes=28', 'reply stage=0 token=4d4c4401 kind=1 error='+('0' if acquire else '2')+' client_data=0 handle='+('12345678' if acquire else '0')+' bytes=48')
 s=s.replace('rx command=9 param1=3 param2=1 bytes=48','rx command=9 param1=3 param2=1 bytes=64')
 if acquire:
  extra='\n'.join([
   'marble-glink-control: tx command=9 param1=1 param2=2 local_head=0 bytes=80',
   'marble-amdb-cycle: query stage=1 token=4d4c4402 opcode=1001022 bytes=64 remote_id=2 remote_bytes=512 module=700100e handle=12345678 graph_commands=0 audio_commands=0',
   'marble-amdb-cycle: tx_done stage=1 command=14 remote_channel=3 remote_id=2',
   'marble-glink-control: rx command=14 param1=3 param2=2 bytes=8 local_tail=0',
   'marble-amdb-cycle: fragment command=9 channel=3 intent=1 chunk=32 left=0 accumulated=32 frame_bytes=48',
   'marble-amdb-cycle: reply stage=1 token=4d4c4402 kind=2 error=0 client_data=0 handle=0 bytes=32 remote_channel=3 local_intent=1',
   'marble-glink-control: tx command=14 param1=1 param2=1 local_head=0 bytes=8',
   'marble-glink-control: rx command=9 param1=3 param2=1 bytes=48 local_tail=0'])
  s=s.replace('marble-pil-boot: observed ',extra+'\nmarble-pil-boot: observed ',1).replace('GPR_packets=1','GPR_packets=2').replace('RX_messages=7 TX_messages=7','RX_messages=9 TX_messages=9')
 n=2 if acquire else 1
 end=f'marble-amdb-cycle: complete sent={n} replies={n} tx_done={n} load_reply=1 unload_reply={n-1} load_error={0 if acquire else 2} unload_error=0 handle={"12345678" if acquire else "0"} rejected=0 fragment_active=0 fragment_bytes=0 PAS_stopped=1 graph_commands=0 audio_acceptance=0'
 s=re.sub(r'marble-amdb-cycle: complete [^\n]+',end,s)
 head=tail=0;lines=[]
 for l in s.splitlines():
  if l.startswith('marble-glink-control: tx '):
   size=int(re.search(r'bytes=(\d+)',l)[1]) if 'bytes=' in l else 8;head+=size;l=re.sub(r'local_head=\d+',f'local_head={head}',l)
  if l.startswith('marble-glink-control: rx '):
   size=int(re.search(r'bytes=(\d+)',l)[1]);tail+=size;l=re.sub(r'local_tail=\d+',f'local_tail={tail}',l)
  lines.append(l)
 return '\n'.join(lines)
class Cycle(unittest.TestCase):
 def test_mfc_library_requires_explicit_module_contract(self):
  text=fixture().replace('module=700100e','module=7001015')
  r=m.inspect(text,module_id='7001015');self.assertTrue(r['passed'],r)
  self.assertEqual(r['module_id'],'07001015')
  self.assertFalse(m.inspect(text)['passed'])
  self.assertFalse(m.inspect(fixture(),module_id='7001015')['passed'])
 def test_two_stage_lifecycle(self):
  s=fixture();r=m.inspect(s);self.assertTrue(r['passed'],r);self.assertTrue(r['module_acquired']);self.assertTrue(r['module_released']);self.assertFalse(r['audio_acceptance'])
  r=old.apps.f.held.scm.m.inspect(s,1,scm_peak=True,wire=True,raw_shutdown=True,held=True,control=True,apps=True,gpr=True,amdb_cycle=True);self.assertTrue(r['passed'],r)
  self.assertFalse(old.apps.f.m.inspect(s,apps=True,gpr=True)['passed'])
  r=m.inspect(s.replace('stage=1 token=4d4c4402 kind=2 error=0 client_data=0','stage=1 token=4d4c4402 kind=2 error=0 client_data=a8'));self.assertTrue(r['passed'],r);self.assertTrue(r['module_released']);self.assertFalse(r['header_conformant'])
 def test_not_available_without_handle(self):
  s=fixture(False);r=m.inspect(s);self.assertTrue(r['passed'],r);self.assertFalse(r['module_acquired']);self.assertFalse(r['module_released'])
  r=old.apps.f.held.scm.m.inspect(s,1,scm_peak=True,wire=True,raw_shutdown=True,held=True,control=True,apps=True,gpr=True,amdb_cycle=True);self.assertTrue(r['passed'],r)
 def test_rejects_wrong_release_and_missing_completion(self):
  for a,b in [('handle=12345678 graph','handle=12345679 graph'),('remote_id=2','remote_id=1'),('token=4d4c4402','token=4d4c4401'),('opcode=1001022','opcode=1001020'),('unload_reply=1','unload_reply=0'),('tx_done=2','tx_done=1'),('bytes=80','bytes=112'),('PAS_stopped=1','PAS_stopped=0'),('stage=1 token=4d4c4402 kind=2 error=0','stage=1 token=4d4c4402 kind=2 error=2'),('graph_commands=0','graph_commands=1')]:self.assertFalse(m.inspect(fixture().replace(a,b))['passed'],a)
 def test_missing_duplicate_events(self):
  s=fixture()
  for l in s.splitlines():
   if l.startswith('marble-amdb-cycle:'):
    self.assertFalse(m.inspect(s.replace(l,''))['passed'],l);self.assertFalse(m.inspect(s+'\n'+l)['passed'],l)
class Controller(unittest.TestCase):
 def test_real_controller_dispatch_recovery_and_reset(self):
  base=load('cycle_controller',BASE/'test_marble_amdb_report.py').Controller()
  code,r=base.simulate(cycle=True);self.assertEqual(code,0,r);self.assertTrue(r['adsp_amdb_load_cycle']['module_released']);self.assertTrue(r['adsp_owned_boot_with_reset_passed'])
  for options in (dict(bad=True),dict(changed=False)):
   code,r=base.simulate(cycle=True,**options);self.assertEqual(code,1,r)
if __name__=='__main__':unittest.main()
