"""Require two distinct transport completions and the original close proof."""
import importlib.util,unittest
from pathlib import Path
TOOLS=Path(__file__).resolve().parents[1]
s=importlib.util.spec_from_file_location('qmp_load_report',TOOLS/'marble-qmp-native-report.py')
m=importlib.util.module_from_spec(s);s.loader.exec_module(m)

def fixture():
    text=(TOOLS/'tests/fixtures/marble-qmp-native-settled.log').read_text()
    text='marble-qmp-load: qualified code_ranges=10 bytes=64 zero_filled=1 on_then_off=1 DSP_start=0 PAS_calls=0 NPA_calls=0 storage_access=0\n'+text
    lines=[]
    for i,value in enumerate(('on','off')):
        lines += [f'marble-qmp-load: tx_before index={i} value={value} bytes=64 flags=0 DSP_start=0',
            f'marble-qmp-load: tx_return index={i} status=0 result=0',
            f'marble-qmp-load: tx_complete index={i} PASS=1 callbacks={i+1} pending=0 invalid=0 mailbox_empty=1 elapsed_us=1200 physical_readback=0']
    text=text.replace('marble-qmp-native: close_return ','\n'.join(lines)+'\nmarble-qmp-native: close_return ')
    text=text.replace('stale_handle_calls=0 raw_writes=0 TX_calls=0','stale_handle_calls=0 raw_writes=0 TX_calls=2')
    text=text.replace('RX=0 TX=0 elapsed_us=','RX=0 TX=2 elapsed_us=').replace('original_restoration=1 raw_writes=0 TX_calls=0','original_restoration=1 raw_writes=0 TX_calls=2')
    return text

class QmpLoadReport(unittest.TestCase):
    def inspect(self,text):return m.inspect(text,settled_close=True,load_state=True)
    def test_full_contract(self):
        r=self.inspect(fixture());self.assertTrue(r['passed'],r)
        self.assertTrue(r['load_state_transport_ack_verified']);self.assertFalse(r['physical_load_state_readback']);self.assertFalse(r['audio_acceptance'])
    def test_distinct_contract(self):
        self.assertFalse(m.inspect(fixture(),settled_close=True)['passed'])
        self.assertFalse(m.inspect(fixture(),load_state=True)['passed'])
    def test_every_business_row_required_once(self):
        for row in fixture().splitlines():
            if row.startswith('marble-qmp-load:'):
                self.assertFalse(self.inspect(fixture().replace(row,''))['passed'],row)
                self.assertFalse(self.inspect(fixture()+'\n'+row)['passed'],row)
    def test_ack_identity_deadline_message_and_cleanup(self):
        for a,b in [('value=off','value=on'),('bytes=64','bytes=48'),('flags=0','flags=1'),
                ('callbacks=2','callbacks=1'),('pending=0','pending=1'),('mailbox_empty=1','mailbox_empty=0'),
                ('elapsed_us=1200','elapsed_us=250000'),('physical_readback=0','physical_readback=1'),
                ('code_ranges=10','code_ranges=9'),('TX_calls=2','TX_calls=1'),('RX=0 TX=2','RX=0 TX=1'),
                ('client_empty=1','client_empty=0'),('status=0 result=0','status=0 result=-5')]:
            self.assertFalse(self.inspect(fixture().replace(a,b))['passed'],a)
    def test_order(self):
        text=fixture();a='marble-qmp-load: tx_before index=0 ';b='marble-qmp-load: tx_before index=1 '
        self.assertFalse(self.inspect(text.replace(a,'TEMP').replace(b,a).replace('TEMP',b))['passed'])

def mailbox_fixture():
    text=fixture().replace('code_ranges=10','code_ranges=13').replace('storage_access=0\n','storage_access=0 adapter=native-mailbox GLink_TX_calls=0\n',1)
    text=text.replace('DSP_start=0\nmarble-qmp-load: tx_return','DSP_start=0 adapter=native-mailbox GLink_TX_calls=0\nmarble-qmp-load: tx_return')
    text=text.replace('result=0\nmarble-qmp-load: tx_complete','result=0 consumed=1\nmarble-qmp-load: tx_complete')
    for i in range(2):
        text=text.replace(f'callbacks={i+1}', 'callbacks=0',1)
        needle=f'marble-qmp-load: tx_complete index={i} '
        line=next(s for s in text.splitlines() if s.startswith(needle))
        text=text.replace(line,line+f' acknowledgements={i+1} native_queues_empty=1')
    return text.replace('RX=0 TX=2 elapsed_us=','RX=0 TX=0 elapsed_us=')

class QmpMailboxReport(unittest.TestCase):
    def inspect(self,text):return m.inspect(text,settled_close=True,mailbox_load_state=True)
    def test_mailbox_ack_contract(self):
        r=self.inspect(mailbox_fixture());self.assertTrue(r['passed'],r)
        self.assertTrue(r['mailbox_load_state_contract']);self.assertFalse(r['physical_load_state_readback'])
        self.assertFalse(m.inspect(mailbox_fixture(),settled_close=True,load_state=True)['passed'])
    def test_no_fake_callback_or_missing_ack(self):
        for a,b in [('callbacks=0','callbacks=1'),('acknowledgements=2','acknowledgements=1'),
                ('native_queues_empty=1','native_queues_empty=0'),('consumed=1','consumed=0'),
                ('GLink_TX_calls=0','GLink_TX_calls=1'),('adapter=native-mailbox','adapter=generic')]:
            self.assertFalse(self.inspect(mailbox_fixture().replace(a,b))['passed'],a)

if __name__=='__main__':unittest.main()
