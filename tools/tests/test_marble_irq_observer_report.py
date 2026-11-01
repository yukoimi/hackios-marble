import importlib.util,unittest
from pathlib import Path
s=importlib.util.spec_from_file_location('irq_observer',Path(__file__).resolve().parents[1]/'marble-irq-observer-report.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
def fixture(interrupted=False):
 lines=['marble-irq-observer: qualified distributor=17100000 redistributor=17180000 stride=40000 banks=32 architecture=3 MPIDR=80000000 IPCC_SPI=261 IRQ_writes=0 IAR_reads=0']
 if interrupted:lines+=['marble-smccc: interrupted index=0 before=ffffffff after=ffffffff service=0 USB_progress_calls=0']
 for phase in ([0,1,2,3] if interrupted else [0,1,3]):
  lines += [f'marble-irq-observer: snapshot phase={phase} PPI_enabled=40000000 PPI_pending=40000000 PPI_active=0 IRQ_writes=0 IAR_reads=0',
   f'marble-irq-observer: SPI phase={phase} bank=8 enabled=20 pending=20 active=0']
 return '\n'.join(lines)
class Observer(unittest.TestCase):
 def test_decode_without_acknowledgement_claim(self):
  for interrupted in (False,True):
   r=m.inspect(fixture(interrupted));self.assertTrue(r['passed'],r);self.assertEqual(r['phases']['1']['pending_IRQs'],[30,261]);self.assertTrue(r['phases']['1']['IPCC_pending']);self.assertEqual(r['IAR_reads'],0)
 def test_missing_or_duplicate(self):
  text=fixture(True)
  for line in text.splitlines():
   if not line.startswith('marble-irq-observer:'):continue
   self.assertFalse(m.inspect(text.replace(line,''))['passed']);self.assertFalse(m.inspect(text+'\n'+line)['passed'])
 def test_geometry_ownership_and_values(self):
  for a,b in [('redistributor=17180000','redistributor=17190000'),('banks=32','banks=8'),('IPCC_SPI=261','IPCC_SPI=229'),('IAR_reads=0','IAR_reads=1'),('IRQ_writes=0','IRQ_writes=1'),('pending=20','pending=100000000'),('architecture=3','architecture=2')]:
   self.assertFalse(m.inspect(fixture().replace(a,b))['passed'],a)
if __name__=='__main__':unittest.main()
