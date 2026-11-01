import importlib.util,unittest
from pathlib import Path
BASE=Path(__file__).parent
def load(name,path):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
o=load('owned_noexec_fixture',BASE/'test_marble_adsp_owned_report.py');m=o.m
def fixture():
 text=o.fixture().replace('ROM-noexec-reset','ROM-boot-stop-reset').replace('phase=ROM-noexec','phase=ROM-boot')
 source=o.smp.boot_fixture();auth=source[source.index('marble-pil-boot: auth '):source.index('marble-pil-held: shutdown ')]
 text=text.replace('marble-pil-held: shutdown ',auth+'marble-pil-held: shutdown ')
 text=text.replace('marble-pil-held: restore-gate ',
  'marble-glink: released PASS=1 active=0 dirty=0 up=0 down=0 invalid=0 open=0 tx=0 dsp_start_attempted=1 pmic_writes=0\nmarble-pil-held: restore-gate ')
 text=text.replace('marble-pil-held: loaded ','marble-pil-progress: owned-dependencies-ready address=1\nmarble-pil-held: loaded ')
 lines=[]
 for line in text.splitlines():
  if line.startswith(('marble-pil-held: restored ','marble-pil-header: complete ','marble-pil-ramfs: released ',
   'marble-npa-client: complete ','marble-npa-native-release: complete ','marble-smp2p-state: ',
   'marble-adsp-owned: complete ','marble-icb-bw: complete ')):
   line=line.replace('auth_reset=0','auth_reset=1').replace('dsp_start=0','dsp_start=1').replace('DSP_start=0','DSP_start=1')
  lines.append(line)
 return '\n'.join(lines)+'\n'
class Boot(unittest.TestCase):
 def test_real_owned_boot_not_audio(self):
  r=m.inspect(fixture(),1);self.assertTrue(r['passed'],r);self.assertTrue(r['dsp_shutdown_verified']);self.assertFalse(r['audio_acceptance']);self.assertFalse(m.inspect(fixture())['passed']);self.assertFalse(m.inspect(o.fixture(),1)['passed'])
 def test_incomplete_execution_or_stop(self):
  for old,new in [('owned-dependencies-ready address=1','owned-dependencies-ready address=0'),('ready_seen=1','ready_seen=0'),('stop_ack_seen=1','stop_ack_seen=0'),('PAS_shutdown_allowed=1','PAS_shutdown_allowed=0'),('verified=1 target_access=0','verified=0 target_access=0'),('accepted=1','accepted=0'),('hold_us=501000','hold_us=900000'),('dsp_start_attempted=1','dsp_start_attempted=0'),('outcome=1','outcome=-1')]:self.assertFalse(m.inspect(fixture().replace(old,new),1)['passed'],old)
 def test_missing_handshake(self):
  for line in fixture().splitlines():
   if line.startswith(('marble-smp2p-boot:','marble-pil-boot:','marble-pil-progress: owned-dependencies-ready')):
    self.assertFalse(m.inspect(fixture().replace(line+'\n',''),1)['passed'],line)
if __name__=='__main__':unittest.main()
