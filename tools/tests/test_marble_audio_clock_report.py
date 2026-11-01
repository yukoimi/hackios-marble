import importlib.util,re,unittest
from pathlib import Path
BASE=Path(__file__).parent
def load(name,path):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
old=load('clock_graph_fixture',BASE/'test_marble_tdm_graph_report.py')
m=load('audio_clock_report',BASE.parent/'marble-audio-clock-report.py')
def fixture():
 s=old.fixture().replace('marble-tdm-graph:','prm:').replace('4d475201','4d434b01').replace('4d475202','4d434b02').replace('opcode=1001000','opcode=100100f').replace('opcode=1001004','opcode=1001010').replace('bytes=296','bytes=76').replace('bytes=312','bytes=96')
 s=s.replace('open_reply','request_reply').replace('close_reply','release_reply').replace('open_error','request_error').replace('close_error','release_error')
 s=s.replace('graph=4001 module=700100e prepare_commands=0 start_commands=0 clock_commands=0','pins_ok=1 graph_commands=0')
 s=s.replace('error=0 client_data=0 bytes=32','kind=1 error=0 client_data=0 bytes=32')
 def pins(after):
  return '\n'.join([f'prm: pin after={after} index={i} cfg=1 io=0' for i in range(7)]+[f'prm: guard after={after} PASS=1 writes=0'])
 s=s.replace('prm: query stage=0 ',pins(0)+'\nprm: query stage=0 ',1)
 s=s.replace('prm: complete ',pins(1)+'\nprm: complete ',1)
 head=0;lines=[]
 for l in s.splitlines():
  if l.startswith('prm:'):
   for a,b in [('local_intent','intent'),('remote_channel','chan'),('frame_bytes','frame'),('accumulated','used'),('remote_bytes','cap'),('remote_id','id')]:l=l.replace(a,b)
  if l.startswith('marble-glink-control: tx '):
   n=int(re.search(r'bytes=(\d+)',l)[1]) if 'bytes=' in l else 8;head+=n;l=re.sub(r'local_head=\d+',f'local_head={head}',l)
  lines.append(l)
 return '\n'.join(lines)
class Clock(unittest.TestCase):
 def test_typed_clock_cycle_and_independent_PAS_recovery(self):
  s=fixture();r=m.inspect(s);self.assertTrue(r['passed'],r);self.assertTrue(r['clock_lease_verified']);self.assertFalse(r['audio_acceptance'])
  owned=old.old.old.apps.f.held.scm.m
  r=owned.inspect(s,1,scm_peak=True,wire=True,raw_shutdown=True,held=True,control=True,apps=True,gpr=True,audio_clock=True);self.assertTrue(r['passed'],r)
  self.assertFalse(owned.inspect(s,1,scm_peak=True,wire=True,raw_shutdown=True,held=True,control=True,apps=True,gpr=True)['passed'])
 def test_unsafe_pins_bad_completion_and_wrong_packets(self):
  for a,b in [('index=0 cfg=1 io=0','index=0 cfg=1 io=2'),('index=3 cfg=1','index=3 cfg=5'),('index=6 cfg=1','index=6 cfg=201'),('release_error=0','release_error=2'),('kind=1','kind=2'),('pins_ok=1','pins_ok=0'),('PAS_stopped=1','PAS_stopped=0'),('bytes=96','bytes=80'),('bytes=76','bytes=64'),('opcode=1001010','opcode=100100f'),('token=4d434b02','token=4d434b01')]:self.assertFalse(m.inspect(fixture().replace(a,b))['passed'],a)
 def test_missing_duplicate_evidence(self):
  s=fixture()
  for l in s.splitlines():
   if l.startswith('prm:'):
    self.assertFalse(m.inspect(s.replace(l,''))['passed'],l);self.assertFalse(m.inspect(s+'\n'+l)['passed'],l)
class Controller(unittest.TestCase):
 def test_real_controller_and_reset_required(self):
  base=load('clock_controller',BASE/'test_marble_amdb_report.py').Controller()
  code,r=base.simulate(clock=True);self.assertEqual(code,0,r);self.assertTrue(r['adsp_audio_clock_cycle']['clock_lease_verified'])
  for kw in (dict(bad=True),dict(changed=False)):
   code,r=base.simulate(clock=True,**kw);self.assertEqual(code,1,r)
if __name__=='__main__':unittest.main()
