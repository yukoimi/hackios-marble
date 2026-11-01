import contextlib,importlib.util,io,json,subprocess,sys,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
def load(name,path):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
m=load('i2c_report',ROOT/'tools/marble-i2c-inventory-report.py')
preview=load('i2c_preview',ROOT/'tools/marble-gpu-preview.py')
def fixture():
 base=0xf0000000
 rows=[f'marble-i2c: qualified base={base:x} table={base+0xa0d8:x} table_rva=a0d8 revision=10000 slots=5 code_rva=27f8 code_bytes=2196 code_digest=1 protocol_calls=0']
 rows += [f'marble-i2c: slot index={i} pointer={base+rva:x} rva={rva:x} invoked=0' for i,rva in enumerate((0x27f8,0x2a8c,0x2ea4,0x2b9c,0x2ff8))]
 rows += ['marble-i2c: complete PASS=1 table_unchanged=1 client_head=0 client_head_unchanged=1 protocol_calls=0 instance_selected=0 controller_qualified=0 MMIO_reads=0 MMIO_writes=0 GPIO_writes=0 clock_calls=0 DSP_start=0 storage_access=0 audio_acceptance=0']
 return '\n'.join(rows)
class Inventory(unittest.TestCase):
 def clock_fixture(self):
  names=('gcc_qupv3_wrap2_core_clk','gcc_qupv3_wrap2_core_2x_clk','gcc_qupv3_wrap_2_m_ahb_clk','gcc_qupv3_wrap_2_s_ahb_clk','gcc_qupv3_wrap2_s0_clk')
  rows=['marble-i2c-board: candidate native_bytes=88 controller=880000 common=8c0000 QUP=2 core_index=16 se_index=0 pins=56,57 function=1 bus_khz=100 timing_se_khz=19200 hardware_calls=0',
   'marble-i2c-clock: qualified base=f1000000 table=f101b0f8 table_rva=1b0f8 revision=10009 get_id_rva=14e4 code_bytes=68 code_digest=1']
  rows += [f'marble-i2c-clock: lookup index={i} name={name} status=0 id={i+1:x} clock_writes=0' for i,name in enumerate(names)]
  rows += ['marble-i2c-clock: complete PASS=1 lookups=5 table_unchanged=1 I2C_head_unchanged=1 clock_writes=0 frequency_requests=0 I2C_calls=0 GPIO_writes=0 controller_qualified=0 audio_acceptance=0']
  return fixture()+'\n'+'\n'.join(rows)
 def test_clock_lookup_requires_explicit_separate_contract(self):
  text=self.clock_fixture();r=m.inspect(text,clock=True);self.assertTrue(r['passed'],r);self.assertTrue(r['clock_names_verified']);self.assertFalse(r['controller_verified'])
  self.assertFalse(m.inspect(text)['passed']);self.assertFalse(m.inspect(fixture(),clock=True)['passed'])
  self.assertFalse(m.inspect(text,dt=True,clock=True)['passed'])
 def cycle_fixture(self):
  def state(phase,i,on=0):return f'marble-i2c-lease: state phase={phase} index={i} id={i+1:x} enabled={on} on={on} hz=19200000 status=0,0,0'
  rows=['marble-i2c-lease: qualified code_rva=1528 code_bytes=420 code_digest=1 metadata_rva=4458 metadata_bytes=148 metadata_digest=1 clocks=5 frequency_requests=0 controller_calls=0']
  rows += [state('before',i) for i in range(5)]+[state('acquire',i) for i in range(5)]
  for i in range(5):rows += [f'marble-i2c-lease: enable index={i} id={i+1:x} status=0',state('acquire',i,1)]
  rows += ['marble-i2c-lease: acquired PASS=1 held=1f uncertain=0 active=1 register_access_qualified=0']
  rows += [f'marble-i2c-lease: disable index={i} id={i+1:x} status=0' for i in range(4,-1,-1)]
  rows += [state('restore',i) for i in range(5)]
  rows += ['marble-i2c-lease: complete PASS=1 acquired=1 restored=1 table_unchanged=1 I2C_head_unchanged=1 held=0 uncertain=0 active=0 dirty=0 hold_us=1000 frequency_requests=0 frequency_measurement=0 register_access_qualified=0 I2C_calls=0 GPIO_writes=0 controller_MMIO_reads=0 controller_MMIO_writes=0 audio_acceptance=0']
  return 'marble-i2c-temperature: phase=before valid=1 maximum_mc=40000 limit_mc=50000 PASS=1\n'+self.clock_fixture()+'\n'+'\n'.join(rows[:-1])+ '\nmarble-i2c-temperature: phase=after valid=1 maximum_mc=40100 limit_mc=55000 PASS=1\n'+rows[-1]
 def test_cycle_explicit_contract_and_failure_evidence(self):
  text=self.cycle_fixture();r=m.inspect(text,clock=True,cycle=True);self.assertTrue(r['passed'],r);self.assertTrue(r['clock_lease_verified']);self.assertFalse(r['controller_verified'])
  self.assertFalse(m.inspect(text,clock=True)['passed']);self.assertFalse(m.inspect(text,cycle=True)['passed'])
  for a,b in [('hz=19200000','hz=0',),('restored=1','restored=0'),('hold_us=1000','hold_us=1000000'),('held=0','held=1'),('uncertain=0','uncertain=1'),('controller_MMIO_reads=0','controller_MMIO_reads=1'),('state phase=restore index=0 id=1 enabled=0','state phase=restore index=0 id=1 enabled=1')]:
   # Changing every frequency equally is valid; test drift only at restore.
   bad=text.replace(a,b) if a!='hz=19200000' else text.replace('phase=restore index=0 id=1 enabled=0 on=0 hz=19200000','phase=restore index=0 id=1 enabled=0 on=0 hz=0')
   self.assertFalse(m.inspect(bad,clock=True,cycle=True)['passed'],a)
  for line in text.splitlines():
   if line.startswith(('marble-i2c-lease:','marble-i2c-temperature:')):
    self.assertFalse(m.inspect(text.replace(line,''),clock=True,cycle=True)['passed']);self.assertFalse(m.inspect(text+'\n'+line,clock=True,cycle=True)['passed'])
 def test_cycle_cli_rejects_mixed_modes_before_USB(self):
  with tempfile.TemporaryDirectory() as tmp:
   out=Path(tmp)/'absent'
   for extra in ([],['--discovery-only','--i2c-clock-info'],['--discovery-only','--i2c-dt-info'],['--discovery-only','--i2c-config-snapshot'],['--discovery-only','--adsp-spf-state']):
    r=subprocess.run([sys.executable,'-X','utf8','tools/marble-gpu-preview.py','--out',str(out),'--i2c-clock-cycle',*extra],cwd=ROOT,capture_output=True,text=True,timeout=8)
    self.assertEqual(r.returncode,2,r.stdout+r.stderr);self.assertIn('I2C clock cycle requires isolated discovery',r.stderr);self.assertFalse(out.exists())
 def test_unavailable_metadata_is_explicit_and_never_frequency_measurement(self):
  text=self.cycle_fixture().replace('hz=19200000 status=0,0,0','hz=0 status=0,0,8000000000000013')
  r=m.inspect(text,clock=True,cycle=True);self.assertTrue(r['passed'],r);self.assertFalse(r['clock_frequency_measured'])
  for a,b in [('hz=0','hz=19200000'),('8000000000000013','8000000000000007'),('metadata_digest=1','metadata_digest=0'),('frequency_measurement=0','frequency_measurement=1')]:
   self.assertFalse(m.inspect(text.replace(a,b),clock=True,cycle=True)['passed'],a)
  self.assertFalse(m.inspect(text.replace('phase=restore index=0 id=1 enabled=0 on=0 hz=0 status=0,0,8000000000000013','phase=restore index=0 id=1 enabled=0 on=0 hz=0 status=0,0,0'),clock=True,cycle=True)['passed'])
 def test_hardware_gated_branch_never_qualifies_register_access(self):
  text=self.cycle_fixture().replace('enabled=1 on=1','enabled=1 on=0')
  r=m.inspect(text,clock=True,cycle=True);self.assertTrue(r['passed'],r);self.assertFalse(r['controller_verified'])
  self.assertFalse(m.inspect(text.replace('register_access_qualified=0','register_access_qualified=1'),clock=True,cycle=True)['passed'])
  self.assertFalse(m.inspect(text.replace('enabled=1 on=0','enabled=0 on=0'),clock=True,cycle=True)['passed'])
 def register_fixture(self):
  addresses=(0x8c0004,0x880040,0x880060,0x880064,0x880068,0x88006c,0x880e24,0x880e28)
  values=(0x10010000,0,0,0,0x300,0x300,0x20100000,0x20100000)
  rows=[f'marble-i2c-registers: translation index={i} address={a:x} par={a&~4095:x} identity=1' for i,a in enumerate(addresses)]
  rows += [f'marble-i2c-registers: read round={r} index={i} address={a:x} value={v:x} writes=0' for r in range(2) for i,(a,v) in enumerate(zip(addresses,values))]
  rows += ['marble-i2c-registers: complete PASS=1 snapshot_captured=1 stable=1 protocol=3 idle=1 FIFO=1 identity_qualified=1 MMIO_reads=16 MMIO_writes=0 I2C_transactions=0 GPIO_writes=0 firmware_loads=0 controller_writes=0 physical_chip_id_verified=0 audio_acceptance=0']
  text=self.cycle_fixture().replace('controller_MMIO_reads=0','controller_MMIO_reads=16')
  return text.replace('marble-i2c-lease: disable index=4 ','\n'.join(rows)+'\nmarble-i2c-lease: disable index=4 ')
 def test_passive_register_capture_separate_from_transactions(self):
  text=self.register_fixture();r=m.inspect(text,clock=True,cycle=True,registers=True)
  self.assertTrue(r['passed'],r);self.assertTrue(r['controller_identity_verified']);self.assertFalse(r['controller_verified']);self.assertFalse(r['chip_id_verified'])
  self.assertFalse(m.inspect(text,clock=True,cycle=True)['passed']);self.assertFalse(m.inspect(text,registers=True)['passed'])
  for a,b in [('address=880068','address=880780'),('par=880000','par=880001'),('MMIO_writes=0','MMIO_writes=1'),('I2C_transactions=0','I2C_transactions=1'),('physical_chip_id_verified=0','physical_chip_id_verified=1'),('value=300','value=100'),('identity_qualified=1','identity_qualified=0')]:
   self.assertFalse(m.inspect(text.replace(a,b),clock=True,cycle=True,registers=True)['passed'],a)
  for line in text.splitlines():
   if line.startswith('marble-i2c-registers:'):
    self.assertFalse(m.inspect(text.replace(line,''),clock=True,cycle=True,registers=True)['passed']);self.assertFalse(m.inspect(text+'\n'+line,clock=True,cycle=True,registers=True)['passed'])
 def test_passive_non_I2C_is_capture_only(self):
  text=self.register_fixture().replace('address=880068 value=300','address=880068 value=0').replace('protocol=3','protocol=0').replace('identity_qualified=1','identity_qualified=0')
  r=m.inspect(text,clock=True,cycle=True,registers=True);self.assertTrue(r['passed'],r);self.assertTrue(r['passive_snapshot_captured']);self.assertFalse(r['controller_identity_verified'])
 def test_register_cli_rejects_mixed_modes_before_USB(self):
  with tempfile.TemporaryDirectory() as tmp:
   out=Path(tmp)/'absent'
   for extra in ([],['--discovery-only','--i2c-clock-cycle'],['--discovery-only','--i2c-dt-info'],['--discovery-only','--adsp-spf-state']):
    r=subprocess.run([sys.executable,'-X','utf8','tools/marble-gpu-preview.py','--out',str(out),'--i2c-register-info',*extra],cwd=ROOT,capture_output=True,text=True,timeout=8)
    self.assertEqual(r.returncode,2,r.stdout+r.stderr);self.assertIn('I2C register inventory requires isolated discovery',r.stderr);self.assertFalse(out.exists())
 def route_fixture(self):
  addresses=m.ROUTE_ADDRESSES
  rows=[f'marble-i2c-route: translation index={i} address={a:x} par={a&~4095:x} identity=1' for i,a in enumerate(addresses)]
  rows += [f'marble-i2c-route: read round={r} index={i} address={a:x} value=0 mask={2 if i>=31 and not i&1 else 0xffffffff:x} writes=0' for r in range(2) for i,a in enumerate(addresses)]
  rows += ['marble-i2c-route: complete PASS=1 words=41 snapshots=2 MMIO_reads=82 MMIO_writes=0 I2C_transactions=0 GPIO_writes=0 clock_frequency_requests=0 bus_ready=0 physical_chip_id_verified=0 audio_acceptance=0']
  text=self.register_fixture().replace('controller_MMIO_reads=16','controller_MMIO_reads=98')
  return text.replace('marble-i2c-lease: disable index=4 ','\n'.join(rows)+'\nmarble-i2c-lease: disable index=4 ')
 def test_route_contract_bounds_order_masks_and_no_chip_claim(self):
  text=self.route_fixture();r=m.inspect(text,clock=True,cycle=True,registers=True,route=True);self.assertTrue(r['passed'],r);self.assertTrue(r['route_snapshot_captured']);self.assertEqual(len(r['route_values']),41);self.assertFalse(r['chip_id_verified'])
  self.assertFalse(m.inspect(text,clock=True,cycle=True,registers=True)['passed'])
  for a,b in [('address=880048','address=880700'),('bus_ready=0','bus_ready=1'),('words=41','words=40'),('mask=2 ','mask=ffffffff '),('controller_MMIO_reads=98','controller_MMIO_reads=16')]:
   self.assertFalse(m.inspect(text.replace(a,b),clock=True,cycle=True,registers=True,route=True)['passed'],a)
  for line in text.splitlines():
   if line.startswith('marble-i2c-route:'):
    self.assertFalse(m.inspect(text.replace(line,''),clock=True,cycle=True,registers=True,route=True)['passed']);self.assertFalse(m.inspect(text+'\n'+line,clock=True,cycle=True,registers=True,route=True)['passed'])
  good=text.replace('round=1 index=32 address=f138004 value=0','round=1 index=32 address=f138004 value=1')
  self.assertTrue(m.inspect(good,clock=True,cycle=True,registers=True,route=True)['passed'])
  bad=text.replace('round=1 index=32 address=f138004 value=0','round=1 index=32 address=f138004 value=2')
  self.assertFalse(m.inspect(bad,clock=True,cycle=True,registers=True,route=True)['passed'])
 def test_route_cli_rejects_mixed_modes_before_USB(self):
  with tempfile.TemporaryDirectory() as tmp:
   out=Path(tmp)/'absent'
   for extra in ([],['--discovery-only','--i2c-register-info'],['--discovery-only','--i2c-clock-cycle'],['--discovery-only','--adsp-spf-state']):
    r=subprocess.run([sys.executable,'-X','utf8','tools/marble-gpu-preview.py','--out',str(out),'--i2c-route-info',*extra],cwd=ROOT,capture_output=True,text=True,timeout=8)
    self.assertEqual(r.returncode,2,r.stdout+r.stderr);self.assertIn('I2C route inventory requires isolated discovery',r.stderr);self.assertFalse(out.exists())
 def setup_fixture(self):
  text=self.route_fixture();values={a:0 for a in m.ROUTE_ADDRESSES}
  values.update({0x880048:0x11,0x880e1c:15,0x12e018:0x100000})
  for a in (0xf138000,0xf139000,0xf101000,0xf178000,0xf134000):values[a]=1
  for i,address in enumerate(m.ROUTE_ADDRESSES):
   text=text.replace(f'index={i} address={address:x} value=0 ',f'index={i} address={address:x} value={values[address]:x} ')
  originals=[values.get(a,0) for a in m.SETUP_ADDRESSES]
  changed=[i for i,(a,b) in enumerate(zip(originals,m.SETUP_VALUES)) if a!=b]
  rows=[f'marble-i2c-setup: saved index={i} address={a:x} before={b:x} desired={d:x}' for i,(a,b,d) in enumerate(zip(m.SETUP_ADDRESSES,originals,m.SETUP_VALUES))]
  rows += [f'marble-i2c-setup: write phase=apply address={m.SETUP_ADDRESSES[i]:x} value={m.SETUP_VALUES[i]:x} observed={m.SETUP_VALUES[i]:x}' for i in changed]
  rows += [f'marble-i2c-setup: applied PASS=1 changed={sum(1<<i for i in changed):x} active=1 IOS=3 status=0 I2C_transactions=0']
  rows += [f'marble-i2c-setup: write phase=restore address={m.SETUP_ADDRESSES[i]:x} value={originals[i]:x} observed={originals[i]:x}' for i in reversed(changed)]
  rows += [f'marble-i2c-setup: preserved index={i} value={values[a]:x} mask={2 if i>=31 and not i&1 else 0xffffffff:x}' for i,a in enumerate(m.ROUTE_ADDRESSES)]
  rows += [f'marble-i2c-setup: complete PASS=1 configured=1 restored=1 route_preserved=1 held=0 active=0 dirty=0 writes={2*len(changed)} bus_khz=100 I2C_transactions=0 reset_writes=0 amplifier_register_writes=0 firmware_loads=0 frequency_requests=0 audio_acceptance=0']
  gpio=2*sum(m.SETUP_ADDRESSES[i]>=0xf100000 for i in changed)
  text=text.replace('GPIO_writes=0 controller_MMIO_reads=98 controller_MMIO_writes=0',f'GPIO_writes={gpio} controller_MMIO_reads=98 controller_MMIO_writes={2*len(changed)-gpio}')
  return text.replace('marble-i2c-lease: disable index=4 ','\n'.join(rows)+'\nmarble-i2c-lease: disable index=4 ')
 def test_setup_restoration_and_readback_contract(self):
  text=self.setup_fixture();options=dict(clock=True,cycle=True,registers=True,route=True,setup=True)
  r=m.inspect(text,**options);self.assertTrue(r['passed'],r);self.assertTrue(r['bus_configuration_verified']);self.assertFalse(r['chip_id_verified']);self.assertFalse(r['audio_acceptance'])
  self.assertFalse(m.inspect(text,clock=True,cycle=True,registers=True,route=True)['passed'])
  for a,b in [('observed=71','observed=11'),('IOS=3','IOS=1'),('status=0 I2C_transactions=0','status=1 I2C_transactions=0'),('reset_writes=0','reset_writes=1'),('route_preserved=1','route_preserved=0'),('bus_khz=100','bus_khz=1000'),('before=11 desired=71','before=10 desired=71'),('GPIO_writes=4 controller_MMIO_reads=98','GPIO_writes=0 controller_MMIO_reads=98')]:
   self.assertFalse(m.inspect(text.replace(a,b),**options)['passed'],a)
  for line in text.splitlines():
   if line.startswith('marble-i2c-setup:'):
    self.assertFalse(m.inspect(text.replace(line,''),**options)['passed']);self.assertFalse(m.inspect(text+'\n'+line,**options)['passed'])
 def test_setup_cli_rejects_mixed_modes_before_USB(self):
  with tempfile.TemporaryDirectory() as tmp:
   out=Path(tmp)/'absent'
   for extra in ([],['--discovery-only','--i2c-route-info'],['--discovery-only','--i2c-register-info'],['--discovery-only','--adsp-spf-state']):
    r=subprocess.run([sys.executable,'-X','utf8','tools/marble-gpu-preview.py','--out',str(out),'--i2c-setup-cycle',*extra],cwd=ROOT,capture_output=True,text=True,timeout=8)
    self.assertEqual(r.returncode,2,r.stdout+r.stderr);self.assertIn('I2C setup requires isolated discovery',r.stderr);self.assertFalse(out.exists())
 def amp_fixture(self):
  text=self.setup_fixture();rv=m.inspect(text,clock=True,cycle=True,registers=True,route=True,setup=True)['route_values'];aux=(0x88026c,0x880270,0x880614,0x88080c,0x880810,0x880814);desired=(1,2,0x7fc0347f,0,13,14)
  rows=[];writes=[]
  def put(device,phase,address,value):
   line=f'marble-i2c-amp: write address={address:x} value={value:x} phase={phase} device={device:x}';rows.append(line);writes.append((address,value))
  for device,gpio in ((0x34,0xf101000),(0x35,0xf178000)):
   before=[int(rv[f'{a:x}'],16) for a in aux];changed=[i for i in range(6) if before[i]!=desired[i]]
   rows += [f'marble-i2c-amp: saved device={device:x} index={i} address={a:x} value={v:x} desired={d:x}' for i,(a,v,d) in enumerate(zip(aux,before,desired))]
   rows += [f'marble-i2c-amp: reset-saved device={device:x} gpio={gpio:x} cfg=1 io=0']
   for i in changed:put(device,'prepare',aux[i],desired[i])
   put(device,'reset-output',gpio,0x201);put(device,'reset-release',gpio+4,2)
   for a,v in ((0x88026c,1),(0x880600,(1<<27)|(device<<9)|4),(0x88080c,1),(0x880700,0),(0x88080c,0),(0x880618,1<<30),(0x880618,1),(0x880270,2),(0x880600,(2<<27)|(device<<9)),(0x880648,0xc000000),(0x880618,0x8000001)):put(device,'transfer',a,v)
   rows += [f'marble-i2c-amp: transfer device={device:x} PASS=1 attempted=1 commands=2 tx_words=1 rx_words=1 raw_rx=3220 chip_id=2032 polls=3 aborts=0 dirty=0 elapsed_us=10 IRQ_seen=48000001 IRQ_last=8000001 S_IRQ_seen=c000000 S_IRQ_last=c000000 idle_IRQ=80 idle_IRQ_enable=7fc0347f']
   put(device,'reset-assert',gpio+4,0);put(device,'reset-restore',gpio,1)
   for i in reversed(changed):put(device,'restore',aux[i],before[i])
   rows += [f'marble-i2c-amp: complete device={device:x} PASS=1 chip_id=2032 reset_restored=1 bus_clean=1 aux_restored=1 dirty=0 logical_quiesce=1 raw_IRQ_restored=0 post_IRQ=80 post_IRQ_enable=0 cold_reset_required=1 amplifier_register_writes=0 speaker_switch_writes=0 audio_acceptance=0']
  text=text.replace('marble-i2c-setup: write phase=restore ','\n'.join(rows)+'\nmarble-i2c-setup: write phase=restore ',1)
  text=text.replace('preserved index=11 value=0 mask=ffffffff','preserved index=11 value=10006a00 mask=0').replace('preserved index=12 value=0 mask=ffffffff','preserved index=12 value=80 mask=ffffff7f')
  text=text.replace('bus_khz=100 I2C_transactions=0 reset_writes=0','bus_khz=100 I2C_transactions=4 reset_writes=8')
  gpio=sum(a>=0xf100000 for a,_ in writes)
  import re
  text=re.sub(r'GPIO_writes=(\d+) controller_MMIO_reads=98 controller_MMIO_writes=(\d+)',lambda x:f'GPIO_writes={int(x[1])+gpio} controller_MMIO_reads=98 controller_MMIO_writes={int(x[2])+len(writes)-gpio}',text)
  return text
 def amp_register_fixture(self):
  import re
  text=self.amp_fixture();extra=0
  for device,gpio in ((0x34,0xf101000),(0x35,0xf178000)):
   rows=[]
   for i,reg in enumerate((1,3,4,5,6,7,8,0x66,0x78,0x7a)):
    for a,v in ((0x88026c,1),(0x880600,(1<<27)|(device<<9)|4),(0x88080c,1),(0x880700,reg),(0x88080c,0),(0x880618,1),(0x880270,2),(0x880600,(2<<27)|(device<<9)),(0x880648,0xc000000),(0x880618,0x8000001)):
     rows.append(f'marble-i2c-amp: write address={a:x} value={v:x} phase=register-read device={device:x}');extra+=1
    value=0x4003 if reg==4 else 0x38 if reg==5 else 0
    rows.append(f'marble-aw-reg: read device={device:x} index={i} reg={reg:x} PASS=1 value={value:x} commands=2 tx_words=1 rx_words=1 aborts=0 dirty=0 elapsed_us=10')
   rows.append(f'marble-aw-reg: complete device={device:x} PASS=1 reads=10 powered_down=1 muted=1 amplifier_register_writes=0 speaker_switch_writes=0 audio_acceptance=0')
   anchor=f'marble-i2c-amp: write address={gpio+4:x} value=0 phase=reset-assert device={device:x}'
   text=text.replace(anchor,'\n'.join(rows)+'\n'+anchor,1)
  text=text.replace('I2C_transactions=4 reset_writes=8','I2C_transactions=44 reset_writes=8')
  text=re.sub(r'controller_MMIO_reads=98 controller_MMIO_writes=(\d+)',lambda x:f'controller_MMIO_reads=98 controller_MMIO_writes={int(x[1])+extra}',text)
  return text
 def test_amp_registers_full_lifecycle_and_mutations(self):
  text=self.amp_register_fixture();options=dict(clock=True,cycle=True,registers=True,route=True,setup=True,amp=True,amp_registers=True)
  r=m.inspect(text,**options);self.assertTrue(r['passed'],r);self.assertTrue(r['amplifier_reset_registers_verified']);self.assertFalse(r['audio_acceptance'])
  for a,b in [('reg=4 PASS=1 value=4003','reg=4 PASS=1 value=4000'),('reg=5 PASS=1 value=38','reg=5 PASS=1 value=28'),('value=1 phase=register-read','value=2 phase=register-read'),('I2C_transactions=44','I2C_transactions=4'),('reads=10','reads=9')]:
   self.assertFalse(m.inspect(text.replace(a,b),**options)['passed'],a)
  for line in text.splitlines():
   if line.startswith('marble-aw-reg:'):
    self.assertFalse(m.inspect(text.replace(line,''),**options)['passed']);self.assertFalse(m.inspect(text+'\n'+line,**options)['passed'])
  options.pop('amp_registers');self.assertFalse(m.inspect(text,**options)['passed'])
 def test_amp_registers_controller_and_recovery(self):
  code,r=self.simulate(amp_registers=True);self.assertEqual(code,0);self.assertTrue(r['i2c_inventory_with_reset_passed']);self.assertTrue(r['i2c_inventory']['amplifier_reset_registers_verified'])
  code,r=self.simulate(amp_registers=True,bad_log=True);self.assertEqual(code,1);self.assertFalse(r['i2c_inventory_with_reset_passed'])
 def amp_quiet_fixture(self):
  import re
  text=self.amp_register_fixture();extra=0
  for device,gpio in ((0x34,0xf101000),(0x35,0xf178000)):
   rows=[]
   def put(phase,a,v):
    nonlocal extra
    rows.append(f'marble-i2c-amp: write address={a:x} value={v:x} phase={phase} device={device:x}');extra+=1
   for a,v in ((0x88026c,3),(0x880600,(1<<27)|(device<<9)),(0x88080c,1),(0x880700,0x38f005),(0x88080c,0),(0x880618,1)):
    put('quiet-write',a,v)
   for i,reg in enumerate((4,5)):
    for a,v in ((0x88026c,1),(0x880600,(1<<27)|(device<<9)|4),(0x88080c,1),(0x880700,reg),(0x88080c,0),(0x880618,1),(0x880270,2),(0x880600,(2<<27)|(device<<9)),(0x880648,0xc000000),(0x880618,0x8000001)):
     put('quiet-read',a,v)
    value=0x4003 if reg==4 else 0xf038
    rows.append(f'marble-aw-quiet: read device={device:x} index={i} reg={reg:x} PASS=1 value={value:x} commands=2 tx_words=1 rx_words=1 aborts=0 dirty=0 elapsed_us=10')
   rows.append(f'marble-aw-quiet: complete device={device:x} PASS=1 before=38 target=f038 after=f038 sysctrl=4003 readbacks=2 commands=1 tx_words=1 rx_words=0 aborts=0 dirty=0 elapsed_us=10 amplifier_register_writes=1 speaker_switch_writes=0 audio_acceptance=0')
   anchor=f'marble-i2c-amp: write address={gpio+4:x} value=0 phase=reset-assert device={device:x}'
   text=text.replace(anchor,'\n'.join(rows)+'\n'+anchor,1)
  text=re.sub(r'(marble-i2c-amp: complete [^\n]*amplifier_register_writes=)0',r'\g<1>1',text)
  text=text.replace('I2C_transactions=44 reset_writes=8 amplifier_register_writes=0','I2C_transactions=54 reset_writes=8 amplifier_register_writes=2')
  text=re.sub(r'controller_MMIO_reads=98 controller_MMIO_writes=(\d+)',lambda x:f'controller_MMIO_reads=98 controller_MMIO_writes={int(x[1])+extra}',text)
  return text
 def test_amp_quiet_lifecycle(self):
  text=self.amp_quiet_fixture();options=dict(clock=True,cycle=True,registers=True,route=True,setup=True,amp=True,amp_registers=True,amp_quiet=True)
  r=m.inspect(text,**options);self.assertTrue(r['passed'],r);self.assertTrue(r['amplifier_quiet_write_verified']);self.assertFalse(r['audio_acceptance'])
  for a,b in [('value=38f005','value=380005'),('value=f038 commands=','value=38 commands='),('sysctrl=4003','sysctrl=4000'),('amplifier_register_writes=2','amplifier_register_writes=0'),('I2C_transactions=54','I2C_transactions=44')]:
   self.assertFalse(m.inspect(text.replace(a,b),**options)['passed'],a)
  for line in text.splitlines():
   if line.startswith('marble-aw-quiet:'):
    self.assertFalse(m.inspect(text.replace(line,''),**options)['passed']);self.assertFalse(m.inspect(text+'\n'+line,**options)['passed'])
  options.pop('amp_quiet');self.assertFalse(m.inspect(text,**options)['passed'])
 def test_amp_quiet_controller_and_recovery(self):
  code,r=self.simulate(amp_quiet=True);self.assertEqual(code,0);self.assertTrue(r['i2c_inventory_with_reset_passed']);self.assertTrue(r['i2c_inventory']['amplifier_quiet_write_verified'])
  code,r=self.simulate(amp_quiet=True,bad_log=True);self.assertEqual(code,1);self.assertFalse(r['i2c_inventory_with_reset_passed'])
 def amp_profile_fixture(self):
  import re
  profile=load('test_aw_profile',ROOT/'tools/marble-aw-profile.py');board=profile.qualify()
  text=self.amp_quiet_fixture();extra=0
  for device,gpio in ((0x34,0xf101000),(0x35,0xf178000)):
   rows=[]
   def put(phase,a,v):
    nonlocal extra
    rows.append(f'marble-i2c-amp: write address={a:x} value={v:x} phase={phase} device={device:x}');extra+=1
   pairs=[(r['address'],profile.target(r['address'],r['value'],0x4003,0x38)) for r in board['devices'][device-0x34]['registers']]+[(0x56,8192)]
   for i,(reg,value) in enumerate(pairs):
    packed=reg|((value>>8)<<8)|((value&255)<<16)
    for a,v in ((0x88026c,3),(0x880600,(1<<27)|(device<<9)),(0x88080c,1),(0x880700,packed),(0x88080c,0),(0x880618,1)):put('profile-write',a,v)
    for a,v in ((0x88026c,1),(0x880600,(1<<27)|(device<<9)|4),(0x88080c,1),(0x880700,reg),(0x88080c,0),(0x880618,1),(0x880270,2),(0x880600,(2<<27)|(device<<9)),(0x880648,0xc000000),(0x880618,0x8000001)):put('profile-read',a,v)
    rows.append(f'marble-aw-profile: word device={device:x} index={i} reg={reg:x} PASS=1 target={value:x} observed={value:x} write_commands=1 read_commands=2 tx_words=2 rx_words=1 aborts=0 dirty=0 elapsed_us=10')
   rows.append(f'marble-aw-profile: complete device={device:x} PASS=1 words=45 efrh=0 efrm1=0 vcalb=2000 amplifier_register_writes=45 power_up=0 speaker_switch_writes=0 audio_acceptance=0')
   anchor=f'marble-i2c-amp: write address={gpio+4:x} value=0 phase=reset-assert device={device:x}'
   text=text.replace(anchor,'\n'.join(rows)+'\n'+anchor,1)
  text=re.sub(r'(marble-i2c-amp: complete [^\n]*amplifier_register_writes=)1',r'\g<1>46',text)
  text=text.replace('I2C_transactions=54 reset_writes=8 amplifier_register_writes=2','I2C_transactions=324 reset_writes=8 amplifier_register_writes=92')
  text=re.sub(r'controller_MMIO_reads=98 controller_MMIO_writes=(\d+)',lambda x:f'controller_MMIO_reads=98 controller_MMIO_writes={int(x[1])+extra}',text)
  return text
 def test_amp_profile_lifecycle(self):
  text=self.amp_profile_fixture();options=dict(clock=True,cycle=True,registers=True,route=True,setup=True,amp=True,amp_registers=True,amp_quiet=True,amp_profile=True)
  r=m.inspect(text,**options);self.assertTrue(r['passed'],r);self.assertTrue(r['amplifier_Music_profile_verified']);self.assertFalse(r['audio_acceptance'])
  for a,b in [('target=b443 observed=b443','target=b440 observed=b440'),('target=f039 observed=f039','target=5a9 observed=5a9'),('vcalb=2000','vcalb=2001'),('words=45','words=44'),('I2C_transactions=324','I2C_transactions=54'),('power_up=0','power_up=1')]:
   self.assertFalse(m.inspect(text.replace(a,b),**options)['passed'],a)
  line=next(l for l in text.splitlines() if l.startswith('marble-aw-profile: word '))
  self.assertFalse(m.inspect(text.replace(line,''),**options)['passed']);self.assertFalse(m.inspect(text+'\n'+line,**options)['passed'])
  options.pop('amp_profile');self.assertFalse(m.inspect(text,**options)['passed'])
 def test_amp_profile_controller_and_recovery(self):
  code,r=self.simulate(amp_profile=True);self.assertEqual(code,0);self.assertTrue(r['i2c_inventory_with_reset_passed']);self.assertTrue(r['i2c_inventory']['amplifier_Music_profile_verified'])
  code,r=self.simulate(amp_profile=True,bad_log=True);self.assertEqual(code,1);self.assertFalse(r['i2c_inventory_with_reset_passed'])
 def audio_i2c_fixture(self):
  pins=load('audio_i2c_pins',ROOT/'tools/tests/test_marble_audio_pins_report.py')
  text=self.setup_fixture()
  marker='marble-i2c-setup: write phase=restore '
  work='ai: work enter=1 configuration_active=1 clocks_active=1 amp_commands=0\n'+pins.fixture()+'\nai: work returned=1 DSP_dirty=0 amp_commands=0\n'
  text=text.replace(marker,work+marker,1)
  return 'ai: begin DSP_start=0 amps_reset=1 frequency_requests=0\n'+text+'\nai: end PASS=1 called=1 DSP_passed=1 DSP_dirty=0 config_active=0 clock_active=0 dirty=0 elapsed_us=800000 amp_commands=0 cold_reset_required=1 audio_acceptance=0\n'
 def test_audio_i2c_hold_native_contract_and_DSP_ordering(self):
  text=self.audio_i2c_fixture();r=m.inspect_audio_hold(text);self.assertTrue(r['passed'],r);self.assertFalse(r['audio_acceptance'])
  for a,b in [('clocks_active=1','clocks_active=0'),('DSP_dirty=0','DSP_dirty=1'),('config_active=0','config_active=1'),('elapsed_us=800000','elapsed_us=30000000'),('route_preserved=1','route_preserved=0'),('frequency_requests=0','frequency_requests=1')]:
   self.assertFalse(m.inspect_audio_hold(text.replace(a,b))['passed'],a)
  for line in text.splitlines():
   if line.startswith('ai:'):
    self.assertFalse(m.inspect_audio_hold(text.replace(line,''))['passed']);self.assertFalse(m.inspect_audio_hold(text+'\n'+line)['passed'])
 def test_audio_i2c_cli_refuses_missing_pins_before_USB(self):
  with tempfile.TemporaryDirectory() as tmp:
   out=Path(tmp)/'live';r=subprocess.run([sys.executable,'-X','utf8','tools/marble-gpu-preview.py','--out',str(out),'--discovery-only','--audio-i2c-hold'],cwd=ROOT,capture_output=True,text=True,timeout=8)
   self.assertEqual(r.returncode,2,r.stdout+r.stderr);self.assertIn('--audio-i2c-hold requires --tdm-pins-cycle',r.stderr);self.assertFalse(out.exists())
 def audio_i2c_window_fixture(self,profile=False):
  pins=load('audio_i2c_window_pins',ROOT/'tools/tests/test_marble_audio_pins_report.py')
  i2c=self.amp_profile_fixture() if profile else self.setup_fixture();at=i2c.index('marble-i2c-board: candidate ');clock_at=i2c.index('marble-i2c-lease: qualified ');prefix=i2c[:at];clock=i2c[at:clock_at];body=i2c[clock_at:]
  pcm=pins.fixture();marker='tp: acquired PASS=1 active=1 writes=6\n'
  window='aw: window begin stage=3 pins_active=1 amp_commands=0\n'+clock+'aw: lookup PASS=1 after_START=1 amplifier_commands=0\naw: temperature valid=1 maximum_mc=40000 limit_mc=50000 PASS=1\n'+body+'\naw: window end PASS=1 config_active=0 clock_active=0 amp_commands=0\n'
  pcm=pcm.replace(marker,marker+window,1)
  text='aw: begin DSP_start=0 amps_reset=1 frequency_requests=0\n'+prefix+pcm+'\naw: end PASS=1 called=1 window_passed=1 DSP_passed=1 DSP_dirty=0 config_active=0 clock_active=0 dirty=0 elapsed_us=800000 amp_commands=0 cold_reset_required=1 audio_acceptance=0\n'
  if profile:text=text.replace('clock_active=0 amp_commands=0','clock_active=0 amp_commands=324').replace('elapsed_us=800000 amp_commands=0','elapsed_us=800000 amp_commands=324')
  return text
 def test_audio_amp_profile_after_START_and_reset_before_release(self):
  text=self.audio_i2c_window_fixture(profile=True);r=m.inspect_audio_window(text,amp_profile=True)
  self.assertTrue(r['passed'],r);self.assertTrue(r['amplifier_Music_profile_verified']);self.assertFalse(r['audio_acceptance'])
  self.assertFalse(m.inspect_audio_window(text)['passed'])
  self.assertFalse(m.inspect_audio_window(self.audio_i2c_window_fixture(),amp_profile=True)['passed'])
  for a,b in [('amp_commands=324','amp_commands=0'),('power_up=0','power_up=1'),('reset_restored=1','reset_restored=0'),('window_passed=1','window_passed=0')]:
   self.assertFalse(m.inspect_audio_window(text.replace(a,b),amp_profile=True)['passed'],a)
 def test_audio_amp_profile_prestart_order(self):
  text=self.audio_i2c_window_fixture(profile=True)
  begin=text.index('aw: window begin ');end=text.index('\n',text.index('aw: window end '))+1
  window=text[begin:end].replace('stage=3 pins_active=1','stage=2 pins_active=0').replace('after_START=1','after_START=0')
  text=text[:begin]+text[end:];at=text.index('tr: q s=2 ');text=text[:at]+window+text[at:]
  r=m.inspect_audio_window(text,amp_profile=True,prestart=True);self.assertTrue(r['passed'],r)
  self.assertTrue(r['qualified_I2C_window_before_START']);self.assertFalse(r['qualified_I2C_window_after_START'])
  self.assertFalse(m.inspect_audio_window(text,amp_profile=True)['passed'])
  self.assertFalse(m.inspect_audio_window(text,prestart=True)['passed'])
  self.assertFalse(m.inspect_audio_window(self.audio_i2c_window_fixture(profile=True),amp_profile=True,prestart=True)['passed'])
 def test_audio_amp_profile_preboot_order(self):
  text=self.audio_i2c_window_fixture(profile=True)
  begin=text.index('aw: window begin ');end=text.index('\n',text.index('aw: window end '))+1
  window=text[begin:end].replace('stage=3 pins_active=1','stage=1 pins_active=0').replace('after_START=1','after_START=0')
  text=text[:begin]+text[end:];at=text.index('marble-adsp-owned: phase=bandwidth ');text=text[:at]+window+text[at:]
  r=m.inspect_audio_window(text,amp_profile=True,prestart=True,preboot=True);self.assertTrue(r['passed'],r)
  self.assertFalse(m.inspect_audio_window(text,amp_profile=True,prestart=True)['passed'])
  self.assertFalse(m.inspect_audio_window(text,amp_profile=True,preboot=True)['passed'])
 def test_extra_QUP_refs_after_START_until_PAS(self):
  import re
  text=self.audio_i2c_window_fixture(profile=True)
  state=[l.replace('marble-i2c-lease:','ah:') for l in text.splitlines() if l.startswith('marble-i2c-lease: state ')]
  enables=[l.replace('marble-i2c-lease:','ah:') for l in text.splitlines() if l.startswith('marble-i2c-lease: enable ')]
  disables=[l.replace('marble-i2c-lease:','ah:') for l in text.splitlines() if l.startswith('marble-i2c-lease: disable ')]
  for j,old in enumerate([l for l in text.splitlines() if l.startswith('marble-i2c-lease: state ')]):
   target=state[10+j%5].replace('ah:','marble-i2c-lease:')
   phase=re.search('phase=(\w+)',old)[1];target=re.sub('phase=\w+','phase='+phase,target)
   text=text.replace(old,target,1)
  acquire='\n'.join(state[:10]+enables+state[10:15]+['ah: begin PASS=1 held=1f uncertain=0 active=1 dirty=0 after_START=1 release_after_PAS=1 frequency_requests=0 voltage_requests=0'])+'\n'
  release='\n'.join(disables+state[15:]+['ah: end PASS=1 restored=1 table_unchanged=1 I2C_head_unchanged=1 held=0 uncertain=0 active=0 dirty=0 PAS_stopped=1 frequency_requests=0 voltage_requests=0'])+'\n'
  at=text.index('marble-i2c-lease: qualified ');text=text[:at]+acquire+text[at:]
  at=text.index('marble-adsp-owned: complete PASS=1');text=text[:at]+release+text[at:]
  r=m.inspect_audio_window(text,amp_profile=True,qup_hold=True);self.assertTrue(r['passed'],r)
  self.assertFalse(m.inspect_audio_window(text,amp_profile=True)['passed'])
  for a,b in [('release_after_PAS=1','release_after_PAS=0'),('ah: end PASS=1','ah: end PASS=0'),('ah: disable index=4','ah: disable index=0')]:
   self.assertFalse(m.inspect_audio_window(text.replace(a,b),amp_profile=True,qup_hold=True)['passed'],a)
  at=text.index('ah: disable index=4 ');end=text.index('\n',text.index('ah: end '))+1
  release=text[at:end];bad=text[:at]+text[end:];at=bad.index('pd: q ');bad=bad[:at]+release+bad[at:]
  self.assertFalse(m.inspect_audio_window(bad,amp_profile=True,qup_hold=True)['passed'])
 def test_actual_runtime_deadline(self):
  text=self.audio_i2c_window_fixture(profile=True)
  text=text.replace('service_acceptance=0 audio_acceptance=0','service_acceptance=0 audio_acceptance=0 runtime_ok=1')
  line='aw: runtime deadline_us=500000 poll_rounds=50 initial_service_cadence_us=100000 thermal_samples=5 includes_I2C_work=1 IRQ_masks_unchanged=1 frequency_requests=0\n'
  at=text.index('marble-smp2p-boot: ready ');text=text[:at]+line+text[at:]
  self.assertTrue(m.inspect_audio_window(text,amp_profile=True,runtime_deadline=True)['passed'])
  self.assertFalse(m.inspect_audio_window(text,amp_profile=True)['passed'])
  for a,b in [('deadline_us=500000','deadline_us=1000000'),('poll_rounds=50','poll_rounds=10'),('includes_I2C_work=1','includes_I2C_work=0'),('IRQ_masks_unchanged=1','IRQ_masks_unchanged=0')]:
   self.assertFalse(m.inspect_audio_window(text.replace(a,b),amp_profile=True,runtime_deadline=True)['passed'],a)
 def digital_fixture(self):
  import re
  text=self.audio_i2c_window_fixture(profile=True);extra=0;commands=0
  for device,gpio in ((0x34,0xf101004),(0x35,0xf178004)):
   down=0xb443 if device==0x34 else 0x3443
   accesses=[(1,4,down),(1,5,0xf039),(0,4,down&~1),(1,4,down&~1),(1,5,0xf039),(1,1,0x11),(0,4,down),(1,4,down),(1,5,0xf039)]
   rows=[]
   for read,reg,value in accesses:
    phase='digital-read' if read else 'digital-write'
    if read:ops=[(0x88026c,1),(0x880600,(1<<27)|(device<<9)|4),(0x88080c,1),(0x880700,reg),(0x88080c,0),(0x880270,2),(0x880600,(2<<27)|(device<<9))]
    else:ops=[(0x88026c,3),(0x880600,(1<<27)|(device<<9)),(0x88080c,1),(0x880700,reg|((value>>8)<<8)|((value&255)<<16)),(0x88080c,0)]
    for a,v in ops:rows.append(f'marble-i2c-amp: write address={a:x} value={v:x} phase={phase} device={device:x}')
    extra+=len(ops);commands+=2 if read else 1
    rows.append(f'marble-aw-digital: access device={device:x} read={read} reg={reg:x} value={value:x} PASS=1 commands={2 if read else 1} dirty=0')
   rows.append(f'marble-aw-digital: complete device={device:x} PASS=1 attempted=1 writes=2 polls=1 locked=1 restored=1 dirty=0 sysctrl={down:x} sysctrl2=f039 status=11 AMPPD_retained=1 HMUTE_retained=1 attenuation_db=90 speaker_switch_writes=0 audio_acceptance=0')
   anchor=f'marble-i2c-amp: write address={gpio:x} value=0 phase=reset-assert device={device:x}'
   text=text.replace(anchor,'\n'.join(rows)+'\n'+anchor,1)
  text=re.sub(r'(marble-i2c-amp: complete [^\n]*amplifier_register_writes=)46',r'\g<1>48',text)
  text=text.replace('I2C_transactions=324','I2C_transactions='+str(324+commands)).replace('amplifier_register_writes=92','amplifier_register_writes=96').replace('amp_commands=324','amp_commands='+str(324+commands))
  text=re.sub(r'controller_MMIO_reads=98 controller_MMIO_writes=(\d+)',lambda r:'controller_MMIO_reads=98 controller_MMIO_writes='+str(int(r[1])+extra),text)
  text=text.replace('service_acceptance=0 audio_acceptance=0','service_acceptance=0 audio_acceptance=0 runtime_ok=1')
  at=text.index('marble-smp2p-boot: ready ');text=text[:at]+'aw: runtime deadline_us=500000 poll_rounds=50 initial_service_cadence_us=100000 thermal_samples=5 includes_I2C_work=1 IRQ_masks_unchanged=1 frequency_requests=0\n'+text[at:]
  return text
 def test_digital_PLL_bounded_writes_restoration_and_reset(self):
  text=self.digital_fixture();r=m.inspect_audio_window(text,amp_profile=True,runtime_deadline=True,digital_pll=True)
  self.assertTrue(r['passed'],r);self.assertTrue(r['amplifier_digital_PLL_verified']);self.assertEqual(r['amplifier_commands'],356)
  self.assertFalse(m.inspect_audio_window(text,amp_profile=True,runtime_deadline=True)['passed'])
  for a,b in [('AMPPD_retained=1','AMPPD_retained=0'),('locked=1','locked=0'),('sysctrl2=f039','sysctrl2=39'),('status=11','status=13'),('polls=1','polls=11'),('I2C_transactions=356','I2C_transactions=324'),('amp_commands=356','amp_commands=324'),('amplifier_register_writes=96','amplifier_register_writes=92')]:
   self.assertFalse(m.inspect_audio_window(text.replace(a,b),amp_profile=True,runtime_deadline=True,digital_pll=True)['passed'],a)
 def test_audio_i2c_window_native_contract_and_START_restore_ordering(self):
  text=self.audio_i2c_window_fixture();r=m.inspect_audio_window(text);self.assertTrue(r['passed'],r)
  for a,b in [('pins_active=1','pins_active=0'),('window_passed=1','window_passed=0'),('DSP_dirty=0','DSP_dirty=1'),('config_active=0','config_active=1'),('maximum_mc=40000 limit_mc=50000','maximum_mc=50000 limit_mc=50000')]:
   self.assertFalse(m.inspect_audio_window(text.replace(a,b))['passed'],a)
  for line in text.splitlines():
   if line.startswith('aw:'):
    self.assertFalse(m.inspect_audio_window(text.replace(line,''))['passed']);self.assertFalse(m.inspect_audio_window(text+'\n'+line)['passed'])
 def test_amp_id_explicit_bounded_read_only_device_contract(self):
  text=self.amp_fixture();options=dict(clock=True,cycle=True,registers=True,route=True,setup=True,amp=True)
  r=m.inspect(text,**options);self.assertTrue(r['passed'],r);self.assertTrue(r['chip_id_verified']);self.assertFalse(r['audio_acceptance'])
  self.assertFalse(m.inspect(text,clock=True,cycle=True,registers=True,route=True,setup=True)['passed'])
  for a,b in [('chip_id=2032','chip_id=2013'),('raw_rx=3220','raw_rx=2032'),('value=0 phase=transfer device=34','value=1 phase=transfer device=34'),('reset_restored=1','reset_restored=0'),('elapsed_us=10','elapsed_us=40000'),('aborts=0','aborts=1'),('address=880700','address=880704'),('address=880618 value=40000000','address=880618 value=ffffffff'),('reset_writes=8','reset_writes=0')]:
   self.assertFalse(m.inspect(text.replace(a,b),**options)['passed'],a)
  for line in text.splitlines():
   if line.startswith('marble-i2c-amp:'):
    self.assertFalse(m.inspect(text.replace(line,''),**options)['passed']);self.assertFalse(m.inspect(text+'\n'+line,**options)['passed'])
 def test_amp_cli_rejects_mixed_modes_before_USB(self):
  with tempfile.TemporaryDirectory() as tmp:
   out=Path(tmp)/'absent'
   for extra in ([],['--discovery-only','--i2c-setup-cycle'],['--discovery-only','--i2c-route-info'],['--discovery-only','--adsp-spf-state']):
    r=subprocess.run([sys.executable,'-X','utf8','tools/marble-gpu-preview.py','--out',str(out),'--i2c-amp-id',*extra],cwd=ROOT,capture_output=True,text=True,timeout=8)
    self.assertEqual(r.returncode,2,r.stdout+r.stderr);self.assertIn('I2C amplifier ID requires isolated discovery',r.stderr);self.assertFalse(out.exists())
 def test_amp_raw_summary_can_only_be_masked_and_is_never_claimed_restored(self):
  text=self.amp_fixture();options=dict(clock=True,cycle=True,registers=True,route=True,setup=True,amp=True)
  for a,b in [('idle_IRQ=80','idle_IRQ=81'),('idle_IRQ_enable=7fc0347f','idle_IRQ_enable=7fc034ff'),('raw_IRQ_restored=0','raw_IRQ_restored=1'),('post_IRQ_enable=0','post_IRQ_enable=80'),('cold_reset_required=1','cold_reset_required=0'),('mask=ffffff7f','mask=ffffff00'),('S_IRQ_seen=c000000','S_IRQ_seen=c000010')]:
   self.assertFalse(m.inspect(text.replace(a,b),**options)['passed'],a)
 def test_clock_lookup_missing_duplicate_mutated_or_written(self):
  text=self.clock_fixture()
  for a,b in [('native_bytes=88','native_bytes=80'),('controller=880000','controller=984000'),('f101b0f8','f101b0f0'),('code_digest=1','code_digest=0'),('status=0','status=800000000000000e'),('id=2','id=1'),('id=2','id=0'),('id=2','id=ffffffffffffffff'),('clock_writes=0','clock_writes=1'),('frequency_requests=0','frequency_requests=1'),('I2C_head_unchanged=1','I2C_head_unchanged=0'),('I2C_calls=0','I2C_calls=1')]:
   self.assertFalse(m.inspect(text.replace(a,b),clock=True)['passed'],a)
  for line in text.splitlines():
   if line.startswith(('marble-i2c-clock:','marble-i2c-board:')):
    self.assertFalse(m.inspect(text.replace(line,''),clock=True)['passed']);self.assertFalse(m.inspect(text+'\n'+line,clock=True)['passed'])
 def test_clock_cli_rejects_mixed_modes_before_USB(self):
  with tempfile.TemporaryDirectory() as tmp:
   out=Path(tmp)/'absent'
   for extra in ([],['--discovery-only','--i2c-inventory'],['--discovery-only','--i2c-dt-info'],['--discovery-only','--i2c-config-snapshot'],['--discovery-only','--adsp-spf-state']):
    r=subprocess.run([sys.executable,'-X','utf8','tools/marble-gpu-preview.py','--out',str(out),'--i2c-clock-info',*extra],cwd=ROOT,capture_output=True,text=True,timeout=8)
    self.assertEqual(r.returncode,2,r.stdout+r.stderr);self.assertIn('I2C clock lookup requires isolated discovery',r.stderr);self.assertFalse(out.exists())
 def dt_fixture(self):
  words=[0xf1000000+4*i for i in range(33)]
  rows=['marble-i2c-DT: observed table=f2000000 words=33 cached_provider=0 calls=0 ABI_qualified=0']
  rows += [f'marble-i2c-DT: word index={i} value={word:x}' for i,word in enumerate(words)]
  rows += [f'marble-i2c-DT: code index={i} pointer={words[i]:x} bytes=32 words=0,1,2,3 invoked=0' for i in (6,20)]
  rows += ['marble-i2c-DT: complete PASS=1 table_unchanged=1 cached_provider_unchanged=1 DT_queries=0 I2C_calls=0 MMIO_reads=0 MMIO_writes=0 GPIO_writes=0 allocation_calls=0 ABI_qualified=0 controller_qualified=0 audio_acceptance=0']
  return fixture()+'\n'+'\n'.join(rows)
 def test_dt_observation_requires_explicit_contract_and_never_claims_abi(self):
  text=self.dt_fixture();r=m.inspect(text,dt=True);self.assertTrue(r['passed'],r);self.assertFalse(r['DT_ABI_verified']);self.assertFalse(r['controller_verified'])
  self.assertFalse(m.inspect(text)['passed']);self.assertFalse(m.inspect(fixture(),dt=True)['passed'])
 def test_dt_missing_mutated_duplicate_or_called(self):
  text=self.dt_fixture()
  for a,b in [('DT_queries=0','DT_queries=1'),('ABI_qualified=0','ABI_qualified=1'),('index=6 pointer=f1000018','index=6 pointer=f100001c'),('cached_provider_unchanged=1','cached_provider_unchanged=0')]:
   self.assertFalse(m.inspect(text.replace(a,b),dt=True)['passed'])
  for line in text.splitlines():
   if line.startswith('marble-i2c-DT:'):
    self.assertFalse(m.inspect(text.replace(line,''),dt=True)['passed']);self.assertFalse(m.inspect(text+'\n'+line,dt=True)['passed'])
 def config_fixture(self):
  text=self.dt_fixture()
  for i,value in ((0,0x10001),(6,0xa70149f8),(20,0xa702620c)):
   text=text.replace(f'index={i} value={0xf1000000+4*i:x}',f'index={i} value={value:x}')
   text=text.replace(f'index={i} pointer={0xf1000000+4*i:x}',f'index={i} pointer={value:x}')
  return text+'\n'+'\n'.join([
   'marble-i2c-config: observed pointer=a6000000 pointer_slot=a7043aa0 code_ranges=2 DT_queries=0',
   'marble-i2c-config: header bytes=41865 magic=d00dfeed payload_read=0',
   'marble-i2c-config: complete PASS=1 bytes=41865 ROM_blob_match=1 pointer_read=1 pointer_unchanged=1 capture_only=1 configuration_qualified=0 DT_queries=0 I2C_calls=0 MMIO_reads=0 MMIO_writes=0 GPIO_writes=0 original_blob_writes=0 audio_acceptance=0'])
 def test_exact_rom_configuration_is_a_separate_contract(self):
  r=m.inspect(self.config_fixture(),dt=True,config=True);self.assertTrue(r['passed'],r);self.assertTrue(r['ROM_snapshot_captured']);self.assertFalse(r['ROM_configuration_verified']);self.assertFalse(r['controller_verified'])
  self.assertFalse(m.inspect(self.config_fixture(),dt=True)['passed'])
  self.assertFalse(m.inspect(self.config_fixture(),config=True)['passed'])
 def test_config_identity_bounds_hash_or_ownership_refused(self):
  for a,b in [('pointer=a6000000','pointer=1495dca0'),('value=a70149f8','value=a70149fc'),('value=10001','value=10002'),('bytes=41865','bytes=41864'),('ROM_blob_match=1','ROM_blob_match=2'),('pointer_unchanged=1','pointer_unchanged=0'),('configuration_qualified=0','configuration_qualified=1'),('original_blob_writes=0','original_blob_writes=1')]:
   self.assertFalse(m.inspect(self.config_fixture().replace(a,b),dt=True,config=True)['passed'],a)
  text=self.config_fixture()
  for line in text.splitlines():
   if line.startswith('marble-i2c-config:'):
    self.assertFalse(m.inspect(text.replace(line,''),dt=True,config=True)['passed']);self.assertFalse(m.inspect(text+'\n'+line,dt=True,config=True)['passed'])
 def test_exact_inventory_has_no_controller_claim(self):
  r=m.inspect(fixture());self.assertTrue(r['passed'],r);self.assertFalse(r['controller_verified']);self.assertFalse(r['chip_id_verified']);self.assertFalse(r['audio_acceptance'])
 def test_mutations_and_incomplete_evidence(self):
  for a,b in [('revision=10000','revision=10001'),('code_digest=1','code_digest=0'),('f000a0d8','f000a0e0'),('f00027f8','f00027fc'),('invoked=0','invoked=1'),('client_head_unchanged=1','client_head_unchanged=0'),('protocol_calls=0','protocol_calls=1'),('instance_selected=0','instance_selected=16'),('MMIO_reads=0','MMIO_reads=1')]:
   self.assertFalse(m.inspect(fixture().replace(a,b))['passed'],a)
  for line in fixture().splitlines():
   self.assertFalse(m.inspect(fixture().replace(line,''))['passed']);self.assertFalse(m.inspect(fixture()+'\n'+line)['passed'])
 def simulate(self,after_temperature=31,changed=True,bad_log=False,clock=False,cycle=False,registers=False,route=False,setup=False,amp=False,amp_registers=False,amp_quiet=False,amp_profile=False):
  amp_quiet=amp_quiet or amp_profile;amp_registers=amp_registers or amp_quiet;amp=amp or amp_registers;setup=setup or amp;route=route or setup;registers=registers or route;cycle=cycle or registers;clock=clock or cycle
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp);out=root/'live';disk=root/'disk';disk.write_bytes(b'RAM');state=[0];commands=[]
   (root/'build-profile.json').write_text(json.dumps(dict(i2c_only=True)))
   boot=root/'boot.img';boot.write_bytes(b'RAM')
   idle='marble-efi-idle: probe PASS=1 active=0 dirty=0 failed=0 saved=0 now=0 period_us=426 waits=64 wait_us=12372 max_wait_us=426 immediate=32 late=0 max_poll_gap_us=426\n'
   def command(args,**kw):
    args=list(map(str,args));commands.append(args);result=''
    if args[0].endswith('adb.exe'):
     if args[-2:]==['reboot','bootloader']:state[0]=1
     elif args[-1]=='devices':result='YOUR_ANDROID_SERIAL\tdevice\n'
     elif args[-1]=='/proc/sys/kernel/random/boot_id':result=('11111111-1111-1111-1111-111111111111' if state[0]<3 or not changed else '22222222-2222-2222-2222-222222222222')+'\n'
     else:raise AssertionError(args)
    elif args[0]=='python':
     self.assertEqual(args[1],'tools/marble-run.py');self.assertIn('boot',args);state[0]=2
    elif args[0].endswith('fastboot.exe'):
     if args[-1]=='devices':result='xnuloader\tfastboot\n' if state[0]==2 else 'YOUR_ANDROID_SERIAL\tfastboot\n'
     elif args[-2:]==['getvar','product']:result='product: marble\n'
     elif args[-2:]==['getvar','result']:result='result: idle\n'
     elif args[-2:]==['getvar','marble-gpu-preview']:result='gpu90-die50-60s-hard55-v2\n'
     elif args[-2:]==['getvar','marble-i2c-inventory']:result='Mu-I2C-table-code-readonly-v1\n'
     elif args[-2:]==['getvar','marble-i2c-clock-info']:result='QUP2-five-clock-ID-readonly-v1\n'
     elif args[-2:]==['getvar','marble-i2c-clock-cycle']:result='QUP2-five-clock-balanced-1ms-v3\n'
     elif args[-2:]==['getvar','marble-i2c-register-info']:result='QUP2-eight-passive-RO-twice-v1\n'
     elif args[-2:]==['getvar','marble-i2c-route-info']:result='QUP2-41-route-words-passive-v1\n'
     elif args[-2:]==['getvar','marble-i2c-setup-cycle']:result='QUP2-100kHz-config-restore-no-transaction-v1\n'
     elif args[-2:]==['getvar','marble-i2c-amp-profile']:result='AW2032-ROM-Music-OTP-down-muted-readback-reset-v1\n'
     elif args[-2:]==['getvar','marble-i2c-amp-quiet']:result='AW2032-90dB-HMUTE-write-readback-reset-v1\n'
     elif args[-2:]==['getvar','marble-i2c-amp-registers']:result='AW2032-reset-readonly-registers-v1\n'
     elif args[-2:]==['getvar','marble-i2c-amp-id']:result='QUP2-two-AW2032-ID-only-bounded-v5\n'
     elif 'get_staged' in args:
      path=Path(args[-1]);log=self.amp_profile_fixture() if amp_profile else self.amp_quiet_fixture() if amp_quiet else self.amp_register_fixture() if amp_registers else self.amp_fixture() if amp else self.setup_fixture() if setup else self.route_fixture() if route else self.register_fixture() if registers else self.cycle_fixture() if cycle else self.clock_fixture() if clock else fixture();path.write_text(idle if path.name=='idle-probe.log' else log.replace('PASS=1','PASS=0') if bad_log else log)
     elif args[-1]=='reboot':state[0]=3
     elif 'oem' in args:self.assertIn(args[-1],('clearscreen','idle-probe','log','i2c-amp-profile' if amp_profile else 'i2c-amp-quiet' if amp_quiet else 'i2c-amp-registers' if amp_registers else 'i2c-amp-id' if amp else 'i2c-setup-cycle' if setup else 'i2c-route-info' if route else 'i2c-register-info' if registers else 'i2c-clock-cycle' if cycle else 'i2c-clock-info' if clock else 'i2c-inventory'))
     else:raise AssertionError(args)
    else:raise AssertionError(args)
    return subprocess.CompletedProcess(args,0,result.encode(),b'')
   argv=['preview','--out',str(out),'--boot-image',str(boot),'--ramdisk',str(disk),'--system-disk',str(disk),'--discovery-only','--amp-profile' if amp_profile else '--amp-quiet' if amp_quiet else '--amp-registers' if amp_registers else '--i2c-amp-id' if amp else '--i2c-setup-cycle' if setup else '--i2c-route-info' if route else '--i2c-register-info' if registers else '--i2c-clock-cycle' if cycle else '--i2c-clock-info' if clock else '--i2c-inventory']
   temperatures=iter((30,after_temperature))
   with patch.object(sys,'argv',argv),patch.object(preview.subprocess,'run',side_effect=command),patch.object(preview,'screen_off'),patch.object(preview,'verify_battery_preflight'),patch.object(preview,'battery_snapshot',side_effect=lambda *a:(None,dict(temperature_c=next(temperatures)))),contextlib.redirect_stdout(io.StringIO()):
    try:preview.main();code=0
    except SystemExit as e:code=e.code
   r=json.loads((out/'exercise.json').read_text());self.assertEqual(state[0],3)
   self.assertTrue(r['android_screen_off']);self.assertTrue(r['discovery_complete'] if not bad_log else r['error'])
   self.assertTrue(all('flash' not in c and 'erase' not in c and not any('/data' in a for a in c) for c in commands))
   return code,r
 def test_complete_controller_and_final_acceptance(self):
  code,r=self.simulate();self.assertEqual(code,0);self.assertTrue(r['i2c_inventory_with_reset_passed'])
 def test_clock_controller_and_reset_acceptance(self):
  code,r=self.simulate(clock=True);self.assertEqual(code,0);self.assertTrue(r['i2c_inventory_with_reset_passed']);self.assertTrue(r['i2c_inventory']['clock_names_verified'])
  for options in (dict(changed=False),dict(after_temperature=38),dict(bad_log=True)):
   code,r=self.simulate(clock=True,**options);self.assertEqual(code,1,options);self.assertFalse(r['i2c_inventory_with_reset_passed'])
 def test_cycle_controller_and_reset_acceptance(self):
  code,r=self.simulate(cycle=True);self.assertEqual(code,0);self.assertTrue(r['i2c_inventory_with_reset_passed']);self.assertTrue(r['i2c_inventory']['clock_lease_verified'])
  for options in (dict(changed=False),dict(after_temperature=38),dict(bad_log=True)):
   code,r=self.simulate(cycle=True,**options);self.assertEqual(code,1,options);self.assertFalse(r['i2c_inventory_with_reset_passed'])
 def test_register_controller_and_reset_acceptance(self):
  code,r=self.simulate(registers=True);self.assertEqual(code,0);self.assertTrue(r['i2c_inventory_with_reset_passed']);self.assertTrue(r['i2c_inventory']['passive_snapshot_captured'])
  for options in (dict(changed=False),dict(after_temperature=38),dict(bad_log=True)):
   code,r=self.simulate(registers=True,**options);self.assertEqual(code,1,options);self.assertFalse(r['i2c_inventory_with_reset_passed'])
 def test_route_controller_and_reset_acceptance(self):
  code,r=self.simulate(route=True);self.assertEqual(code,0);self.assertTrue(r['i2c_inventory_with_reset_passed']);self.assertTrue(r['i2c_inventory']['route_snapshot_captured'])
  for options in (dict(changed=False),dict(after_temperature=38),dict(bad_log=True)):
   code,r=self.simulate(route=True,**options);self.assertEqual(code,1,options);self.assertFalse(r['i2c_inventory_with_reset_passed'])
 def test_setup_controller_and_reset_acceptance(self):
  code,r=self.simulate(setup=True);self.assertEqual(code,0);self.assertTrue(r['i2c_inventory_with_reset_passed']);self.assertTrue(r['i2c_inventory']['bus_configuration_verified'])
  for options in (dict(changed=False),dict(after_temperature=38),dict(bad_log=True)):
   code,r=self.simulate(setup=True,**options);self.assertEqual(code,1,options);self.assertFalse(r['i2c_inventory_with_reset_passed'])
 def test_amp_controller_and_reset_acceptance(self):
  code,r=self.simulate(amp=True);self.assertEqual(code,0);self.assertTrue(r['i2c_inventory_with_reset_passed']);self.assertTrue(r['i2c_inventory']['chip_id_verified'])
  for options in (dict(changed=False),dict(after_temperature=38),dict(bad_log=True)):
   code,r=self.simulate(amp=True,**options);self.assertEqual(code,1,options);self.assertFalse(r['i2c_inventory_with_reset_passed'])
 def test_early_discovery_exit_cannot_skip_failed_reset_acceptance(self):
  for options in (dict(changed=False),dict(after_temperature=38),dict(bad_log=True)):
   code,r=self.simulate(**options);self.assertEqual(code,1,options);self.assertFalse(r['i2c_inventory_with_reset_passed'])
if __name__=='__main__':unittest.main()
