"""Reject audio claims, unowned FIFO/notifications, missing controls and early OFF."""
import importlib.util,unittest
from pathlib import Path
BASE=Path(__file__).parent
def load(name,file):
 s=importlib.util.spec_from_file_location(name,BASE/file);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
held=load('control_held_fixture','test_marble_adsp_held_report.py')
boot=load('control_boot_fixture','test_marble_adsp_owned_boot_report.py')
smem=load('control_smem_fixture','test_marble_glink_smem_report.py')
irq=load('control_irq_fixture','test_marble_irq_observer_report.py')
s=importlib.util.spec_from_file_location('control_report',BASE.parent/'marble-glink-control-report.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
BEGIN='marble-glink-control: begin qualified_SMEM=1 local_head=0 local_tail=0 native_dispatch=0 allocator_calls=0 local_channels=0 DSP_start=0'
EVENTS='\n'.join([
 'marble-glink-control: remote item=480 address=80ad3260 bytes=16384 NC_pages=1 nonoverlap=1 allocator_calls=0',
 'marble-glink-control: tx command=0 param1=1 param2=1 local_head=8',
 'marble-glink-control: tx command=1 param1=1 param2=1 local_head=16',
 'marble-glink-control: rx command=0 param1=1 param2=1 bytes=8 local_tail=8',
 'marble-glink-control: rx command=1 param1=1 param2=1 bytes=8 local_tail=16'])
END='marble-glink-control: complete PASS=1 FIFO_seen=1 version_seen=1 version_ACK=1 RX_messages=2 TX_messages=2 remote_opens=0 GLINK_clears=1 SMP2P_clears=2 native_unchanged=1 allocator_calls=0 native_dispatch=0 local_channels=0 GPR_packets=0 PAS_stopped=1 DSP_start=1 audio_acceptance=0'
def fixture():
 text=held.fixture().replace('ROM-noexec-reset','ROM-boot-stop-reset').replace('phase=ROM-noexec','phase=ROM-boot')
 source=boot.o.smp.boot_fixture();auth=source[source.index('marble-pil-boot: auth '):source.index('marble-pil-held: shutdown ')]
 auth=auth.replace('marble-pil-boot: observed ',EVENTS+'\nmarble-pil-boot: observed ',1)
 text=text.replace('marble-pil-progress: QMP-held-on-return',BEGIN+'\nmarble-pil-progress: QMP-held-on-return',1)
 text=text.replace('marble-pil-progress: PAS-metadata-shutdown-before',auth+'marble-pil-progress: PAS-metadata-shutdown-before',1)
 text=text.replace('marble-qmp-hold: stop ','marble-smccc: resume_audit samples=0 owned_service_calls=0 USB_progress_calls=0 foreign_clears=0 allocator_calls=0 native_dispatch=0 limit_calls=128 limit_us=1000000 thermal_samples=1 thermal_ok=1 die_max_mc=41000\nmarble-qmp-hold: stop ',1)
 text=text.replace('marble-pil-progress: PAS-metadata-shutdown-return',END+'\nmarble-pil-progress: PAS-metadata-shutdown-return',1)
 text=text.replace('marble-pil-held: restore-gate ',
  'marble-glink: released PASS=1 active=0 dirty=0 up=0 down=0 invalid=0 open=0 tx=0 dsp_start_attempted=1 pmic_writes=0\nmarble-pil-held: restore-gate ')
 text=text.replace('marble-pil-held: loaded ','marble-pil-progress: owned-dependencies-ready address=1\nmarble-pil-held: loaded ',1)
 lines=[]
 for line in text.splitlines():
  if line.startswith(('marble-pil-held: restored ','marble-pil-header: complete ','marble-pil-ramfs: released ',
   'marble-npa-client: complete ','marble-npa-native-release: complete ','marble-smp2p-state: ',
   'marble-adsp-owned: complete ','marble-icb-bw: complete ','marble-smccc: PAS_stop ','marble-qmp-hold: stop ','marble-qmp-hold: complete ')):
   line=line.replace('auth_reset=0','auth_reset=1') if not line.startswith('marble-smccc:') else line
   line=line.replace('dsp_start=0','dsp_start=1').replace('DSP_start=0','DSP_start=1')
  lines.append(line)
 return irq.fixture()+'\n'+smem.LOG+'\n'+'\n'.join(lines)+'\n'
class Control(unittest.TestCase):
 def inspect(self,text):return held.scm.m.inspect(text,1,scm_peak=True,wire=True,raw_shutdown=True,held=True,control=True)
 def test_complete_control_not_audio(self):
  text=fixture();r=m.inspect(text);self.assertTrue(r['passed'],r)
  r=self.inspect(text);self.assertTrue(r['passed'],r);self.assertTrue(r['GLINK_control_link_verified']);self.assertTrue(r['dsp_shutdown_verified']);self.assertFalse(r['audio_acceptance'])
 def test_explicit_contract(self):
  self.assertFalse(held.scm.m.inspect(fixture(),1,scm_peak=True,wire=True,raw_shutdown=True,held=True)['passed'])
  self.assertFalse(self.inspect(held.fixture())['passed'])
 def test_bounds_ownership_and_no_services(self):
  for a,b in [('address=80ad3260','address=80acf070'),('version_ACK=1','version_ACK=0'),('local_head=16','local_head=24'),
    ('local_tail=16','local_tail=24'),('command=0 param1=1','command=9 param1=1'),('native_unchanged=1','native_unchanged=0'),
    ('SMP2P_clears=2','SMP2P_clears=-1'),('native_dispatch=0','native_dispatch=1'),('GPR_packets=0','GPR_packets=1'),('local_channels=0','local_channels=1')]:
   self.assertFalse(self.inspect(fixture().replace(a,b))['passed'],a)
 def test_missing_duplicate_refusal_and_early_off(self):
  text=fixture()
  for line in (BEGIN,END,*EVENTS.splitlines()):
   self.assertFalse(self.inspect(text.replace(line,''))['passed'],line);self.assertFalse(self.inspect(text+'\n'+line)['passed'],line)
  self.assertFalse(self.inspect(text+'marble-glink-control: refused step=frame\n')['passed'])
  self.assertFalse(self.inspect(text.replace('PAS_before_OFF=1','PAS_before_OFF=0'))['passed'])
 def test_qualified_interrupted_service_only(self):
  text=fixture().replace('samples=0 owned_service_calls=0','samples=1 owned_service_calls=1').replace('calls=1 outcome=0','calls=2 outcome=0')
  line='marble-smccc: interrupted index=0 before=30002 after=ffffffff service=1 USB_progress_calls=0'
  text=text.replace('marble-smccc: resume_audit ',line+'\nmarble-smccc: resume_audit ',1)
  text=text.replace(irq.fixture(),irq.fixture(True).replace('marble-smccc: interrupted index=0 before=ffffffff after=ffffffff service=0 USB_progress_calls=0\n',''))
  self.assertTrue(self.inspect(text)['passed'])
  for a,b in [('before=30002','before=40000'),('service=1','service=0'),('USB_progress_calls=0','USB_progress_calls=1'),('limit_calls=128','limit_calls=129'),('owned_service_calls=1','owned_service_calls=0'),('thermal_samples=1','thermal_samples=0'),('die_max_mc=41000','die_max_mc=55000')]:
   self.assertFalse(self.inspect(text.replace(a,b))['passed'],a)
 def test_polled_SMP2P_does_not_require_fabricated_clear(self):
  text=fixture().replace('SMP2P_clears=2','SMP2P_clears=0');r=self.inspect(text)
  self.assertTrue(r['passed'],r);self.assertFalse(r['audio_acceptance'])
  self.assertFalse(self.inspect(text.replace('stop_ack_seen=1','stop_ack_seen=0'))['passed'])
  self.assertFalse(self.inspect(text.replace('PAS_shutdown_allowed=1','PAS_shutdown_allowed=0'))['passed'])
if __name__=='__main__':unittest.main()
