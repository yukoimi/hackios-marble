"""Refuse ambiguous cleanup and apparently successful but incomplete votes."""
import importlib.util,unittest
from pathlib import Path
s=importlib.util.spec_from_file_location('npa_report',Path(__file__).resolve().parents[1]/'marble-npa-report.py')
npa=importlib.util.module_from_spec(s);s.loader.exec_module(npa)

def fixture(votes=False):
    lines=[f'marble-npa-client: create index={i} status=0 handle={0x90000000+i*0x100:x}' for i in range(2)]
    if votes:lines += [f'marble-npa-client: request index={i} configured_level=256 status=0 aggregate=256 actual_state=256 PASS=1 DSP_start=0' for i in range(2)]
    lines += [f'marble-npa-client: destroy index={i} status=0 implicit_complete=1' for i in (1,0)]
    lines += [f'marble-npa-client: restore index={i} PASS=1 client_head=0 state=256 max=480 aggregates=0' for i in range(2)]
    lines += [f'marble-npa-client: complete PASS=1 created=2 destroyed=2 restored=2 active=0 dirty=0 elapsed_us=100500 watchdog_clear=0 explicit_requests={2 if votes else 0} DSP_start=0 audio_acceptance=0',
              'marble-npa-client-state: attempted=1 active=0 dirty=0']
    return '\n'.join(lines)

def native_fixture():
    text=fixture(True).replace('PASS=1 client_head=0 state=256','PASS=0 client_head=0 state=0')
    text=text.replace('max=480 aggregates=0','max=480 aggregates=0 read_mask=f')
    text=text.replace('complete PASS=1 created=2 destroyed=2 restored=2 active=0 dirty=0','complete PASS=0 created=2 destroyed=2 restored=0 active=0 dirty=1')
    text=text.replace('watchdog_clear=0','watchdog_clear=8000000000000007').replace('attempted=1 active=0 dirty=0','attempted=1 active=0 dirty=1')
    bindings=[f'marble-npa-vcs: index={i} exact_binding=1 rail_cached_corner=256 resource_cached_state=256 corner_config=0 min=0 max=480 init=256 physical_voltage_unmeasured=1 protocol_calls=0 DSP_start=0' for i in range(2)]
    releases=[f'marble-npa-native-release: index={i} PASS=1 rail_read=1 rail_cached_corner=0 resource_cached_state=0 empty_clients=1 aggregate_zero=1 physical_voltage_unmeasured=1' for i in range(2)]
    final='marble-npa-native-release: complete PASS=1 released=2 original_restore=0 reset_required=1 watchdog_retained=1 active=0 dirty=1 DSP_start=0 audio_acceptance=0'
    return '\n'.join(bindings+[text]+releases+[final])

def powered_fixture():
    action='\n'.join((
        'marble-npa-held: begin active=2 requests=2 watchdog_seconds=30 limit_us=15000000 DSP_start=0',
        'marble-pil-held: loaded PASS=1 native_rc=0 files=24 ROM_matches=24 zero_regions=25 loads=25 backup_retained=1 original_restored=0 auth_reset=0 dsp_start=0',
        'marble-pil-held: shutdown status=0 transport=1 secure=0 verified=1 target_access=0',
        'marble-pil-held: restore-gate shutdown_verified=1 display_disjoint=1 target_access=1',
        'marble-pil-held: restored PASS=1 original_restored=1 free_status=0 backup_retained=0 shutdown_before_restore=1 DSP_start=0',
        'marble-pil-header: complete PASS=1 metadata_clear=1 PAS_metadata_requested=1 auth_reset=0 dsp_start=0',
        'marble-pil-ramfs: released PASS=1 installed=0 dirty=0 file_writes=0 block_reads=0 block_writes=0 watchdog_clear=8000000000000007 dsp_start=0 opens=57 closes=57',
        'marble-npa-held: returned outcome=1 release_allowed=1 active=2 watchdog_retained=1'))
    return native_fixture().replace('marble-npa-client: destroy index=1',action+'\nmarble-npa-client: destroy index=1').replace('elapsed_us=100500','elapsed_us=9000000')

