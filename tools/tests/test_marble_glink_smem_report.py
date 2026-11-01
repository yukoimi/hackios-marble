"""Reject uninitialized/aliased native queues and unowned writes before DSP start."""
import importlib.util,unittest
from pathlib import Path
s=importlib.util.spec_from_file_location('glink_smem_report',Path(__file__).resolve().parents[1]/'marble-glink-smem-report.py')
m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
LOG='\n'.join([
 'marble-glink-smem: qualified native_ranges=4 transport_slots=17 transport_count=5 stride=170 index=0 config_rva=d2a8 remote_pid=2 FIFO_config=16384 MTU=4096 IPCC_client=3 signal=0 partition=80acf000 partition_bytes=196608 allocator_calls=0 RX_dispatch=0',
 'marble-glink-smem: get item=478 status=0 address=80acf040 bytes=32 PASS=1 flags=40000000 allocator_calls=0 FIFO_reads=0',
 'marble-glink-smem: get item=479 status=0 address=80acf070 bytes=16384 PASS=1 flags=40000000 allocator_calls=0 FIFO_reads=0',
 'marble-glink-smem: get item=480 status=800000000000000e address=0 bytes=0 PASS=1 flags=40000000 allocator_calls=0 FIFO_reads=0',
 'marble-qmp-native: listeners PASS=1 total=1 matched=1 unknown=0 terminated=1 callback_calls=0',
 'marble-glink-smem: geometry descriptor=1 FIFO_binding=1 TX_size=16384 RX_size=0 RX_missing=1 descriptor_stable=1 own_indices_zero=1 remote_indices_zero=1 cached_indices=1 cached_TX_head=0 cached_RX_tail=0 indices=0,0,0,0 transport_unchanged=1 listeners=1 runtime_RX_qualified=0',
 'marble-glink-smem: complete PASS=1 initialized_SMEM=1 private_GET_calls=3 allocator_calls=0 descriptor_reads=64 FIFO_reads=0 RX_dispatch=0 channel_open=0 TX_calls=0 IRQ_writes=0 PAS_calls=0 DSP_start=0 storage_access=0 audio_acceptance=0'])
class Inventory(unittest.TestCase):
 def test_metadata_only(self):
  r=m.inspect(LOG);self.assertTrue(r['passed'],r);self.assertFalse(r['runtime_RX_qualified']);self.assertFalse(r['dsp_started'])
 def test_explicit_refusal_cannot_be_hidden_by_later_pass(self):
  self.assertFalse(m.inspect('marble-glink-smem: refused step=core\n'+LOG)['passed'])
 def test_history_not_runtime_readiness(self):
  text=LOG.replace('remote_indices_zero=1','remote_indices_zero=0').replace('indices=0,0,0,0','indices=100,0,0,80')
  self.assertTrue(m.inspect(text)['passed']);self.assertFalse(m.inspect(text.replace('runtime_RX_qualified=0','runtime_RX_qualified=1'))['passed'])
 def test_missing_duplicate_and_alias(self):
  for line in LOG.splitlines():
   self.assertFalse(m.inspect(LOG.replace(line,''))['passed']);self.assertFalse(m.inspect(LOG+'\n'+line)['passed'])
  for a,b in [('address=80acf070','address=80acf040'),('address=80acf040','address=80900000'),('address=80acf070','address=80aff000'),('address=0 bytes=0','address=80ad3080 bytes=344')]:
   self.assertFalse(m.inspect(LOG.replace(a,b))['passed'],a)
 def test_no_mutation_or_foreign_owner(self):
  for a,b in [('own_indices_zero=1','own_indices_zero=0'),('indices=0,0,0,0','indices=0,8,0,0'),('cached_TX_head=0','cached_TX_head=8'),('total=1','total=2'),('remote_pid=2','remote_pid=3'),('transport_slots=17','transport_slots=16'),('descriptor_stable=1','descriptor_stable=0'),('initialized_SMEM=1','initialized_SMEM=0')]:
   self.assertFalse(m.inspect(LOG.replace(a,b))['passed'],a)
  for key in ('allocator_calls','RX_dispatch','channel_open','TX_calls','IRQ_writes','PAS_calls','DSP_start','storage_access','audio_acceptance'):
   self.assertFalse(m.inspect(LOG.replace(key+'=0',key+'=1'))['passed'],key)
if __name__=='__main__':unittest.main()
