import importlib.util,unittest
from pathlib import Path
s=importlib.util.spec_from_file_location('power_report',Path(__file__).resolve().parents[1]/'marble-power-report.py')
m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
def fixture():return '\n'.join([
 'marble-qmp: qualified native_exact=1 address=c300000 bytes=1024 PAR=0 identity=1 protocol_calls=0',
 'marble-qmp: descriptor stable=1 valid=1 magic=4d41494c version=1 features=0 local_offset=100 local_bytes=256 remote_offset=200 remote_bytes=256',
 'marble-qmp: complete PASS=1 local_message_bytes=0 remote_message_bytes=0 descriptor_reads=120 length_reads=8 payload_reads=0 MMIO_writes=0 IPCC_writes=0 channel_open_calls=0 DSP_start=0',
 'marble-power-resource: index=0 name=/pmic/client/xo canonical=/pmic/client/xo state=1 max=1 clients=80010000 read_mask=f',
 'marble-power-resource: complete PASS=1 visited=1 terminated=1 xo_matches=1 protocol_calls=0 clients_created=0 resource_votes=0 DSP_start=0',
 'marble-adsp-power: complete PASS=1 qmp=1 resources=1 clock_enable=0 clock_disable=0 votes=0 PAS_calls=0 DSP_start=0 storage_access=0'])
class PowerInventory(unittest.TestCase):
 def test_expected(self):
  r=m.inspect(fixture());self.assertTrue(r['passed'],r);self.assertFalse(r['audio_acceptance']);self.assertFalse(r['resource_leases_verified'])
 def test_refuses_mutation_or_execution(self):
  for k in ['protocol_calls','payload_reads','MMIO_writes','IPCC_writes','channel_open_calls','DSP_start','clients_created','resource_votes','clock_enable','clock_disable','votes','PAS_calls','storage_access']:
   self.assertFalse(m.inspect(fixture().replace(k+'=0',k+'=1'))['passed'],k)
 def test_refuses_missing_and_duplicate(self):
  for line in fixture().splitlines():
   self.assertFalse(m.inspect(fixture().replace(line,''))['passed'])
  self.assertFalse(m.inspect(fixture()+'\n'+fixture())['passed'])
 def test_bounds_and_overflow(self):
  for a,b in [('local_offset=100','local_offset=fffffff0'),('local_bytes=256','local_bytes=4294967295'),('remote_offset=200','remote_offset=100'),('local_message_bytes=0','local_message_bytes=257'),('index=0','index=1'),('visited=1','visited=97'),('read_mask=f','read_mask=7'),('stable=1','stable=0')]:
   self.assertFalse(m.inspect(fixture().replace(a,b))['passed'],a)
 def test_order(self):
  self.assertFalse(m.inspect('\n'.join(reversed(fixture().splitlines())))['passed'])
 def test_placeholder_is_not_a_working_xo(self):
  text=fixture().replace('index=0 name=/pmic/client/xo canonical=/pmic/client/xo state=1 max=1 clients=80010000 read_mask=f',
      'placeholder index=0 name=/pmic/client/xo definition=0 read_mask=1').replace('xo_matches=1','xo_matches=0')
  r=m.inspect(text);self.assertTrue(r['passed'],r);self.assertEqual(r['xo'],[]);self.assertFalse(r['resource_leases_verified'])
  self.assertFalse(m.inspect(text.replace('definition=0','definition=1'))['passed'])
  self.assertFalse(m.inspect(text.replace('xo_matches=0','xo_matches=1'))['passed'])
class XoInventory(unittest.TestCase):
 def fixture(self):return '\n'.join([
  'marble-adsp-xo: snapshot_begin address=80860000 bytes=131072 pages=32 allocated=0 snapshots=2 votes=0 MMIO_writes=0 DSP_start=0',
  'marble-adsp-xo: resource name=xo.lvl stable=1 parsed=1 qualified=1 address=30000 slave=3 version=256 aux_bytes=0 issue=0,0,0 checksum_authenticated=0',
  'marble-adsp-xo: complete PASS=1 read_bytes=262144 free_status=0 elapsed_us=1500 resource_votes=0 native_clock_calls=0 RPMh_calls=0 NPA_calls=0 QMP_calls=0 PAS_calls=0 DSP_start=0 storage_access=0 lease_accepted=0'])
 def test_valid_metadata_is_not_a_lease(self):
  r=m.inspect_xo(self.fixture());self.assertTrue(r['passed'],r);self.assertFalse(r['resource_leases_verified'])
 def test_missing_duplicate_order(self):
  text=self.fixture()
  for line in text.splitlines():
   self.assertFalse(m.inspect_xo(text.replace(line,''))['passed']);self.assertFalse(m.inspect_xo(text+'\n'+line)['passed'])
  self.assertFalse(m.inspect_xo('\n'.join(reversed(text.splitlines())))['passed'])
 def test_bounds_and_cleanup(self):
  for a,b in [('address=30000','address=0'),('address=30000','address=50000'),('address=30000','address=30001'),('stable=1','stable=0'),('elapsed_us=1500','elapsed_us=1000000'),('free_status=0','free_status=1'),('aux_bytes=0','aux_bytes=65536'),('pages=32','pages=31'),('allocated=0','allocated=1')]:
   self.assertFalse(m.inspect_xo(self.fixture().replace(a,b))['passed'],a)
 def test_no_vote_or_functionality_claim(self):
  for key in ('votes','MMIO_writes','DSP_start','resource_votes','native_clock_calls','RPMh_calls','NPA_calls','QMP_calls','PAS_calls','storage_access','lease_accepted'):
   self.assertFalse(m.inspect_xo(self.fixture().replace(key+'=0',key+'=1'))['passed'],key)
