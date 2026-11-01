import importlib.util,unittest
from pathlib import Path
s=importlib.util.spec_from_file_location('smp_report',Path(__file__).resolve().parents[1]/'marble-smp2p-report.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
def fixture():return '\n'.join((
    'marble-smp2p: qualified exact_allocator=1 lock_hooks_stub=1 DSP_off_profile=1 local_item=429 remote_item=443 region=80900000 region_bytes=2097152 partition=80acf000 partition_bytes=131072 free_before=16480 cached_before=131072 payload_reads=0',
    'marble-smp2p: allocated status=0 item=429 bytes=344 address=80ad3070 flags=40000000 DSP_start=0 ipcc_writes=0',
    'marble-smp2p: prepared PASS=1 magic=504d5324 version=1 local=0 remote=2 total=16 valid=1 master_value=0 free_delta=360 glink_items_unchanged=1 owned_bytes=340 payload_reads=340 payload_write_bytes=344',
    'marble-smp2p: complete PASS=1 attempted=1 dirty=1 elapsed_us=150 reset_required=1 watchdog_retained=1 DSP_start=0 ipcc_writes=0 audio_acceptance=0',
    'marble-smp2p-state: attempted=1 dirty=1 DSP_start=0'))
def notify_fixture():
    text='marble-smp2p-ipcc: qualified PASS=1 exact_table=1 resident_base=ed18000 APPS_client=8 target_client=3 signal=2 send_offset=c protocol_calls=0 MMIO_reads=0 MMIO_writes=0\n'+fixture()
    text=text.replace('marble-smp2p: complete ','marble-smp2p: notified PASS=1 issued=1 target_client=3 signal=2 register=ed1800c value=30002 owned_header_unchanged=1 thermal_ok=1 die_mc=39000 delivery_verified=0 DSP_start=0 IRQ_changes=0\nmarble-smp2p: complete ')
    return text.replace('elapsed_us=150','elapsed_us=50300').replace('DSP_start=0 ipcc_writes=0 audio_acceptance','DSP_start=0 ipcc_writes=1 audio_acceptance')
def boot_fixture():
    spec=importlib.util.spec_from_file_location('npa_boot_test',Path(__file__).with_name('test_marble_npa_report.py'))
    n=importlib.util.module_from_spec(spec);spec.loader.exec_module(n)
    initial=notify_fixture().replace('marble-smp2p-state: attempted=1 dirty=1 DSP_start=0','marble-smp2p-state: attempted=1 dirty=1 DSP_start=1')
    handshake='\n'.join((
        'marble-smp2p-boot: remote item=443 bytes=344 address=80ad31d8 local=2 remote=0 stable_copy=1 bounded_reads=340 allocator_calls=0',
        'marble-smp2p-boot: ready PASS=1 ready_seen=1 value=6 wait_us=1000 fatal=0 stale_ack=0',
        'marble-smp2p-boot: stop-ack PASS=1 ready_seen=1 stop_ack_seen=1 value=a wait_us=10000',
        'marble-smp2p-boot: stopped PASS=1 ready_seen=1 stop_ack_seen=1 stop_clear=1 notifications=3 PAS_shutdown_allowed=1 payload_access=0'))
    return initial+'\n'+n.boot_fixture().replace('marble-pil-held: shutdown ',handshake+'\nmarble-pil-held: shutdown ')
class SmpEvidence(unittest.TestCase):
    def test_expected(self):
        r=m.inspect(fixture());self.assertTrue(r['passed'],r);self.assertFalse(r['cold_reset_verified']);self.assertFalse(r['dsp_started']);self.assertFalse(r['audio_acceptance'])
    def test_direction_and_extent(self):
        for a,b in (('local_item=429','local_item=443'),('address=80ad3070','address=80ad3078'),('partition_bytes=131072','partition_bytes=1'),('bytes=344','bytes=340')):
            self.assertFalse(m.inspect(fixture().replace(a,b))['passed'])
    def test_no_execution_or_notification(self):
        for a,b in (('DSP_start=0','DSP_start=1'),('ipcc_writes=0','ipcc_writes=1'),('master_value=0','master_value=1')):
            self.assertFalse(m.inspect(fixture().replace(a,b))['passed'])
    def test_reset_watchdog_and_bounds(self):
        for a,b in (('dirty=1','dirty=0'),('reset_required=1','reset_required=0'),('watchdog_retained=1','watchdog_retained=0'),('elapsed_us=150','elapsed_us=1000000')):
            self.assertFalse(m.inspect(fixture().replace(a,b))['passed'])
    def test_duplicate_and_order(self):
        lines=fixture().splitlines();self.assertFalse(m.inspect(fixture()+'\n'+lines[-1])['passed'])
        lines[0],lines[1]=lines[1],lines[0];self.assertFalse(m.inspect('\n'.join(lines))['passed'])
    def test_clean_refusal_is_not_success(self):
        r=m.inspect('marble-smp2p-state: attempted=1 dirty=0 DSP_start=0');self.assertTrue(r['clean_refusal']);self.assertFalse(r['passed'])
    def test_notification_separate_contract(self):
        r=m.inspect_notify(notify_fixture());self.assertTrue(r['passed'],r);self.assertFalse(r['delivery_verified']);self.assertTrue(r['ipcc_writes']);self.assertFalse(r['dsp_started'])
        self.assertFalse(m.inspect(notify_fixture())['passed']);self.assertFalse(m.inspect_notify(fixture())['passed'])
    def test_notification_target_and_qualification(self):
        for a,b in (('resident_base=ed18000','resident_base=ed10000'),('target_client=3','target_client=2'),('register=ed1800c','register=ed1801c'),('value=30002','value=20002'),('exact_table=1','exact_table=0')):
            self.assertFalse(m.inspect_notify(notify_fixture().replace(a,b))['passed'])
    def test_notification_not_delivery_or_irq_change(self):
        for a,b in (('delivery_verified=0','delivery_verified=1'),('IRQ_changes=0','IRQ_changes=1'),('owned_header_unchanged=1','owned_header_unchanged=0'),('thermal_ok=1','thermal_ok=0'),('die_mc=39000','die_mc=55000')):
            self.assertFalse(m.inspect_notify(notify_fixture().replace(a,b))['passed'])
    def test_notification_must_follow_publication(self):
        lines=notify_fixture().splitlines();at=next(i for i,x in enumerate(lines) if x.startswith('marble-smp2p: notified '));line=lines.pop(at)
        lines.insert(next(i for i,x in enumerate(lines) if x.startswith('marble-smp2p: prepared ')),line)
        self.assertFalse(m.inspect_notify('\n'.join(lines))['passed'])
    def test_powered_stage_requires_both_lifetimes(self):
        spec=importlib.util.spec_from_file_location('npa_test',Path(__file__).with_name('test_marble_npa_report.py'))
        n=importlib.util.module_from_spec(spec);spec.loader.exec_module(n)
        text=notify_fixture()+'\n'+n.powered_fixture()
        self.assertTrue(m.inspect_powered_stage(text)['passed'])
        self.assertFalse(m.inspect_powered_stage(n.powered_fixture())['passed'])
        self.assertFalse(m.inspect_powered_stage(text.replace('ipcc_writes=1','ipcc_writes=0'))['passed'])
        self.assertFalse(m.inspect_powered_stage(n.powered_fixture()+'\n'+notify_fixture())['passed'])
    def test_boot_requires_real_ready_stop_and_pas_closure(self):
        r=m.inspect_boot(boot_fixture());self.assertTrue(r['passed'],r);self.assertTrue(r['smp2p_ready_and_stop_verified']);self.assertFalse(r['dsp_service_ready']);self.assertFalse(r['audio_acceptance'])
        self.assertFalse(m.inspect_powered_stage(boot_fixture())['passed'])
    def test_boot_refuses_stale_ack_and_fatal(self):
        for a,b in (('value=6 wait_us=1000','value=e wait_us=1000'),('value=a wait_us=10000','value=2 wait_us=10000'),('value=a wait_us=10000','value=b wait_us=10000'),('stop_ack_seen=1','stop_ack_seen=0'),('stop_clear=1','stop_clear=0')):
            self.assertFalse(m.inspect_boot(boot_fixture().replace(a,b))['passed'])
    def test_boot_enforces_time_and_notification_bounds(self):
        for a,b in (('wait_us=1000','wait_us=2000000'),('wait_us=10000','wait_us=5000000'),('notifications=3','notifications=2'),('notifications=3','notifications=9'),('PAS_shutdown_allowed=1','PAS_shutdown_allowed=0')):
            self.assertFalse(m.inspect_boot(boot_fixture().replace(a,b))['passed'])
    def test_boot_never_accepts_restore_before_handshake(self):
        lines=boot_fixture().splitlines();at=next(i for i,x in enumerate(lines) if x.startswith('marble-smp2p-boot: stopped '));line=lines.pop(at)
        lines.insert(next(i for i,x in enumerate(lines) if x.startswith('marble-pil-held: restore-gate ')),line)
        self.assertFalse(m.inspect_boot('\n'.join(lines))['passed'])
if __name__=='__main__':unittest.main()
