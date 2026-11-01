import importlib.util,re,unittest
from pathlib import Path
BASE=Path(__file__).parent
def load(name,path):
    s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
f=load('analog_fixture',BASE/'test_marble_i2c_inventory_report.py')
board=load('analog_board',BASE.parent/'marble-aw-analog-board.py')
OPTIONS=dict(amp_profile=True,runtime_deadline=True,digital_pll=True,digital_early_stop=True,fast_stop=True,muted_analog=True)

def fixture():
    text=f.Inventory().digital_fixture();extra=0;commands=0
    for device in (0x34,0x35):
        digital=0xb442 if device==0x34 else 0x3442
        accesses=[(1,4,digital),(1,5,0xf039),(1,1,0x11),(0,4,digital&~2),
                  (1,4,digital&~2),(1,5,0xf039),(1,1,0x311),(0,4,digital),(1,4,digital),(1,5,0xf039)]
        rows=[]
        for read,reg,value in accesses:
            phase='analog-read' if read else 'analog-write'
            if read:ops=[(0x88026c,1),(0x880600,(1<<27)|(device<<9)|4),(0x88080c,1),(0x880700,reg),(0x88080c,0),(0x880270,2),(0x880600,(2<<27)|(device<<9))]
            else:ops=[(0x88026c,3),(0x880600,(1<<27)|(device<<9)),(0x88080c,1),(0x880700,reg|((value>>8)<<8)|((value&255)<<16)),(0x88080c,0)]
            extra+=len(ops);commands+=2 if read else 1
            rows += [f'marble-i2c-amp: write address={a:x} value={v:x} phase={phase} device={device:x}' for a,v in ops]
            rows.append(f'marble-aw-analog: access device={device:x} read={read} reg={reg:x} value={value:x} PASS=1 commands={2 if read else 1} dirty=0')
        rows.append(f'marble-aw-analog: complete device={device:x} PASS=1 attempted=1 writes=2 polls=1 ready=1 restored=1 dirty=0 status=311 HMUTE_retained=1 attenuation_db=90 speaker_switch_writes=0 audio_acceptance=0 elapsed_us=3000')
        # Insert before the digital restore transaction, after its lock reply.
        anchor=f'marble-aw-digital: access device={device:x} read=1 reg=1 value=11 PASS=1 commands=2 dirty=0\n'
        text=text.replace(anchor,anchor+'\n'.join(rows)+'\n',1)
    text=text.replace('AMPPD_retained=1','AMPPD_retained=0')
    text=re.sub(r'(marble-i2c-amp: complete [^\n]*amplifier_register_writes=)48',r'\g<1>50',text)
    text=text.replace('I2C_transactions=356',f'I2C_transactions={356+commands}').replace('amplifier_register_writes=96','amplifier_register_writes=100').replace('amp_commands=356',f'amp_commands={356+commands}')
    text=re.sub(r'controller_MMIO_reads=98 controller_MMIO_writes=(\d+)',lambda r:'controller_MMIO_reads=98 controller_MMIO_writes='+str(int(r[1])+extra),text)
    text=text.replace('hold_limit_ms=500','hold_limit_ms=600').replace('deadline_us=500000 poll_rounds=50','deadline_us=600000 poll_rounds=60')
    at=text.index('marble-pil-boot: auth ')
    text=text[:at]+'aud-typed-budget: version=1 minimum_us=300000 maximum_us=600000 poll_rounds=60 completion_required=1 final_thermal_required=1 audio_acceptance=0\n'+text[at:]
    at=text.index('marble-pil-boot: observed ')
    text=text[:at]+'aud-early: typed_complete=1 round=43 minimum_us=300000 maximum_us=750000 thermal_samples=5 final_thermal_ok=1 idle_dwell_omitted=1 PAS_required=1 audio_acceptance=0\n'+text[at:]
    at=text.index('marble-smp2p-boot: stop-ack ')
    return text[:at]+'aud-stop-policy: poll_us=100 timeout_us=5000000 thermal_interval_us=100000 counter_read_only=1 IRQ_masks_unchanged=1 fresh_ACK_required=1\n'+text[at:]

class AnalogReport(unittest.TestCase):
    def test_source_binding(self):
        r=board.inspect();self.assertTrue(r['passed']);self.assertFalse(r['audio_acceptance'])
        self.assertEqual([(d['sysctrl_digital'],d['sysctrl_analog'],d['sysctrl2']) for d in r['devices']],[(0xb442,0xb440,0xf039),(0x3442,0x3440,0xf039)])

    def test_exact_muted_lifecycle(self):
        text=fixture();r=f.m.inspect_audio_window(text,**OPTIONS)
        self.assertTrue(r['passed'],r);self.assertEqual(r['amplifier_commands'],392)
        self.assertTrue(r['amplifier_muted_analog_verified']);self.assertFalse(r['audio_acceptance'])
        old=dict(OPTIONS);old.pop('muted_analog')
        self.assertFalse(f.m.inspect_audio_window(text,**old)['passed'])

    def test_faults_restore_mute_counts_and_timing_rejected(self):
        text=fixture()
        for a,b in [('status=311','status=313'),('status=311','status=331'),('ready=1','ready=0'),
                    ('polls=1','polls=11'),('elapsed_us=3000','elapsed_us=100000'),('HMUTE_retained=1','HMUTE_retained=0'),
                    ('I2C_transactions=392','I2C_transactions=356'),('amplifier_register_writes=100','amplifier_register_writes=96'),
                    ('reg=5 value=f039','reg=5 value=f029'),('value=40b404 phase=analog-write','value=42b404 phase=analog-write')]:
            with self.subTest(a=a):
                changed=text.replace(a,b);self.assertNotEqual(text,changed)
                self.assertFalse(f.m.inspect_audio_window(changed,**OPTIONS)['passed'])

    def test_missing_and_duplicate_analog_evidence_rejected(self):
        text=fixture()
        for line in text.splitlines():
            if line.startswith('marble-aw-analog:'):
                self.assertFalse(f.m.inspect_audio_window(text.replace(line+'\n','',1),**OPTIONS)['passed'],line)
                self.assertFalse(f.m.inspect_audio_window(text+'\n'+line,**OPTIONS)['passed'],line)
        begin=text.index('marble-i2c-amp: write address=88026c value=1 phase=analog-read device=34')
        end=text.index('\n',text.index('marble-aw-analog: complete device=34'))+1
        block=text[begin:end];bad=text[:begin]+text[end:]
        self.assertFalse(f.m.inspect_audio_window(block+bad,**OPTIONS)['passed'])

if __name__=='__main__':unittest.main()