def boot_fixture():
    lines=powered_fixture().splitlines();new=[]
    for line in lines:
        if line.startswith('marble-pil-held: shutdown '):
            new += ['marble-pil-boot: auth status=0 transport=1 secure=0 elapsed_us=200 accepted=1 hold_limit_ms=500 payload_access=0',
                'marble-pil-boot: observed hold_us=501000 thermal_samples=5 thermal_ok=1 die_mc=42000 link_up=0 link_down=0 link_invalid=0 service_acceptance=0 audio_acceptance=0']
        if line.startswith('marble-pil-held: restore-gate '):new.append('marble-glink: released PASS=1 active=0 dirty=0 up=0 down=0 invalid=0 open=0 tx=0 dsp_start_attempted=1 pmic_writes=0')
        if line.startswith(('marble-pil-held: restored ','marble-pil-header: complete ','marble-pil-ramfs: released ','marble-npa-client: complete ','marble-npa-native-release: complete ')):
            line=line.replace('auth_reset=0','auth_reset=1').replace('dsp_start=0','dsp_start=1').replace('DSP_start=0','DSP_start=1')
        new.append(line)
    return '\n'.join(new)

class NpaEvidence(unittest.TestCase):
    def test_native_stages(self):
        for votes in (False,True):
            r=npa.inspect(fixture(votes),2 if votes else 0)
            self.assertTrue(r['passed'],r);self.assertTrue(r['cleanup_verified']);self.assertFalse(r['audio_acceptance'])
    def test_refusal_can_be_clean_but_never_passed(self):
        r=npa.inspect('marble-npa-client-state: attempted=0 active=0 dirty=0',0)
        self.assertTrue(r['cleanup_verified']);self.assertFalse(r['passed'])
    def test_conflicting_state_never_authorizes_reboot(self):
        r=npa.inspect(fixture()+'\nmarble-npa-client-state: attempted=1 active=1 dirty=1',0)
        self.assertFalse(r['passed']);self.assertFalse(r['cleanup_verified'])
    def test_handle_alias(self):
        self.assertFalse(npa.inspect(fixture().replace('handle=90000100','handle=90000000'),0)['passed'])
    def test_failed_destroy(self):
        self.assertFalse(npa.inspect(fixture().replace('destroy index=0 status=0','destroy index=0 status=80000007'),0)['passed'])
    def test_restore_required(self):
        self.assertFalse(npa.inspect(fixture().replace('state=256 max=480 aggregates=0','state=256 max=480 aggregates=256'),0)['passed'])
    def test_wrong_level(self):
        self.assertFalse(npa.inspect(fixture(True).replace('configured_level=256','configured_level=384'),2)['passed'])
    def test_request_mode_and_count(self):
        self.assertFalse(npa.inspect(fixture(),2)['passed'])
        self.assertFalse(npa.inspect(fixture(True)+'\nmarble-npa-client: request index=1 configured_level=256 status=0 aggregate=256 actual_state=256 PASS=1 DSP_start=0',2)['passed'])
    def test_watchdog_and_duration(self):
        for field,value in (('watchdog_clear=0','watchdog_clear=80000007'),('elapsed_us=100500','elapsed_us=5000000')):
            self.assertFalse(npa.inspect(fixture().replace(field,value),0)['passed'])
    def test_native_release_never_claims_original_restore(self):
        r=npa.inspect_native_release(native_fixture());self.assertTrue(r['passed'],r)
        self.assertFalse(r['cleanup_verified']);self.assertFalse(r['original_restoration']);self.assertTrue(r['reset_required'])
        self.assertFalse(r['cold_reset_verified']);self.assertFalse(npa.inspect(native_fixture(),2)['passed'])
    def test_native_requires_both_driver_and_framework_zero(self):
        for field,value in (('rail_read=1','rail_read=0'),('rail_cached_corner=0','rail_cached_corner=256'),('read_mask=f','read_mask=7')):
            self.assertFalse(npa.inspect_native_release(native_fixture().replace(field,value))['passed'])
    def test_native_requires_original_failure_and_watchdog(self):
        for field,value in (('original_restore=0','original_restore=1'),('watchdog_retained=1','watchdog_retained=0'),('reset_required=1','reset_required=0')):
            self.assertFalse(npa.inspect_native_release(native_fixture().replace(field,value))['passed'])
    def test_native_refuses_another_board_configuration(self):
        self.assertFalse(npa.inspect_native_release(native_fixture().replace('corner_config=0','corner_config=1'))['passed'])
    def test_native_refuses_conflicting_final_record(self):
        self.assertFalse(npa.inspect_native_release(native_fixture()+'\nmarble-npa-client-state: attempted=1 active=1 dirty=1')['passed'])
    def test_powered_lifetime_separate_budget(self):
        r=npa.inspect_powered_stage(powered_fixture());self.assertTrue(r['passed'],r)
        self.assertFalse(r['original_restoration']);self.assertFalse(r['cold_reset_verified']);self.assertFalse(r['audio_acceptance'])
        self.assertFalse(npa.inspect_native_release(powered_fixture())['passed'])
    def test_powered_requires_shutdown_and_ownership(self):
        for before,after in (('verified=1 target_access=0','verified=0 target_access=0'),('display_disjoint=1','display_disjoint=0'),('backup_retained=0','backup_retained=1')):
            self.assertFalse(npa.inspect_powered_stage(powered_fixture().replace(before,after))['passed'])
    def test_powered_refuses_early_restore_and_power_release(self):
        lines=powered_fixture().splitlines()
        for prefix in ('marble-pil-held: restored ','marble-npa-client: destroy index=1'):
            altered=lines[:];at=next(i for i,x in enumerate(altered) if x.startswith(prefix));line=altered.pop(at)
            altered.insert(next(i for i,x in enumerate(altered) if x.startswith('marble-pil-held: shutdown ')),line)
            self.assertFalse(npa.inspect_powered_stage('\n'.join(altered))['passed'])
    def test_powered_requires_watchdog_and_balanced_files(self):
        for before,after in (('watchdog_clear=8000000000000007 dsp_start','watchdog_clear=0 dsp_start'),('opens=57 closes=57','opens=57 closes=56'),('metadata_clear=1','metadata_clear=0')):
            self.assertFalse(npa.inspect_powered_stage(powered_fixture().replace(before,after))['passed'])
    def test_powered_refuses_missing_or_duplicate_record(self):
        text=powered_fixture()
        line=next(x for x in text.splitlines() if x.startswith('marble-pil-held: shutdown '))
        for value in (text.replace(line,''),text+'\n'+line):self.assertFalse(npa.inspect_powered_stage(value)['passed'])
    def test_powered_total_bound_and_no_dsp(self):
        for before,after in (('elapsed_us=9000000','elapsed_us=15000000'),('auth_reset=0 dsp_start=0','auth_reset=1 dsp_start=1'),('outcome=1 release_allowed=1','outcome=-1 release_allowed=0')):
            self.assertFalse(npa.inspect_powered_stage(powered_fixture().replace(before,after))['passed'])
    def test_bounded_boot_is_a_separate_acceptance(self):
        r=npa.inspect_bounded_boot(boot_fixture());self.assertTrue(r['passed'],r)
        self.assertTrue(r['dsp_secure_auth_accepted']);self.assertFalse(r['dsp_service_ready']);self.assertFalse(r['audio_acceptance'])
        self.assertFalse(npa.inspect_powered_stage(boot_fixture())['passed']);self.assertFalse(npa.inspect_native_release(boot_fixture())['passed'])
    def test_bounded_boot_rejects_secure_error_or_lost_observer(self):
        for before,after in (('secure=0 elapsed_us=200','secure=7 elapsed_us=200'),('dsp_start_attempted=1','dsp_start_attempted=0'),('active=0 dirty=0 up=0','active=1 dirty=1 up=0'),('thermal_ok=1','thermal_ok=0')):
            self.assertFalse(npa.inspect_bounded_boot(boot_fixture().replace(before,after))['passed'])
    def test_bounded_boot_requires_bounded_observation(self):
        for before,after in (('hold_us=501000','hold_us=499999'),('hold_us=501000','hold_us=750000'),('die_mc=42000','die_mc=55000'),('thermal_samples=5','thermal_samples=4')):
            self.assertFalse(npa.inspect_bounded_boot(boot_fixture().replace(before,after))['passed'])
    def test_bounded_boot_rejects_observer_release_before_stop(self):
        lines=boot_fixture().splitlines();at=next(i for i,x in enumerate(lines) if x.startswith('marble-glink: released '));line=lines.pop(at)
        lines.insert(next(i for i,x in enumerate(lines) if x.startswith('marble-pil-held: shutdown ')),line)
        self.assertFalse(npa.inspect_bounded_boot('\n'.join(lines))['passed'])

if __name__=='__main__':unittest.main()
