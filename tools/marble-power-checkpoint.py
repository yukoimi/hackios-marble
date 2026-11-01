"""Verified incremental peripheral milestone; restore alongside immutable UI v8."""
import argparse,hashlib,importlib.util,json,subprocess,tarfile
from datetime import datetime,timezone
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda:f.read(4*1024*1024),b''):h.update(block)
    return h.hexdigest()

def pack(archive,entries):
    records={}
    with tarfile.open(archive,'w:gz',compresslevel=3) as tf:
        for name,(path,mode) in sorted(entries.items()):
            info=tf.gettarinfo(str(path),arcname=name);info.uid=info.gid=0;info.uname=info.gname=''
            info.mode=0o755 if mode=='100755' else 0o644
            if mode=='120000':
                info.type=tarfile.SYMTYPE;info.linkname=str(path.readlink()) if path.is_symlink() else path.read_text().strip();info.size=0
                records[name]=dict(link=info.linkname);tf.addfile(info)
            else:
                records[name]=dict(bytes=path.stat().st_size,sha256=digest(path))
                with path.open('rb') as stream:tf.addfile(info,stream)
                if digest(path)!=records[name]['sha256']:raise ValueError('source changed during packing: '+name)
    with tarfile.open(archive,'r:gz') as tf:
        names=[]
        for item in tf:
            names.append(item.name);r=records[item.name]
            if item.issym():
                if item.linkname!=r['link']:raise ValueError('link mismatch')
            else:
                h=hashlib.sha256();stream=tf.extractfile(item)
                for block in iter(lambda:stream.read(4*1024*1024),b''):h.update(block)
                if item.size!=r['bytes'] or h.hexdigest()!=r['sha256']:raise ValueError('archive mismatch: '+item.name)
        if len(names)!=len(set(names)) or set(names)!=set(records):raise ValueError('archive closure mismatch')
    return records

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('directory',type=Path);p.add_argument('--verify',action='store_true');a=p.parse_args()
    target=a.directory.resolve()
    if not target.is_relative_to(ROOT/'out'):p.error('local out directory required')
    if a.verify:
        data=json.loads((target/'manifest.json').read_text())
        for name,row in data['files'].items():
            path=(target/name).resolve()
            if not path.is_relative_to(target) or digest(path)!=row['sha256']:raise ValueError('checkpoint mismatch: '+name)
        print('Verified',len(data['files']),'checkpoint files; archive members verified when created');return
    if target.exists():p.error('new directory required')
    s=importlib.util.spec_from_file_location('base_checkpoint',ROOT/'tools/marble-checkpoint.py');base=importlib.util.module_from_spec(s);s.loader.exec_module(base)
    sources=base.sources();required={x.relative_to(ROOT).as_posix() for x in (ROOT/'xnuloader').glob('marble_*.h')}
    if not required<=sources.keys() or 'xnuloader/marble_efi_chain.c' not in sources:raise ValueError('loader source closure missing')
    directories=['out/peripheral-power-audit-v1-plan','out/gpr-source-v1-plan','out/adsp-dependency-source-v1-plan','out/adsp-native-lease-audit-v1-plan','out/adsp-native-dependencies-v2-plan','out/adsp-owned-package-v1-plan','out/qmp-order-v74-audit','out/scm-bandwidth-v76-audit','out/scm-waitq-v85-audit','out/ipcc-native-v1-plan','out/glink-smem-v90-audit','out/glink-runtime-v94-audit','out/irq-observer-v96-audit','out/spf-core-v99-audit','out/audio-route-v103-plan','out/i2c-controller-v105-audit']
    for pattern in ('daily-adsp-power-v40*','daily-qmp-native-v41*','daily-qmp-native-pump-v42*','daily-qmp-peer-close-v43*','daily-qmp-settled-close-v44*','daily-adsp-xo-v45*','daily-rpmh-zero-v46*','daily-rpmh-zero-v47*','daily-icb-inventory-v48*','daily-icb-inventory-v49*',
                    'daily-adsp-dependencies-v50*','daily-icb-zero-v51*','daily-icb-trace-v52*','daily-icb-bound-*v53*','daily-icb-bound-*v54*','daily-adsp-slim-v55*','daily-xo-npa-software-v56*','daily-xo-rpmh-hold-v57*',
                    'daily-icb-bandwidth-v58*','daily-icb-bandwidth-diff-v59*','daily-icb-bandwidth-cache-v60*','daily-icb-derived-cache-v61*','daily-icb-topology-v62*','daily-icb-node-identity-v63*','daily-icb-native-run-v64*','daily-icb-run-bandwidth-v65*','daily-adsp-owned-dependencies-v66*','daily-adsp-input-package-v67*','daily-adsp-owned-ROM-v68*','daily-adsp-owned-boot-v69*',
                    'daily-qmp-load-state-v70*','daily-qmp-mailbox-v71*','daily-adsp-qmp-ROM-v72*','daily-adsp-qmp-ROM-v73*','daily-adsp-qmp-ROM-v74*','daily-adsp-qmp-ROM-v75*','daily-icb-scm-peak-v76*',
                    'daily-adsp-scm-ROM-v77*','daily-adsp-scm-qmp-ROM-v78*','daily-qmp-wire-v79*','daily-qmp-wire-v80*','daily-qmp-wire-v81*','daily-adsp-scm-wire-ROM-v82*','daily-adsp-scm-channel-ROM-v83*','daily-qmp-tpl-v84*','daily-qmp-smccc-v85*','daily-adsp-scm-bounded-ROM-v86*','daily-qmp-ipcc-v87*','daily-adsp-scm-ipcc-ROM-v88*','daily-adsp-scm-ipcc-ROM-v89*','daily-glink-smem-v90*','daily-glink-smem-v91*','daily-glink-smem-v92*','daily-adsp-held-ROM-v93*','daily-adsp-control-v94*','daily-adsp-control-v95*','daily-adsp-control-v96*','daily-adsp-control-v97*','daily-adsp-control-v98*','daily-adsp-apps-v99*','daily-adsp-apps-v100*','daily-adsp-spf-v101*','audio-route-v102*','qmp-wire-v81-cooldown*','qmp-channel-v83-cooldown*','pas-ipcc-v89-cooldown*'):
        directories += [x.relative_to(ROOT).as_posix() for x in sorted((ROOT/'out').glob(pattern)) if x.is_dir()]
    directories += ['out/efi-chain-v147-audit','out/efi-chain-v147b-plan','out/audio-tdm-pins-v148-audit','out/audio-aw-profile-v151-audit']
    directories += ['out/audio-i2c-vendor-v159-audit']
    directories += [x.relative_to(ROOT).as_posix() for pattern in ('audio-aw-digital-v*-audit','upstream-audio-v*-audit') for x in sorted((ROOT/'out').glob(pattern)) if x.is_dir()]
    directories += ['out/audioreach-tdm-v121-audit','out/audio-tdm-board-v122-plan','out/audio-prm-clock-v130-audit','out/audio-pcm-board-v136-plan','out/audio-pcm-graph-v136-audit','out/audio-lpass-tbu-v144-audit']
    for pattern in ('daily-i2c-*','daily-adsp-amdb-*','daily-adsp-tdm-*','daily-adsp-audio-clock-*','daily-adsp-pcm-*','audio-log-format-v*-reference'):
        directories += [x.relative_to(ROOT).as_posix() for x in sorted((ROOT/'out').glob(pattern)) if x.is_dir()]
    reports={}
    for name in directories:
        path=ROOT/name/'exercise.json'
        if path.exists():
            r=json.loads(path.read_text())
            # These explicit preflight connection refusals occurred before
            # any reboot or firmware probe. Preserve unconfirmed screen-off
            # evidence without counting either refusal as a hardware pass.
            no_boot=False
            if name in ('out/daily-qmp-wire-v79-live','out/daily-i2c-amp-profile-v151-live'):
                battery=json.loads((ROOT/name/'battery-before.json').read_text())
                no_boot=(r.get('error')=='Android battery temperature was not confirmed' and
                    not battery.get('confirmed') and 'phone_reboot_requested' not in r and
                    'idle_probe' not in r and not (ROOT/name/'qmp-wire-cycle.log').exists() and not (ROOT/name/'i2c-inventory.log').exists())
            if not r.get('android_screen_off') and not no_boot:raise ValueError('phone recovery unfinished: '+name)
            reports[name]=dict(error=r.get('error'),battery_before_c=r.get('battery_before_c'),battery_after_c=r.get('battery_after_c'),
                qmp_passed=r.get('qmp_native_cycle_with_reset_passed',False),inventory_passed=r.get('adsp_power_inventory',{}).get('passed',False),
                qmp_mailbox_ack_passed=bool(r.get('qmp_mailbox_load_state_cycle_requested') and r.get('qmp_native_cycle_with_reset_passed') and r.get('qmp_native_report',{}).get('load_state_transport_ack_verified')),
                owned_post_load_qmp_unwind_passed=bool(r.get('adsp_qmp_stage_requested') and r.get('adsp_owned_with_reset_passed') and r.get('adsp_owned_report',{}).get('qmp_noexec_unwind_verified')),
                xo_metadata_passed=r.get('adsp_xo_inventory',{}).get('passed',False),
                rpmh_zero_passed=r.get('rpmh_zero_with_reset_passed',False),icb_metadata_passed=r.get('adsp_icb_inventory',{}).get('passed',False),
                icb_zero_passed=r.get('icb_zero_with_reset_passed',False),xo_software_passed=r.get('xo_npa_with_reset_passed',False),
                xo_physical_hold_passed=r.get('xo_rpmh_hold_with_reset_passed',False),bandwidth_request_reset_passed=r.get('icb_bandwidth_with_reset_passed',False),
                native_icb_run_passed=r.get('icb_run_with_reset_passed',False),run_bandwidth_passed=r.get('icb_run_bandwidth_with_reset_passed',False),
                scm_peak_bandwidth_passed=bool(r.get('icb_run_scm_bandwidth_requested') and r.get('icb_run_bandwidth_with_reset_passed') and r.get('icb_run_bandwidth_report',{}).get('scm_peak_request_verified')),
                simultaneous_dependencies_passed=r.get('adsp_dependency_with_reset_passed',False),
                owned_noexec_ROM_passed=r.get('adsp_owned_with_reset_passed',False),
                owned_SCM_ROM_passed=bool(r.get('adsp_scm_stage_requested') and r.get('adsp_owned_with_reset_passed') and r.get('adsp_owned_report',{}).get('scm_peak_held_ROM_verified')),
                wire_passed=r.get('qmp_wire_with_reset_passed',False),
                wire_TPL_roundtrip_passed=bool(r.get('qmp_tpl_cycle_requested') and r.get('qmp_wire_with_reset_passed') and r.get('qmp_wire_report',{}).get('TPL_roundtrip_verified')),
                wire_bounded_SMCCC_queries_passed=bool(r.get('qmp_smccc_cycle_requested') and r.get('qmp_wire_with_reset_passed') and r.get('qmp_wire_report',{}).get('SMCCC_availability_verified')),
                wire_owned_IPCC_ack_passed=bool(r.get('qmp_ipcc_cycle_requested') and r.get('qmp_wire_with_reset_passed') and r.get('qmp_wire_report',{}).get('owned_IPCC_acknowledgement_verified')),
                owned_SCM_wire_ROM_passed=bool(r.get('adsp_scm_wire_stage_requested') and r.get('adsp_owned_with_reset_passed') and r.get('adsp_owned_report',{}).get('AP_wire_noexec_unwind_verified')),
                owned_SCM_channel_ROM_passed=bool(r.get('adsp_scm_channel_stage_requested') and r.get('adsp_owned_with_reset_passed') and r.get('adsp_owned_report',{}).get('AP_channel_noexec_unwind_verified')),
                owned_IPCC_bounded_PAS_passed=bool(r.get('adsp_scm_ipcc_stage_requested') and r.get('adsp_owned_with_reset_passed') and r.get('adsp_owned_report',{}).get('owned_IPCC_PAS_shutdown_verified')),
                held_load_state_shutdown_passed=bool(r.get('adsp_scm_held_stage_requested') and r.get('adsp_owned_with_reset_passed') and r.get('adsp_owned_report',{}).get('load_state_held_through_shutdown_verified')),
                GLINK_SMEM_inventory_passed=bool(r.get('glink_smem_inventory',{}).get('passed') and r.get('android_screen_off')),
                control_boot_passed=bool(r.get('adsp_control_boot_requested') and r.get('adsp_owned_boot_with_reset_passed') and r.get('adsp_owned_boot_report',{}).get('GLINK_control_link_verified')),
                apps_open_passed=bool(r.get('adsp_apps_open_requested') and r.get('adsp_owned_boot_with_reset_passed') and r.get('adsp_owned_boot_report',{}).get('adsp_apps_channel_verified')),
                SPF_ready_passed=bool(r.get('adsp_spf_state_requested') and r.get('adsp_owned_boot_with_reset_passed') and r.get('adsp_spf_state',{}).get('spf_ready')),
                i2c_inventory_passed=bool(r.get('i2c_inventory_with_reset_passed') and r.get('i2c_inventory',{}).get('protocol_and_code_verified')),
                i2c_runtime_snapshot_passed=bool(r.get('i2c_inventory_with_reset_passed') and r.get('i2c_runtime_config_capture_passed')),
                i2c_clock_names_passed=bool(r.get('i2c_inventory_with_reset_passed') and r.get('i2c_inventory',{}).get('clock_names_verified')),
                i2c_clock_lease_passed=bool(r.get('i2c_inventory_with_reset_passed') and r.get('i2c_inventory',{}).get('clock_lease_verified')),
                i2c_passive_snapshot_passed=bool(r.get('i2c_inventory_with_reset_passed') and r.get('i2c_inventory',{}).get('passive_snapshot_captured')),
                i2c_controller_identity_passed=bool(r.get('i2c_inventory_with_reset_passed') and r.get('i2c_inventory',{}).get('controller_identity_verified')),
                i2c_setup_restore_passed=bool(r.get('i2c_inventory_with_reset_passed') and r.get('i2c_inventory',{}).get('bus_configuration_verified')),
                i2c_two_amplifier_IDs_passed=bool(r.get('i2c_amp_id_requested') and r.get('i2c_inventory_with_reset_passed') and r.get('i2c_inventory',{}).get('chip_id_verified')),
                i2c_reset_registers_passed=bool(r.get('i2c_inventory_with_reset_passed') and r.get('i2c_inventory',{}).get('amplifier_reset_registers_verified')),
                i2c_quiet_write_passed=bool(r.get('i2c_inventory_with_reset_passed') and r.get('i2c_inventory',{}).get('amplifier_quiet_write_verified')),
                i2c_Music_profile_passed=bool(r.get('i2c_inventory_with_reset_passed') and r.get('i2c_inventory',{}).get('amplifier_Music_profile_verified')),
                AMDB_library_cycle_passed=bool(r.get('adsp_owned_boot_with_reset_passed') and r.get('adsp_amdb_load_cycle',{}).get('passed') and r.get('adsp_amdb_load_cycle',{}).get('module_acquired') and r.get('adsp_amdb_load_cycle',{}).get('module_released')),
                TDM_graph_cycle_passed=bool(r.get('adsp_owned_boot_with_reset_passed') and r.get('adsp_tdm_graph_cycle',{}).get('graph_created_and_closed')),
                TDM_board_config_passed=bool(r.get('adsp_owned_boot_with_reset_passed') and r.get('adsp_tdm_graph_cycle',{}).get('board_configuration_accepted')),
                guarded_PRM_clock_cycle_passed=bool(r.get('adsp_owned_boot_with_reset_passed') and r.get('adsp_audio_clock_cycle',{}).get('clock_lease_verified')),
                silent_TDM_run_passed=bool(r.get('adsp_owned_boot_with_reset_passed') and r.get('adsp_tdm_run_cycle',{}).get('silent_TDM_lifecycle_verified')),
                owned_audio_context_passed=bool(r.get('adsp_owned_boot_with_reset_passed') and r.get('adsp_audio_context_inventory',{}).get('passed')),
                owned_audio_mapping_passed=bool(r.get('adsp_owned_boot_with_reset_passed') and r.get('adsp_audio_map_cycle',{}).get('passed')),
                typed_DSP_map_unmap_passed=bool(r.get('adsp_owned_boot_with_reset_passed') and r.get('adsp_pcm_memory_cycle',{}).get('passed')),
                silent_PCM_consumption_passed=bool(r.get('adsp_owned_boot_with_reset_passed') and r.get('adsp_pcm_data_cycle',{}).get('typed_buffer_consumed')),
                TDM_pins_restore_passed=bool(r.get('adsp_owned_boot_with_reset_passed') and r.get('adsp_tdm_pins_cycle',{}).get('TDM_pins_routed_and_restored')),
                QUP2_minimum_hold_passed=bool(r.get('adsp_owned_boot_with_reset_passed') and r.get('qup_core_keepalive',{}).get('passed')),
                DSP_I2C_window_passed=bool(r.get('adsp_owned_boot_with_reset_passed') and r.get('audio_i2c_window',{}).get('passed')),
                DSP_I2C_Music_profile_passed=bool(r.get('adsp_owned_boot_with_reset_passed') and r.get('audio_i2c_window',{}).get('amplifier_Music_profile_verified')),
                DSP_I2C_digital_PLL_passed=bool(r.get('adsp_owned_boot_with_reset_passed') and r.get('audio_i2c_window',{}).get('amplifier_digital_PLL_verified')),
                DSP_I2C_muted_analog_passed=bool(r.get('adsp_owned_boot_with_reset_passed') and r.get('audio_i2c_window',{}).get('amplifier_muted_analog_verified')),
                DSP_USB_RAM_only_passed=bool(r.get('adsp_owned_boot_with_reset_passed') and r.get('audio_usb_ram_only',{}).get('passed')),
                owned_boot_passed=r.get('adsp_owned_boot_with_reset_passed',False),
                boot_not_attempted=no_boot,android_screen_off=bool(r.get('android_screen_off')),dsp_started=(None if r.get('adsp_owned_boot_requested') else False),audio_acceptance=False)
    passed=[k for k,v in reports.items() if v['qmp_passed']]
    if len(passed)<2:raise ValueError('two completed QMP closure/reset tests required')
    if len([v for v in reports.values() if v.get('qmp_mailbox_ack_passed')])<2:raise ValueError('two completed QMP mailbox business ACK/reset tests required')
    if len([v for v in reports.values() if v.get('rpmh_zero_passed')])<2:raise ValueError('two completed RPMh zero-command closure/reset tests required')
    if not any(v.get('icb_metadata_passed') for v in reports.values()):raise ValueError('complete native ICB route snapshot required')
    if len([v for v in reports.values() if v.get('wire_owned_IPCC_ack_passed')])<2:raise ValueError('two completed own-QMP IPCC acknowledgement/reset runs required')
    if len([v for v in reports.values() if v.get('owned_IPCC_bounded_PAS_passed')])<2:raise ValueError('two complete owned IPCC/bounded PAS no-exec cleanup/reset runs required')
    if len([v for v in reports.values() if v.get('control_boot_passed')])<2:raise ValueError('two complete DSP control/PAS/restore/explicit reset runs required')
    if len([v for v in reports.values() if v.get('apps_open_passed')])<2:raise ValueError('two adsp_apps open/stop/restore/reset runs required')
    if len([v for v in reports.values() if v.get('SPF_ready_passed')])<2:raise ValueError('two read-only matched SPF-ready/stop/restore/reset runs required')
    if len([v for v in reports.values() if v.get('i2c_runtime_snapshot_passed')])<2:raise ValueError('two bounded runtime DT snapshots/reset runs required')
    if not any(v.get('i2c_clock_lease_passed') for v in reports.values()):raise ValueError('one complete five-clock lease/reset run required')
    if len([v for v in reports.values() if v.get('i2c_two_amplifier_IDs_passed')])<2:raise ValueError('two complete two-amplifier ID/reset runs required')
    if len([v for v in reports.values() if v.get('AMDB_library_cycle_passed')])<2:raise ValueError('two complete TDM library acquire/release/stop/reset runs required')
    if not any(v.get('TDM_board_config_passed') for v in reports.values()):raise ValueError('board-calibrated TDM graph closure/reset required')
    if not any(v.get('guarded_PRM_clock_cycle_passed') for v in reports.values()):raise ValueError('guarded PRM clock request/release/reset required')
    if not any(v.get('silent_PCM_consumption_passed') for v in reports.values()):raise ValueError('one typed silent PCM consumption/unmap/restore/reset run required')
    if not any(v.get('TDM_pins_restore_passed') for v in reports.values()):raise ValueError('actual cape pin restore/reset run required')
    if not any(v.get('i2c_Music_profile_passed') for v in reports.values()):raise ValueError('two-amplifier Music profile/readback/reset run required')
    previous=ROOT/'out/checkpoint-ui-peripherals-v8-20261029/manifest.json'
    if not previous.is_file():raise ValueError('immutable v8 prerequisite missing')
    target.mkdir()
    (target/'git.patch').write_bytes(subprocess.check_output(['git','diff','--binary','HEAD'],cwd=ROOT))
    source_records=pack(target/'sources.tar.gz',sources)
    if not required<=source_records.keys():raise ValueError('finished source archive lacks loader headers')
    (target/'source-files.json').write_text(json.dumps(source_records,indent=2)+'\n')
    evidence={x.relative_to(ROOT).as_posix():(x,'100644') for directory in directories for x in (ROOT/directory).rglob('*') if x.is_file()}
    evidence_records=pack(target/'peripheral-evidence.tar.gz',evidence)
    (target/'evidence-files.json').write_text(json.dumps(evidence_records,indent=2)+'\n')
    manifest=dict(created_utc=datetime.now(timezone.utc).isoformat(),
        status='IN PROGRESS; DSP dependencies/secure lifecycle, amplifier IDs, TDM library acquire/release, board-calibrated TDM graph open/close and guarded PRM clock request/release verified with cold reset; silent TDM lifecycle status is recorded per run; legacy client byte and masked RX caveats retained; connected PCM graph, LPASS ECATS hardware translation, exact owned SMMU map/restore, typed DSP MAP/UNMAP and one silent WREP PCM consumption, actual cape TDM pins restore, UEFI memory loading, AW2032 reset-state snapshots, 90dB/HMUTE write/readback and board Music profiles with per-chip OTP calibration/readback/reset verified; digital PLL lock and muted analog AMPPD-cycle status recorded per run; speaker output, iOS audio and full peripherals incomplete',
        policy='temporary fastboot boot and RAM images only; no Android userdata access',
        prerequisite=dict(path=str(previous.parent),manifest_sha256=digest(previous),scope='stable graphics/system disks and previous build inputs; immutable; external toolchain/runtime environment still required'),
        loader_headers=len(required),source_members=len(source_records),evidence_members=len(evidence_records),reports=reports,
        verified_qmp_runs=passed,files={})
    for path in sorted(target.iterdir()):
        if path.is_file():manifest['files'][path.name]=dict(bytes=path.stat().st_size,sha256=digest(path))
    (target/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print('Saved and verified',len(source_records),'sources,',len(evidence_records),'evidence files,',len(required),'loader headers:',target)
if __name__=='__main__':main()
