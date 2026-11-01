import importlib.util,re,unittest
from pathlib import Path
BASE=Path(__file__).parent
def load(name,path):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
old=load('graph_pair_fixture',BASE/'test_marble_amdb_cycle_report.py')
m=load('graph_report',BASE.parent/'marble-tdm-graph-report.py')
def fixture(configure=False):
 s=old.fixture().replace('marble-amdb-cycle:','marble-tdm-graph:').replace('4d4c4401','4d475201').replace('4d4c4402','4d475202').replace('opcode=1001020','opcode=1001000').replace('opcode=1001022','opcode=1001004')
 s=s.replace('opcode=1001000 bytes=64','opcode=1001000 bytes=296').replace('local_head=144 bytes=80','local_head=144 bytes=312',1)
 s=s.replace('chunk=48 left=0 accumulated=48 frame_bytes=64','chunk=32 left=0 accumulated=32 frame_bytes=48').replace('bytes=48 remote_channel=3','bytes=32 remote_channel=3',1).replace('rx command=9 param1=3 param2=1 bytes=64','rx command=9 param1=3 param2=1 bytes=48',1)
 end='marble-tdm-graph: complete sent=2 replies=2 tx_done=2 open_reply=1 close_reply=1 open_error=0 close_error=0 rejected=0 fragment_active=0 fragment_bytes=0 PAS_stopped=1 graph=4001 module=700100e prepare_commands=0 start_commands=0 clock_commands=0 amp_commands=0 audio_acceptance=0'
 if configure:
  s=s.replace('opcode=1001000 bytes=296','opcode=1001000 bytes=400');end='marble-tdm-graph: configuration params=3\n'+end
 s=re.sub(r'marble-tdm-graph: complete [^\n]+',lambda _:end,s)
 head=tail=0;lines=[];first=True
 for l in s.splitlines():
  if l.startswith('marble-tdm-graph:') and not l.startswith('marble-tdm-graph: complete '):l=re.sub(r' (kind|handle|module|graph_commands|audio_commands)=[^\s]+','',l)
  if l.startswith('marble-glink-control: tx '):
   if 'command=9 ' in l and first:l=re.sub(r'bytes=\d+','bytes='+str(416 if configure else 312),l);first=False
   n=int(re.search(r'bytes=(\d+)',l)[1]) if 'bytes=' in l else 8;head+=n;l=re.sub(r'local_head=\d+',f'local_head={head}',l)
  if l.startswith('marble-glink-control: rx '):
   n=int(re.search(r'bytes=(\d+)',l)[1]);tail+=n;l=re.sub(r'local_tail=\d+',f'local_tail={tail}',l)
  lines.append(l)
 return '\n'.join(lines)
class Graph(unittest.TestCase):
 def test_graph_open_close_and_recovery_contract(self):
  s=fixture();r=m.inspect(s);self.assertTrue(r['passed'],r);self.assertTrue(r['graph_created_and_closed']);self.assertFalse(r['audio_acceptance'])
  owned=old.old.apps.f.held.scm.m
  r=owned.inspect(s,1,scm_peak=True,wire=True,raw_shutdown=True,held=True,control=True,apps=True,gpr=True,tdm_graph=True);self.assertTrue(r['passed'],r)
  self.assertFalse(owned.inspect(s,1,scm_peak=True,wire=True,raw_shutdown=True,held=True,control=True,apps=True,gpr=True)['passed'])
 def test_basic_client_quirk_does_not_change_routing(self):
  r=m.inspect(fixture().replace('client_data=0','client_data=58'));self.assertTrue(r['passed'],r);self.assertFalse(r['header_conformant'])
 def test_rejects_failure_start_clock_and_wrong_identity(self):
  for a,b in [('open_error=0','open_error=2'),('close_error=0','close_error=2'),('error=0 client_data','error=2 client_data'),('prepare_commands=0','prepare_commands=1'),('start_commands=0','start_commands=1'),('clock_commands=0','clock_commands=1'),('amp_commands=0','amp_commands=1'),('graph=4001','graph=4002'),('module=700100e','module=700100f'),('opcode=1001004','opcode=1001002'),('remote_id=2','remote_id=1'),('bytes=296','bytes=64'),('bytes=312','bytes=80'),('PAS_stopped=1','PAS_stopped=0')]:self.assertFalse(m.inspect(fixture().replace(a,b))['passed'],a)
 def test_missing_duplicate_events(self):
  s=fixture()
  for l in s.splitlines():
   if l.startswith('marble-tdm-graph:'):
    self.assertFalse(m.inspect(s.replace(l,''))['passed'],l);self.assertFalse(m.inspect(s+'\n'+l)['passed'],l)
class Configuration(unittest.TestCase):
 def test_board_calibration_requires_explicit_contract(self):
  s=fixture(configure=True);r=m.inspect(s,configure=True);self.assertTrue(r['passed'],r);self.assertTrue(r['board_configuration_accepted']);self.assertFalse(r['audio_acceptance']);self.assertFalse(m.inspect(s)['passed'])
  owned=old.old.apps.f.held.scm.m
  r=owned.inspect(s,1,scm_peak=True,wire=True,raw_shutdown=True,held=True,control=True,apps=True,gpr=True,tdm_graph=True,tdm_config=True);self.assertTrue(r['passed'],r)
  self.assertFalse(owned.inspect(s,1,scm_peak=True,wire=True,raw_shutdown=True,held=True,control=True,apps=True,gpr=True,tdm_graph=True)['passed'])
 def test_wrong_parameters_or_clock_activation_rejected(self):
  for a,b in [('params=3','params=4'),('clock_commands=0','clock_commands=1'),('bytes=400','bytes=296'),('bytes=416','bytes=312')]:self.assertFalse(m.inspect(fixture(True).replace(a,b),configure=True)['passed'],a)
class Controller(unittest.TestCase):
 def test_actual_controller_and_failure_recovery(self):
  base=load('graph_controller',BASE/'test_marble_amdb_report.py').Controller()
  code,r=base.simulate(config=True);self.assertEqual(code,0,r);self.assertTrue(r['adsp_tdm_graph_cycle']['board_configuration_accepted'])
  code,r=base.simulate(graph=True);self.assertEqual(code,0,r);self.assertTrue(r['adsp_tdm_graph_cycle']['graph_created_and_closed']);self.assertTrue(r['adsp_owned_boot_with_reset_passed'])
  for options in (dict(bad=True),dict(changed=False)):
   code,r=base.simulate(graph=True,**options);self.assertEqual(code,1,r)
if __name__=='__main__':unittest.main()
