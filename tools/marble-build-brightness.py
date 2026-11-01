"""Build a separate brightness candidate and archive its exact source closure."""
import argparse,hashlib,json,os,shutil,struct,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def static_entry(linked,destination):
    # Same UNIXTHREAD conversion used by marble-build-touch.py. No dyld/SDK.
    data=bytearray(linked.read_bytes());header=list(struct.unpack_from('<8I',data))
    commands=[];pos=32;text_base=entry=None
    for _ in range(header[4]):
        cmd,size=struct.unpack_from('<II',data,pos);raw=bytes(data[pos:pos+size]);pos+=size
        if cmd==0x19:
            commands.append(raw)
            if raw[8:24].rstrip(b'\0')==b'__TEXT':text_base=struct.unpack_from('<Q',raw,24)[0]
        elif cmd==0x80000028:entry=struct.unpack_from('<Q',raw,8)[0]
        elif cmd==2:
            symoff,nsyms,_,_=struct.unpack_from('<4I',raw,8)
            for i in range(nsyms):
                _,typ,_,_,_=struct.unpack_from('<IBBHQ',data,symoff+16*i)
                if typ&0xe==0 and typ&1:raise RuntimeError('unresolved static reference')
            commands.append(raw)
        elif cmd in (0xc,0x18,0x8000001c):raise RuntimeError('unexpected dynamic dependency')
    if text_base is None or entry is None:raise RuntimeError('static entry missing')
    state=[0]*34;state[32]=text_base+entry
    commands.append(struct.pack('<4I34Q',5,288,6,68,*state))
    blob=b''.join(commands)
    if len(blob)>0x1000:raise RuntimeError('header padding exceeded')
    header[4]=len(commands);header[5]=len(blob);header[6]=1
    data[:32]=struct.pack('<8I',*header);data[32:32+len(blob)]=blob
    if len(blob)<pos-32:data[32+len(blob):pos]=bytes(pos-32-len(blob))
    destination.write_bytes(data)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--mu-image',type=Path,default=Path(os.environ.get('MARBLE_MU_IMAGE','Mu-marble.img')),help='locally obtained Mu image; identity remains checked by marble-image.py')
    p.add_argument('--efi-opt',choices=('O1','Os','Oz'),default='O1',help='explicit EFI size optimization; retain the original embedded FAT disk layout')
    p.add_argument('--efi-memory-chain',action='store_true',help='LZ4/SHA256 EFI wrapper using UEFI memory LoadImage/StartImage; firmware-only')
    p.add_argument('--efi-chain-codec',choices=('lz4','lzma'),default='lz4',help='bounded EFI memory-chain codec; defaults to original LZ4')
    p.add_argument('--efi-compact-fat-root',action='store_true',help='16-entry FAT root within unchanged disk/partition extents; firmware-only')
    p.add_argument('--efi-lto',action='store_true',help='enable link-time optimization only for the two EFI targets; retain original disk geometry')
    p.add_argument('--efi-pack-formats-from',type=Path,help='LZ4-pack complete constant log formats from an explicit uncompressed EFI reference; firmware profiles only')
    p.add_argument('--efi-pack-logs-from',type=Path,help='losslessly deduplicate repeated print prefixes using an explicit reference EFI; isolated firmware profiles only')
    p.add_argument('--firmware-only',action='store_true',help='separate peripheral diagnostic image that refuses XNU/GPU startup; does not change the desktop loader')
    p.add_argument('--adsp-only',action='store_true',help='firmware-only dependency image omitting legacy SCM/PIL/UI command handlers; retains normal boot disk geometry')
    p.add_argument('--owned-stage-only',action='store_true',help='dedicated no-exec ROM dependency image, omitting other diagnostic handlers; requires --adsp-only')
    p.add_argument('--qmp-only',action='store_true',help='dedicated AOP load-state lifecycle image; no PIL/dependency handlers; requires --firmware-only')
    p.add_argument('--icb-only',action='store_true',help='dedicated ICB SCM bandwidth lifecycle; omits PIL/QMP/UI handlers; requires --firmware-only')
    p.add_argument('--glink-only',action='store_true',help='dedicated read-only native lpass SMEM inventory; omits PIL/QMP/UI handlers; requires --firmware-only')
    p.add_argument('--held-only',action='store_true',help='dedicated no-exec ROM/PAS load_state-held lifecycle; requires isolated --firmware-only')
    p.add_argument('--control-only',action='store_true',help='dedicated bounded DSP control-link boot/stop; no application channels or audio; isolated --firmware-only')
    p.add_argument('--apps-only',action='store_true',help='dedicated adsp_apps open/remote-intent diagnostic; no local intents or GPR DATA; isolated --firmware-only')
    p.add_argument('--gpr-only',action='store_true',help='isolated read-only GPR SPF state query; no audio; requires --firmware-only')
    p.add_argument('--qup-core-keepalive',action='store_true',help='minimal QUP2 ACTIVE/WAKE BCM vote retained until cold reset; requires audio I2C window')
    p.add_argument('--audio-amp-profile',action='store_true',help='ROM Music/OTP down-muted readback in qualified DSP START window; no playback')
    p.add_argument('--audio-amp-prestart',action='store_true',help='complete down-muted Music profile before START; keep first PCM path bounded')
    p.add_argument('--audio-amp-direct-wait',action='store_true',help='bounded CNTVCT waits during 500ms Music runtime; IRQ masks unchanged')
    p.add_argument('--audio-amp-preboot',action='store_true',help='complete down-muted ROM profile before DSP authentication, under QUP2 keepalive')
    p.add_argument('--audio-qup-hold',action='store_true',help='extra balanced QUP refs after START until verified PAS/ROM restore; no rate changes')
    p.add_argument('--audio-runtime-deadline',action='store_true',help='service DSP every round against a real 500ms deadline; includes I2C work')
    p.add_argument('--audio-digital-pll',action='store_true',help='digital-only PWDN/PLL check; AMPPD/HMUTE/90dB retained, always restore/reset')
    p.add_argument('--audio-digital-capture',action='store_true',help='diagnostic return before SMP2P STOP; retain DSP leases/watchdog for log capture and cold reset')
    p.add_argument('--audio-digital-early-stop',action='store_true',help='digital diagnostic STOP immediately after typed PCM/UNMAP/pin restore; no extra idle dwell')
    p.add_argument('--audio-quiet-progress',action='store_true',help='retain typed RAM logs but omit repetitive USB INFO round/Clock traces')
    p.add_argument('--audio-runtime-600ms',action='store_true',help='explicit bounded600ms digital diagnostic with six thermal samples')
    p.add_argument('--audio-fast-stop',action='store_true',help='100us bounded counter polling for digital SMP2P STOP; keep5s timeout/100ms thermal cadence')
    p.add_argument('--audio-usb-ram-only',action='store_true',help='retain post-auth DSP progress in RAM without USB INFO handshakes; requires fast STOP')
    p.add_argument('--audio-muted-analog',action='store_true',help='bounded AMPPD cycle with exact HMUTE/90dB; requires RAM-only USB profile')
    p.add_argument('--audio-owner-kernel',action='store_true',help='isolated combined XNU/EFI zero2 resource-owner smoke, requires complete muted firmware profile')
    p.add_argument('--audio-owner-board',action='store_true',help='explicit owner zero2 with existing muted Music/OTP and TDM pin lifecycle; requires LZMA chain')
    p.add_argument('--audio-owner-periods',type=int,choices=(2,8,24),default=2,help='explicit bounded owner periods; 24 needs separate v2 streaming profile')
    p.add_argument('--audio-owner-pcm',action='store_true',help='explicit original-guard PCM device-write probe, <=64 LSB; eight periods, board remains muted')
    p.add_argument('--audio-board-held',action='store_true',help='hold both muted amplifiers through DATA; explicit drained/quiet gate before STOP')
    p.add_argument('--audio-owner-refill',action='store_true',help='explicit finite PCM8 live-producer gap/refill profile; amps stay muted')
    p.add_argument('--audio-qmp-close-snapshot',action='store_true',help='coherent borrowed QMP close snapshots within the original 750ms deadline')
    p.add_argument('--audio-owner-native-link',action='store_true',help='native XNU link ownership; omit the legacy EFI GLink observer registration')
    p.add_argument('--audio-owner-stream',action='store_true',help='separate v2 muted 24-period 128ms producer/wrap test; original 600ms ceiling')
    p.add_argument('--audio-tpl-events',action='store_true',help='passive pinned Mu event snapshots around PAS RestoreTPL; no callback or timer changes')
    p.add_argument('--audio-display-dma-only',action='store_true',help='audio-only display DMA audit using the original bus lease without VSync timing qualification')
    p.add_argument('--audio-owner-prelaunch',action='store_true',help='start XNU after QMP preflights, before channel acquisition; defer cap until ON')
    p.add_argument('--audio-owner-warmup-ms',type=int,choices=(0,100),default=0)
    p.add_argument('--audio-owner-handshake-ms',type=int,choices=(10,50,100),default=10)
    p.add_argument('--audio-owner-fast-handshake',action='store_true',help='service native XNU startup every10ms; real replies and original bounds required')
    p.add_argument('--audio-usb-qmp-lease',action='store_true',help='hold qualified USB poll pause until QMP close; same outer 1s bound')
    p.add_argument('--audio-runtime-sampling',action='store_true',help='RAM-only own timer during DSP runtime; closed before PAS; strict 600ms bound')
    p.add_argument('--audio-tpl-sampling',action='store_true',help='own temporary software timer samples RAM queue during RestoreTPL, then cancels/closes; foreign timers unchanged')
    p.add_argument('--audio-usb-poll-lease',action='store_true',help='pinned Mu 100ms USB port poll: SetTimer cancel/restart only during PAS TPL8; full original PAS limit, no charging writes')
    p.add_argument('--audio-owner-preauth-trace',action='store_true',help='explicit synchronous owner run with USB phase labels only before DSP AUTH; no post-AUTH USB tracing')
    p.add_argument('--audio-native-endpoint',action='store_true',help='isolated two-zero-period test of the XNU endpoint against the real EFI DSP provider; no XNU handoff')
    p.add_argument('--audio-input-format',choices=('native96','ios48-mfc'),default='native96',help='explicit isolated DSP MFC graph, preserving the iOS input format')
    p.add_argument('--amdb-module',choices=('tdm','mfc'),default='tdm',help='one read-only AMDB metadata module, no graph')
    p.add_argument('--audio-pcm-repeat',action='store_true',help='two serial silent WREP requests with distinct tokens and retired page reuse; original runtime budget')
    p.add_argument('--audio-pcm-periods',type=int,choices=(2,8),default=2,help='bounded silent serial periods; eight requires repeat profile')
    p.add_argument('--audio-pas-timing',action='store_true',help='RAM-only per-call PAS/thermal time accounting; original bounds unchanged')
    p.add_argument('--audio-i2c-window',action='store_true',help='brief QUP2 lease after DSP START, before PCM; no amp commands')
    p.add_argument('--audio-i2c-hold',action='store_true',help='hold qualified QUP2 configuration/clocks across silent DSP lifecycle; no amplifier commands')
    p.add_argument('--tdm-pins-only',action='store_true',help='board-qualified four TDM pins around silent PCM; amplifiers remain reset')
    p.add_argument('--pcm-data-only',action='store_true',help='one zero-filled 4096B WREP buffer and typed consumption event; no pins or amplifier writes')
    p.add_argument('--pcm-memory-only',action='store_true',help='typed DSP MAP/UNMAP around connected graph; retained owned SID/4K lease, no PCM')
    p.add_argument('--audio-map-probe',action='store_true',help='isolated 4K context/LPASS ECATS/exact stream restore; no PCM')
    p.add_argument('--audio-context-inventory',action='store_true',help='passive unreferenced context snapshot; requires DMA inventory')
    p.add_argument('--audio-dma-inventory',action='store_true',help='passive APPS SMMU GR0 snapshot during connected PCM graph probe; no context or DMA writes')
    p.add_argument('--tdm-pcm-graph-only',action='store_true',help='isolated connected shared-memory-source to TDM graph lifecycle; no host buffers or amplifiers')
    p.add_argument('--tdm-run-only',action='store_true',help='isolated silent TDM open/prepare/start/stop/close with passive pin guards')
    p.add_argument('--audio-clock-only',action='store_true',help='isolated guarded PRM tertiary clock request/release; no graphs, pins or amplifier commands')
    p.add_argument('--tdm-config-only',action='store_true',help='isolated graph open with qualified TDM board calibration then close; no PREPARE/START, clocks or amplifiers')
    p.add_argument('--tdm-graph-only',action='store_true',help='isolated disconnected TDM graph open/close; no PREPARE/START, clocks or amplifier commands')
    p.add_argument('--amdb-load-only',action='store_true',help='isolated one pre-registered TDM module library acquire/release; no graph, clocks or audio')
    p.add_argument('--amdb-only',action='store_true',help='isolated one TDM AMDB module metadata query; no graph/audio; requires --firmware-only')
    p.add_argument('--amp-profile-only',action='store_true',help='board Music profile plus OTP calibration, held down/muted; isolated I2C')
    p.add_argument('--amp-quiet-only',action='store_true',help='AW2032 minimum volume write/readback, amps remain powered down; isolated I2C')
    p.add_argument('--amp-registers-only',action='store_true',help='isolated AW2032 reset-state register reads; requires --i2c-only')
    p.add_argument('--i2c-only',action='store_true',help='isolated I2C protocol/code inventory; no controller calls; requires --firmware-only')
    a=p.parse_args()
    if a.audio_owner_board and not (a.audio_owner_kernel and a.efi_memory_chain and a.efi_chain_codec=='lzma'):p.error('--audio-owner-board requires --audio-owner-kernel and explicit LZMA memory chain')
    if a.audio_owner_preauth_trace and not a.audio_owner_board:p.error('--audio-owner-preauth-trace requires --audio-owner-board')
    if a.audio_owner_periods!=2 and not a.audio_owner_kernel:p.error('--audio-owner-periods requires --audio-owner-kernel')
    if a.audio_owner_pcm and not (a.audio_owner_board and (a.audio_owner_periods==8 or (a.audio_owner_stream and a.audio_owner_periods==24))):p.error('--audio-owner-pcm requires muted board and an explicit PCM period profile')
    if a.audio_board_held and not (a.audio_owner_pcm and a.audio_muted_analog and not a.audio_i2c_hold):p.error('--audio-board-held requires PCM8 muted analog without outer I2C hold')
    if a.audio_owner_refill and not a.audio_board_held:p.error('--audio-owner-refill requires --audio-board-held')
    if a.audio_qmp_close_snapshot and not (a.audio_owner_refill or a.audio_owner_stream):p.error('--audio-qmp-close-snapshot requires explicit live producer')
    if a.audio_owner_native_link and not ((a.audio_owner_refill or a.audio_owner_stream) and a.audio_qmp_close_snapshot):p.error('--audio-owner-native-link requires explicit producer and coherent close profile')
    if a.audio_owner_stream and not (a.audio_owner_periods==24 and a.audio_board_held and a.audio_owner_native_link and not a.audio_owner_refill):p.error('--audio-owner-stream requires native-only held PCM24 without refill8 mode')
    if a.audio_owner_periods==24 and not a.audio_owner_stream:p.error('24 periods require --audio-owner-stream')
    if a.audio_tpl_events and not (a.audio_owner_board and a.audio_pas_timing):p.error('--audio-tpl-events requires owner board and PAS timing')
    if a.audio_display_dma_only and not (a.audio_owner_board and a.audio_owner_stream and a.audio_owner_periods==24):p.error('--audio-display-dma-only requires muted owner-board stream24')
    if a.audio_owner_prelaunch and not (a.audio_owner_fast_handshake and a.audio_owner_handshake_ms==100 and a.audio_owner_warmup_ms==100):p.error('--audio-owner-prelaunch requires explicit original100ms service profile')
    if a.audio_owner_warmup_ms and not a.audio_owner_fast_handshake:p.error('--audio-owner-warmup-ms requires explicit handshake profile')
    if a.audio_owner_fast_handshake and not (a.audio_owner_stream and a.audio_usb_qmp_lease):p.error('--audio-owner-fast-handshake requires qualified runtime stream and QMP lease')
    if a.audio_usb_qmp_lease and not (a.audio_usb_poll_lease and a.audio_owner_stream and a.audio_qmp_close_snapshot):p.error('--audio-usb-qmp-lease requires qualified runtime stream/USB/coherent QMP profile')
    if a.audio_runtime_sampling and not (a.audio_owner_stream and a.audio_tpl_events and not a.audio_tpl_sampling):p.error('--audio-runtime-sampling requires stream24 and event qualification, excludes PAS sampler')
    if a.audio_tpl_sampling and not a.audio_tpl_events:p.error('--audio-tpl-sampling requires --audio-tpl-events')
    if a.audio_usb_poll_lease and (not a.audio_tpl_events or a.audio_tpl_sampling):p.error('--audio-usb-poll-lease requires owner event qualification without the diagnostic sampler')
    if a.amp_profile_only:a.amp_quiet_only=True
    if a.amp_quiet_only:a.amp_registers_only=True
    if a.amp_registers_only and not a.i2c_only:p.error('--amp-registers-only requires --i2c-only')
    if a.efi_pack_formats_from and (a.efi_pack_logs_from or not a.firmware_only or not a.efi_pack_formats_from.is_file()):p.error('--efi-pack-formats-from requires isolated firmware, an existing reference and no prefix packing')
    if a.efi_pack_logs_from and (not a.firmware_only or not a.efi_pack_logs_from.is_file()):p.error('--efi-pack-logs-from requires --firmware-only and an existing reference EFI')
    if a.efi_memory_chain and not a.firmware_only:p.error('--efi-memory-chain requires --firmware-only')
    if a.efi_chain_codec!='lz4' and not a.efi_memory_chain:p.error('--efi-chain-codec requires --efi-memory-chain')
    if a.efi_compact_fat_root and not a.firmware_only:p.error('--efi-compact-fat-root requires --firmware-only')
    if a.qup_core_keepalive and not a.audio_i2c_window:p.error('--qup-core-keepalive requires --audio-i2c-window')
    if a.audio_amp_profile and (not a.qup_core_keepalive or a.amp_profile_only or a.i2c_only):p.error('--audio-amp-profile requires --qup-core-keepalive and no isolated amp mode')
    if a.audio_amp_prestart and not a.audio_amp_profile:p.error('--audio-amp-prestart requires --audio-amp-profile')
    if a.audio_amp_direct_wait and not a.audio_amp_prestart:p.error('--audio-amp-direct-wait requires --audio-amp-prestart')
    if a.audio_amp_preboot and not a.audio_amp_prestart:p.error('--audio-amp-preboot requires --audio-amp-prestart')
    if a.audio_qup_hold and (not a.audio_amp_profile or a.audio_amp_prestart or a.audio_amp_preboot):p.error('--audio-qup-hold requires after-START --audio-amp-profile')
    if a.audio_runtime_deadline and (not a.audio_amp_profile or a.audio_amp_prestart or a.audio_amp_direct_wait or a.audio_qup_hold):p.error('--audio-runtime-deadline requires after-START Music without alternate wait/hold')
    if a.audio_digital_pll and not a.audio_runtime_deadline:p.error('--audio-digital-pll requires --audio-runtime-deadline')
    if a.audio_digital_capture and not a.audio_digital_pll:p.error('--audio-digital-capture requires --audio-digital-pll')
    if a.audio_digital_early_stop and (not a.audio_digital_pll or a.audio_digital_capture):p.error('--audio-digital-early-stop requires digital PLL without capture')
    if a.audio_quiet_progress and not a.audio_digital_pll:p.error('--audio-quiet-progress requires --audio-digital-pll')
    if a.audio_runtime_600ms and (not a.audio_quiet_progress or a.audio_digital_capture or a.audio_digital_early_stop):p.error('--audio-runtime-600ms requires quiet digital PLL without capture/early STOP')
    if a.audio_fast_stop and (not a.audio_digital_early_stop or not a.audio_quiet_progress):p.error('--audio-fast-stop requires quiet digital early STOP')
    if a.audio_usb_ram_only and not a.audio_fast_stop:p.error('--audio-usb-ram-only requires --audio-fast-stop')
    if a.audio_muted_analog and not a.audio_usb_ram_only:p.error('--audio-muted-analog requires --audio-usb-ram-only')
    if a.audio_pcm_periods!=2 and not a.audio_pcm_repeat:p.error('--audio-pcm-periods=8 requires --audio-pcm-repeat')
    if a.audio_pcm_repeat and not a.audio_muted_analog:p.error('--audio-pcm-repeat requires --audio-muted-analog')
    if a.audio_pas_timing and not a.audio_pcm_repeat:p.error('--audio-pas-timing requires --audio-pcm-repeat')
    if a.audio_i2c_window:a.audio_i2c_hold=True
    if a.audio_i2c_hold and not a.tdm_pins_only:p.error('--audio-i2c-hold requires --tdm-pins-only')
    if a.tdm_pins_only and not a.pcm_data_only:p.error('--tdm-pins-only requires --pcm-data-only')
    if a.pcm_data_only and not a.pcm_memory_only:p.error('--pcm-data-only requires --pcm-memory-only')
    if a.pcm_memory_only and not a.audio_map_probe:p.error('--pcm-memory-only requires --audio-map-probe')
    if a.audio_map_probe and not a.audio_context_inventory:p.error('--audio-map-probe requires --audio-context-inventory')
    if a.audio_context_inventory and not a.audio_dma_inventory:p.error('--audio-context-inventory requires --audio-dma-inventory')
    if a.audio_dma_inventory and not a.tdm_pcm_graph_only:p.error('--audio-dma-inventory requires --tdm-pcm-graph-only')
    if a.tdm_pcm_graph_only:
        if not a.firmware_only or any((a.adsp_only,a.owned_stage_only,a.qmp_only,a.icb_only,a.glink_only,a.held_only,a.control_only,a.apps_only,a.gpr_only,a.i2c_only,a.amdb_only,a.amdb_load_only,a.tdm_graph_only,a.tdm_config_only,a.audio_clock_only,a.tdm_run_only)):p.error('--tdm-pcm-graph-only requires isolated --firmware-only')
        a.tdm_run_only=True
    if a.tdm_run_only:
        if not a.firmware_only or any((a.adsp_only,a.owned_stage_only,a.qmp_only,a.icb_only,a.glink_only,a.held_only,a.control_only,a.apps_only,a.gpr_only,a.i2c_only,a.amdb_only,a.amdb_load_only,a.tdm_graph_only,a.tdm_config_only,a.audio_clock_only)):p.error('--tdm-run-only requires isolated --firmware-only')
        a.gpr_only=True
    if a.audio_clock_only:
        if not a.firmware_only or any((a.adsp_only,a.owned_stage_only,a.qmp_only,a.icb_only,a.glink_only,a.held_only,a.control_only,a.apps_only,a.gpr_only,a.i2c_only,a.amdb_only,a.amdb_load_only,a.tdm_graph_only,a.tdm_config_only)):p.error('--audio-clock-only requires isolated --firmware-only')
        a.gpr_only=True
    if a.tdm_config_only:
        if not a.firmware_only or any((a.adsp_only,a.owned_stage_only,a.qmp_only,a.icb_only,a.glink_only,a.held_only,a.control_only,a.apps_only,a.gpr_only,a.i2c_only,a.amdb_only,a.amdb_load_only,a.tdm_graph_only)):p.error('--tdm-config-only requires isolated --firmware-only')
        a.tdm_graph_only=True
    if a.tdm_graph_only:
        if not a.firmware_only or any((a.adsp_only,a.owned_stage_only,a.qmp_only,a.icb_only,a.glink_only,a.held_only,a.control_only,a.apps_only,a.gpr_only,a.i2c_only,a.amdb_only,a.amdb_load_only)):p.error('--tdm-graph-only requires isolated --firmware-only')
        a.gpr_only=True
    if a.amdb_load_only:
        if not a.firmware_only or any((a.adsp_only,a.owned_stage_only,a.qmp_only,a.icb_only,a.glink_only,a.held_only,a.control_only,a.apps_only,a.gpr_only,a.i2c_only,a.amdb_only)):p.error('--amdb-load-only requires isolated --firmware-only')
        a.gpr_only=True
    if a.amdb_only:
        if not a.firmware_only or any((a.adsp_only,a.owned_stage_only,a.qmp_only,a.icb_only,a.glink_only,a.held_only,a.control_only,a.apps_only,a.gpr_only,a.i2c_only)):p.error('--amdb-only requires isolated --firmware-only')
        a.gpr_only=True
    if a.adsp_only and not a.firmware_only:p.error('--adsp-only requires --firmware-only')
    if a.owned_stage_only and not a.adsp_only:p.error('--owned-stage-only requires --adsp-only')
    if a.qmp_only and (not a.firmware_only or a.adsp_only or a.owned_stage_only):p.error('--qmp-only requires --firmware-only and cannot combine other profiles')
    if a.icb_only and (not a.firmware_only or a.adsp_only or a.owned_stage_only or a.qmp_only):p.error('--icb-only requires --firmware-only and cannot combine other profiles')
    if a.glink_only and (not a.firmware_only or a.adsp_only or a.owned_stage_only or a.qmp_only or a.icb_only):p.error('--glink-only requires --firmware-only and cannot combine other profiles')
    if a.held_only and (not a.firmware_only or a.adsp_only or a.owned_stage_only or a.qmp_only or a.icb_only or a.glink_only):p.error('--held-only requires --firmware-only and cannot combine other profiles')
    if a.control_only and (not a.firmware_only or a.adsp_only or a.owned_stage_only or a.qmp_only or a.icb_only or a.glink_only or a.held_only):p.error('--control-only requires --firmware-only and cannot combine other profiles')
    if a.apps_only and (not a.firmware_only or a.adsp_only or a.owned_stage_only or a.qmp_only or a.icb_only or a.glink_only or a.held_only or a.control_only):p.error('--apps-only requires --firmware-only and cannot combine other profiles')
    if a.gpr_only and (not a.firmware_only or any((a.adsp_only,a.owned_stage_only,a.qmp_only,a.icb_only,a.glink_only,a.held_only,a.control_only,a.apps_only))):p.error('--gpr-only requires isolated --firmware-only')
    if a.i2c_only and (not a.firmware_only or any((a.adsp_only,a.owned_stage_only,a.qmp_only,a.icb_only,a.glink_only,a.held_only,a.control_only,a.apps_only,a.gpr_only))):p.error('--i2c-only requires isolated --firmware-only')
    if a.audio_input_format=='ios48-mfc' and not (a.tdm_pcm_graph_only and a.audio_pcm_repeat and a.audio_pcm_periods==2):p.error('MFC format requires isolated PCM graph and exactly two silent repeat requests')
    if a.audio_owner_kernel and not (a.firmware_only and not a.audio_native_endpoint and a.audio_input_format=='ios48-mfc' and a.audio_muted_analog and a.audio_digital_early_stop and a.audio_pas_timing and not any((a.audio_amp_preboot,a.audio_amp_prestart,a.audio_digital_capture,a.audio_qup_hold))):p.error('--audio-owner-kernel requires isolated muted MFC/early-stop/timing without native EFI endpoint')
    if a.audio_native_endpoint and not (a.firmware_only and a.audio_input_format=='ios48-mfc' and a.audio_muted_analog and a.audio_digital_early_stop and a.audio_pas_timing and not any((a.audio_amp_preboot,a.audio_amp_prestart,a.audio_digital_capture,a.audio_qup_hold))):p.error('--audio-native-endpoint requires isolated muted MFC/early-stop/timing profile')
    if a.amdb_module!='tdm' and not (a.amdb_only or a.amdb_load_only):p.error('--amdb-module=mfc requires isolated AMDB metadata or load profile')
    import ast
    for path in ['tools/marble-gpu-preview.py','tools/marble-power-report.py','tools/marble-icb-zero-report.py','tools/marble-xo-npa-report.py','tools/marble-xo-rpmh-report.py','tools/marble-icb-bandwidth-report.py','tools/marble-icb-run-report.py','tools/marble-icb-run-bandwidth-report.py','tools/marble-adsp-dependency-report.py','tools/marble-adsp-owned-report.py','tools/marble-gpr-state-report.py','tools/marble-amdb-report.py','tools/marble-amdb-cycle-report.py','tools/marble-tdm-graph-report.py','tools/marble-audio-clock-report.py','tools/marble-i2c-inventory-report.py']:
        ast.parse((ROOT/path).read_text(encoding='utf8'),filename=path)
    out=a.out.resolve();out.mkdir(exist_ok=False)
    # Reject source edits made while the compiler is running. The archived
    # headers must describe the bytes actually compiled into this image.
    inputs={p for d in ('xnuloader','tools/tests','userland/libmini') for p in (ROOT/d).rglob('*') if p.is_file() and p.suffix in ('.c','.h','.S')}
    inputs.add(ROOT/'tools/marble-qup-board.py')
    inputs.add(ROOT/'tools/marble-aw-profile.py')
    inputs.add(ROOT/'tools/marble-aw-digital-board.py')
    inputs.add(ROOT/'tools/marble-audio-runtime-contract.py')
    inputs.update(ROOT/p for p in ('tools/marble-build-brightness.py','tools/marble-efi-pack.py','tools/marble-audio-pins-board.py','tools/marble-image.py','tools/marble-brightness-test.c','tools/marble-brightness-service.c','tools/marble-brightness-observer.c','tools/marble-lz4.c','tools/marble-lz4-efi.h','xnu/files/bsd/dev/marble_gpu/marble_gmu_preflight.h','xnu/files/bsd/dev/marble_gpu/marble_ecats.h'))
    inputs.update(p for p in (ROOT/'xnu/files/bsd/dev/marble_gpu/upstream').glob('ArmSmmu4K*') if p.is_file())
    if a.efi_chain_codec=='lzma':inputs.add(ROOT/'tools/efi-libc/string.h')
    if a.audio_native_endpoint or a.audio_owner_kernel:
        inputs.update((ROOT/'xnu/files/bsd/sys').glob('marble_audio_*.h'))
        inputs.update((ROOT/'xnu/files/bsd/sys/marble_audio_wire').glob('*'))
    compiled_inputs={p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
    packed_source=None
    if a.efi_pack_logs_from:
        import importlib.util
        spec=importlib.util.spec_from_file_location('log_prefix_pack',ROOT/'tools/marble-log-prefix-pack.py');packer=importlib.util.module_from_spec(spec);spec.loader.exec_module(packer)
        packed_source=out/'packed-inputs/xnuloader'
        packer.create(ROOT/'xnuloader',packed_source,a.efi_pack_logs_from)
        compiled_inputs['tools/marble-log-prefix-pack.py']=hashlib.sha256((ROOT/'tools/marble-log-prefix-pack.py').read_bytes()).hexdigest()
    packed_inputs={p.relative_to(out).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in (out/'packed-inputs').rglob('*') if p.is_file()} if packed_source else {}
    llvm=ROOT.parent/'toolchains/llvm-mingw-20261020-ucrt-x86_64/bin'
    commands=[]
    with (out/'build.log').open('w',encoding='utf8') as log:
        def run(args):
            args=list(map(str,args));commands.append(args)
            (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
            r=subprocess.run(args,cwd=ROOT,capture_output=True,text=True,errors='replace')
            log.write(r.stdout+r.stderr);log.flush()
            if r.returncode:raise RuntimeError(r.stdout+r.stderr)
        if a.qup_core_keepalive:run(['python','tools/marble-qup-board.py','--out',out/'qup-board.json'])
        if a.amp_profile_only or a.audio_amp_profile:run(['python','tools/marble-aw-profile.py','--out',out/'amp-profile-board.json','--verify-header','xnuloader/marble_aw2032_music.h'])
        if a.audio_digital_pll:run(['python','tools/marble-aw-digital-board.py','--out',out/'amp-digital-board.json'])
        if a.audio_muted_analog:run(['python','tools/marble-aw-analog-board.py','--out',out/'amp-analog-board.json'])
        if a.efi_pack_formats_from:
            run([llvm/'clang.exe','-O2','tools/marble-lz4.c','xnuloader/vendor/lz4/lz4.c','xnuloader/vendor/lz4/lz4hc.c','-o',out/'marble-lz4.exe'])
            import importlib.util
            spec=importlib.util.spec_from_file_location('format_pack',ROOT/'tools/marble-format-pack.py');packer=importlib.util.module_from_spec(spec);spec.loader.exec_module(packer)
            packed_source=out/'packed-inputs/xnuloader'
            compiled_inputs['tools/marble-format-pack.py']=hashlib.sha256((ROOT/'tools/marble-format-pack.py').read_bytes()).hexdigest()
            packer.create(ROOT/'xnuloader',packed_source,a.efi_pack_formats_from,out/'marble-lz4.exe')
            packed_inputs={p.relative_to(out).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in (out/'packed-inputs').rglob('*') if p.is_file()}
        if a.tdm_pins_only:run(['python','tools/marble-audio-pins-board.py','--out',out/'tdm-pin-contract.json'])
        if a.tdm_config_only:run(['python','tools/marble-tdm-board.py','--material','out/audio-tdm-board-v122-plan','--out',out/'tdm-board-contract.json'])
        efi=['--target=aarch64-unknown-windows','-ffreestanding','-fno-builtin','-fno-stack-protector','-fshort-wchar','-mgeneral-regs-only','-'+a.efi_opt,'-Wall','-Wextra','-Wno-unused-parameter']
        if a.efi_lto:efi.append('-flto')
        for name,extra in [('marble',['-DXNU_MARBLE=1']),('generic',[])]:
            if name=='marble' and a.firmware_only and not a.audio_owner_kernel:extra.append('-DXNU_MARBLE_FIRMWARE_ONLY=1')
            if name=='marble' and a.adsp_only:extra.append('-DXNU_MARBLE_ADSP_ONLY=1')
            if name=='marble' and a.owned_stage_only:extra.append('-DXNU_MARBLE_OWNED_STAGE_ONLY=1')
            if name=='marble' and a.qmp_only:extra+=['-DXNU_MARBLE_QMP_ONLY=1','-DXNU_MARBLE_ADSP_ONLY=1','-DXNU_MARBLE_OWNED_STAGE_ONLY=1']
            if name=='marble' and a.icb_only:extra+=['-DXNU_MARBLE_ICB_ONLY=1','-DXNU_MARBLE_ADSP_ONLY=1','-DXNU_MARBLE_OWNED_STAGE_ONLY=1']
            if name=='marble' and a.glink_only:extra+=['-DXNU_MARBLE_GLINK_ONLY=1','-DXNU_MARBLE_QMP_ONLY=1','-DXNU_MARBLE_ADSP_ONLY=1','-DXNU_MARBLE_OWNED_STAGE_ONLY=1']
            if name=='marble' and a.held_only:extra+=['-DXNU_MARBLE_HELD_ONLY=1','-DXNU_MARBLE_ADSP_ONLY=1','-DXNU_MARBLE_OWNED_STAGE_ONLY=1']
            if name=='marble' and a.control_only:extra+=['-DXNU_MARBLE_CONTROL_ONLY=1','-DXNU_MARBLE_HELD_ONLY=1','-DXNU_MARBLE_ADSP_ONLY=1','-DXNU_MARBLE_OWNED_STAGE_ONLY=1']
            if name=='marble' and a.apps_only:extra+=['-DXNU_MARBLE_APPS_ONLY=1','-DXNU_MARBLE_CONTROL_ONLY=1','-DXNU_MARBLE_HELD_ONLY=1','-DXNU_MARBLE_ADSP_ONLY=1','-DXNU_MARBLE_OWNED_STAGE_ONLY=1']
            if name=='marble' and a.gpr_only:extra+=['-DXNU_MARBLE_GPR_ONLY=1','-DXNU_MARBLE_APPS_ONLY=1','-DXNU_MARBLE_CONTROL_ONLY=1','-DXNU_MARBLE_HELD_ONLY=1','-DXNU_MARBLE_ADSP_ONLY=1','-DXNU_MARBLE_OWNED_STAGE_ONLY=1']
            if name=='marble' and a.tdm_pins_only:extra.append('-DXNU_MARBLE_TDM_PINS_ONLY=1')
            if name=='marble' and a.qup_core_keepalive:extra.append('-DXNU_MARBLE_QUP_CORE_KEEPALIVE=1')
            if name=='marble' and a.audio_i2c_window:extra.append('-DXNU_MARBLE_AUDIO_I2C_WINDOW=1')
            if name=='marble' and a.audio_i2c_hold:extra.append('-DXNU_MARBLE_AUDIO_I2C_HOLD=1')
            if name=='marble' and a.pcm_data_only:extra.append('-DXNU_MARBLE_PCM_DATA_ONLY=1')
            if name=='marble' and a.pcm_memory_only:extra.append('-DXNU_MARBLE_PCM_MEMORY_ONLY=1')
            if name=='marble' and a.audio_map_probe:extra.append('-DXNU_MARBLE_AUDIO_MAP_PROBE=1')
            if name=='marble' and a.audio_context_inventory:extra.append('-DXNU_MARBLE_AUDIO_CONTEXT_INVENTORY=1')
            if name=='marble' and a.audio_dma_inventory:extra.append('-DXNU_MARBLE_AUDIO_DMA_INVENTORY=1')
            if name=='marble' and a.tdm_pcm_graph_only:extra.append('-DXNU_MARBLE_TDM_PCM_GRAPH_ONLY=1')
            if name=='marble' and a.tdm_run_only:extra.append('-DXNU_MARBLE_TDM_RUN_ONLY=1')
            if name=='marble' and a.audio_input_format=='ios48-mfc':extra.append('-DXNU_MARBLE_AUDIO_MFC=1')
            if name=='marble' and a.audio_owner_kernel:extra.extend(['-DXNU_MARBLE_AUDIO_OWNER=1','-DXNU_MARBLE_AUDIO_QUEUE_ONLY=1','-DMARBLE_PCM_REQUEST_LIMIT=2048'])
            if name=='marble' and a.audio_owner_board:extra.append('-DXNU_MARBLE_AUDIO_OWNER_BOARD=1')
            if name=='marble' and a.audio_owner_kernel:extra.append('-DMARBLE_AUDIO_OWNER_PERIODS='+str(a.audio_owner_periods))
            if name=='marble' and a.audio_owner_pcm:extra.append('-DXNU_MARBLE_AUDIO_OWNER_PCM=1')
            if name=='marble' and a.audio_board_held:extra.append('-DXNU_MARBLE_AUDIO_BOARD_HELD=1')
            if name=='marble' and a.audio_owner_refill:extra.append('-DXNU_MARBLE_AUDIO_OWNER_REFILL=1')
            if name=='marble' and a.audio_qmp_close_snapshot:extra.append('-DXNU_MARBLE_AUDIO_QMP_CLOSE_SNAPSHOT=1')
            if name=='marble' and a.audio_owner_native_link:extra.append('-DXNU_MARBLE_AUDIO_OWNER_NATIVE_LINK=1')
            if name=='marble' and a.audio_owner_stream:extra.append('-DXNU_MARBLE_AUDIO_OWNER_STREAM=1')
            if name=='marble' and a.audio_tpl_events:extra.append('-DXNU_MARBLE_AUDIO_TPL_EVENTS=1')
            if name=='marble' and a.audio_display_dma_only:extra.append('-DXNU_MARBLE_AUDIO_DISPLAY_DMA_ONLY=1')
            if name=='marble' and a.audio_owner_prelaunch:extra.append('-DXNU_MARBLE_AUDIO_OWNER_PRELAUNCH=1')
            if name=='marble' and a.audio_owner_fast_handshake:extra+=['-DXNU_MARBLE_AUDIO_FAST_HANDSHAKE=1',f'-DXNU_MARBLE_AUDIO_HANDSHAKE_ROUNDS={a.audio_owner_handshake_ms//10}',f'-DXNU_MARBLE_AUDIO_HANDSHAKE_WARMUP_ROUNDS={a.audio_owner_warmup_ms//10}']
            if name=='marble' and a.audio_usb_qmp_lease:extra.append('-DXNU_MARBLE_AUDIO_USB_QMP_LEASE=1')
            if name=='marble' and a.audio_runtime_sampling:extra.append('-DXNU_MARBLE_AUDIO_RUNTIME_SAMPLING=1')
            if name=='marble' and a.audio_tpl_sampling:extra.append('-DXNU_MARBLE_AUDIO_TPL_SAMPLING=1')
            if name=='marble' and a.audio_usb_poll_lease:extra.append('-DXNU_MARBLE_AUDIO_USB_POLL_LEASE=1')
            if name=='marble' and a.audio_owner_preauth_trace:extra.append('-DXNU_MARBLE_AUDIO_OWNER_PREAUTH_TRACE=1')
            if name=='marble' and a.audio_native_endpoint:extra.extend(['-DXNU_MARBLE_AUDIO_NATIVE_ENDPOINT=1','-DMARBLE_PCM_REQUEST_LIMIT=2048'])
            if name=='marble' and a.audio_clock_only:extra.append('-DXNU_MARBLE_AUDIO_CLOCK_ONLY=1')
            if name=='marble' and a.tdm_config_only:extra.append('-DXNU_MARBLE_TDM_CONFIG_ONLY=1')
            if name=='marble' and a.tdm_graph_only:extra.append('-DXNU_MARBLE_TDM_GRAPH_ONLY=1')
            if name=='marble' and a.amdb_load_only:extra.append('-DXNU_MARBLE_AMDB_LOAD_ONLY=1')
            if name=='marble' and a.amdb_only:extra.append('-DXNU_MARBLE_AMDB_ONLY=1')
            if name=='marble' and a.amdb_module=='mfc':extra.append('-DXNU_MARBLE_AMDB_MFC=1')
            if name=='marble' and a.amp_profile_only:extra.append('-DXNU_MARBLE_AMP_PROFILE_ONLY=1')
            if name=='marble' and a.audio_amp_profile:extra.extend(['-DXNU_MARBLE_AUDIO_AMP_PROFILE=1','-DXNU_MARBLE_AMP_PROFILE_ONLY=1','-DXNU_MARBLE_AMP_QUIET_ONLY=1','-DXNU_MARBLE_AMP_REGISTERS_ONLY=1'])
            if name=='marble' and a.audio_amp_prestart:extra.append('-DXNU_MARBLE_AUDIO_AMP_PRESTART=1')
            if name=='marble' and a.audio_amp_direct_wait:extra.append('-DXNU_MARBLE_AUDIO_AMP_DIRECT_WAIT=1')
            if name=='marble' and a.audio_amp_preboot:extra.append('-DXNU_MARBLE_AUDIO_AMP_PREBOOT=1')
            if name=='marble' and a.audio_qup_hold:extra.append('-DXNU_MARBLE_AUDIO_QUP_HOLD=1')
            if name=='marble' and a.audio_runtime_deadline:extra.append('-DXNU_MARBLE_AUDIO_RUNTIME_DEADLINE=1')
            if name=='marble' and a.audio_digital_pll:extra.append('-DXNU_MARBLE_AUDIO_DIGITAL_PLL=1')
            if name=='marble' and a.audio_digital_capture:extra.append('-DXNU_MARBLE_AUDIO_DIGITAL_CAPTURE=1')
            if name=='marble' and a.audio_digital_early_stop:extra.append('-DXNU_MARBLE_AUDIO_DIGITAL_EARLY_STOP=1')
            if name=='marble' and a.audio_quiet_progress:extra.append('-DXNU_MARBLE_AUDIO_QUIET_PROGRESS=1')
            if name=='marble' and a.audio_runtime_600ms:extra.append('-DXNU_MARBLE_AUDIO_RUNTIME_600MS=1')
            if name=='marble' and a.audio_fast_stop:extra.append('-DXNU_MARBLE_AUDIO_FAST_STOP=1')
            if name=='marble' and a.audio_usb_ram_only:extra.append('-DXNU_MARBLE_AUDIO_USB_RAM_ONLY=1')
            if name=='marble' and a.audio_muted_analog:extra.append('-DXNU_MARBLE_AUDIO_MUTED_ANALOG=1')
            if name=='marble' and a.audio_pcm_repeat:extra.extend(['-DXNU_MARBLE_AUDIO_PCM_REPEAT=1',f'-DXNU_MARBLE_AUDIO_PCM_PERIODS={a.audio_pcm_periods}'])
            if name=='marble' and a.audio_pas_timing:extra.append('-DXNU_MARBLE_AUDIO_PAS_TIMING=1')
            if name=='marble' and a.amp_quiet_only:extra.append('-DXNU_MARBLE_AMP_QUIET_ONLY=1')
            if name=='marble' and a.amp_registers_only:extra.append('-DXNU_MARBLE_AMP_REGISTERS_ONLY=1')
            if name=='marble' and a.i2c_only:extra+=['-DXNU_MARBLE_I2C_ONLY=1','-DXNU_MARBLE_CONTROL_ONLY=1','-DXNU_MARBLE_QMP_ONLY=1','-DXNU_MARBLE_HELD_ONLY=1','-DXNU_MARBLE_ADSP_ONLY=1','-DXNU_MARBLE_OWNED_STAGE_ONLY=1']
            obj=out/f'loader-{name}.obj'
            source=packed_source/'loader.c' if name=='marble' and packed_source else 'xnuloader/loader.c'
            if name=='marble' and packed_source:extra+=['-DXNU_MARBLE_LOG_FORMAT_PACK=1' if a.efi_pack_formats_from else '-DXNU_MARBLE_LOG_PREFIX_PACK=1','-iquote',str(ROOT/'xnuloader')]
            run([llvm/'clang.exe',*efi,*extra,'-c',source,'-o',obj])
            link_objects=[obj]
            if name=='marble' and a.efi_pack_formats_from:
                codec_obj=out/'lz4-efi.obj'
                run([llvm/'clang.exe',*efi,'-DLZ4_FREESTANDING=1','-DLZ4_FAST_DEC_LOOP=0','-DLZ4_memcpy=memcpy','-DLZ4_memmove=memmove','-DLZ4_memset=memset','-include','tools/marble-lz4-efi.h','-c','xnuloader/vendor/lz4/lz4.c','-o',codec_obj])
                link_objects.append(codec_obj)
            run([llvm/'ld.lld.exe','-flavor','link','/subsystem:efi_application','/entry:efi_main','/nodefaultlib','/base:0xA1000000',f'/out:{out}/loader-{name}.efi',*link_objects])
        for name in ('marble-brightness-mailbox','marble-policy','marble-dsi','marble-display-phase','marble-result','marble-thermal-window','marble-pil-ramfs','marble-pil-segment','marble-display-dma','marble-pil-memory','marble-smp2p','marble-qmp','marble-glink-wire','marble-gpr','marble-glink-data','marble-gmu-preflight','marble-rpmh-native','marble-icb-native','marble-icb-lease','marble-code-digest','marble-xo-npa','marble-xo-rpmh','marble-icb-bandwidth','marble-adsp-package','marble-smccc','marble-i2c-board','marble-i2c-clock-lease','marble-i2c-registers','marble-i2c-route','marble-i2c-config-lease','marble-i2c-fifo','marble-aw2032-registers','marble-aw2032-quiet','marble-aw2032-profile','marble-aw2032-digital','marble-aw2032-analog','marble-aw2032-combined','marble-amdb','marble-amdb-cycle','marble-amdb-cycle-fw','marble-tdm-graph','marble-tdm-graph-fw','marble-tdm-config','marble-tdm-config-fw','marble-audio-clock','marble-audio-clock-fw','marble-tdm-run','marble-tdm-run-fw','marble-pcm-graph','marble-pcm-graph-fw','marble-audio-dma','marble-audio-dma-fw','marble-audio-context-fw','marble-audio-map','marble-ecats','marble-audio-tbu-fw','marble-pcm-memory','marble-pcm-memory-fw','marble-pcm-data','marble-pcm-data-fw','marble-pcm-repeat-fw','marble-audio-pins','marble-audio-completion','marble-qup-keepalive','marble-tpl-events','marble-tpl-sampler','marble-usb-poll-lease','marble-aw2032-held','marble-audio-board-gate','marble-i2c-board-held-fw','marble-audio-refill','marble-qmp-close-snapshot','marble-audio-stream-window','marble-audio-runtime-bound','marble-runtime-sampler'):
            run([llvm/'clang.exe','-O2','-Wall','-Wextra',f'tools/tests/{name}.c','-o',out/(name+'.exe')])
            run([out/(name+'.exe')])
            if name=='marble-adsp-package' and (ROOT/'out/adsp-owned-package-v1-plan/dependencies.bin').is_file():
                run([out/(name+'.exe'),ROOT/'out/adsp-owned-package-v1-plan/dependencies.bin'])
        flags=['--target=arm64-apple-ios16.5','-Os','-ffreestanding','-fno-builtin','-fno-stack-protector','-Wall','-Wextra','-Iuserland/libmini']
        objects=[]
        for name,src in [('test','tools/marble-brightness-test.c'),('mini','userland/libmini/mini.c'),('start','userland/libmini/start.S')]:
            obj=out/(name+'.o');objects.append(obj)
            run([llvm/'clang.exe',*flags,'-c',src,'-o',obj])
        linked=out/'test.linked'
        run([llvm/'ld.lld.exe','-flavor','darwin','-arch','arm64','-no_fixup_chains','-no_adhoc_codesign','-headerpad','0x1000','-e','_start','-pagezero_size','0x100000000','-platform_version','ios','16.5','16.5','-o',linked,*objects])
        static_entry(linked,out/'marble-brightness-test')
        run([llvm/'clang.exe',*flags,'-c','tools/marble-brightness-service.c','-o',out/'service.o'])
        run([llvm/'ld.lld.exe','-flavor','darwin','-arch','arm64','-no_fixup_chains','-no_adhoc_codesign','-headerpad','0x1000','-e','_start','-pagezero_size','0x100000000','-platform_version','ios','16.5','16.5','-o',out/'service.linked',out/'service.o',out/'mini.o',out/'start.o'])
        static_entry(out/'service.linked',out/'marble-brightness-service')
        run(['python','tools/marble-thermal-idle-image.py','--program',out/'marble-brightness-test','--out',out/'test.apfs'])
        run([llvm/'clang.exe',*flags,'-DMARBLE_BRIGHTNESS_INVENTORY=0','-DMARBLE_BRIGHTNESS_BACKEND=1','-c','tools/marble-brightness-observer.c','-o',out/'observer.o'])
        run([llvm/'ld.lld.exe','-flavor','darwin','-arch','arm64','-dylib','-undefined','dynamic_lookup','-no_fixup_chains','-no_adhoc_codesign','-install_name','/redwood/lib/marble-brightness-observer.dylib','-platform_version','ios','16.5','16.5','-o',out/'marble-brightness-observer.dylib',out/'observer.o'])
        image_loader=out/'loader-marble.efi'
        if a.efi_memory_chain:
            run(['python','tools/marble-efi-pack.py','--efi',image_loader,'--out',out/'efi-chain','--codec',a.efi_chain_codec])
            image_loader=out/'efi-chain/loader-chain.efi'
        run(['python','tools/marble-image.py','--source',a.mu_image,'--loader',image_loader,'--lzma-encoder','out/marble-lzma.exe','--output',out/'loader-boot.img',*(['--compact-fat-root'] if a.efi_compact_fat_root else [])])
    if any(hashlib.sha256((ROOT/p).read_bytes()).hexdigest()!=sha for p,sha in compiled_inputs.items()):raise RuntimeError('compiler input changed during build; candidate is not qualified')
    if any(hashlib.sha256((out/p).read_bytes()).hexdigest()!=sha for p,sha in packed_inputs.items()):raise RuntimeError('packed compiler input changed during build')
    if packed_source:(out/'packed-inputs.json').write_text(json.dumps(packed_inputs,indent=2)+'\n')
    (out/'compiled-inputs.json').write_text(json.dumps(compiled_inputs,indent=2)+'\n')
    (out/'build-profile.json').write_text(json.dumps(dict(audio_display_dma_only=a.audio_display_dma_only,audio_owner_prelaunch=a.audio_owner_prelaunch,audio_owner_warmup_ms=a.audio_owner_warmup_ms,audio_owner_handshake_ms=a.audio_owner_handshake_ms,audio_owner_fast_handshake=a.audio_owner_fast_handshake,audio_usb_qmp_lease=a.audio_usb_qmp_lease,audio_runtime_sampling=a.audio_runtime_sampling,audio_owner_stream=a.audio_owner_stream,audio_owner_native_link=a.audio_owner_native_link,audio_qmp_close_snapshot=a.audio_qmp_close_snapshot,audio_owner_refill=a.audio_owner_refill,audio_board_held=a.audio_board_held,audio_usb_poll_lease=a.audio_usb_poll_lease,audio_tpl_sampling=a.audio_tpl_sampling,audio_tpl_events=a.audio_tpl_events,audio_owner_pcm=a.audio_owner_pcm,audio_owner_periods=a.audio_owner_periods,audio_owner_preauth_trace=a.audio_owner_preauth_trace,audio_owner_board=a.audio_owner_board,efi_chain_codec=a.efi_chain_codec,audio_owner_kernel=a.audio_owner_kernel,audio_native_endpoint=a.audio_native_endpoint,audio_input_format=a.audio_input_format,amdb_module=a.amdb_module,audio_pcm_periods=a.audio_pcm_periods,audio_pas_timing=a.audio_pas_timing,audio_pcm_repeat=a.audio_pcm_repeat,audio_muted_analog=a.audio_muted_analog,audio_usb_ram_only=a.audio_usb_ram_only,audio_fast_stop=a.audio_fast_stop,audio_runtime_600ms=a.audio_runtime_600ms,audio_quiet_progress=a.audio_quiet_progress,audio_digital_early_stop=a.audio_digital_early_stop,audio_digital_capture=a.audio_digital_capture,audio_digital_pll=a.audio_digital_pll,audio_runtime_deadline=a.audio_runtime_deadline,audio_qup_hold=a.audio_qup_hold,audio_amp_preboot=a.audio_amp_preboot,audio_amp_direct_wait=a.audio_amp_direct_wait,audio_amp_prestart=a.audio_amp_prestart,audio_amp_profile=a.audio_amp_profile,qup_core_keepalive=a.qup_core_keepalive,audio_i2c_window=a.audio_i2c_window,audio_i2c_hold=a.audio_i2c_hold,amp_profile_only=a.amp_profile_only,amp_quiet_only=a.amp_quiet_only,amp_registers_only=a.amp_registers_only,tdm_pins_only=a.tdm_pins_only,efi_memory_chain=a.efi_memory_chain,efi_compact_fat_root=a.efi_compact_fat_root,pcm_data_only=a.pcm_data_only,pcm_memory_only=a.pcm_memory_only,audio_map_probe=a.audio_map_probe,audio_context_inventory=a.audio_context_inventory,efi_format_pack_reference_sha256=hashlib.sha256(a.efi_pack_formats_from.read_bytes()).hexdigest() if a.efi_pack_formats_from else None,audio_dma_inventory=a.audio_dma_inventory,efi_log_pack_reference_sha256=hashlib.sha256(a.efi_pack_logs_from.read_bytes()).hexdigest() if a.efi_pack_logs_from else None,tdm_pcm_graph_only=a.tdm_pcm_graph_only,tdm_run_only=a.tdm_run_only,efi_opt=a.efi_opt,efi_lto=a.efi_lto,firmware_only=a.firmware_only and not a.audio_owner_kernel,adsp_only=a.adsp_only,owned_stage_only=a.owned_stage_only,qmp_only=a.qmp_only,icb_only=a.icb_only,glink_only=a.glink_only,held_only=a.held_only,control_only=a.control_only,apps_only=a.apps_only,gpr_only=a.gpr_only,amdb_only=a.amdb_only,amdb_load_only=a.amdb_load_only,tdm_graph_only=a.tdm_graph_only,tdm_config_only=a.tdm_config_only,audio_clock_only=a.audio_clock_only,i2c_only=a.i2c_only,xnu_allowed=a.audio_owner_kernel or not a.firmware_only),indent=2)+'\n')
    paths=[Path('xnuloader/loader.c'),*Path(ROOT/'xnuloader').glob('*.h'),Path(__file__).relative_to(ROOT),Path('tools/marble-brightness-test.c'),Path('tools/marble-brightness-live-test.py'),Path('tools/marble-brightness-service.c'),Path('tools/marble-brightness-observer.c'),Path('tools/marble-run.py'),Path('tools/marble-gpu-preview.py'),Path('tools/marble-image.py'),Path('tools/marble-thermal-idle-image.py'),*Path(ROOT/'userland/libmini').glob('*'),*Path(ROOT/'tools/tests').glob('marble-*.c')]
    paths += [Path('tools/marble-npa-report.py'),Path('tools/tests/test_marble_npa_report.py'),Path('tools/marble-smp2p-report.py'),Path('tools/tests/test_marble_smp2p_report.py'),Path('tools/marble-power-report.py'),Path('tools/tests/test_marble_power_report.py')]
    paths += [Path('tools/marble-qmp-native-report.py'),Path('tools/tests/test_marble_qmp_native_report.py')]
    paths += [Path('tools/marble-qmp-code.py'),Path('tools/tests/test_marble_qmp_load_report.py')]
    paths += [Path('tools/tests/fixtures/marble-qmp-native-settled.log'),Path('tools/tests/fixtures/marble-rpmh-native-zero.log'),Path('tools/tests/fixtures/marble-icb-native-routes.log')]
    paths += [Path('xnu/files/bsd/dev/marble_gpu/marble_gmu_preflight.h')]
    paths += [Path('tools/marble-rpmh-native-report.py'),Path('tools/tests/test_marble_rpmh_native_report.py')]
    paths += [Path('tools/marble-native-lease-code.py'),Path('tools/tests/test_marble_native_probe_cli.py')]
    paths += [Path('tools/marble-icb-zero-report.py'),Path('tools/tests/test_marble_icb_zero_report.py'),Path('tools/tests/fixtures/marble-adsp-dependencies.log')]
    paths += [Path('tools/marble-xo-npa-report.py'),Path('tools/tests/test_marble_xo_npa_report.py')]
    paths += [Path('tools/marble-xo-rpmh-report.py'),Path('tools/marble-rpmh-xo-code.py'),Path('tools/tests/test_marble_xo_rpmh_report.py')]
    paths += [Path('tools/marble-icb-bandwidth-report.py'),Path('tools/marble-icb-bandwidth-code.py'),Path('tools/tests/test_marble_icb_bandwidth_report.py')]
    paths += [Path('tools/marble-icb-run-report.py'),Path('tools/marble-icb-run-code.py'),Path('tools/tests/test_marble_icb_run_report.py')]
    paths += [Path('tools/marble-icb-run-bandwidth-report.py'),Path('tools/tests/test_marble_icb_run_bandwidth_report.py')]
    paths += [Path('tools/marble-adsp-dependency-report.py'),Path('tools/tests/test_marble_adsp_dependency_report.py')]
    paths += [Path('tools/marble-adsp-package.py'),Path('tools/marble-adsp-owned-report.py'),Path('tools/tests/test_marble_adsp_owned_report.py')]
    paths += [Path('tools/tests/test_marble_adsp_owned_boot_report.py')]
    paths += [Path('tools/tests/test_marble_adsp_qmp_report.py')]
    paths += [Path('tools/tests/test_marble_scm_bandwidth_report.py')]
    paths += [Path('tools/tests/test_marble_adsp_scm_report.py')]
    paths += [Path('tools/marble-qmp-wire-report.py'),Path('tools/tests/test_marble_qmp_wire_report.py')]
    paths += [Path('tools/tests/test_marble_adsp_wire_report.py'),Path('tools/marble-ipcc-code.py'),Path('tools/marble-glink-smem-code.py'),Path('tools/marble-glink-smem-report.py'),Path('tools/tests/test_marble_glink_smem_report.py')]
    paths += [Path('tools/tests/test_marble_adsp_held_report.py')]
    paths += [Path('tools/marble-glink-control-report.py'),Path('tools/tests/test_marble_glink_control_report.py')]
    paths += [Path('tools/marble-irq-observer-report.py'),Path('tools/tests/test_marble_irq_observer_report.py')]
    paths += [Path('tools/tests/test_marble_glink_apps_report.py'),Path('tools/marble-gpr-state-report.py'),Path('tools/tests/test_marble_gpr_state_report.py')]
    paths += [Path('tools/marble-i2c-inventory-report.py'),Path('tools/tests/test_marble_i2c_inventory_report.py'),Path('tools/marble-aw-acf.py'),Path('tools/tests/test_marble_aw_acf.py'),Path('tools/marble-audio-android-reference.py'),Path('tools/marble-audio-board.py'),Path('tools/marble-fdt.py'),Path('tools/tests/test_marble_fdt.py')]
    paths += [Path('tools/marble-amdb-report.py'),Path('tools/tests/test_marble_amdb_report.py')]
    paths += [Path('tools/marble-amdb-cycle-report.py'),Path('tools/tests/test_marble_amdb_cycle_report.py')]
    paths += [Path('tools/marble-tdm-graph-report.py'),Path('tools/tests/test_marble_tdm_graph_report.py'),Path('tools/marble-tdm-board.py')]
    paths += [Path('tools/marble-format-pack.py'),Path('tools/tests/test_marble_format_pack.py'),Path('tools/marble-lz4.c'),Path('tools/marble-lz4-efi.h'),*Path(ROOT/'xnuloader/vendor/lz4').glob('*')]
    paths += [Path('tools/marble-audio-pins-board.py'),Path('tools/marble-audio-pins-report.py'),Path('tools/tests/test_marble_audio_pins_report.py')]
    paths += [Path('tools/marble-efi-pack.py'),Path('tools/tests/test_marble_efi_pack.py'),Path('xnuloader/marble_efi_chain.c')]
    paths += list((ROOT/'xnuloader/vendor/lzma').glob('*'))+[Path('tools/efi-libc/string.h')]
    paths += [Path('tools/marble-qup-board.py'),Path('tools/tests/test_marble_qup_board.py')]
    paths += [Path('tools/marble-aw-profile.py'),Path('tools/tests/test_marble_aw_profile.py')]
    paths += [Path('tools/marble-aw-digital-board.py'),Path('tools/tests/test_marble_aw_digital.py'),Path('tools/marble-audio-runtime-contract.py')]
    paths += [Path('tools/marble-pcm-data-report.py'),Path('tools/tests/test_marble_pcm_data_report.py')]
    paths += [Path('tools/marble-pcm-memory-report.py'),Path('tools/tests/test_marble_pcm_memory_report.py'),Path('xnu/files/bsd/dev/marble_gpu/marble_ecats.h')]
    paths += [Path('tools/marble-audio-map-report.py'),Path('tools/tests/test_marble_audio_map_report.py'),*Path(ROOT/'xnu/files/bsd/dev/marble_gpu/upstream').glob('ArmSmmu4K*')]
    paths += [Path('tools/marble-audio-context-report.py'),Path('tools/tests/test_marble_audio_context_report.py')]
    paths += [Path('tools/marble-audio-dma-report.py'),Path('tools/tests/test_marble_audio_dma_report.py')]
    paths += [Path('tools/marble-tdm-run-report.py'),Path('tools/tests/test_marble_tdm_run_report.py'),Path('tools/marble-log-prefix-pack.py'),Path('tools/tests/test_marble_log_prefix_pack.py')]
    paths += [Path('tools/marble-audio-clock-report.py'),Path('tools/tests/test_marble_audio_clock_report.py')]
    paths += list(Path(ROOT/'xnuloader/vendor/sha256').glob('*'))
    if a.audio_native_endpoint or a.audio_owner_kernel:paths += list((ROOT/'xnu/files/bsd/sys').glob('marble_audio_*.h'))+list((ROOT/'xnu/files/bsd/sys/marble_audio_wire').glob('*'))
    sources={}
    for path in paths:
        path=(ROOT/path).resolve()
        if not path.is_file():continue
        rel=path.relative_to(ROOT);dest=out/'source'/rel;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,dest)
        sources[rel.as_posix()]=hashlib.sha256(path.read_bytes()).hexdigest()
    (out/'sources.json').write_text(json.dumps(sources,indent=2)+'\n')
    artifacts={f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in out.iterdir() if f.is_file()}
    (out/'artifacts.json').write_text(json.dumps(artifacts,indent=2)+'\n')
    print(out)
if __name__=='__main__':main()
