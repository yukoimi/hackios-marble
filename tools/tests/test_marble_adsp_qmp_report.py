"""Validate nested LPI/QMP/XO/bandwidth/ROM ownership and retained watchdog."""
import importlib.util,unittest
from pathlib import Path
BASE=Path(__file__).parent
def load(name,file):
    s=importlib.util.spec_from_file_location(name,file);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
owned=load('qmp_owned_fixture',BASE/'test_marble_adsp_owned_report.py')
qmp=load('qmp_mailbox_fixture',BASE/'test_marble_qmp_load_report.py')
m=owned.m
def fixture():
    text=owned.fixture();wire=qmp.mailbox_fixture()
    wire=wire.replace('GLink_TX_calls=0\n','GLink_TX_calls=0 owned=1 deferred=1\n',1).replace('limit_us=2000000','limit_us=15000000')
    wire=wire.replace('elapsed_us=5245','elapsed_us=1000000').replace('watchdog_clear=0','watchdog_clear=ffffffffffffffff')
    wire=wire.replace('TX_calls=2 NPA_calls=0 PAS_calls=0','TX_calls=2 NPA_calls=coordinated PAS_calls=coordinated')
    wire=wire.rstrip()+' watchdog_retained=1 owned=1\n'
    at=wire.index('marble-qmp-load: tx_before index=1 ')
    first=wire[:at]+'marble-qmp-load: held_before LPI_clients=2 DSP_start=0 watchdog_retained=1 ROM_loaded=1\n'
    last='marble-qmp-load: held_return PASS=1 outcome=1 DSP_start=0 ROM_restored=0 watchdog_retained=1 noexec_cancel=1\n'+wire[at:]
    line=next(s for s in text.splitlines() if s.startswith('marble-pil-held: loaded '))
    return text.replace(line+'\n',line+'\n'+first+last+'marble-qmp-load: CPU_context PASS=1 DAIF_before=3c0 DAIF_after=3c0 direct_mask_writes=0\nmarble-pil-held: post_load_gate PASS=1 ROM_loaded=1 PAS_initialized=1 DSP_start=0\n')

class OwnedQmp(unittest.TestCase):
    def inspect(self,text):return m.inspect(text,qmp=True)
    def test_nested_noexec(self):
        r=self.inspect(fixture());self.assertTrue(r['passed'],r);self.assertTrue(r['qmp_noexec_unwind_verified']);self.assertFalse(r['qmp_held_ROM_verified'])
        self.assertFalse(r['dsp_started']);self.assertFalse(r['audio_acceptance'])
    def test_refuses_plain_old_stage(self):self.assertFalse(self.inspect(owned.fixture())['passed'])
    def test_watchdog_ownership_off_ack_and_budget(self):
        for a,b in [('ROM_restored=0','ROM_restored=1'),('watchdog_retained=1 owned=1','watchdog_retained=0 owned=1'),
                ('watchdog_clear=ffffffffffffffff native_event_calls','watchdog_clear=0 native_event_calls'),
                ('elapsed_us=1000000','elapsed_us=12500000'),('acknowledgements=2','acknowledgements=1'),
                ('NPA_calls=coordinated','NPA_calls=0'),('DAIF_after=3c0','DAIF_after=0')]:
            self.assertFalse(self.inspect(fixture().replace(a,b))['passed'],a)
    def test_held_proofs_unique(self):
        for row in fixture().splitlines():
            if row.startswith('marble-qmp-load: held_'):
                self.assertFalse(self.inspect(fixture().replace(row,''))['passed'])
                self.assertFalse(self.inspect(fixture()+'\n'+row)['passed'])

if __name__=='__main__':unittest.main()