class IcbInventory(unittest.TestCase):
 def test_real_complete_hardware_routes(self):
  text=(Path(__file__).parent/'fixtures/marble-icb-native-routes.log').read_text()
  r=m.inspect_icb(text);self.assertTrue(r['passed'],r);self.assertEqual(len(r['routes']),1018)
  self.assertFalse(m.inspect_icb(text.replace('nodes=1018','nodes=1017'))['passed'])
 def fixture(self):return '\n'.join([
  'marble-adsp-icb: snapshot exact_abi=1 Init_calls=0 config=80000004 first=1 second=1 stable=1 trees=1 nodes=2',
  'marble-adsp-icb: route index=0 tree=0 master=23 slave=0 hops=3 hardware_identity_verified=0',
  'marble-adsp-icb: route index=1 tree=0 master=0 slave=0 hops=1 hardware_identity_verified=0',
  'marble-adsp-icb: complete PASS=1 elapsed_us=400 native_calls=0 clients_created=0 votes=0 MMIO_writes=0 NPA_calls=0 RPMh_calls=0 PAS_calls=0 DSP_start=0 storage_access=0 bandwidth_lease=0'])
 def test_valid_is_not_bandwidth_lease(self):
  r=m.inspect_icb(self.fixture());self.assertTrue(r['passed'],r);self.assertFalse(r['bandwidth_lease_verified']);self.assertFalse(r['audio_acceptance'])
 def test_missing_duplicate_order(self):
  for line in self.fixture().splitlines():
   self.assertFalse(m.inspect_icb(self.fixture().replace(line,''))['passed']);self.assertFalse(m.inspect_icb(self.fixture()+'\n'+line)['passed'])
  self.assertFalse(m.inspect_icb('\n'.join(reversed(self.fixture().splitlines())))['passed'])
 def test_metadata_and_hardware_identity(self):
  for a,b in [('stable=1','stable=0'),('hops=3','hops=17'),('nodes=2','nodes=4097'),('master=23','master=1025'),('tree=0','tree=1'),('elapsed_us=400','elapsed_us=1000000'),('hardware_identity_verified=0','hardware_identity_verified=1')]:
   self.assertFalse(m.inspect_icb(self.fixture().replace(a,b))['passed'],a)
 def test_no_business_calls(self):
  for key in ('Init_calls','native_calls','clients_created','votes','MMIO_writes','NPA_calls','RPMh_calls','PAS_calls','DSP_start','storage_access','bandwidth_lease'):
   self.assertFalse(m.inspect_icb(self.fixture().replace(key+'=0',key+'=1'))['passed'],key)
class DependencyInventory(unittest.TestCase):
 def fixture(self):
  base=(Path(__file__).parent/'fixtures/marble-icb-native-routes.log').read_text().rstrip()
  lines=[base,'marble-adsp-dependencies: snapshot first=1 second=1 stable=1 firmware_master=23 firmware_slave=0 hops=4']
  for i in range(8):lines.append(f'marble-adsp-hop: index={i//2} side={i%2} name=node{i} id={i} bus=virtual bus_base=0 width=8 metadata_only=1')
  lines.append('marble-adsp-dependencies: npa resource=80000004 definition=80001004 plugin=80002000 state=1 clients=0')
  for i in range(8):lines.append(f'marble-adsp-dependencies: plugin_word index={i} value=0')
  for i in range(20):lines.append(f'marble-adsp-dependencies: clock_word index={i} value=0')
  lines.append('marble-adsp-dependencies: complete PASS=1 native_calls=0 clients_created=0 votes=0 MMIO_writes=0 DSP_start=0 storage_access=0 lease_accepted=0')
  return '\n'.join(lines)
 def test_valid_metadata_is_not_identity_or_lease(self):
  r=m.inspect_dependencies(self.fixture());self.assertTrue(r['passed'],r);self.assertFalse(r['hardware_identity_verified']);self.assertFalse(r['resource_leases_verified'])
 def test_missing_duplicate_metadata(self):
  text=self.fixture()
  for line in text.splitlines():
   if line.startswith('marble-adsp-dependencies:') or line.startswith('marble-adsp-hop:'):
    self.assertFalse(m.inspect_dependencies(text.replace(line,''))['passed']);self.assertFalse(m.inspect_dependencies(text+'\n'+line)['passed'])
 def test_scope_and_sequence(self):
  text=self.fixture()
  for a,b in [('stable=1','stable=0'),('metadata_only=1','metadata_only=0'),('hops=4','hops=5'),('lease_accepted=0','lease_accepted=1'),('resource=80000004','resource=0'),('native_calls=0','native_calls=1')]:
   self.assertFalse(m.inspect_dependencies(text.replace(a,b))['passed'],a)
if __name__=='__main__':unittest.main()
