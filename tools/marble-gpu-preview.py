"""Bounded GPU UI tests or isolated firmware probes; RAM only, with recovery."""
from marble_local import ADB as MARBLE_ADB, ANDROID_SERIAL as MARBLE_ANDROID_SERIAL, FASTBOOT as MARBLE_FASTBOOT
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import shutil
import subprocess
import time
from marble_android import battery_snapshot, screen_off, verify_battery_preflight

ROOT=Path(__file__).resolve().parents[1]

class DiscoveryComplete(Exception):
    """Leave discovery through normal recovery and final acceptance checks."""

def layout_gate(current,reference):
    """Match fixed app glyphs, without waiting for wallpaper/widget animation.

    Mail, FaceTime, Health and Settings are fixed in this RAM profile. Every
    patch must match; a single matching icon or the Dock is insufficient.
    """
    from PIL import ImageChops,ImageStat
    if current.size!=reference.size or current.size!=(1080,2400):
        return dict(passed=False,reason='geometry differs')
    centers=((160,860),(910,860),(160,1700),(663,1700))
    deltas=[]
    for x,y in centers:
        box=(x-32,y-32,x+32,y+32)
        deltas.append(sum(ImageStat.Stat(ImageChops.difference(current.crop(box),reference.crop(box))).mean)/3)
    return dict(passed=max(deltas)<18 and sum(deltas)/len(deltas)<12,patch_deltas=deltas,
                scope='fixed app glyph positions only; not a renderer or blur acceptance test')

def settings_target(current,template):
    """Locate the actual Settings glyph while locale/app registration settles."""
    from PIL import ImageChops,ImageStat
    if current.size!=(1080,2400) or template.size!=(84,84):return dict(passed=False,reason='geometry differs')
    half=42;y=1700
    scores=[]
    for x in (160,414,663,910):
        tile=current.crop((x-half,y-half,x+half,y+half))
        score=sum(ImageStat.Stat(ImageChops.difference(tile,template)).mean)/3
        scores.append((score,x))
    scores.sort();best,x=scores[0]
    return dict(passed=best<14 and scores[1][0]-best>10,coords=[x,y],
        differences=[dict(x=col,mean_delta=score) for score,col in scores],
        scope='unique match of the real Settings glyph; capture must still verify application launch')

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--ramdisk',type=Path,default=Path('out/marble-root-gpu-preview-v1.apfs'))
    p.add_argument('--system-disk',type=Path,default=Path('out/marble-system.apfs'))
    p.add_argument('--daily-buttons',action='store_true')
    p.add_argument('--brightness-backend',action='store_true')
    p.add_argument('--host-clock',action='store_true')
    p.add_argument('--manual-ten-minute',action='store_true',help='user-requested 600-second manual preview; no synthetic exercise or early blackout; temperature guards retained')
    p.add_argument('--kernel',type=Path,default=Path('out/linux-build/mach-marble-cpucap-v3'))
    p.add_argument('--boot-image',type=Path,default=Path('out/marble-gpu-preview-v1-loader-boot.img'))
    p.add_argument('--pacing',choices=('immediate','hardware'),default='immediate')
    p.add_argument('--exercise',action='store_true',help='drive bounded synthetic settings/scroll/home/hold/page checks and save captures')
    p.add_argument('--scene',choices=('core','settings-focus','animation-focus','panels','panel-focus','control-focus','control-home','pages-focus','brightness','brightness-observe','brightness-cycle','daily','power','locale','native-power','native-panels','native-pages','native-animation','native-locale'),default='core')
    p.add_argument('--daily-probe',action='store_true')
    p.add_argument('--i2c-config-snapshot',action='store_true',help='isolated bounded runtime DT capture and semantic comparison with pinned ROM; no DT/I2C/hardware calls')
    p.add_argument('--i2c-config-reference',type=Path,default=Path('out/i2c-controller-v105-audit/uefi-config.dtb'))
    p.add_argument('--i2c-dt-info',action='store_true',help='isolated native DT-provider table/code observations for I2C mapping; no DT or I2C calls')
    p.add_argument('--i2c-inventory',action='store_true',help='isolated read-only Mu I2C table/code inventory; no controller calls')
    p.add_argument('--i2c-clock-info',action='store_true',help='isolated five board clock-name lookups; no clock writes, I2C or GPIO calls')
    p.add_argument('--i2c-clock-cycle',action='store_true',help='isolated balanced 1ms QUP2 clock references; no frequency requests, I2C or GPIO calls')
    p.add_argument('--i2c-register-info',action='store_true',help='isolated two passive snapshots of eight RO controller registers under balanced clock references; no transactions')
    p.add_argument('--amp-profile',action='store_true',help='isolated board Music profile, held down/muted, OTP calibration and reset')
    p.add_argument('--amp-quiet',action='store_true',help='isolated minimum volume write/readback; no amplifier power-up or playback')
    p.add_argument('--amp-registers',action='store_true',help='isolated AW2032 reset-state register snapshot; no amplifier data writes')
    p.add_argument('--i2c-amp-id',action='store_true',help='isolated bounded read of the two fixed amplifier chip IDs; no amplifier register data writes or playback')
    p.add_argument('--i2c-setup-cycle',action='store_true',help='isolated reversible 100kHz FIFO and pinmux configuration; no I2C command or amplifier reset')
    p.add_argument('--i2c-route-info',action='store_true',help='isolated passive controller/GCC/pin snapshots under balanced references; no transactions or GPIO writes')
    p.add_argument('--glink-smem-info',action='store_true',help='isolated read-only preallocated native lpass transport/SMEM GET inventory; no queue dispatch, allocation, PAS or DSP')
    p.add_argument('--adsp-power-info',action='store_true',help='isolated read-only AOP QMP descriptor and native power resource inventory; no DSP start')
    p.add_argument('--rpmh-zero-cycle',action='store_true',help='isolated native RPMh zero-command create/delete and exact cache/client snapshots; no votes/DSP; cold reset')
    p.add_argument('--xo-npa-test',action='store_true',help='isolated software required/suppressible XO aggregation; no hardware calls; provider retained until cold reset')
    p.add_argument('--xo-rpmh-hold',action='store_true',help='isolated physical XO ON vote, active completion, retained until mandatory cold reset; no zero vote or DSP')
    p.add_argument('--audio-fast-stop',action='store_true',help='100us STOP poll, original5s timeout/100ms thermal and fresh ACK')
    p.add_argument('--audio-usb-ram-only',action='store_true',help='retain post-auth DSP progress in RAM without USB INFO handshakes; requires fast STOP')
    p.add_argument('--audio-muted-analog',action='store_true',help='bounded AMPPD cycle with exact HMUTE/90dB; requires RAM-only USB profile')
    p.add_argument('--audio-input-format',choices=('native96','ios48-mfc'),default='native96',help='explicit isolated DSP MFC graph, preserving the iOS input format')
    p.add_argument('--amdb-module',choices=('tdm','mfc'),default='tdm',help='one read-only AMDB metadata module, no graph')
    p.add_argument('--audio-pcm-repeat',action='store_true',help='two serial silent WREP requests with distinct tokens and retired page reuse; original runtime budget')
    p.add_argument('--audio-pcm-periods',type=int,choices=(2,8),default=2,help='bounded silent serial periods; eight requires repeat profile')
    p.add_argument('--audio-pas-timing',action='store_true',help='RAM-only per-call PAS/thermal time accounting; original bounds unchanged')
    p.add_argument('--audio-runtime-600ms',action='store_true',help='explicit600ms digital runtime with six thermal samples')
    p.add_argument('--audio-quiet-progress',action='store_true',help='omit repetitive USB INFO while retaining complete typed RAM log')
    p.add_argument('--audio-digital-early-stop',action='store_true',help='diagnostic STOP on typed PCM/UNMAP/pin restore; preserve separate shorter-runtime evidence')
    p.add_argument('--audio-digital-capture',action='store_true',help='diagnostic return before STOP, capture RAM log and observe watchdog cold reset; no PAS acceptance')
    p.add_argument('--audio-digital-pll',action='store_true',help='PWDN-only digital PLL test; analog amp disabled, hard mute and90dB retained')
    p.add_argument('--audio-runtime-deadline',action='store_true',help='actual 500ms DSP runtime deadline inclusive of I2C work')
    p.add_argument('--audio-qup-hold',action='store_true',help='extra balanced QUP refs from START until verified PAS stop/restore')
    p.add_argument('--audio-amp-preboot',action='store_true',help='Music/OTP down-muted readback before DSP auth under QUP2 minimum hold')
    p.add_argument('--audio-amp-direct-wait',action='store_true',help='bounded CNTVCT runtime waits with unchanged IRQ masks and thermal cadence')
    p.add_argument('--audio-amp-prestart',action='store_true',help='down-muted Music profile before START; no playback')
    p.add_argument('--audio-amp-profile',action='store_true',help='Music/OTP down-muted readback in qualified DSP START window; no playback')
    p.add_argument('--qup-core-keepalive',action='store_true',help='minimum QUP2 BCM ACTIVE/WAKE vote held until cold reset')
    p.add_argument('--audio-i2c-window',action='store_true',help='brief QUP2 lease after DSP START; no amplifier commands')
    p.add_argument('--audio-i2c-hold',action='store_true',help='qualified QUP2 leases around silent DSP test, no amplifier commands')
    p.add_argument('--tdm-pins-cycle',action='store_true',help='board TDM pin lease during silent PCM; requires --pcm-data-cycle')
    p.add_argument('--pcm-data-cycle',action='store_true',help='one silent PCM page and typed WREP consumption; requires --pcm-memory-cycle')
    p.add_argument('--pcm-memory-cycle',action='store_true',help='typed DSP MAP/UNMAP with held owned SID/4K lease; requires --audio-map-cycle')
    p.add_argument('--audio-map-cycle',action='store_true',help='isolated context mapping/ATS/restore; requires --audio-context-info')
    p.add_argument('--audio-context-info',action='store_true',help='passive context snapshot; requires --audio-dma-info')
    p.add_argument('--audio-dma-info',action='store_true',help='passive SMMU route inventory; requires isolated connected PCM graph probe')
    p.add_argument('--adsp-tdm-pcm-graph-cycle',action='store_true',help='isolated connected PCM graph lifecycle; no host PCM buffers, pin writes or amplifiers')
    p.add_argument('--adsp-tdm-run-cycle',action='store_true',help='isolated silent TDM lifecycle; pins remain inputs and amplifiers reset')
    p.add_argument('--adsp-audio-clock-cycle',action='store_true',help='isolated guarded PRM clock request/release; no graphs, pin writes or amplifier commands')
    p.add_argument('--adsp-tdm-config-cycle',action='store_true',help='isolated graph open with board TDM calibration then close; no PREPARE/START, clock or amplifier commands')
    p.add_argument('--adsp-tdm-graph-cycle',action='store_true',help='isolated disconnected TDM graph open/close; no PREPARE/START or hardware commands')
    p.add_argument('--adsp-amdb-load-cycle',action='store_true',help='isolated acquire/release of one existing TDM library; no graph or audio')
    p.add_argument('--adsp-amdb-info',action='store_true',help='isolated one TDM module metadata query; may inspect a DSP library, no graph or audio')
    p.add_argument('--adsp-spf-state',action='store_true',help='isolated one read-only GPR SPF state query, bounded local intent and complete stop/reset; no audio')
    p.add_argument('--adsp-apps-open',action='store_true',help='isolated adsp_apps bidirectional open and remote-intent metadata; no local intents or GPR DATA; full DSP stop/reset')
    p.add_argument('--adsp-control-boot',action='store_true',help='isolated DSP control-only boot with qualified lpass queues, load_state held through stop, bounded GLINK/SMP2P polling and cold reset; no audio')
    p.add_argument('--adsp-owned-boot',action='store_true',help='isolated new RUN/XO/LPI/bandwidth-held DSP auth/fresh stop ACK/verified shutdown; pinned RAM package and cold reset')
    p.add_argument('--adsp-owned-stage',action='store_true',help='isolated pinned RAM package no-exec ROM stage while holding native RUN/XO/LPI/bandwidth; cold reset')
    p.add_argument('--adsp-qmp-stage',action='store_true',help='owned no-exec ROM stage with QMP load_state on/off inside the LPI lifetime; pinned RAM package and cold reset')
    p.add_argument('--adsp-scm-stage',action='store_true',help='owned ROM no-exec stage with the Linux SCM logical peak held throughout; no QMP or DSP; cold reset')
    p.add_argument('--adsp-scm-qmp-stage',action='store_true',help='owned no-exec ROM with Linux SCM peak plus post-load QMP on/off/close cancellation; no DSP; cold reset')
    p.add_argument('--adsp-scm-held-stage',action='store_true',help='isolated SCM ROM no-exec with load_state ON retained through bounded PAS shutdown, OFF/close afterwards; cold reset')
    p.add_argument('--adsp-scm-ipcc-stage',action='store_true',help='SCM peak ROM/wire no-exec with own QMP receive ACK and initialized-register bounded shutdown; cold reset')
    p.add_argument('--adsp-scm-bounded-stage',action='store_true',help='SCM peak ROM/AP-wire no-exec experiment with initialized-register bounded shutdown; observe WAITQ without replay; cold reset')
    p.add_argument('--adsp-scm-channel-stage',action='store_true',help='SCM peak no-exec ROM with only AP channel handshake; no load_state messages, native GLink core or DSP; cold reset')
    p.add_argument('--adsp-scm-wire-stage',action='store_true',help='isolated SCM peak ROM no-exec stage with AP-owned mailbox transfers and firmware-context preservation; no GLink core/DSP; cold reset')
    p.add_argument('--adsp-package',type=Path,help='exact MADSP001 RAM-only input bundle for --adsp-owned-stage')
    p.add_argument('--adsp-dependency-cycle',action='store_true',help='isolated simultaneous native RUN/XO/LPI256/1MB/s request/complete, cold reset; no DSP')
    p.add_argument('--icb-run-bandwidth-cycle',action='store_true',help='isolated native post-init RUN then 1MB/s request/release, mandatory cold reset; no DSP')
    p.add_argument('--icb-run-scm-bandwidth-cycle',action='store_true',help='isolated native RUN and Linux U32_MAX kB/s SCM peak request for 10ms, completion/thermal audit and release; no DSP/PAS/QMP')
    p.add_argument('--icb-run-cycle',action='store_true',help='isolated published native ICB post-init HOLD to RUN transition; cold reset; no DSP')
    p.add_argument('--icb-bandwidth-cycle',action='store_true',help='isolated native 1MB/s crypto-DDR request/complete/destroy; no DSP; cold reset')
    p.add_argument('--icb-zero-cycle',action='store_true',help='isolated native crypto/DDR vector client create/destroy; zero bandwidth requests; cold reset')
    p.add_argument('--adsp-dependency-info',action='store_true',help='isolated native ICB endpoint names/bus metadata and XO source descriptor; no clients/votes/DSP')
    p.add_argument('--adsp-icb-info',action='store_true',help='isolated native ICB route-tree RAM inventory; no clients or bandwidth votes')
    p.add_argument('--adsp-xo-info',action='store_true',help='isolated read-only XO command DB snapshots and existing bounds parser; no clock/RPMh votes or DSP start')
    p.add_argument('--qmp-native-cycle',action='store_true',help='isolated native AOP channel open/close; no TX, raw ISR/MMIO calls or DSP start; watchdog on uncertain cleanup')
    p.add_argument('--qmp-native-pump-cycle',action='store_true',help='isolated exact native QMP event dispatch plus channel lifecycle, only after qualifying passive native listeners; no TX/DSP')
    p.add_argument('--qmp-native-peer-close',action='store_true',help='independent native both-end-closed contract plus cold reset; original descriptor restoration remains separately reported; no TX/DSP')
    p.add_argument('--qmp-native-settled-close',action='store_true',help='native channel close plus bounded transport-only settling; restore descriptor and release local callbacks; no TX/DSP; cold reset')
    p.add_argument('--qmp-load-state-cycle',action='store_true',help='isolated native 64-byte ADSP load_state on/off TX, per-packet transport ACK, settled close and cold reset; no DSP')
    p.add_argument('--qmp-mailbox-load-state-cycle',action='store_true',help='QMP mailbox transfer through exact native transport primitive; remote empty-mailbox ACK, no fabricated GLink intents; no DSP')
    p.add_argument('--qmp-wire-cycle',action='store_true',help='borrow empty AP QMP channel/mailbox using Linux wire protocol and pinned native doorbell only; no GLink callbacks/PAS/DSP; cold reset')
    p.add_argument('--qmp-tpl-cycle',action='store_true',help='independent native TPL8 raise/restore before and after qualified AP wire cycle; no SCM/PAS/DSP; cold reset')
    p.add_argument('--qmp-ipcc-cycle',action='store_true',help='AP QMP handshake with receive-ID audit and one acknowledgement only for its newly observed source0/signal0; no IRQ configuration/PAS; cold reset')
    p.add_argument('--qmp-smccc-cycle',action='store_true',help='bounded raw standard/fast availability query before/after AP wire; only INTERRUPTED resumes; no WAITQ replay or PAS; cold reset')
    p.add_argument('--scm-info',action='store_true',help='discovery only: identify the exact resident SCM protocol table without calling it')
    p.add_argument('--scm-availability',action='store_true',help='discovery only: four exact availability queries through the qualified modern SCM table; no PAS init/auth/reset')
    p.add_argument('--adc-channels',action='store_true',help='discovery only: exact Mu ADC GetChannel lookup, no conversions or initialization')
    p.add_argument('--adc-battery',action='store_true',help='discovery only: two guarded native VADC conversions, no charger or threshold changes')
    p.add_argument('--pil-driver',type=Path,help='discovery only: pinned no-AUTO native PIL derivative; DSP requires separate explicit cycle option')
    p.add_argument('--pil-config',action='store_true',help='copy native ADSP2/SLPI configurations after no-AUTO installation, no DSP image load')
    p.add_argument('--pil-files',type=Path,help='native open/read/close probe of the pinned read-only ROM RAM filesystem; requires --pil-driver and discovery only')
    p.add_argument('--pil-adsp-cycle',action='store_true',help='explicit native ADSP2 load/2s hold/shutdown from ROM RAM files; no GPU or XNU startup')
    p.add_argument('--pil-memory',action='store_true',help='discovery only: ADSP carveout EFI memory-map/translation checks, no target reads or DSP start')
    p.add_argument('--pil-header-only',action='store_true',help='native MDT import with a pinned PAS-refusal derivative; cannot authenticate/start DSP')
    p.add_argument('--pil-elf-only',action='store_true',help='also run the exact native ELF parser on the imported header, no PAS calls')
    p.add_argument('--pil-pas-metadata',action='store_true',help='isolated metadata init/shutdown through the exact SCM table; no payload copy or DSP execution')
    p.add_argument('--pil-pas-memory',action='store_true',help='also qualify PAS memory setup for the reserved ADSP carveout; no payload access, auth-reset or DSP execution')
    p.add_argument('--pil-first-segment',action='store_true',help='bounded native ADSP2 b01 load/readback/restore after PAS memory setup; always shutdown; no DSP execution')
    p.add_argument('--pil-all-segments',action='store_true',help='qualified native ADSP2 load/readback including BSS, restore carveout and shutdown; no DSP execution')
    p.add_argument('--pil-powered-stage',action='store_true',help='isolated NPA-held full ROM load/readback; PAS shutdown before memory restore; native power release and verified cold reset; no DSP execution')
    p.add_argument('--pil-smp2p-stage',action='store_true',help='isolated SMP2P preparation/notification plus powered ROM stage; no DSP execution; mandatory cold reset')
    p.add_argument('--pil-smp2p-boot',action='store_true',help='isolated DSP auth with fresh SMP2P ready/stop-ack gate before PAS shutdown; retained backup/power/watchdog on failure; no channels/audio')
    p.add_argument('--pil-bounded-boot',action='store_true',help='isolated secure PAS auth/500ms observation/verified shutdown under a qualified power lease; restore then mandatory cold reset; no channels/audio')
    p.add_argument('--pil-proxy-config',action='store_true',help='native proxy DT configuration inventory; no acquire/release events or DSP startup')
    p.add_argument('--npa-info',action='store_true',help='discovery only: exact resident NPA protocol table inventory; no client creation or votes')
    p.add_argument('--npa-vcs-info',action='store_true',help='discovery only: exact VCS driver/rail binding and cached corner configuration, no protocol calls or actual requests')
    p.add_argument('--npa-resources',action='store_true',help='discovery only: bounded mapped reads of initialized LPI resource objects; no protocol or plugin calls')
    p.add_argument('--npa-clients',action='store_true',help='discovery only: qualified native zero-request client create/destroy; includes implicit completion; no DSP startup')
    p.add_argument('--npa-native-release',action='store_true',help='isolated native configured requests/release-to-zero, retaining original-restoration failure and requiring verified Android cold reset; no DSP startup')
    p.add_argument('--npa-votes',action='store_true',help='disabled after v29 original-state restoration failure; retained only to reject old command lines explicitly')
    p.add_argument('--dsi-info',action='store_true',help='read documented DSI state under a balanced display-bus clock lease')
    p.add_argument('--display-memory-info',action='store_true',help='idle-only documented Demura/SSPP status and source-address audit; no framebuffer or payload access')
    p.add_argument('--dsi-read-power',action='store_true',help='discovery only: one bounded DCS GET_POWER_MODE, no brightness write')
    p.add_argument('--dsi-read-identity',action='store_true',help='discovery only: fixed panel-ID/brightness reads; no max-return change or panel write')
    p.add_argument('--dsi-dim-probe',action='store_true',help='discovery only: reduce brightness for 250ms, verify and restore the exact original level')
    p.add_argument('--smem-info',action='store_true')
    p.add_argument('--smem-channels',action='store_true')
    p.add_argument('--smp2p-prepare',action='store_true',help='isolated native APPS-owned SMP2P allocation/initialization, no IPCC or DSP start; mandatory verified cold reset')
    p.add_argument('--smp2p-notify',action='store_true',help='isolated preparation plus one native-qualified IPCC notification; no DSP execution, IRQ changes or delivery claim; mandatory cold reset')
    p.add_argument('--glink-probe',action='store_true',help='idle-only local link notification registration/release; no channel open or DSP start')
    p.add_argument('--buttons-probe',action='store_true')
    p.add_argument('--discovery-only',action='store_true')
    p.add_argument('--fb-threads',type=int,choices=(2,4),default=2)
    p.add_argument('--render-scale',type=int,choices=(2,3),default=3,help='2 opts into the source kernel lower-resolution layout candidate; panel stays 1080x2400, with softer detail; inspect reported geometry')
    p.add_argument('--record-seconds',type=int,choices=range(0,61),default=0)
    p.add_argument('--record-start',type=int,choices=range(0,61),default=0)
    p.add_argument('--record-mb',type=int,choices=(96,192),default=96)
    p.add_argument('--record-step',type=int,choices=(6,8),default=6)
    p.add_argument('--ui-settle',type=float,default=3,help='bounded time before each completed-composition capture; page identity still needs visual audit')
    p.add_argument('--settings-template',type=Path,default=Path('out/ui-settings-glyph-2x-v1.png'),help='local 84x84 verified Settings glyph crop used only by locale acceptance')
    p.add_argument('--desktop-template-gate',action='store_true',help='require the uniquely matched real Settings desktop glyph before host-driven gestures; does not prove rendering correctness')
    p.add_argument('--quiet-ui',action='store_true',help='omit per-runloop touch latency and CPU diagnostics; EFI thermal evidence remains required')
    p.add_argument('--control-origin',choices=('edge','body'),default='edge',help='diagnose Control Center native dismissal from the bottom edge or the empty area below its modules; report the origin explicitly')
    a=p.parse_args()
    if a.amp_profile:
        if not a.discovery_only or a.pil_driver or a.pil_files or any(v for k,v in vars(a).items() if isinstance(v,bool) and k not in ('amp_profile','discovery_only')):p.error('AW2032 profile requires isolated discovery')
        a.amp_quiet=True
    if a.amp_quiet:
        if not a.discovery_only or a.pil_driver or a.pil_files or any(v for k,v in vars(a).items() if isinstance(v,bool) and k not in ('amp_quiet','amp_profile','discovery_only')):p.error('AW2032 quiet write requires isolated discovery')
        a.amp_registers=True
    if a.amp_registers:
        if not a.discovery_only or a.pil_driver or a.pil_files or any(v for k,v in vars(a).items() if isinstance(v,bool) and k not in ('amp_registers','amp_quiet','amp_profile','discovery_only')):p.error('AW2032 register reads require isolated discovery')
        a.i2c_amp_id=True
    if a.i2c_amp_id:
        if not a.discovery_only or a.pil_driver or a.pil_files or any(v for k,v in vars(a).items() if isinstance(v,bool) and k not in ('i2c_amp_id','amp_registers','amp_quiet','amp_profile','discovery_only')):p.error('I2C amplifier ID requires isolated discovery')
        a.i2c_setup_cycle=True
    if a.i2c_setup_cycle:
        if not a.discovery_only or a.pil_driver or a.pil_files or any(v for k,v in vars(a).items() if isinstance(v,bool) and k not in ('i2c_setup_cycle','i2c_amp_id','amp_registers','amp_quiet','amp_profile','discovery_only')):p.error('I2C setup requires isolated discovery')
        a.i2c_route_info=True
    if a.i2c_route_info:
        if not a.discovery_only or a.pil_driver or a.pil_files or any(v for k,v in vars(a).items() if isinstance(v,bool) and k not in ('i2c_route_info','i2c_setup_cycle','i2c_amp_id','amp_registers','amp_quiet','amp_profile','discovery_only')):p.error('I2C route inventory requires isolated discovery')
        a.i2c_register_info=True
    if a.i2c_register_info:
        if not a.discovery_only or a.pil_driver or a.pil_files or any(v for k,v in vars(a).items() if isinstance(v,bool) and k not in ('i2c_register_info','i2c_route_info','i2c_setup_cycle','i2c_amp_id','amp_registers','amp_quiet','amp_profile','discovery_only')):p.error('I2C register inventory requires isolated discovery')
        a.i2c_clock_cycle=True
    if a.i2c_clock_cycle:
        if not a.discovery_only or a.pil_driver or a.pil_files or any(v for k,v in vars(a).items() if isinstance(v,bool) and k not in ('i2c_clock_cycle','i2c_register_info','i2c_route_info','i2c_setup_cycle','i2c_amp_id','amp_registers','amp_quiet','amp_profile','discovery_only')):p.error('I2C clock cycle requires isolated discovery')
        a.i2c_clock_info=True
    if a.i2c_clock_info:
        if not a.discovery_only or a.pil_driver or a.pil_files or any(v for k,v in vars(a).items() if isinstance(v,bool) and k not in ('i2c_clock_info','i2c_clock_cycle','i2c_register_info','i2c_route_info','i2c_setup_cycle','i2c_amp_id','amp_registers','amp_quiet','amp_profile','discovery_only')):p.error('I2C clock lookup requires isolated discovery')
        a.i2c_inventory=True
    if a.i2c_config_snapshot:
        if not a.discovery_only or a.pil_driver or a.pil_files or any(v for k,v in vars(a).items() if isinstance(v,bool) and k not in ('i2c_config_snapshot','discovery_only')):p.error('I2C config snapshot requires isolated discovery')
        a.i2c_dt_info=True
    if a.i2c_dt_info:
        if not a.discovery_only or a.pil_driver or a.pil_files or any(v for k,v in vars(a).items() if isinstance(v,bool) and k not in ('i2c_dt_info','i2c_config_snapshot','discovery_only')):p.error('I2C DT inventory requires isolated discovery')
        a.i2c_inventory=True
    if a.i2c_inventory and (not a.discovery_only or a.pil_driver or a.pil_files or any(v for k,v in vars(a).items() if isinstance(v,bool) and k not in ('i2c_inventory','i2c_dt_info','i2c_config_snapshot','i2c_clock_info','i2c_clock_cycle','i2c_register_info','i2c_route_info','i2c_setup_cycle','i2c_amp_id','amp_registers','amp_quiet','amp_profile','discovery_only'))):p.error('I2C inventory requires isolated discovery')
    if a.i2c_config_snapshot and (not a.i2c_config_reference.is_file() or hashlib.sha256(a.i2c_config_reference.read_bytes()).hexdigest()!='358db7a726eb6ed8945600f688d9d96575f12c55a590a418b80cae0ece51e452'):p.error('I2C configuration capture requires the pinned ROM FDT; no phone commands issued')
    if a.glink_smem_info and (not a.discovery_only or a.pil_driver or a.pil_files or any(v for k,v in vars(a).items()
            if isinstance(v,bool) and k not in ('glink_smem_info','discovery_only'))):
        p.error('GLink SMEM inventory requires isolated discovery')
    if a.qmp_ipcc_cycle:
        if not a.discovery_only or a.pil_driver or a.pil_files or any(v for k,v in vars(a).items()
                if isinstance(v,bool) and k not in ('qmp_ipcc_cycle','discovery_only')):
            p.error('QMP/IPCC lifecycle requires isolated discovery')
        a.qmp_wire_cycle=True
    if a.qmp_smccc_cycle:
        if not a.discovery_only or a.pil_driver or a.pil_files or any(v for k,v in vars(a).items()
                if isinstance(v,bool) and k not in ('qmp_smccc_cycle','discovery_only')):
            p.error('QMP/SMCCC lifecycle requires isolated discovery')
        a.qmp_wire_cycle=True
    if a.qmp_tpl_cycle:
        if not a.discovery_only or a.pil_driver or a.pil_files or any(v for k,v in vars(a).items()
                if isinstance(v,bool) and k not in ('qmp_tpl_cycle','discovery_only')):
            p.error('QMP/TPL lifecycle requires isolated discovery')
        a.qmp_wire_cycle=True
    if a.audio_fast_stop and (not a.audio_digital_early_stop or not a.audio_quiet_progress):p.error('--audio-fast-stop requires quiet digital early STOP')
    if a.audio_usb_ram_only and not a.audio_fast_stop:p.error('--audio-usb-ram-only requires --audio-fast-stop')
    if a.audio_muted_analog and not a.audio_usb_ram_only:p.error('--audio-muted-analog requires --audio-usb-ram-only')
    if a.audio_pcm_periods!=2 and not a.audio_pcm_repeat:p.error('--audio-pcm-periods=8 requires --audio-pcm-repeat')
    if a.audio_pcm_repeat and not a.audio_muted_analog:p.error('--audio-pcm-repeat requires --audio-muted-analog')
    if a.audio_pas_timing and not a.audio_pcm_repeat:p.error('--audio-pas-timing requires --audio-pcm-repeat')
    if a.audio_runtime_600ms and (not a.audio_quiet_progress or a.audio_digital_capture or a.audio_digital_early_stop):p.error('--audio-runtime-600ms requires quiet digital PLL without capture/early STOP')
    if a.audio_quiet_progress and not a.audio_digital_pll:p.error('--audio-quiet-progress requires --audio-digital-pll')
    if a.audio_digital_early_stop and (not a.audio_digital_pll or a.audio_digital_capture):p.error('--audio-digital-early-stop requires digital PLL without capture')
    if a.audio_digital_capture and not a.audio_digital_pll:p.error('--audio-digital-capture requires --audio-digital-pll')
    if a.audio_digital_pll and not a.audio_runtime_deadline:p.error('--audio-digital-pll requires --audio-runtime-deadline')
    if a.audio_runtime_deadline and (not a.audio_amp_profile or a.audio_amp_prestart or a.audio_amp_direct_wait or a.audio_qup_hold):p.error('--audio-runtime-deadline requires after-START Music without alternate wait/hold')
    if a.audio_qup_hold and (not a.audio_amp_profile or a.audio_amp_prestart or a.audio_amp_preboot):p.error('--audio-qup-hold requires after-START --audio-amp-profile')
    if a.audio_amp_preboot and not a.audio_amp_prestart:p.error('--audio-amp-preboot requires --audio-amp-prestart')
    if a.audio_amp_direct_wait and not a.audio_amp_prestart:p.error('--audio-amp-direct-wait requires --audio-amp-prestart')
    if a.audio_amp_prestart and not a.audio_amp_profile:p.error('--audio-amp-prestart requires --audio-amp-profile')
    if a.audio_amp_profile and not a.qup_core_keepalive:p.error('--audio-amp-profile requires --qup-core-keepalive')
    if a.qup_core_keepalive and not a.audio_i2c_window:p.error('--qup-core-keepalive requires --audio-i2c-window')
    if a.audio_i2c_window and (a.audio_i2c_hold or not a.tdm_pins_cycle):p.error('--audio-i2c-window requires --tdm-pins-cycle and no hold mode')
    if a.audio_i2c_hold and not a.tdm_pins_cycle:p.error('--audio-i2c-hold requires --tdm-pins-cycle')
    if a.tdm_pins_cycle and not a.pcm_data_cycle:p.error('--tdm-pins-cycle requires --pcm-data-cycle')
    if a.pcm_data_cycle and not a.pcm_memory_cycle:p.error('--pcm-data-cycle requires --pcm-memory-cycle')
    if a.pcm_memory_cycle and not a.audio_map_cycle:p.error('--pcm-memory-cycle requires --audio-map-cycle')
    if a.audio_map_cycle and not a.audio_context_info:p.error('--audio-map-cycle requires --audio-context-info')
    if a.audio_context_info and not a.audio_dma_info:p.error('--audio-context-info requires --audio-dma-info')
    if a.audio_dma_info and not a.adsp_tdm_pcm_graph_cycle:p.error('--audio-dma-info requires isolated --adsp-tdm-pcm-graph-cycle')
    if a.adsp_tdm_pcm_graph_cycle:
        if not a.discovery_only or a.pil_driver or a.pil_files or any(v for k,v in vars(a).items() if isinstance(v,bool) and k not in ('audio_pas_timing','audio_pcm_repeat','audio_muted_analog','audio_usb_ram_only','audio_fast_stop','audio_runtime_600ms','audio_quiet_progress','audio_digital_early_stop','audio_digital_capture','audio_digital_pll','audio_runtime_deadline','audio_qup_hold','audio_amp_preboot','audio_amp_direct_wait','audio_amp_prestart','audio_amp_profile','qup_core_keepalive','audio_i2c_window','audio_i2c_hold','tdm_pins_cycle','pcm_data_cycle','pcm_memory_cycle','audio_map_cycle','audio_context_info','audio_dma_info','adsp_tdm_pcm_graph_cycle','discovery_only')):p.error('PCM graph requires isolated discovery')
        a.adsp_tdm_run_cycle=True
    if a.adsp_tdm_run_cycle:
        if not a.discovery_only or a.pil_driver or a.pil_files or any(v for k,v in vars(a).items() if isinstance(v,bool) and k not in ('audio_pas_timing','audio_pcm_repeat','audio_muted_analog','audio_usb_ram_only','audio_fast_stop','audio_runtime_600ms','audio_quiet_progress','audio_digital_early_stop','audio_digital_capture','audio_digital_pll','audio_runtime_deadline','audio_qup_hold','audio_amp_preboot','audio_amp_direct_wait','audio_amp_prestart','audio_amp_profile','qup_core_keepalive','audio_i2c_window','audio_i2c_hold','tdm_pins_cycle','pcm_data_cycle','pcm_memory_cycle','audio_map_cycle','audio_context_info','audio_dma_info','adsp_tdm_pcm_graph_cycle','adsp_tdm_run_cycle','discovery_only')):p.error('TDM run requires isolated discovery')
        a.adsp_spf_state=True
    if a.adsp_audio_clock_cycle:
        if not a.discovery_only or a.pil_driver or a.pil_files or any(v for k,v in vars(a).items() if isinstance(v,bool) and k not in ('adsp_audio_clock_cycle','discovery_only')):p.error('audio clock requires isolated discovery')
        a.adsp_spf_state=True
    if a.adsp_tdm_config_cycle:
        if not a.discovery_only or a.pil_driver or a.pil_files or any(v for k,v in vars(a).items() if isinstance(v,bool) and k not in ('adsp_tdm_config_cycle','discovery_only')):p.error('TDM configuration requires isolated discovery')
        a.adsp_tdm_graph_cycle=True
    if a.adsp_tdm_graph_cycle:
        if not a.discovery_only or a.pil_driver or a.pil_files or any(v for k,v in vars(a).items() if isinstance(v,bool) and k not in ('adsp_tdm_graph_cycle','adsp_tdm_config_cycle','adsp_audio_clock_cycle','discovery_only')):p.error('TDM graph cycle requires isolated discovery')
        a.adsp_spf_state=True
    if a.adsp_amdb_load_cycle:
        if not a.discovery_only or a.pil_driver or a.pil_files or any(v for k,v in vars(a).items() if isinstance(v,bool) and k not in ('adsp_amdb_load_cycle','discovery_only')):p.error('AMDB library cycle requires isolated discovery')
        a.adsp_spf_state=True
    if a.adsp_amdb_info:
        if not a.discovery_only or a.pil_driver or a.pil_files or any(v for k,v in vars(a).items() if isinstance(v,bool) and k not in ('adsp_amdb_info','discovery_only')):p.error('AMDB metadata requires isolated discovery')
        a.adsp_spf_state=True
    if a.adsp_spf_state:
        if not a.discovery_only or a.pil_driver or a.pil_files or any(v for k,v in vars(a).items() if isinstance(v,bool) and k not in ('audio_pas_timing','audio_pcm_repeat','audio_muted_analog','audio_usb_ram_only','audio_fast_stop','audio_runtime_600ms','audio_quiet_progress','audio_digital_early_stop','audio_digital_capture','audio_digital_pll','audio_runtime_deadline','audio_qup_hold','audio_amp_preboot','audio_amp_direct_wait','audio_amp_prestart','audio_amp_profile','qup_core_keepalive','audio_i2c_window','audio_i2c_hold','tdm_pins_cycle','pcm_data_cycle','pcm_memory_cycle','audio_map_cycle','audio_context_info','audio_dma_info','adsp_tdm_pcm_graph_cycle','adsp_tdm_run_cycle','adsp_spf_state','adsp_amdb_info','adsp_amdb_load_cycle','adsp_tdm_graph_cycle','adsp_tdm_config_cycle','adsp_audio_clock_cycle','discovery_only')):p.error('SPF query requires isolated discovery')
        a.adsp_apps_open=True
    if a.adsp_apps_open:
        if not a.discovery_only or a.pil_driver or a.pil_files or any(v for k,v in vars(a).items()
                if isinstance(v,bool) and k not in ('audio_pas_timing','audio_pcm_repeat','audio_muted_analog','audio_usb_ram_only','audio_fast_stop','audio_runtime_600ms','audio_quiet_progress','audio_digital_early_stop','audio_digital_capture','audio_digital_pll','audio_runtime_deadline','audio_qup_hold','audio_amp_preboot','audio_amp_direct_wait','audio_amp_prestart','audio_amp_profile','qup_core_keepalive','audio_i2c_window','audio_i2c_hold','tdm_pins_cycle','pcm_data_cycle','pcm_memory_cycle','audio_map_cycle','audio_context_info','audio_dma_info','adsp_tdm_pcm_graph_cycle','adsp_tdm_run_cycle','adsp_apps_open','adsp_spf_state','adsp_amdb_info','adsp_amdb_load_cycle','adsp_tdm_graph_cycle','adsp_tdm_config_cycle','adsp_audio_clock_cycle','discovery_only')):
            p.error('ADSP apps open requires isolated discovery')
        a.adsp_control_boot=True
    if a.adsp_control_boot:
        if not a.discovery_only or a.pil_driver or a.pil_files or any(v for k,v in vars(a).items()
                if isinstance(v,bool) and k not in ('audio_pas_timing','audio_pcm_repeat','audio_muted_analog','audio_usb_ram_only','audio_fast_stop','audio_runtime_600ms','audio_quiet_progress','audio_digital_early_stop','audio_digital_capture','audio_digital_pll','audio_runtime_deadline','audio_qup_hold','audio_amp_preboot','audio_amp_direct_wait','audio_amp_prestart','audio_amp_profile','qup_core_keepalive','audio_i2c_window','audio_i2c_hold','tdm_pins_cycle','pcm_data_cycle','pcm_memory_cycle','audio_map_cycle','audio_context_info','audio_dma_info','adsp_tdm_pcm_graph_cycle','adsp_tdm_run_cycle','adsp_control_boot','adsp_apps_open','adsp_spf_state','adsp_amdb_info','adsp_amdb_load_cycle','adsp_tdm_graph_cycle','adsp_tdm_config_cycle','adsp_audio_clock_cycle','discovery_only')):
            p.error('ADSP control boot requires isolated discovery')
        a.adsp_owned_boot=True
    if a.adsp_scm_held_stage:
        if not a.discovery_only or a.pil_driver or a.pil_files or any(v for k,v in vars(a).items()
                if isinstance(v,bool) and k not in ('adsp_scm_held_stage','discovery_only')):
            p.error('ADSP SCM/held no-exec stage requires isolated discovery')
        a.adsp_scm_ipcc_stage=True
    if a.adsp_scm_ipcc_stage:
        if not a.discovery_only or a.pil_driver or a.pil_files or any(v for k,v in vars(a).items()
                if isinstance(v,bool) and k not in ('adsp_scm_ipcc_stage','adsp_scm_held_stage','discovery_only')):
            p.error('ADSP SCM/IPCC no-exec stage requires isolated discovery')
        a.adsp_owned_stage=True
    if a.adsp_scm_bounded_stage:
        if not a.discovery_only or a.pil_driver or a.pil_files or any(v for k,v in vars(a).items()
                if isinstance(v,bool) and k not in ('adsp_scm_bounded_stage','discovery_only')):
            p.error('ADSP SCM/bounded no-exec stage requires isolated discovery')
        p.error('v86 bounded stop disabled at INTERRUPTED limit; qualify IPCC receipt before a changed experiment')
    if a.adsp_scm_channel_stage:
        if not a.discovery_only or a.pil_driver or a.pil_files or any(v for k,v in vars(a).items()
                if isinstance(v,bool) and k not in ('adsp_scm_channel_stage','discovery_only')):
            p.error('ADSP SCM/channel no-exec stage requires isolated discovery')
        p.error('v83 channel/PAS combination disabled after unverified shutdown; no phone command issued')
    if a.adsp_scm_wire_stage:
        if not a.discovery_only or a.pil_driver or a.pil_files or any(v for k,v in vars(a).items()
                if isinstance(v,bool) and k not in ('adsp_scm_wire_stage','discovery_only')):
            p.error('ADSP SCM/wire no-exec stage requires isolated discovery')
        p.error('v82 wire/PAS combination disabled after unverified shutdown; no phone command issued')
    if a.qmp_wire_cycle and (not a.discovery_only or a.pil_driver or a.pil_files or any(v for k,v in vars(a).items()
            if isinstance(v,bool) and k not in ('qmp_wire_cycle','qmp_tpl_cycle','qmp_smccc_cycle','qmp_ipcc_cycle','discovery_only'))):
        p.error('QMP wire lifecycle requires isolated discovery')
    if a.adsp_scm_qmp_stage:
        if not a.discovery_only or a.pil_driver or a.pil_files or any(v for k,v in vars(a).items()
                if isinstance(v,bool) and k not in ('adsp_scm_qmp_stage','discovery_only')):
            p.error('ADSP SCM/QMP no-exec stage requires isolated discovery')
        p.error('v78c native GLink QMP/PAS combination disabled after unverified shutdown; no phone command issued')
    if a.adsp_scm_stage:
        if not a.discovery_only or a.pil_driver or a.pil_files or any(v for k,v in vars(a).items()
                if isinstance(v,bool) and k not in ('adsp_scm_stage','discovery_only')):
            p.error('ADSP SCM no-exec stage requires isolated discovery')
        a.adsp_owned_stage=True
    if a.icb_run_scm_bandwidth_cycle:
        if not a.discovery_only or a.pil_driver or a.pil_files or any(v for k,v in vars(a).items()
                if isinstance(v,bool) and k not in ('icb_run_scm_bandwidth_cycle','discovery_only')):
            p.error('SCM bandwidth lifecycle requires isolated discovery')
        a.icb_run_bandwidth_cycle=True
    if a.qmp_load_state_cycle:p.error('v70 generic QMP TX disabled after missing remote intent; no device operation')
    if a.adsp_qmp_stage:
        if not a.discovery_only or a.pil_driver or a.pil_files or any(v for k,v in vars(a).items()
                if isinstance(v,bool) and k not in ('adsp_qmp_stage','discovery_only')):
            p.error('ADSP QMP no-exec stage requires isolated discovery')
        a.adsp_owned_stage=True
        if a.boot_image.is_file() and hashlib.sha256(a.boot_image.read_bytes()).hexdigest() in (
                'f68ad6bc55ef7256d9824bd5f01edc13ff12af902640408e1ca9235c7128e861',
                '1192dfc8f69875a319f3bc18047a057b73bf7c3d88fc1b64d7e6d3ba0d7983ca',
                '540d504e9ec9f130f1947e6bf731edf36b73ddce0f12af24583ddc31ca5f07df'):
            p.error('v73-v75 QMP/PAS combinations disabled after unverified secure cleanup; no phone command issued')
    if a.adsp_control_boot and a.boot_image.is_file() and hashlib.sha256(a.boot_image.read_bytes()).hexdigest()=='66d8400624b7fbbf3eecac552e69838e900a3877f2a8f26efb3ce1ede993dd70':
        p.error('v94b control boot disabled after interrupted PAS shutdown; use the changed resume-service candidate')
    if a.adsp_owned_boot and a.boot_image.is_file() and hashlib.sha256(a.boot_image.read_bytes()).hexdigest()=='ab23e1ba24cbbd9bdfeb4141b8a93c304303d56b6907f77e5a1018ccf7ed11c9':
        p.error('owned DSP v69 disabled after unverified PAS shutdown; qualify AOP/load_state before a changed boot experiment')
    if a.audio_input_format=='ios48-mfc' and not (a.adsp_tdm_pcm_graph_cycle and a.audio_pcm_repeat and a.audio_pcm_periods==2):p.error('MFC format requires isolated PCM graph and exactly two silent repeat requests')
    if a.amdb_module!='tdm' and not (a.adsp_amdb_info or a.adsp_amdb_load_cycle):p.error('--amdb-module=mfc requires isolated AMDB info/load')
    profile=a.boot_image.parent/'build-profile.json'
    if profile.is_file() and json.loads(profile.read_text()).get('audio_input_format','native96')!=a.audio_input_format:p.error('input format must match image profile')
    if profile.is_file() and json.loads(profile.read_text()).get('amdb_module','tdm')!=a.amdb_module:p.error('AMDB module must match image profile')
    hardware_status=a.boot_image.parent/'hardware-status.json'
    if hardware_status.is_file():
        status=json.loads(hardware_status.read_text())
        if not a.boot_image.is_file() or status.get('image_sha256')!=hashlib.sha256(a.boot_image.read_bytes()).hexdigest():p.error('hardware status does not match the boot image; no phone command issued')
        if status.get('boot_allowed') is False or status.get('hardware_status','').startswith('FAILED'):p.error('hardware-failed or superseded boot image is disabled; use a changed candidate; no phone command issued')
    if profile.is_file() and json.loads(profile.read_text()).get('i2c_only') and not (a.discovery_only and a.i2c_inventory):p.error('dedicated I2C image requires isolated --i2c-inventory')
    if profile.is_file() and bool(json.loads(profile.read_text()).get('audio_fast_stop'))!=a.audio_fast_stop:p.error('fast STOP image and explicit option must match')
    if profile.is_file() and bool(json.loads(profile.read_text()).get('audio_usb_ram_only'))!=a.audio_usb_ram_only:p.error('RAM-only USB image and explicit option must match')
    if profile.is_file() and bool(json.loads(profile.read_text()).get('audio_muted_analog'))!=a.audio_muted_analog:p.error('muted analog image and explicit option must match')
    if profile.is_file() and json.loads(profile.read_text()).get('audio_pcm_periods',2)!=a.audio_pcm_periods:p.error('PCM period count must match image profile')
    if profile.is_file() and bool(json.loads(profile.read_text()).get('audio_pcm_repeat'))!=a.audio_pcm_repeat:p.error('repeat PCM image and explicit option must match')
    if profile.is_file() and bool(json.loads(profile.read_text()).get('audio_pas_timing'))!=a.audio_pas_timing:p.error('PAS timing image and explicit option must match')
    if profile.is_file() and bool(json.loads(profile.read_text()).get('audio_runtime_600ms'))!=a.audio_runtime_600ms:p.error('600ms runtime image and explicit option must match')
    if profile.is_file() and bool(json.loads(profile.read_text()).get('audio_quiet_progress'))!=a.audio_quiet_progress:p.error('quiet progress image and explicit option must match')
    if profile.is_file() and bool(json.loads(profile.read_text()).get('audio_digital_early_stop'))!=a.audio_digital_early_stop:p.error('digital early STOP image and explicit option must match')
    if profile.is_file() and bool(json.loads(profile.read_text()).get('audio_digital_capture'))!=a.audio_digital_capture:p.error('digital capture image and explicit option must match')
    if profile.is_file() and bool(json.loads(profile.read_text()).get('audio_digital_pll'))!=a.audio_digital_pll:p.error('digital PLL image and explicit option must match')
    if profile.is_file() and bool(json.loads(profile.read_text()).get('audio_runtime_deadline'))!=a.audio_runtime_deadline:p.error('audio runtime deadline image and explicit option must match')
    if profile.is_file() and bool(json.loads(profile.read_text()).get('audio_qup_hold'))!=a.audio_qup_hold:p.error('audio QUP hold image and explicit option must match')
    if profile.is_file() and bool(json.loads(profile.read_text()).get('audio_amp_preboot'))!=a.audio_amp_preboot:p.error('audio amp preboot image and explicit option must match')
    if profile.is_file() and bool(json.loads(profile.read_text()).get('audio_amp_direct_wait'))!=a.audio_amp_direct_wait:p.error('audio amp direct wait image and explicit option must match')
    if profile.is_file() and bool(json.loads(profile.read_text()).get('audio_amp_prestart'))!=a.audio_amp_prestart:p.error('audio amp prestart image and explicit option must match')
    if profile.is_file() and bool(json.loads(profile.read_text()).get('audio_amp_profile'))!=a.audio_amp_profile:p.error('audio amp profile image and explicit option must match')
    if profile.is_file() and json.loads(profile.read_text()).get('qup_core_keepalive') and not a.qup_core_keepalive:p.error('QUP2 keepalive image requires --qup-core-keepalive')
    if profile.is_file() and json.loads(profile.read_text()).get('tdm_pins_only') and not a.tdm_pins_cycle:p.error('TDM pin image requires --tdm-pins-cycle')
    if profile.is_file() and json.loads(profile.read_text()).get('pcm_data_only') and not a.pcm_data_cycle:p.error('PCM data image requires --pcm-data-cycle')
    if profile.is_file() and json.loads(profile.read_text()).get('pcm_memory_only') and not a.pcm_memory_cycle:p.error('PCM mapping image requires --pcm-memory-cycle')
    if profile.is_file() and json.loads(profile.read_text()).get('audio_map_probe') and not a.audio_map_cycle:p.error('mapping image requires --audio-map-cycle')
    if profile.is_file() and json.loads(profile.read_text()).get('audio_context_inventory') and not a.audio_context_info:p.error('context audit image requires --audio-context-info')
    if profile.is_file() and json.loads(profile.read_text()).get('audio_dma_inventory') and not (a.discovery_only and a.adsp_tdm_pcm_graph_cycle and a.audio_dma_info):p.error('dedicated audio DMA image requires isolated --audio-dma-info')
    if profile.is_file() and json.loads(profile.read_text()).get('tdm_pcm_graph_only') and not (a.discovery_only and a.adsp_tdm_pcm_graph_cycle):p.error('dedicated PCM graph image requires isolated --adsp-tdm-pcm-graph-cycle')
    if profile.is_file() and json.loads(profile.read_text()).get('tdm_run_only') and not (a.discovery_only and a.adsp_tdm_run_cycle):p.error('dedicated TDM run image requires isolated --adsp-tdm-run-cycle')
    if profile.is_file() and json.loads(profile.read_text()).get('audio_clock_only') and not (a.discovery_only and a.adsp_audio_clock_cycle):p.error('dedicated audio clock image requires isolated --adsp-audio-clock-cycle')
    if profile.is_file() and json.loads(profile.read_text()).get('tdm_config_only') and not (a.discovery_only and a.adsp_tdm_config_cycle):p.error('dedicated TDM config image requires isolated --adsp-tdm-config-cycle')
    if profile.is_file() and json.loads(profile.read_text()).get('tdm_graph_only') and not (a.discovery_only and a.adsp_tdm_graph_cycle):p.error('dedicated TDM graph image requires isolated --adsp-tdm-graph-cycle')
    if profile.is_file() and json.loads(profile.read_text()).get('amdb_load_only') and not (a.discovery_only and a.adsp_amdb_load_cycle):p.error('dedicated AMDB library image requires isolated --adsp-amdb-load-cycle')
    if profile.is_file() and json.loads(profile.read_text()).get('amdb_only') and not (a.discovery_only and a.adsp_amdb_info):p.error('dedicated AMDB image requires isolated --adsp-amdb-info')
    if profile.is_file() and json.loads(profile.read_text()).get('gpr_only') and not (a.discovery_only and a.adsp_spf_state):p.error('dedicated GPR image requires isolated --adsp-spf-state')
    if profile.is_file() and json.loads(profile.read_text()).get('apps_only') and not (a.discovery_only and a.adsp_apps_open):
        p.error('dedicated apps image requires isolated discovery with --adsp-apps-open')
    if profile.is_file() and json.loads(profile.read_text()).get('control_only') and not (a.discovery_only and a.adsp_control_boot and not a.adsp_apps_open):
        p.error('dedicated control image requires isolated discovery with --adsp-control-boot')
    if profile.is_file() and json.loads(profile.read_text()).get('held_only') and not (a.discovery_only and a.adsp_scm_held_stage):
        p.error('dedicated held image requires isolated discovery with --adsp-scm-held-stage')
    if profile.is_file() and json.loads(profile.read_text()).get('glink_only') and not (a.discovery_only and a.glink_smem_info):
        p.error('dedicated GLink image requires isolated discovery with --glink-smem-info')
    if profile.is_file() and json.loads(profile.read_text()).get('icb_only') and not a.icb_run_scm_bandwidth_cycle:
        p.error('dedicated ICB image requires isolated --icb-run-scm-bandwidth-cycle')
    if profile.is_file() and json.loads(profile.read_text()).get('qmp_only') and not (a.qmp_mailbox_load_state_cycle or a.qmp_wire_cycle):
        p.error('dedicated QMP image requires isolated --qmp-mailbox-load-state-cycle or --qmp-wire-cycle')
    if profile.is_file() and json.loads(profile.read_text()).get('owned_stage_only') and not (a.adsp_owned_stage or a.adsp_owned_boot):
        p.error('dedicated owned-stage image requires isolated --adsp-owned-stage or --adsp-owned-boot')
    if a.adsp_owned_boot and (not a.discovery_only or a.pil_driver or a.pil_files or any(v for k,v in vars(a).items()
            if isinstance(v,bool) and k not in ('audio_pas_timing','audio_pcm_repeat','audio_muted_analog','audio_usb_ram_only','audio_fast_stop','audio_runtime_600ms','audio_quiet_progress','audio_digital_early_stop','audio_digital_capture','audio_digital_pll','audio_runtime_deadline','audio_qup_hold','audio_amp_preboot','audio_amp_direct_wait','audio_amp_prestart','audio_amp_profile','qup_core_keepalive','audio_i2c_window','audio_i2c_hold','tdm_pins_cycle','pcm_data_cycle','pcm_memory_cycle','audio_map_cycle','audio_context_info','audio_dma_info','adsp_tdm_pcm_graph_cycle','adsp_tdm_run_cycle','adsp_owned_boot','adsp_control_boot','adsp_apps_open','adsp_spf_state','adsp_amdb_info','adsp_amdb_load_cycle','adsp_tdm_graph_cycle','adsp_tdm_config_cycle','adsp_audio_clock_cycle','discovery_only'))):
        p.error('ADSP owned secure boot requires isolated discovery')
    if a.adsp_owned_stage and (not a.discovery_only or a.pil_driver or a.pil_files or any(v for k,v in vars(a).items()
            if isinstance(v,bool) and k not in ('adsp_owned_stage','adsp_qmp_stage','adsp_scm_stage','adsp_scm_qmp_stage','adsp_scm_wire_stage','adsp_scm_channel_stage','adsp_scm_bounded_stage','adsp_scm_ipcc_stage','adsp_scm_held_stage','discovery_only'))):
        p.error('ADSP owned no-exec stage requires isolated discovery')
    if a.adsp_package and not (a.adsp_owned_stage or a.adsp_owned_boot):p.error('--adsp-package requires isolated --adsp-owned-stage')
    if (a.adsp_owned_stage or a.adsp_owned_boot) and (not a.adsp_package or not a.adsp_package.is_file() or hashlib.sha256(a.adsp_package.read_bytes()).hexdigest()!='ce5a2ee6534ea6fe4e5d2272252989854a3e0277893f49c87d63376458f7d237'):
        p.error('ADSP owned stage requires the pinned MADSP001 package')
    if a.adsp_dependency_cycle and (not a.discovery_only or a.pil_driver or a.pil_files or any(v for k,v in vars(a).items()
            if isinstance(v,bool) and k not in ('adsp_dependency_cycle','discovery_only'))):
        p.error('ADSP dependency lifecycle requires isolated discovery')
    if a.icb_run_bandwidth_cycle and (not a.discovery_only or a.pil_driver or a.pil_files or any(v for k,v in vars(a).items()
            if isinstance(v,bool) and k not in ('icb_run_bandwidth_cycle','icb_run_scm_bandwidth_cycle','discovery_only'))):
        p.error('ICB RUN bandwidth lifecycle requires isolated discovery')
    if a.icb_run_cycle and (not a.discovery_only or a.pil_driver or a.pil_files or any(v for k,v in vars(a).items()
            if isinstance(v,bool) and k not in ('icb_run_cycle','discovery_only'))):
        p.error('ICB RUN transition requires isolated discovery')
    if a.icb_bandwidth_cycle and (not a.discovery_only or a.pil_driver or a.pil_files or any(v for k,v in vars(a).items()
            if isinstance(v,bool) and k not in ('icb_bandwidth_cycle','discovery_only'))):
        p.error('ICB bandwidth lifecycle requires isolated discovery')
    if a.xo_rpmh_hold and (not a.discovery_only or a.pil_driver or a.pil_files or any(v for k,v in vars(a).items()
            if isinstance(v,bool) and k not in ('xo_rpmh_hold','discovery_only'))):
        p.error('physical XO hold requires isolated discovery')
    if a.xo_npa_test and (not a.discovery_only or a.pil_driver or a.pil_files or any(v for k,v in vars(a).items()
            if isinstance(v,bool) and k not in ('xo_npa_test','discovery_only'))):
        p.error('XO provider test requires isolated discovery')
    if a.icb_zero_cycle and a.boot_image.is_file() and hashlib.sha256(a.boot_image.read_bytes()).hexdigest() in ('f17888cacdf516f03d33e16ad3849f49512728e08a646cf5777380a24e21c2d4', '9c9b88771f8be4d985d74a1b082122164e7a0241eba6cf96a5f037d4e9ea0b91'):
        p.error('failed ICB v51/v52 image is disabled; explicit native DestroyClient binding required')
    if a.icb_zero_cycle and (not a.discovery_only or a.pil_driver or a.pil_files or any(v for k,v in vars(a).items()
            if isinstance(v,bool) and k not in ('icb_zero_cycle','discovery_only'))):
        p.error('ICB zero-request lifecycle requires isolated discovery')
    if a.adsp_dependency_info and (not a.discovery_only or a.pil_driver or a.pil_files or any(v for k,v in vars(a).items()
            if isinstance(v,bool) and k not in ('adsp_dependency_info','discovery_only'))):
        p.error('dependency inventory requires isolated discovery')
    if a.rpmh_zero_cycle and (not a.discovery_only or a.pil_driver or a.pil_files or any(v for k,v in vars(a).items()
            if isinstance(v,bool) and k not in ('rpmh_zero_cycle','discovery_only'))):
        p.error('RPMh zero-command lifecycle requires isolated discovery')
    if a.adsp_icb_info and (not a.discovery_only or a.pil_driver or a.pil_files or any(v for k,v in vars(a).items()
            if isinstance(v,bool) and k not in ('adsp_icb_info','discovery_only'))):
        p.error('ICB inventory requires isolated discovery')
    if a.adsp_xo_info and (not a.discovery_only or any(v for k,v in vars(a).items()
            if isinstance(v,bool) and k not in ('adsp_xo_info','discovery_only'))):
        p.error('XO inventory requires isolated discovery; no other probe or UI action may be combined')
    if a.qmp_mailbox_load_state_cycle:
        if not a.discovery_only or a.pil_driver or a.pil_files or any(v for k,v in vars(a).items()
                if isinstance(v,bool) and k not in ('qmp_mailbox_load_state_cycle','discovery_only')):
            p.error('QMP load_state lifecycle requires isolated discovery')
        a.qmp_native_settled_close=True
    if a.qmp_native_settled_close:
        if a.qmp_native_cycle or a.qmp_native_pump_cycle or a.qmp_native_peer_close:p.error('choose one native QMP lifecycle contract')
        a.qmp_native_pump_cycle=True
    if a.qmp_native_peer_close:
        if a.qmp_native_cycle or a.qmp_native_pump_cycle:p.error('choose one native QMP lifecycle contract')
        a.qmp_native_pump_cycle=True
    if a.qmp_native_pump_cycle:
        if a.qmp_native_cycle:p.error('choose one native QMP lifecycle mode')
        a.qmp_native_cycle=True
    if a.qmp_native_cycle and (not a.discovery_only or any(v for k,v in vars(a).items()
            if isinstance(v,bool) and k not in ('qmp_native_cycle','qmp_native_pump_cycle','qmp_native_peer_close','qmp_native_settled_close','qmp_mailbox_load_state_cycle','discovery_only'))):
        p.error('native QMP lifecycle requires isolated discovery; no other probe, UI or hardware action may be combined')
    if a.adsp_power_info and (not a.discovery_only or any(getattr(a,k) for k in ('exercise','manual_ten_minute','pil_driver','pil_files','smp2p_prepare','smp2p_notify','npa_clients','npa_native_release','npa_votes','dsi_dim_probe','buttons_probe'))):
        p.error('ADSP power inventory requires isolated discovery; no hardware mutations may be combined')
    if a.pil_bounded_boot:p.error('bounded DSP boot disabled after v34b lost USB during PAS shutdown; qualify SMP2P/stop handling before another auth-reset')
    if a.pil_smp2p_boot:p.error('SMP2P DSP boot disabled after v38 received stop ACK but lost USB during PAS shutdown; no phone commands issued')
    if a.pil_smp2p_stage:a.pil_powered_stage=True
    if a.smp2p_notify:a.smp2p_prepare=True
    if a.smp2p_prepare:
        if not a.discovery_only or any(getattr(a,key) for key in ('daily_probe','scm_info','scm_availability','adc_channels','adc_battery','pil_driver','pil_files','pil_config','pil_adsp_cycle','pil_memory','pil_header_only','pil_elf_only','pil_pas_metadata','pil_pas_memory','pil_first_segment','pil_all_segments','pil_powered_stage','npa_info','npa_resources','npa_vcs_info','npa_clients','npa_native_release','npa_votes','dsi_info','display_memory_info','dsi_read_power','dsi_read_identity','dsi_dim_probe','smem_info','smem_channels','glink_probe','buttons_probe','exercise','manual_ten_minute')):
            p.error('SMP2P prepare/reset must be isolated from every other probe and UI action')
    if a.pil_bounded_boot:a.pil_powered_stage=True
    if a.pil_powered_stage:
        if not a.discovery_only or not a.pil_files or any(getattr(a,key) for key in ('npa_clients','npa_native_release','npa_votes','npa_info','npa_resources','npa_vcs_info','exercise','manual_ten_minute','glink_probe','dsi_dim_probe','buttons_probe')):
            p.error('powered PIL stage requires isolated discovery and pinned RAM files')
        a.pil_all_segments=True
    if a.npa_native_release:
        if any(getattr(a,key) for key in ('daily_probe','scm_info','scm_availability','adc_channels','adc_battery','pil_driver','pil_config','pil_files','pil_adsp_cycle','pil_memory','pil_header_only','pil_elf_only','pil_pas_metadata','pil_pas_memory','pil_first_segment','pil_all_segments','pil_proxy_config','npa_info','npa_resources','npa_vcs_info','npa_votes','dsi_info','display_memory_info','dsi_read_power','dsi_read_identity','dsi_dim_probe','smem_info','smem_channels','glink_probe','buttons_probe','exercise','manual_ten_minute')):
            p.error('native NPA release/reset must be isolated from all other probes and UI actions')
        a.npa_clients=True
    if a.npa_votes:a.npa_clients=True
    if a.npa_votes:p.error('native vote cycle disabled after v29 failed original-state restoration; audit release semantics before another hardware vote')
    if a.pil_all_segments:a.pil_first_segment=True;a.display_memory_info=True
    if a.pil_first_segment:a.pil_pas_memory=True
    if a.pil_pas_memory:a.pil_pas_metadata=True
    if a.pil_pas_metadata:a.pil_elf_only=True;a.scm_availability=True
    if a.pil_elf_only:a.pil_header_only=True
    if not a.ramdisk.is_file() or a.ramdisk.stat().st_size>0x800000:
        p.error('support RAM image must exist and fit below the reserved 8 MiB input mailbox boundary; no device operation performed')
    if not a.system_disk.is_file():p.error('system disk is missing; no device operation performed')
    if a.scene=='locale' and not a.settings_template.is_file():p.error('locale test requires the local Settings glyph template')
    if a.desktop_template_gate and (not a.exercise or a.scene.startswith('native-') or not a.settings_template.is_file()):
        p.error('desktop template gate requires host-driven exercise and the verified local Settings glyph')
    if a.manual_ten_minute and (a.exercise or a.discovery_only):
        p.error('ten-minute mode is for manual desktop testing without automated exercise/discovery')
    budget=600 if a.manual_ten_minute else 90
    if not 0<=a.ui_settle<=3:p.error('--ui-settle must be between 0 and 3 seconds')
    if a.brightness_backend and not (a.daily_buttons and a.pacing=='hardware'):
        p.error('brightness backend requires daily buttons and hardware pacing')
    if a.dsi_read_power and not a.discovery_only:p.error('--dsi-read-power requires --discovery-only')
    if a.display_memory_info and not a.discovery_only:p.error('--display-memory-info requires --discovery-only')
    if a.pil_proxy_config and not a.discovery_only:p.error('--pil-proxy-config requires --discovery-only')
    if a.npa_info and not a.discovery_only:p.error('--npa-info requires --discovery-only')
    if a.npa_vcs_info and not a.discovery_only:p.error('--npa-vcs-info requires --discovery-only')
    if a.npa_resources and not a.discovery_only:p.error('--npa-resources requires --discovery-only')
    if a.npa_clients and not a.discovery_only:p.error('--npa-clients requires --discovery-only')
    if a.dsi_read_identity and not a.discovery_only:p.error('--dsi-read-identity requires --discovery-only')
    if a.dsi_dim_probe and not a.discovery_only:p.error('--dsi-dim-probe requires --discovery-only')
    if a.glink_probe and not a.discovery_only:p.error('--glink-probe requires --discovery-only')
    if a.adc_channels and not a.discovery_only:p.error('--adc-channels requires --discovery-only')
    if a.scm_info and not a.discovery_only:p.error('--scm-info requires --discovery-only')
    if a.scm_availability and not a.discovery_only:p.error('--scm-availability requires --discovery-only')
    if a.adc_battery and not a.discovery_only:p.error('--adc-battery requires --discovery-only')
    if a.adc_channels and a.adc_battery:p.error('choose channel lookup or battery conversions')
    if a.pil_driver:
        if not a.discovery_only:p.error('--pil-driver requires --discovery-only')
        driver_sha=hashlib.sha256(a.pil_driver.read_bytes()).hexdigest()
        if driver_sha not in ('de49a2969d4dfe0a84f3be4b4cdc156be32120aaceb19b100e9130a22b21db42','cc310ec13454b595bfc3d5302e41de9eb5eecc97cb490412db47cca6d5b2fc44'):p.error('unqualified no-AUTO PIL derivative')
    if a.pil_config and not a.pil_driver:p.error('--pil-config requires --pil-driver and discovery only')
    if a.pil_files:
        if not a.pil_driver:p.error('--pil-files requires --pil-driver and discovery only')
        if hashlib.sha256(a.pil_files.read_bytes()).hexdigest()!='2848fc433f124550c95d72e5de2697858d68be3070ddf2b5d8816448eca9dc19':p.error('unqualified ROM RAM filesystem')
    if a.pil_adsp_cycle and not a.pil_files:p.error('--pil-adsp-cycle requires the pinned PIL driver/RAM files and discovery only')
    if a.pil_adsp_cycle:p.error('ADSP cycle disabled after v10b unverified device reset; qualify smaller stages first')
    if a.pil_memory and not a.discovery_only:p.error('--pil-memory requires --discovery-only')
    if a.pil_header_only and (not a.pil_files or not a.pil_driver or driver_sha!='cc310ec13454b595bfc3d5302e41de9eb5eecc97cb490412db47cca6d5b2fc44'):p.error('header probe requires RAM files and the exact PAS-refusal derivative')
    if a.scene=='brightness' and not (a.pacing=='hardware' and a.daily_buttons and a.exercise):
        p.error('brightness scene requires hardware pacing, daily buttons and exercise')
    a.out=a.out.resolve();a.out.mkdir(exist_ok=False)
    # Archive the actual controller/reporters before touching USB. Build-time
    # source archives can predate a host-only report fix.
    host_sources={}
    for source in (ROOT/'tools').rglob('*.py'):
        rel=source.relative_to(ROOT);data=source.read_bytes()
        dest=a.out/'host-source'/rel;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(data)
        host_sources[rel.as_posix()]=hashlib.sha256(data).hexdigest()
    (a.out/'host-sources.json').write_text(json.dumps(host_sources,indent=2)+'\n')
    logs=a.out/'logs';logs.mkdir()
    report=dict(experiment='discovery-v1' if a.discovery_only else ('gpu600-manual-v1' if a.manual_ten_minute else 'gpu90-v2'),budget_seconds=budget,new_gpu_attempt=not a.discovery_only,
        scope='isolated firmware discovery/lifecycle; no XNU or GPU' if a.discovery_only else 'manual GPU preview with new EFI wait/cap combination, not thermal acceptance',
        continuous_battery_monitor=False,automatic_retry=False,old_manifests_preserved=True)
    report['render_scale']=a.render_scale
    if a.pil_pas_metadata:
        report['requested_pil_stage']=('smp2p-auth-stop' if a.pil_smp2p_boot else 'bounded-auth-stop' if a.pil_bounded_boot else 'powered-stop-restore' if a.pil_powered_stage else 'all-segments' if a.pil_all_segments else 'first-segment' if a.pil_first_segment else
                                       'memory-setup' if a.pil_pas_memory else 'metadata')
    def save():
        (a.out/'exercise.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
    def run(*args,timeout=15):
        try:
            r=subprocess.run(list(map(str,args)),capture_output=True,timeout=timeout,cwd=ROOT)
        except subprocess.TimeoutExpired as exc:
            # Fastboot INFO packets may be the last pre-reset evidence.
            raw=(exc.stdout or b'')+(exc.stderr or b'')
            (a.out/f'command-timeout-{len(list(a.out.glob("command-timeout-*.txt"))):02}.txt').write_bytes(raw)
            raise
        text=(r.stdout+r.stderr).decode(errors='replace')
        if r.returncode:
            (a.out/f'command-failure-{len(list(a.out.glob("command-failure-*.txt"))):02}.txt').write_text(text,encoding='utf8')
            raise RuntimeError(text)
        return text
    def fb(*args,timeout=12):return run(MARBLE_FASTBOOT,'-s','xnuloader',*args,timeout=timeout)
    def module(name,path):
        s=importlib.util.spec_from_file_location(name,ROOT/path)
        m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
    thermal=module('thermal_preview_report','tools/marble-thermal-report.py')
    idle=module('idle_preview_report','tools/marble-efi-idle-report.py')
    npa=module('npa_lifecycle_report','tools/marble-npa-report.py') if a.npa_clients or a.pil_powered_stage else None
    inspect_held=(npa.inspect_bounded_boot if a.pil_bounded_boot else npa.inspect_powered_stage) if a.pil_powered_stage else None
    smp=module('smp2p_prepare_report','tools/marble-smp2p-report.py') if a.smp2p_prepare or a.pil_smp2p_stage else None
    if a.pil_smp2p_stage:inspect_held=smp.inspect_boot if a.pil_smp2p_boot else smp.inspect_powered_stage
    inspect_smp=(smp.inspect_notify if a.smp2p_notify else smp.inspect) if a.smp2p_prepare else None
    actions=[('tap','settings',(663,1700)),('swipe','scroll',(530,1900,530,700)),
             ('swipe','home',(540,2375,540,1000)),('hold','menu',(663,1700)),
             ('tap','dismiss',(1010,1700)),('swipe','page',(950,1200,120,1200)),
             ('swipe','page-home',(120,1200,950,1200))]
    if a.scene=='panels':
        actions=[('swipe','widgets',(120,1200,950,1200)),
                 ('swipe','widgets-home',(950,1200,120,1200)),
                 ('swipe','library',(950,1200,120,1200)),
                 ('swipe','library-home',(120,1200,950,1200)),
                 ('swipe','notifications',(150,8,150,1550)),
                 ('swipe','notifications-close',(540,2398,540,800)),
                 ('swipe','control-center',(990,8,990,1550)),
                 ('swipe','control-close',(540,2398,540,800))]
    if a.scene=='panel-focus':
        actions=[('swipe','notifications',(150,8,150,1550)),
                 ('swipe','notifications-close',(540,2398,540,800)),
                 ('swipe','control-center',(990,8,990,1550)),
                 ('swipe','control-close',(540,2398,540,800))]
    if a.scene in ('control-focus','control-home'):
        actions=[('swipe','control-center',(990,8,990,1550)),
                 ('swipe','control-close',(540,2398 if a.control_origin=='edge' else 2160,540,800)),
                 ('tap','control-backdrop',(540,2160))]
        if a.scene=='control-home':
            actions=actions[:2]+[('tap','settings-after-control',(663,1700)),('swipe','settings-home',(540,2398,540,800))]
        report['control_dismissal_origin']=a.control_origin
    if a.scene=='settings-focus':
        actions=[('tap','settings',(663,1700)),('swipe','settings-scroll',(530,1900,530,700)),
                 ('swipe','settings-scroll-back',(530,700,530,1900)),
                 ('swipe','settings-exit',(540,2375,540,1000))]
    if a.scene=='animation-focus':
        actions=[]
        for i in range(2):
            actions += [('tap',f'settings-open-{i}',(663,1700)),
                        # Begin at the bottommost usable pixel so that a
                        # delayed edge sample cannot start in app content.
                        ('swipe',f'settings-exit-{i}',(540,2398,540,1000))]
    if a.scene=='locale':
        actions=[('tap','locale-settings',(663,1700)),('swipe','locale-scroll',(540,1900,540,850)),
                 ('swipe','locale-home',(540,2375,540,800)),('swipe','locale-keyboard',(540,900,540,1800))]
    if a.scene=='pages-focus':
        # Use the empty row below the app grid, avoiding a synthetic DOWN on
        # an icon while the compositor is still completing an earlier frame.
        actions=[('swipe','today',(120,2000,950,2000)),
                 ('swipe','today-home',(950,2000,120,2000)),
                 ('swipe','page-two',(950,2000,120,2000)),
                 ('swipe','app-library',(950,2000,120,2000)),
                 ('swipe','library-page-two',(120,2000,950,2000)),
                 ('swipe','pages-home',(120,2000,950,2000))]
    if a.scene=='brightness':
        actions=[('brightness-dim','dimmed',()),('tap','settings-dimmed',(663,1700)),
                 ('swipe','scroll-dimmed',(530,1900,530,700)),
                 ('swipe','home-dimmed',(540,2375,540,1000))]
    if a.scene=='brightness-observe':
        actions=[('swipe','control-center',(990,8,990,1550)),
                 ('swipe','slider-down',(660,1070,660,1240)),
                 ('swipe','slider-up',(660,1240,660,980))]
    if a.scene=='brightness-cycle':
        actions=[('swipe','control-center',(990,8,990,1550)),
                 ('swipe','slider-low',(660,1150,660,1250)),
                 ('swipe','slider-high',(660,1240,660,980)),
                 ('swipe','slider-low-again',(660,1050,660,1250))]
    if a.scene in ('daily','power'):
        if not a.daily_buttons:p.error('daily scene requires --daily-buttons and the matching support disk')
        actions=[('key','volume-up',(2,)),('key','volume-down',(3,)),
                 ('key','lock',(1,)),('key','wake',(1,)),
                 ('swipe','unlock',(540,2375,540,800)),
                 ('tap','settings-after-wake',(663,1700))]
        if a.scene=='power':
            actions=[('key','lock',(1,)),('capture','black-held',()),('wake-unlock','single-unlock',()),
                     ('tap','settings-after-wake',(663,1700)),('key','final-lock',(1,))]
    report['scene']=a.scene
    action_gap=.2 if a.scene in ('brightness-observe','brightness-cycle') else 2
    action_index=0;action_at=0;ui_ready=False;native_captured=set()
    def ui_action(kind,name,coords=(),settle=None):
        if kind=='brightness-dim':
            report['brightness_lease_requested']=True;save()
            result=fb('oem','brightness-dim')
            report['brightness_dim_command_ok']=True;save()
            return result+ui_action('capture',name)
        extra=['--duration-ms','650'] if kind=='swipe' and name=='control-close' else []
        return run('python','tools/marble-ui-regression.py',kind,name,*coords,
                   '--out',a.out/'ui','--settle',a.ui_settle if settle is None else settle,*extra,timeout=24 if kind=='wake-unlock' else 12)
    save()
    try:
        _,before=battery_snapshot(a.out,'battery-before')
        verify_battery_preflight(a.out/'battery-before.json')
        report['battery_before_c']=before['temperature_c'];save()
        if a.i2c_inventory or a.npa_native_release or a.pil_powered_stage or a.smp2p_prepare or a.qmp_native_cycle or a.qmp_wire_cycle or a.rpmh_zero_cycle or a.icb_zero_cycle or a.xo_npa_test or a.xo_rpmh_hold or a.icb_bandwidth_cycle or a.icb_run_cycle or a.icb_run_bandwidth_cycle or a.adsp_dependency_cycle or a.adsp_owned_stage or a.adsp_owned_boot:
            boot_id=run(MARBLE_ADB,'-s',MARBLE_ANDROID_SERIAL,'shell','cat','/proc/sys/kernel/random/boot_id').strip()
            if not re.fullmatch(r'[0-9a-fA-F-]{36}',boot_id):raise RuntimeError('Android boot ID unavailable before native-release test')
            report['android_boot_id_before_sha256']=hashlib.sha256(boot_id.encode()).hexdigest();save()
        screen_off(a.out)
        print('Battery before:',before['temperature_c'],'C; Android screen OFF; temporary boot next',flush=True)
        report['phone_reboot_requested']=True;save()
        print(run(MARBLE_ADB,'-s',MARBLE_ANDROID_SERIAL,'reboot','bootloader'),flush=True)
        deadline=time.monotonic()+45
        bootloader_ready=False
        while time.monotonic()<deadline:
            if MARBLE_ANDROID_SERIAL in run(MARBLE_FASTBOOT,'devices'):
                try:
                    if 'product: marble' in run(MARBLE_FASTBOOT,'-s',MARBLE_ANDROID_SERIAL,'getvar','product',timeout=5):
                        bootloader_ready=True;break
                except (RuntimeError,subprocess.TimeoutExpired):pass
            time.sleep(1)
        if not bootloader_ready:raise RuntimeError('bootloader USB not responsive; reconnect needed, no image was booted')
        print(run('python','tools/marble-run.py','--serial',MARBLE_ANDROID_SERIAL,'boot','--image',a.boot_image,timeout=70),flush=True)
        deadline=time.monotonic()+90
        while time.monotonic()<deadline:
            if 'xnuloader' in run(MARBLE_FASTBOOT,'devices'):break
            time.sleep(1)
        if 'result: idle' not in fb('getvar','result'):raise RuntimeError('fresh idle loader not found')
        if not a.discovery_only and 'marble-firmware-only: 1' in fb('getvar','marble-firmware-only'):
            raise RuntimeError('firmware-only image refuses desktop sessions; no system disk upload attempted')
        if 'gpu90-die50-60s-hard55-v2' not in fb('getvar','marble-gpu-preview'):
            raise RuntimeError('preview capability mismatch')
        if a.manual_ten_minute and 'manual600-die50-60s-hard55-v1' not in fb('getvar','marble-gpu-manual-preview'):
            raise RuntimeError('ten-minute manual preview capability mismatch')
        if a.render_scale==2 and '2x-preview-v1' not in fb('getvar','marble-render-scale'):
            raise RuntimeError('2x render candidate needs its scale-policy loader; no system upload attempted')
        print(fb('oem','clearscreen'),flush=True)
        print(fb('oem','idle-probe'),flush=True)
        fb('oem','log');fb('get_staged',a.out/'idle-probe.log')
        probe=idle.inspect((a.out/'idle-probe.log').read_text(errors='replace'),'probe')
        report['idle_probe']=probe;save()
        if not probe['passed']:raise RuntimeError('EFI wait probe failed')
        if a.smp2p_prepare:
            if 'owned-init-noipcc-reset-v1' not in fb('getvar','marble-smp2p'):raise RuntimeError('SMP2P prepare/reset capability mismatch')
            if a.smp2p_notify and 'one-adsp-kick-no-start-v1' not in fb('getvar','marble-smp2p-notify'):raise RuntimeError('IPCC notification capability mismatch')
            report['smp2p_prepare_requested']=True;save()
            report['smp2p_notify_requested']=a.smp2p_notify;save()
            try:
                print(fb('oem','smp2p-prepare-notify' if a.smp2p_notify else 'smp2p-prepare',timeout=40),flush=True)
                report['smp2p_prepare_command_ok']=True
            finally:
                fb('oem','log');fb('get_staged',a.out/'smp2p-prepare.log')
            report['smp2p_prepare_report']=inspect_smp((a.out/'smp2p-prepare.log').read_text(errors='replace'));save()
            if not report['smp2p_prepare_report']['passed']:raise RuntimeError('AP-owned SMP2P preparation evidence incomplete')
        if a.rpmh_zero_cycle:
            report['rpmh_zero_cycle_requested']=True;save()
            try:
                print(fb('oem','rpmh-zero-cycle'),flush=True)
                report['rpmh_zero_command_ok']=True
            finally:
                fb('oem','log');fb('get_staged',a.out/'rpmh-zero-cycle.log')
            audit=module('rpmh_native_report','tools/marble-rpmh-native-report.py')
            report['rpmh_zero_report']=audit.inspect((a.out/'rpmh-zero-cycle.log').read_text(errors='replace'));save()
            if not report['rpmh_zero_report']['passed']:raise RuntimeError('native RPMh zero-command closure incomplete')
        if a.xo_rpmh_hold:
            report['xo_rpmh_hold_requested']=True;save()
            try:
                print(fb('oem','xo-rpmh-hold'),flush=True)
                report['xo_rpmh_command_ok']=True
            finally:
                fb('oem','log');fb('get_staged',a.out/'xo-rpmh-hold.log')
            audit=module('xo_rpmh_report','tools/marble-xo-rpmh-report.py')
            report['xo_rpmh_report']=audit.inspect((a.out/'xo-rpmh-hold.log').read_text(errors='replace'));save()
            if not report['xo_rpmh_report']['passed']:raise RuntimeError('physical XO hold incomplete; reset contract required')
        if a.xo_npa_test:
            report['xo_npa_test_requested']=True;save()
            try:
                print(fb('oem','xo-npa-test'),flush=True)
                report['xo_npa_command_ok']=True
            finally:
                fb('oem','log');fb('get_staged',a.out/'xo-npa-test.log')
            audit=module('xo_npa_report','tools/marble-xo-npa-report.py')
            report['xo_npa_report']=audit.inspect((a.out/'xo-npa-test.log').read_text(errors='replace'));save()
            if not report['xo_npa_report']['passed']:raise RuntimeError('software XO provider lifecycle incomplete')
        if a.adsp_owned_boot:
            if a.adsp_control_boot and 'SCM-held-poll-GLINK-SMP2P-stop-v4' not in fb('getvar','marble-adsp-control-boot'):raise RuntimeError('DSP control lifecycle capability mismatch; no package uploaded')
            if not a.adsp_control_boot and 'package-boot-run-xo-lpi-bw-v1' not in fb('getvar','marble-adsp-owned-boot'):raise RuntimeError('owned DSP boot loader capability mismatch; package not uploaded')
            if a.adsp_spf_state and not (a.adsp_amdb_info or a.adsp_amdb_load_cycle or a.adsp_tdm_graph_cycle or a.adsp_audio_clock_cycle or a.adsp_tdm_run_cycle) and 'GPR-SPF-readonly-one-query-v1' not in fb('getvar','marble-adsp-spf-state'):raise RuntimeError('SPF read-only capability mismatch; package not uploaded')
            if a.audio_fast_stop and 'STOP100us-5s-thermal100ms-ACK-required-v1' not in fb('getvar','marble-audio-fast-stop'):raise RuntimeError('fast STOP capability mismatch')
            if a.audio_usb_ram_only and 'post-auth-RAM-noUSB-INFO-v1' not in fb('getvar','marble-audio-usb-ram-only'):raise RuntimeError('RAM-only USB capability mismatch')
            if a.audio_muted_analog and 'AMPPD-cycle-HMUTE90dB-10x1ms-v1' not in fb('getvar','marble-audio-muted-analog'):raise RuntimeError('muted analog capability mismatch')
            if a.audio_pcm_repeat and f'WREP{a.audio_pcm_periods}-tokens-serial4K-reuse-after-DONE-TX-v1' not in fb('getvar','marble-audio-pcm-repeat'):raise RuntimeError('repeat PCM capability mismatch')
            if a.audio_pas_timing and 'RAM-perSMC-time-original1s128-v1' not in fb('getvar','marble-audio-pas-timing'):raise RuntimeError('PAS timing capability mismatch')
            if a.audio_runtime_600ms and 'digital600ms-six-thermal-samples-v1' not in fb('getvar','marble-audio-runtime-600ms'):raise RuntimeError('600ms runtime capability mismatch')
            if a.audio_quiet_progress and 'typed-RAM-log-no-repetitive-USB-INFO-v1' not in fb('getvar','marble-audio-quiet-progress'):raise RuntimeError('quiet progress capability mismatch')
            if a.audio_digital_early_stop and 'PLL-typed-complete-before-idle-STOP-v1' not in fb('getvar','marble-audio-digital-early-stop'):raise RuntimeError('digital early STOP capability mismatch')
            if a.audio_digital_capture and 'PLL-inband-hex-beforeSTOP-watchdog-reset-v2' not in fb('getvar','marble-audio-digital-capture'):raise RuntimeError('digital capture capability mismatch')
            if a.audio_digital_pll and 'AW2032-PWDN-only-PLL-20ms-AMPPD-HMUTE-v1' not in fb('getvar','marble-audio-digital-pll'):raise RuntimeError('digital PLL capability mismatch')
            if a.audio_runtime_deadline and 'DSP-500ms-deadline-I2C-inclusive-v1' not in fb('getvar','marble-audio-runtime-deadline'):raise RuntimeError('audio runtime deadline capability mismatch')
            if a.audio_qup_hold and 'QUP2-extra-refs-afterSTART-untilPAS-v1' not in fb('getvar','marble-audio-qup-hold'):raise RuntimeError('audio QUP hold capability mismatch')
            if a.audio_amp_preboot and 'AW2032-Music-before-auth-down-muted-v1' not in fb('getvar','marble-audio-amp-preboot'):raise RuntimeError('audio amp preboot capability mismatch')
            if a.audio_amp_direct_wait and 'CNTVCT-500ms-same-thermal-IRQ-unchanged-v1' not in fb('getvar','marble-audio-amp-direct-wait'):raise RuntimeError('audio amp direct wait capability mismatch')
            if a.audio_amp_prestart and 'AW2032-Music-before-START-down-muted-v1' not in fb('getvar','marble-audio-amp-prestart'):raise RuntimeError('audio amp prestart capability mismatch')
            if a.audio_amp_profile and 'AW2032-Music-after-START-down-muted-v1' not in fb('getvar','marble-audio-amp-profile'):raise RuntimeError('audio amp profile capability mismatch')
            if a.qup_core_keepalive and 'QUP2-minimum-AMC-WAKE-retained-v1' not in fb('getvar','marble-qup-core-keepalive'):raise RuntimeError('QUP2 keepalive capability mismatch')
            if a.audio_i2c_window and 'QUP2-afterSTART-late-clockID-noamp-v2' not in fb('getvar','marble-audio-i2c-window'):raise RuntimeError('audio I2C window capability mismatch')
            if a.audio_i2c_hold and 'QUP2-config-clocks-across-PCM-noamp-v1' not in fb('getvar','marble-audio-i2c-hold'):raise RuntimeError('audio I2C hold capability mismatch')
            if a.tdm_pins_cycle and 'CAPE-TDM4-RESTORE-v1' not in fb('getvar','marble-tdm-pins-cycle'):raise RuntimeError('TDM pin capability mismatch')
            if a.pcm_data_cycle and 'APM-WREP-ZERO4096-v1' not in fb('getvar','marble-pcm-data-cycle'):raise RuntimeError('PCM data capability mismatch')
            if a.pcm_memory_cycle and 'APM-MAP44-UNMAP28-v1' not in fb('getvar','marble-pcm-memory-cycle'):raise RuntimeError('DSP memory capability mismatch')
            if a.audio_map_cycle and 'APPS-CB-ECATS-RESTORE2' not in fb('getvar','marble-audio-map-cycle'):raise RuntimeError('mapping cycle capability mismatch')
            if a.audio_context_info and 'APPS-CB-RO1' not in fb('getvar','marble-audio-context-info'):raise RuntimeError('context read-only capability mismatch')
            if a.audio_dma_info and 'APPS-SID1801-RO1' not in fb('getvar','marble-audio-dma-info'):raise RuntimeError('audio DMA read-only capability mismatch')
            if a.adsp_tdm_run_cycle and (('TDM608-MFC48to96-noBUF1' if a.audio_input_format=='ios48-mfc' else 'TDM528-PCM-noBUF1') if a.adsp_tdm_pcm_graph_cycle else 'TDM432-run-v1') not in fb('getvar','marble-adsp-tdm-pcm-graph-cycle' if a.adsp_tdm_pcm_graph_cycle else 'marble-adsp-tdm-run-cycle'):raise RuntimeError('TDM run capability mismatch; no package uploaded')
            if a.adsp_audio_clock_cycle and 'PRM204-12288k-lease-v1' not in fb('getvar','marble-adsp-audio-clock-cycle'):raise RuntimeError('audio clock capability mismatch; no package uploaded')
            if a.adsp_tdm_config_cycle and 'TDMcfg-noSTART1' not in fb('getvar','marble-adsp-tdm-config-cycle'):raise RuntimeError('TDM configuration capability mismatch; no package uploaded')
            if a.adsp_tdm_graph_cycle and not a.adsp_tdm_config_cycle and 'GPR-one-disconnected-TDM-open-close-v1' not in fb('getvar','marble-adsp-tdm-graph-cycle'):raise RuntimeError('TDM graph capability mismatch; no package uploaded')
            if a.adsp_amdb_load_cycle and f'GPR-AMDB-one-{a.amdb_module.upper()}-load-unload-v1' not in fb('getvar','marble-adsp-amdb-load-cycle'):raise RuntimeError('AMDB library capability mismatch; no package uploaded')
            if a.adsp_amdb_info and f'GPR-AMDB-one-{a.amdb_module.upper()}-metadata-query-v1' not in fb('getvar','marble-adsp-amdb-info'):raise RuntimeError('AMDB capability mismatch; package not uploaded')
            if a.adsp_apps_open and not a.adsp_spf_state and 'lpass-adsp-apps-intents512-noDATA-v2' not in fb('getvar','marble-adsp-apps-open'):raise RuntimeError('adsp_apps capability mismatch; package not uploaded')
            print(fb('stage',a.adsp_package,timeout=40),flush=True)
            report['adsp_owned_boot_requested']=True;report['adsp_control_boot_requested']=a.adsp_control_boot;report['adsp_apps_open_requested']=a.adsp_apps_open;report['adsp_spf_state_requested']=a.adsp_spf_state and not (a.adsp_amdb_info or a.adsp_amdb_load_cycle or a.adsp_tdm_graph_cycle or a.adsp_audio_clock_cycle or a.adsp_tdm_run_cycle);report['adsp_amdb_info_requested']=a.adsp_amdb_info;report['adsp_amdb_load_cycle_requested']=a.adsp_amdb_load_cycle;report['adsp_tdm_graph_cycle_requested']=a.adsp_tdm_graph_cycle;report['adsp_tdm_config_cycle_requested']=a.adsp_tdm_config_cycle;report['adsp_audio_dma_inventory_requested']=a.audio_dma_info;report['adsp_tdm_pcm_graph_cycle_requested']=a.adsp_tdm_pcm_graph_cycle;report['adsp_tdm_run_cycle_requested']=a.adsp_tdm_run_cycle;report['adsp_audio_clock_cycle_requested']=a.adsp_audio_clock_cycle;save()
            try:
                print(fb('oem','adsp-tdm-pcm-graph-cycle' if a.adsp_tdm_pcm_graph_cycle else 'adsp-tdm-run-cycle' if a.adsp_tdm_run_cycle else 'adsp-audio-clock-cycle' if a.adsp_audio_clock_cycle else 'adsp-tdm-config-cycle' if a.adsp_tdm_config_cycle else 'adsp-tdm-graph-cycle' if a.adsp_tdm_graph_cycle else 'adsp-amdb-load-cycle' if a.adsp_amdb_load_cycle else 'adsp-amdb-info' if a.adsp_amdb_info else 'adsp-spf-state' if a.adsp_spf_state else 'adsp-apps-open' if a.adsp_apps_open else 'adsp-control-boot' if a.adsp_control_boot else 'adsp-owned-boot',timeout=40),flush=True)
                report['adsp_owned_boot_command_ok']=True
            except Exception as command_exc:
                report['adsp_owned_boot_command_error']=str(command_exc);save()
                raise
            finally:
                try:
                    fb('oem','log');fb('get_staged',a.out/'adsp-owned-boot.log')
                except Exception as log_exc:
                    report['adsp_owned_boot_log_error']=str(log_exc);save()
                    if not report.get('adsp_owned_boot_command_error'):raise
            audit=module('adsp_owned_boot_report','tools/marble-adsp-owned-report.py')
            if a.audio_pas_timing:
                timing=module('audio_pas_timing_report','tools/marble-pas-timing-report.py')
                report['audio_pas_timing']=timing.inspect((a.out/'adsp-owned-boot.log').read_text(errors='replace'));save()
                if not report['audio_pas_timing']['passed']:raise RuntimeError('PAS timing evidence incomplete')
            report['adsp_owned_boot_report']=audit.inspect((a.out/'adsp-owned-boot.log').read_text(errors='replace'),1,scm_peak=a.adsp_control_boot,wire=a.adsp_control_boot,raw_shutdown=a.adsp_control_boot,held=a.adsp_control_boot,control=a.adsp_control_boot,apps=a.adsp_apps_open,gpr=a.adsp_spf_state,amdb=a.adsp_amdb_info,amdb_cycle=a.adsp_amdb_load_cycle,tdm_graph=a.adsp_tdm_graph_cycle,tdm_config=a.adsp_tdm_config_cycle,audio_clock=a.adsp_audio_clock_cycle,tdm_run=a.adsp_tdm_run_cycle,tdm_pcm_graph=a.adsp_tdm_pcm_graph_cycle,audio_dma=a.audio_dma_info,pcm_memory=a.pcm_memory_cycle,pcm_data=(a.audio_pcm_periods if a.audio_pcm_repeat else a.pcm_data_cycle),tdm_pins=a.tdm_pins_cycle,mfc=a.audio_input_format=='ios48-mfc');save()
            if not report['adsp_owned_boot_report']['passed']:raise RuntimeError('owned DSP auth/stop/shutdown/restore closure incomplete')
            if a.qup_core_keepalive:
                qup=module('qup_keepalive_report','tools/marble-i2c-inventory-report.py')
                report['qup_core_keepalive']=qup.inspect_qup_core_hold((a.out/'adsp-owned-boot.log').read_text(errors='replace'));save()
                if not report['qup_core_keepalive']['passed']:raise RuntimeError('minimum QUP2 BCM ownership/acknowledgement incomplete')
            if a.audio_i2c_window:
                i2c=module('audio_i2c_window_report','tools/marble-i2c-inventory-report.py')
                report['audio_i2c_window']=i2c.inspect_audio_window((a.out/'adsp-owned-boot.log').read_text(errors='replace'),amp_profile=a.audio_amp_profile,prestart=a.audio_amp_prestart,preboot=a.audio_amp_preboot,qup_hold=a.audio_qup_hold,runtime_deadline=a.audio_runtime_deadline,digital_pll=a.audio_digital_pll,runtime_600ms=a.audio_runtime_600ms,digital_early_stop=a.audio_digital_early_stop,fast_stop=a.audio_fast_stop,muted_analog=a.audio_muted_analog);save()
                if a.audio_usb_ram_only:
                    policy=module('audio_usb_report','tools/marble-audio-usb-report.py')
                    report['audio_usb_ram_only']=policy.inspect((a.out/'adsp-owned-boot.log').read_text(errors='replace'));save()
                    if not report['audio_usb_ram_only']['passed']:raise RuntimeError('post-auth RAM-only USB policy incomplete')
                if not report['audio_i2c_window']['passed']:raise RuntimeError('audio I2C START window lifecycle incomplete')
            if a.audio_i2c_hold:
                i2c=module('audio_i2c_hold_report','tools/marble-i2c-inventory-report.py')
                report['audio_i2c_hold']=i2c.inspect_audio_hold((a.out/'adsp-owned-boot.log').read_text(errors='replace'));save()
                if not report['audio_i2c_hold']['passed']:raise RuntimeError('combined audio I2C/DSP lifecycle incomplete')
            if a.adsp_tdm_run_cycle:
                tdm=module('tdm_run_report','tools/marble-tdm-run-report.py')
                report['adsp_tdm_run_cycle']=tdm.inspect((a.out/'adsp-owned-boot.log').read_text(errors='replace'),pcm_graph=a.adsp_tdm_pcm_graph_cycle,pcm_memory=a.pcm_memory_cycle,pcm_data=(a.audio_pcm_periods if a.audio_pcm_repeat else a.pcm_data_cycle),tdm_pins=a.tdm_pins_cycle,mfc=a.audio_input_format=='ios48-mfc');save()
                if not report['adsp_tdm_run_cycle']['passed']:raise RuntimeError('TDM run lifecycle incomplete; recovery still required')
                if a.audio_dma_info:
                    dma=module('audio_dma_report','tools/marble-audio-dma-report.py')
                    report['adsp_audio_dma_inventory']=dma.inspect((a.out/'adsp-owned-boot.log').read_text(errors='replace'));save()
                    if a.audio_context_info:
                        ctx=module('audio_context_report','tools/marble-audio-context-report.py')
                        report['adsp_audio_context_inventory']=ctx.inspect((a.out/'adsp-owned-boot.log').read_text(errors='replace'));save()
                        if not report['adsp_audio_context_inventory']['passed']:raise RuntimeError('context snapshot refused; recovery required')
                    if a.audio_map_cycle:
                        mapping=module('audio_map_report','tools/marble-audio-map-report.py')
                        report['adsp_audio_map_cycle']=mapping.inspect((a.out/'adsp-owned-boot.log').read_text(errors='replace'),held=a.pcm_memory_cycle,pcm_data=(a.audio_pcm_periods if a.audio_pcm_repeat else a.pcm_data_cycle));save()
                        if not report['adsp_audio_map_cycle']['passed']:raise RuntimeError('mapping cycle failed; recovery required')
                    if a.pcm_memory_cycle:
                        memory=module('pcm_memory_report','tools/marble-pcm-memory-report.py')
                        report['adsp_pcm_memory_cycle']=memory.inspect((a.out/'adsp-owned-boot.log').read_text(errors='replace'),pcm_data=(a.audio_pcm_periods if a.audio_pcm_repeat else a.pcm_data_cycle));save()
                        if not report['adsp_pcm_memory_cycle']['passed']:raise RuntimeError('DSP MAP/UNMAP cycle failed; recovery required')
                    if a.pcm_data_cycle:
                        data=module('pcm_data_report','tools/marble-pcm-data-report.py')
                        report['adsp_pcm_data_cycle']=data.inspect((a.out/'adsp-owned-boot.log').read_text(errors='replace'),requests=a.audio_pcm_periods if a.audio_pcm_repeat else 1,mfc=a.audio_input_format=='ios48-mfc');save()
                        if not report['adsp_pcm_data_cycle']['passed']:raise RuntimeError('silent PCM consumption failed; recovery required')
                    if a.tdm_pins_cycle:
                        pins=module('audio_pins_report','tools/marble-audio-pins-report.py')
                        report['adsp_tdm_pins_cycle']=pins.inspect((a.out/'adsp-owned-boot.log').read_text(errors='replace'));save()
                        if not report['adsp_tdm_pins_cycle']['passed']:raise RuntimeError('TDM pin lease failed; recovery required')
                    if not report['adsp_audio_dma_inventory']['passed']:raise RuntimeError('passive DMA inventory incomplete; lifecycle recovery still required')
            elif a.adsp_audio_clock_cycle:
                clock=module('audio_clock_report','tools/marble-audio-clock-report.py')
                report['adsp_audio_clock_cycle']=clock.inspect((a.out/'adsp-owned-boot.log').read_text(errors='replace'));save()
                if not report['adsp_audio_clock_cycle']['passed']:raise RuntimeError('audio clock request/release incomplete; recovery still required')
            elif a.adsp_tdm_graph_cycle:
                graph=module('tdm_graph_report','tools/marble-tdm-graph-report.py')
                report['adsp_tdm_graph_cycle']=graph.inspect((a.out/'adsp-owned-boot.log').read_text(errors='replace'),configure=a.adsp_tdm_config_cycle);save()
                if not report['adsp_tdm_graph_cycle']['passed']:raise RuntimeError('TDM graph open/close incomplete; recovery still required')
            elif a.adsp_amdb_load_cycle:
                metadata=module('amdb_cycle_report','tools/marble-amdb-cycle-report.py')
                report['adsp_amdb_load_cycle']=metadata.inspect((a.out/'adsp-owned-boot.log').read_text(errors='replace'),module_id='7001015' if a.amdb_module=='mfc' else '700100e');save()
                if not report['adsp_amdb_load_cycle']['passed']:raise RuntimeError('AMDB library acquire/release incomplete; recovery still required')
            elif a.adsp_amdb_info:
                metadata=module('amdb_report','tools/marble-amdb-report.py')
                report['adsp_amdb_info']=metadata.inspect((a.out/'adsp-owned-boot.log').read_text(errors='replace'),module_id='7001015' if a.amdb_module=='mfc' else '700100e');save()
                if not report['adsp_amdb_info']['passed']:raise RuntimeError('AMDB metadata exchange incomplete; recovery still required')
            elif a.adsp_spf_state:
                gpr=module('gpr_state_report','tools/marble-gpr-state-report.py')
                report['adsp_spf_state']=gpr.inspect((a.out/'adsp-owned-boot.log').read_text(errors='replace'));save()
                if not report['adsp_spf_state']['passed']:raise RuntimeError('read-only SPF exchange incomplete or invalid; lifecycle recovery still required')
            if a.adsp_control_boot:
                report['adsp_control_post_return_preserved']='marble-qmp-wire-preserved: 1' in fb('getvar','marble-qmp-wire-preserved');save()
                if not report['adsp_control_post_return_preserved']:raise RuntimeError('DSP control lifecycle changed QMP context after return')
        if a.adsp_owned_stage:
            if 'package-noexec-run-xo-lpi-bw-v1' not in fb('getvar','marble-adsp-owned-stage'):raise RuntimeError('owned no-exec loader capability mismatch; package not uploaded')
            if a.adsp_scm_held_stage and 'SCM-ROM-loadstate-on-through-stop-v1' not in fb('getvar','marble-adsp-scm-held-stage'):raise RuntimeError('held load_state lifecycle capability mismatch; package not uploaded')
            if a.adsp_scm_ipcc_stage and not a.adsp_scm_held_stage and 'SCM-ROM-IRQ-ack-bounded-stop-v1' not in fb('getvar','marble-adsp-scm-ipcc-stage'):raise RuntimeError('IPCC/bounded shutdown capability mismatch; package not uploaded')
            if a.adsp_scm_bounded_stage and 'SCM-ROM-register-stop-noexec-v1' not in fb('getvar','marble-adsp-scm-bounded-stage'):raise RuntimeError('bounded shutdown capability mismatch; package not uploaded')
            if a.adsp_scm_channel_stage and 'SCM-ROM-AP-channel-noexec-v1' not in fb('getvar','marble-adsp-scm-channel-stage'):raise RuntimeError('SCM/AP channel-only capability mismatch; package not uploaded')
            if a.adsp_scm_wire_stage and 'SCM-ROM-AP-wire-noexec-v1' not in fb('getvar','marble-adsp-scm-wire-stage'):raise RuntimeError('owned SCM/AP wire capability mismatch; package not uploaded')
            if a.adsp_scm_qmp_stage and 'SCM-QMP-preauth-noexec-v1' not in fb('getvar','marble-adsp-scm-qmp-stage'):raise RuntimeError('owned SCM/QMP no-exec capability mismatch; package not uploaded')
            if a.adsp_scm_stage and 'linux-SCM-peak-ROM-noexec-v1' not in fb('getvar','marble-adsp-scm-stage'):raise RuntimeError('owned SCM no-exec capability mismatch; package not uploaded')
            if a.adsp_qmp_stage and 'qmp-ROM-preauth-cancel-v4' not in fb('getvar','marble-adsp-qmp-stage'):raise RuntimeError('owned QMP no-exec capability mismatch; package not uploaded')
            print(fb('stage',a.adsp_package,timeout=40),flush=True)
            report['adsp_owned_stage_requested']=True;save()
            report['adsp_qmp_stage_requested']=a.adsp_qmp_stage or a.adsp_scm_qmp_stage;save()
            report['adsp_scm_stage_requested']=a.adsp_scm_stage or a.adsp_scm_qmp_stage or a.adsp_scm_wire_stage or a.adsp_scm_channel_stage or a.adsp_scm_bounded_stage or a.adsp_scm_ipcc_stage;save()
            report['adsp_scm_wire_stage_requested']=a.adsp_scm_wire_stage;save()
            report['adsp_scm_channel_stage_requested']=a.adsp_scm_channel_stage;save()
            report['adsp_scm_bounded_stage_requested']=a.adsp_scm_bounded_stage;save()
            report['adsp_scm_ipcc_stage_requested']=a.adsp_scm_ipcc_stage and not a.adsp_scm_held_stage;report['adsp_scm_held_stage_requested']=a.adsp_scm_held_stage;save()
            command_error=None
            try:
                print(fb('oem','adsp-scm-held-stage' if a.adsp_scm_held_stage else 'adsp-scm-ipcc-stage' if a.adsp_scm_ipcc_stage else 'adsp-scm-bounded-stage' if a.adsp_scm_bounded_stage else 'adsp-scm-channel-stage' if a.adsp_scm_channel_stage else 'adsp-scm-wire-stage' if a.adsp_scm_wire_stage else 'adsp-scm-qmp-stage' if a.adsp_scm_qmp_stage else 'adsp-scm-stage' if a.adsp_scm_stage else 'adsp-qmp-stage' if a.adsp_qmp_stage else 'adsp-owned-stage',timeout=40),flush=True)
                report['adsp_owned_command_ok']=True
            except Exception as exc:
                command_error=exc;report['adsp_owned_command_error']=str(exc);save();raise
            finally:
                try:
                    fb('oem','log');fb('get_staged',a.out/'adsp-owned-stage.log')
                except Exception as exc:
                    report['adsp_owned_log_error']=str(exc);save()
                    if command_error is None:raise
            audit=module('adsp_owned_report','tools/marble-adsp-owned-report.py')
            report['adsp_owned_report']=audit.inspect((a.out/'adsp-owned-stage.log').read_text(errors='replace'),qmp=a.adsp_qmp_stage or a.adsp_scm_qmp_stage,scm_peak=a.adsp_scm_stage or a.adsp_scm_qmp_stage or a.adsp_scm_wire_stage or a.adsp_scm_channel_stage or a.adsp_scm_bounded_stage or a.adsp_scm_ipcc_stage,wire=a.adsp_scm_wire_stage or a.adsp_scm_channel_stage or a.adsp_scm_bounded_stage or a.adsp_scm_ipcc_stage,wire_messages=not a.adsp_scm_channel_stage,raw_shutdown=a.adsp_scm_bounded_stage or a.adsp_scm_ipcc_stage,ipcc_ack=a.adsp_scm_ipcc_stage and not a.adsp_scm_held_stage,held=a.adsp_scm_held_stage);save()
            if not report['adsp_owned_report']['passed']:raise RuntimeError('dependency-held ROM no-exec closure incomplete')
            if a.adsp_scm_wire_stage or a.adsp_scm_channel_stage or a.adsp_scm_bounded_stage or a.adsp_scm_ipcc_stage:
                report['adsp_scm_wire_post_return_preserved']='marble-qmp-wire-preserved: 1' in fb('getvar','marble-qmp-wire-preserved');save()
                if not report['adsp_scm_wire_post_return_preserved']:raise RuntimeError('AP wire firmware context changed after ROM return')
        if a.adsp_dependency_cycle:
            report['adsp_dependency_cycle_requested']=True;save()
            try:
                print(fb('oem','adsp-dependency-cycle'),flush=True)
                report['adsp_dependency_command_ok']=True
            finally:
                fb('oem','log');fb('get_staged',a.out/'adsp-dependency-cycle.log')
            audit=module('adsp_dependency_report','tools/marble-adsp-dependency-report.py')
            report['adsp_dependency_report']=audit.inspect((a.out/'adsp-dependency-cycle.log').read_text(errors='replace'));save()
            if not report['adsp_dependency_report']['passed']:raise RuntimeError('native ICB RUN transition incomplete')
        if a.icb_run_bandwidth_cycle:
            if a.icb_run_scm_bandwidth_cycle and 'linux-U32max-kBps-owned10ms-reset-v1' not in fb('getvar','marble-icb-scm-peak'):
                raise RuntimeError('SCM peak native capability missing; no bandwidth request issued')
            report['icb_run_bandwidth_cycle_requested']=True;save()
            report['icb_run_scm_bandwidth_requested']=a.icb_run_scm_bandwidth_cycle;save()
            try:
                print(fb('oem','icb-run-scm-bandwidth-cycle' if a.icb_run_scm_bandwidth_cycle else 'icb-run-bandwidth-cycle'),flush=True)
                report['icb_run_bandwidth_command_ok']=True
            finally:
                fb('oem','log');fb('get_staged',a.out/'icb-run-bandwidth-cycle.log')
            audit=module('icb_run_bandwidth_report','tools/marble-icb-run-bandwidth-report.py')
            report['icb_run_bandwidth_report']=audit.inspect((a.out/'icb-run-bandwidth-cycle.log').read_text(errors='replace'),a.icb_run_scm_bandwidth_cycle);save()
            if not report['icb_run_bandwidth_report']['passed']:raise RuntimeError('native ICB RUN transition incomplete')
        if a.icb_run_cycle:
            report['icb_run_cycle_requested']=True;save()
            try:
                print(fb('oem','icb-run-cycle'),flush=True)
                report['icb_run_command_ok']=True
            finally:
                fb('oem','log');fb('get_staged',a.out/'icb-run-cycle.log')
            audit=module('icb_run_report','tools/marble-icb-run-report.py')
            report['icb_run_report']=audit.inspect((a.out/'icb-run-cycle.log').read_text(errors='replace'));save()
            if not report['icb_run_report']['passed']:raise RuntimeError('native ICB RUN transition incomplete')
        if a.icb_bandwidth_cycle:
            report['icb_bandwidth_cycle_requested']=True;save()
            try:
                print(fb('oem','icb-bandwidth-cycle'),flush=True)
                report['icb_bandwidth_command_ok']=True
            finally:
                fb('oem','log');fb('get_staged',a.out/'icb-bandwidth-cycle.log')
            audit=module('icb_bandwidth_report','tools/marble-icb-bandwidth-report.py')
            report['icb_bandwidth_report']=audit.inspect((a.out/'icb-bandwidth-cycle.log').read_text(errors='replace'));save()
            if not report['icb_bandwidth_report']['passed']:raise RuntimeError('native ICB bandwidth closure incomplete')
        if a.icb_zero_cycle:
            report['icb_zero_cycle_requested']=True;save()
            try:
                print(fb('oem','icb-zero-cycle'),flush=True)
                report['icb_zero_command_ok']=True
            finally:
                fb('oem','log');fb('get_staged',a.out/'icb-zero-cycle.log')
            audit=module('icb_zero_report','tools/marble-icb-zero-report.py')
            report['icb_zero_report']=audit.inspect((a.out/'icb-zero-cycle.log').read_text(errors='replace'));save()
            if not report['icb_zero_report']['passed']:raise RuntimeError('native ICB zero-request closure incomplete')
        if a.adsp_dependency_info:
            report['adsp_dependency_inventory_requested']=True;save()
            try:
                print(fb('oem','adsp-dependency-info'),flush=True)
                report['adsp_dependency_command_ok']=True
            finally:
                fb('oem','log');fb('get_staged',a.out/'adsp-dependency-info.log')
            audit=module('dependency_inventory','tools/marble-power-report.py')
            report['adsp_dependency_inventory']=audit.inspect_dependencies((a.out/'adsp-dependency-info.log').read_text(errors='replace'));save()
            if not report['adsp_dependency_inventory']['passed']:raise RuntimeError('native dependency inventory incomplete')
        if a.adsp_icb_info:
            report['adsp_icb_inventory_requested']=True;save()
            try:
                print(fb('oem','adsp-icb-info'),flush=True)
                report['adsp_icb_command_ok']=True
            finally:
                fb('oem','log');fb('get_staged',a.out/'adsp-icb-info.log')
            audit=module('icb_inventory','tools/marble-power-report.py')
            report['adsp_icb_inventory']=audit.inspect_icb((a.out/'adsp-icb-info.log').read_text(errors='replace'));save()
            if not report['adsp_icb_inventory']['passed']:raise RuntimeError('native ICB inventory incomplete')
        if a.adsp_xo_info:
            report['adsp_xo_inventory_requested']=True;save()
            try:
                print(fb('oem','adsp-xo-info'),flush=True)
                report['adsp_xo_command_ok']=True
            finally:
                fb('oem','log');fb('get_staged',a.out/'adsp-xo-info.log')
            audit=module('xo_inventory','tools/marble-power-report.py')
            report['adsp_xo_inventory']=audit.inspect_xo((a.out/'adsp-xo-info.log').read_text(errors='replace'));save()
            if not report['adsp_xo_inventory']['passed']:raise RuntimeError('XO command DB qualification incomplete')
        if a.qmp_wire_cycle:
            if 'linux-owned-channel64-ack-wire-v2' not in fb('getvar','marble-qmp-wire'):raise RuntimeError('QMP wire capability missing; no AP writes issued')
            if a.qmp_tpl_cycle and 'native-TPL8-roundtrip-noSMC-v1' not in fb('getvar','marble-qmp-tpl'):raise RuntimeError('QMP/TPL capability missing; no AP writes issued')
            if a.qmp_ipcc_cycle and 'AP-own-QMP-receive-clear-v1' not in fb('getvar','marble-qmp-ipcc'):raise RuntimeError('QMP/IPCC capability missing; no interrupt acknowledgement issued')
            if a.qmp_smccc_cycle and 'bounded-register-query-noPAS-v1' not in fb('getvar','marble-qmp-smccc'):raise RuntimeError('QMP/SMCCC capability missing; no raw calls issued')
            report['qmp_wire_cycle_requested']=True;save();command_error=None
            report['qmp_tpl_cycle_requested']=a.qmp_tpl_cycle;save()
            report['qmp_smccc_cycle_requested']=a.qmp_smccc_cycle;save()
            report['qmp_ipcc_cycle_requested']=a.qmp_ipcc_cycle;save()
            try:
                print(fb('oem','qmp-ipcc-cycle' if a.qmp_ipcc_cycle else 'qmp-smccc-cycle' if a.qmp_smccc_cycle else 'qmp-tpl-cycle' if a.qmp_tpl_cycle else 'qmp-wire-cycle'),flush=True)
                report['qmp_wire_command_ok']=True
            except Exception as exc:
                command_error=exc;report['qmp_wire_command_error']=str(exc);save();raise
            finally:
                try:
                    fb('oem','log');fb('get_staged',a.out/'qmp-wire-cycle.log')
                except Exception as exc:
                    report['qmp_wire_log_error']=str(exc);save()
                    if command_error is None:raise
            audit=module('qmp_wire_report','tools/marble-qmp-wire-report.py')
            report['qmp_wire_report']=audit.inspect((a.out/'qmp-wire-cycle.log').read_text(errors='replace'),tpl=a.qmp_tpl_cycle,smccc=a.qmp_smccc_cycle,ipcc=a.qmp_ipcc_cycle);save()
            report['qmp_wire_post_return_preserved']='marble-qmp-wire-preserved: 1' in fb('getvar','marble-qmp-wire-preserved');save()
            if not report['qmp_wire_report']['passed'] or not report['qmp_wire_post_return_preserved']:raise RuntimeError('QMP wire firmware preservation not verified')
        if a.qmp_native_cycle:
            if a.qmp_mailbox_load_state_cycle and 'transport64-on-off-ack-close-reset-v1' not in fb('getvar','marble-qmp-mailbox-load-state'):
                raise RuntimeError('QMP business TX capability missing; no TX issued')
            report['qmp_native_cycle_requested']=True;save()
            report['qmp_mailbox_load_state_cycle_requested']=a.qmp_mailbox_load_state_cycle;save()
            report['qmp_native_event_dispatch_requested']=a.qmp_native_pump_cycle;save()
            report['qmp_native_peer_close_contract_requested']=a.qmp_native_peer_close;save()
            report['qmp_native_settled_close_contract_requested']=a.qmp_native_settled_close;save()
            try:
                print(fb('oem','qmp-mailbox-load-state-cycle' if a.qmp_mailbox_load_state_cycle else 'qmp-native-settled-close' if a.qmp_native_settled_close else 'qmp-native-peer-close' if a.qmp_native_peer_close else 'qmp-native-pump-cycle' if a.qmp_native_pump_cycle else 'qmp-native-cycle'),flush=True)
                report['qmp_native_command_ok']=True
            except Exception as exc:
                report['qmp_native_command_error']=str(exc);save()
            finally:
                fb('oem','log');fb('get_staged',a.out/'qmp-native-cycle.log')
            audit=module('qmp_native_report','tools/marble-qmp-native-report.py')
            report['qmp_native_report']=audit.inspect((a.out/'qmp-native-cycle.log').read_text(errors='replace'),a.qmp_native_peer_close,a.qmp_native_settled_close,mailbox_load_state=a.qmp_mailbox_load_state_cycle);save()
            if not report['qmp_native_report']['passed']:raise RuntimeError('native QMP lifecycle not verified; see evidence')
        if a.i2c_inventory:
            if 'Mu-I2C-table-code-readonly-v1' not in fb('getvar','marble-i2c-inventory'):raise RuntimeError('I2C inventory capability mismatch')
            if a.i2c_dt_info and 'Mu-DT-provider-table-only-v1' not in fb('getvar','marble-i2c-dt-info'):raise RuntimeError('I2C DT inventory capability mismatch')
            if a.i2c_config_snapshot and 'RAM-DT-41865-capture-only-v2' not in fb('getvar','marble-i2c-config-snapshot'):raise RuntimeError('I2C ROM snapshot capability mismatch')
            if a.i2c_clock_info and 'QUP2-five-clock-ID-readonly-v1' not in fb('getvar','marble-i2c-clock-info'):raise RuntimeError('I2C clock lookup capability mismatch')
            if a.i2c_clock_cycle and 'QUP2-five-clock-balanced-1ms-v3' not in fb('getvar','marble-i2c-clock-cycle'):raise RuntimeError('I2C clock cycle capability mismatch')
            if a.i2c_register_info and 'QUP2-eight-passive-RO-twice-v1' not in fb('getvar','marble-i2c-register-info'):raise RuntimeError('I2C register inventory capability mismatch')
            if a.i2c_route_info and 'QUP2-41-route-words-passive-v1' not in fb('getvar','marble-i2c-route-info'):raise RuntimeError('I2C route inventory capability mismatch')
            if a.i2c_setup_cycle and 'QUP2-100kHz-config-restore-no-transaction-v1' not in fb('getvar','marble-i2c-setup-cycle'):raise RuntimeError('I2C setup capability mismatch')
            if a.amp_profile and 'AW2032-ROM-Music-OTP-down-muted-readback-reset-v1' not in fb('getvar','marble-i2c-amp-profile'):raise RuntimeError('AW2032 profile capability mismatch')
            if a.amp_quiet and 'AW2032-90dB-HMUTE-write-readback-reset-v1' not in fb('getvar','marble-i2c-amp-quiet'):raise RuntimeError('AW2032 quiet capability mismatch')
            if a.amp_registers and 'AW2032-reset-readonly-registers-v1' not in fb('getvar','marble-i2c-amp-registers'):raise RuntimeError('AW2032 register capability mismatch')
            if a.i2c_amp_id and 'QUP2-two-AW2032-ID-only-bounded-v5' not in fb('getvar','marble-i2c-amp-id'):raise RuntimeError('I2C amplifier ID capability mismatch')
            report['i2c_inventory_requested']=True;report['i2c_DT_inventory_requested']=a.i2c_dt_info;report['i2c_config_snapshot_requested']=a.i2c_config_snapshot;report['i2c_clock_info_requested']=a.i2c_clock_info;save()
            try:
                report['i2c_clock_cycle_requested']=a.i2c_clock_cycle;save()
                report['i2c_register_info_requested']=a.i2c_register_info;save()
                report['i2c_route_info_requested']=a.i2c_route_info;save()
                report['i2c_setup_cycle_requested']=a.i2c_setup_cycle;save()
                report['i2c_amp_id_requested']=a.i2c_amp_id;report['amp_registers_requested']=a.amp_registers;report['amp_quiet_requested']=a.amp_quiet;report['amp_profile_requested']=a.amp_profile;save()
                print(fb('oem','i2c-amp-profile' if a.amp_profile else 'i2c-amp-quiet' if a.amp_quiet else 'i2c-amp-registers' if a.amp_registers else 'i2c-amp-id' if a.i2c_amp_id else 'i2c-setup-cycle' if a.i2c_setup_cycle else 'i2c-route-info' if a.i2c_route_info else 'i2c-register-info' if a.i2c_register_info else 'i2c-clock-cycle' if a.i2c_clock_cycle else 'i2c-clock-info' if a.i2c_clock_info else 'i2c-config-snapshot' if a.i2c_config_snapshot else 'i2c-dt-info' if a.i2c_dt_info else 'i2c-inventory'),flush=True)
            except Exception as exc:
                report['i2c_command_error']=str(exc);save()
            finally:
                fb('oem','log');fb('get_staged',a.out/'i2c-inventory.log')
            audit=module('i2c_inventory_report','tools/marble-i2c-inventory-report.py')
            report['i2c_inventory']=audit.inspect((a.out/'i2c-inventory.log').read_text(errors='replace'),dt=a.i2c_dt_info,config=a.i2c_config_snapshot,clock=a.i2c_clock_info,cycle=a.i2c_clock_cycle,registers=a.i2c_register_info,route=a.i2c_route_info,setup=a.i2c_setup_cycle,amp=a.i2c_amp_id,amp_registers=a.amp_registers,amp_quiet=a.amp_quiet,amp_profile=a.amp_profile);save()
            if report.get('i2c_command_error') or not report['i2c_inventory']['passed']:raise RuntimeError('I2C protocol/code inventory not qualified')
            if a.i2c_config_snapshot:
                fb('oem','i2c-config-blob');fb('get_staged',a.out/'runtime-uefi-config.dtb')
                raw=(a.out/'runtime-uefi-config.dtb').read_bytes()
                if len(raw)!=41865:raise RuntimeError('runtime DT snapshot length mismatch')
                raw_same=hashlib.sha256(raw).hexdigest()==audit.CONFIG_SHA256
                if raw_same!=report['i2c_inventory']['raw_ROM_blob_match']:raise RuntimeError('runtime DT hash evidence disagrees')
                parser=module('runtime_fdt_capture','tools/marble-fdt.py')
                current=parser.parse(raw);reference=parser.parse(a.i2c_config_reference.read_bytes())
                diff=[dict(node=node,property=key) for node in sorted(set(current)|set(reference)) for key in sorted(set(current.get(node,{}))|set(reference.get(node,{}))) if current.get(node,{}).get(key)!=reference.get(node,{}).get(key)]
                (a.out/'runtime-DT-differences.json').write_text(json.dumps(diff,indent=2)+'\n')
                report['i2c_runtime_config_ROM_match']=raw_same
                report['i2c_runtime_config_semantic_ROM_match']=current==reference
                report['i2c_runtime_config_node_count']=len(current)
                report['i2c_runtime_config_changed_properties']=len(diff)
                report['i2c_runtime_config_capture_passed']=True;save()
        if a.glink_smem_info:
            if 'preallocated-lpass-GET-only-v1' not in fb('getvar','marble-glink-smem-info'):raise RuntimeError('native GLink inventory capability missing; no GET called')
            report['glink_smem_inventory_requested']=True;save()
            try:
                print(fb('oem','glink-smem-info'),flush=True)
                report['glink_smem_inventory_command_ok']=True
            except Exception as exc:
                report['glink_smem_inventory_command_error']=str(exc);save()
            finally:
                fb('oem','log');fb('get_staged',a.out/'glink-smem-info.log')
            audit=module('glink_smem_inventory','tools/marble-glink-smem-report.py')
            report['glink_smem_inventory']=audit.inspect((a.out/'glink-smem-info.log').read_text(errors='replace'));save()
            if not report.get('glink_smem_inventory_command_ok') or not report['glink_smem_inventory']['passed']:raise RuntimeError('native GLink SMEM inventory not qualified')
        if a.adsp_power_info:
            report['adsp_power_inventory_requested']=True;save()
            try:
                print(fb('oem','adsp-power-info'),flush=True)
                report['adsp_power_inventory_command_ok']=True
            finally:
                fb('oem','log');fb('get_staged',a.out/'adsp-power-info.log')
            audit=module('power_inventory','tools/marble-power-report.py')
            report['adsp_power_inventory']=audit.inspect((a.out/'adsp-power-info.log').read_text(errors='replace'));save()
            if not report['adsp_power_inventory']['passed']:raise RuntimeError('power inventory evidence incomplete')
        if a.daily_probe:
            print(fb('oem','daily-info'),flush=True)
            fb('oem','log');fb('get_staged',a.out/'daily-info.log')
        if a.scm_info:
            try:
                print(fb('oem','scm-info'),flush=True)
                report['scm_inventory_command_ok']=True
            finally:
                fb('oem','log');fb('get_staged',a.out/'scm-info.log')
        if a.scm_availability:
            try:
                print(fb('oem','scm-availability',timeout=40),flush=True)
                report['scm_availability_command_ok']=True
            finally:
                fb('oem','log');fb('get_staged',a.out/'scm-availability.log')
        if a.pil_memory:
            try:
                print(fb('oem','pil-adsp-memory'),flush=True)
                report['pil_memory_command_ok']=True
            finally:
                fb('oem','log');fb('get_staged',a.out/'pil-memory.log')
        if a.display_memory_info:
            try:
                print(fb('oem','display-memory-info',timeout=40),flush=True)
                report['display_memory_info_command_ok']=True
            finally:
                fb('oem','log');fb('get_staged',a.out/'display-memory-info.log')
        if a.npa_vcs_info:
            try:
                print(fb('oem','npa-vcs-info'),flush=True)
                report['npa_vcs_inventory_command_ok']=True
            finally:
                fb('oem','log');fb('get_staged',a.out/'npa-vcs-info.log')
        if a.npa_clients:
            if a.npa_native_release and 'native-off-reset-v1' not in fb('getvar','marble-npa-release'):
                raise RuntimeError('native release/reset loader capability missing; no request issued')
            report['npa_client_cycle_requested']=True;save()
            report['npa_vote_cycle_requested']=a.npa_votes or a.npa_native_release
            report['npa_native_release_requested']=a.npa_native_release;save()
            try:
                print(fb('oem','npa-native-release' if a.npa_native_release else 'npa-votes' if a.npa_votes else 'npa-clients',timeout=40),flush=True)
                report['npa_client_cycle_command_ok']=True
            finally:
                fb('oem','log');fb('get_staged',a.out/'npa-clients.log')
            text=(a.out/'npa-clients.log').read_text(errors='replace')
            report['npa_lifecycle_report']=npa.inspect(text,2 if a.npa_votes or a.npa_native_release else 0)
            report['npa_client_cycle_accepted']=report['npa_lifecycle_report']['passed']
            if a.npa_native_release:
                report['npa_native_release_report']=npa.inspect_native_release(text);save()
                if not report['npa_native_release_report']['passed']:raise RuntimeError('native NPA release/reset evidence incomplete; retain recovery watchdog')
            elif not report['npa_client_cycle_accepted']:raise RuntimeError('NPA command without complete client lifecycle evidence')
            save()
        if a.npa_resources:
            try:
                print(fb('oem','npa-resources'),flush=True)
                report['npa_resource_inventory_command_ok']=True
            finally:
                fb('oem','log');fb('get_staged',a.out/'npa-resources.log')
        if a.npa_info:
            try:
                print(fb('oem','npa-info'),flush=True)
                report['npa_inventory_command_ok']=True
            finally:
                fb('oem','log');fb('get_staged',a.out/'npa-info.log')
        if a.pil_proxy_config:
            try:
                print(fb('oem','pil-proxy-config',timeout=40),flush=True)
                report['pil_proxy_config_command_ok']=True
            finally:
                fb('oem','log');fb('get_staged',a.out/'pil-proxy-config.log')
        if a.pil_driver:
            report['pil_driver']={'path':str(a.pil_driver.resolve()),'sha256':hashlib.sha256(a.pil_driver.read_bytes()).hexdigest()};save()
            try:
                print(fb('stage',a.pil_driver),flush=True)
                print(fb('oem','pil-install',timeout=40),flush=True)
                report['pil_install_command_ok']=True
            finally:
                fb('oem','log');fb('get_staged',a.out/'pil-install.log')
            if a.pil_config:
                try:
                    print(fb('oem','pil-config',timeout=40),flush=True)
                    report['pil_config_command_ok']=True
                finally:
                    fb('oem','log');fb('get_staged',a.out/'pil-config.log')
            if a.pil_files:
                probe_error=None
                try:
                    print(fb('stage',a.pil_files,timeout=40),flush=True)
                    if a.pil_powered_stage:
                        if 'held-load-stop-restore-reset-v1' not in fb('getvar','marble-pil-powered'):raise RuntimeError('powered-stage capability mismatch; no requests issued')
                        if a.pil_smp2p_stage and 'smp2p-notify-powered-noexec-reset-v1' not in fb('getvar','marble-pil-smp2p-stage'):raise RuntimeError('SMP2P-powered-stage capability mismatch')
                        if a.pil_smp2p_boot and 'ready-stopack-restore-reset-v1' not in fb('getvar','marble-pil-smp2p-boot'):raise RuntimeError('SMP2P-boot capability mismatch')
                        if a.pil_bounded_boot and 'pas-auth500-stop-restore-reset-v1' not in fb('getvar','marble-pil-boot'):raise RuntimeError('bounded-boot capability mismatch; no requests issued')
                        report.update(npa_client_cycle_requested=True,npa_vote_cycle_requested=True,npa_native_release_requested=True,pil_powered_stage_requested=True);save()
                        report['pil_bounded_boot_requested']=a.pil_bounded_boot;save()
                        report['pil_smp2p_stage_requested']=a.pil_smp2p_stage;save()
                        report['pil_smp2p_boot_requested']=a.pil_smp2p_boot;save()
                    command=('pil-smp2p-boot' if a.pil_smp2p_boot else 'pil-smp2p-stage' if a.pil_smp2p_stage else 'pil-bounded-boot' if a.pil_bounded_boot else 'pil-powered-stage' if a.pil_powered_stage else 'pil-all-segments' if a.pil_all_segments else 'pil-first-segment' if a.pil_first_segment else 'pil-pas-memory' if a.pil_pas_memory else
                             'pil-pas-metadata' if a.pil_pas_metadata else 'pil-elf-probe' if a.pil_elf_only else
                             'pil-header-probe' if a.pil_header_only else 'pil-adsp-cycle' if a.pil_adsp_cycle else 'pil-file-probe')
                    print(fb('oem',command,timeout=40),flush=True)
                    report['pil_file_probe_command_ok']=True
                    report['pil_adsp_cycle_command_ok']=a.pil_adsp_cycle
                    report['pil_header_only_command_ok']=a.pil_header_only
                    report['pil_elf_only_command_ok']=a.pil_elf_only
                    report['pil_pas_metadata_command_ok']=a.pil_pas_metadata
                    report['pil_pas_memory_command_ok']=a.pil_pas_memory
                    report['pil_first_segment_command_ok']=a.pil_first_segment
                    report['pil_all_segments_command_ok']=a.pil_all_segments
                except Exception as exc:
                    probe_error=exc;report['pil_file_probe_error']=str(exc);save();raise
                finally:
                    try:
                        fb('oem','log');fb('get_staged',a.out/'pil-file-probe.log')
                    except Exception as exc:
                        report['pil_file_probe_log_error']=str(exc);save()
                        if probe_error is None:raise
                if a.pil_powered_stage:
                    text=(a.out/'pil-file-probe.log').read_text(errors='replace')
                    report['npa_native_release_report']=inspect_held(text)
                    report['npa_client_cycle_accepted']=npa.inspect(text,2)['passed'];save()
                    if not report['npa_native_release_report']['passed']:raise RuntimeError('powered stop/restore/release evidence incomplete; retain recovery watchdog')
        if a.adc_channels or a.adc_battery:
            adc_command='adc-battery' if a.adc_battery else 'adc-channels'
            try:
                print(fb('oem',adc_command),flush=True)
                report[adc_command.replace('-','_')+'_command_ok']=True
            finally:
                fb('oem','log');fb('get_staged',a.out/(adc_command+'.log'))
        if a.dsi_info:
            try:
                print(fb('oem','dsi-info'),flush=True)
                report['dsi_info_command_ok']=True
            finally:
                fb('oem','log');fb('get_staged',a.out/'dsi-info.log')
        if a.dsi_read_power or a.dsi_read_identity or a.dsi_dim_probe:
            dsi_command='dsi-dim-probe' if a.dsi_dim_probe else ('dsi-read-identity' if a.dsi_read_identity else 'dsi-read-power')
            try:
                print(fb('oem',dsi_command),flush=True)
                report['dsi_read_power_command_ok']=True
                report[dsi_command.replace('-','_')+'_command_ok']=True
            finally:
                fb('oem','log');fb('get_staged',a.out/(dsi_command+'.log'))
        if a.buttons_probe:
            print(fb('oem','buttons-probe'),flush=True)
            fb('oem','log');fb('get_staged',a.out/'buttons-probe.log')
        if a.glink_probe:
            try:
                print(fb('oem','glink-probe'),flush=True)
                report['glink_probe_command_ok']=True
            finally:
                fb('oem','log');fb('get_staged',a.out/'glink-probe.log')
        if a.smem_info or a.smem_channels:
            try:
                print(fb('oem','smem-channels' if a.smem_channels else 'smem-info'),flush=True)
                report['smem_info_command_ok']=True
            except Exception as exc:
                report['smem_info_error']=str(exc);save();raise
            finally:
                # Preserve the command error even if the device disappears
                # before the separate log transfer can finish.
                try:
                    fb('oem','log');fb('get_staged',a.out/'smem-info.log')
                except Exception as exc:
                    report['smem_info_log_error']=str(exc);save();raise
        if a.discovery_only:
            report['discovery_complete']=True;save();raise DiscoveryComplete
        report['system_disk']={'path':str(a.system_disk.resolve()),'sha256':hashlib.sha256(a.system_disk.read_bytes()).hexdigest()};save()
        print(run('python','tools/marble-run.py','ramdisk',a.system_disk,timeout=300),flush=True)
        command=['python','tools/marble-run.py','run','--kernel',a.kernel,
            '--ramdisk',a.ramdisk,'--seconds',str(budget),'--cpus','4',
            '--fb-threads',str(a.fb_threads),'--render-scale',str(a.render_scale),'--input-mailbox','--cpu-cap','--efi-idle','--gpu-preview',
            '--battery-preflight',a.out/'battery-before.json','--gpu-probe','17','--gpu-runtime',
            '--gpu-composition','--display-pacing',a.pacing,'--detach']
        if a.daily_buttons:command+=['--daily-buttons']
        if a.brightness_backend:command+=['--brightness-backend']
        if a.host_clock:command+=['--host-clock']
        if a.manual_ten_minute:command+=['--manual-ten-minute']
        if a.exercise and not a.quiet_ui:command+=['--diagnostics']
        if a.record_seconds:command+=['--record-seconds',a.record_seconds,'--record-start',a.record_start,
                                     '--record-mb',a.record_mb,'--record-step',a.record_step]
        report['command']=list(map(str,command));save()
        # Include launch/initialization time in the host budget: the firmware
        # deadline already runs while the detached command is still returning.
        start=time.monotonic()
        print(run(*command,timeout=35),flush=True)
        fresh=time.monotonic();stamp=None;captured=False
        report['started']=True;save()
        while time.monotonic()-start<budget+6:
            state=fb('getvar','result')
            if 'result: running' not in state:break
            fb('oem','log')
            dest=logs/f'{len(list(logs.glob("*.log"))):03}.log'
            fb('get_staged',dest)
            text=dest.read_bytes().split(b'\0PMOC')[0].decode(errors='replace')
            rows=re.findall(r'marble-thermal: valid=(\d+) sensors=(\d+) maximum_mc=(-?\d+).*limit_mc=55000 ok=(\d+) at_us=(\d+)',text)
            if rows:
                valid,sensors,temp,ok,latest=map(int,rows[-1])
                if latest!=stamp:fresh=time.monotonic();stamp=latest
                report['latest_die_mc']=temp
                report['observed_peak_mc']=max(report.get('observed_peak_mc',-40000),*(int(r[2]) for r in rows))
                if valid!=1 or sensors!=19 or ok!=1 or temp>=55000:
                    report['failure']='live temperature stop';save();break
            if time.monotonic()-fresh>5:
                report['failure']='fresh temperature samples unavailable';save();break
            if any(s in text for s in ('marble-thermal: STOP requested','panic(cpu ',
                'EARLY KERNEL EXCEPTION','completion timeout','marble-cpucap: runtime request changed')):
                report['failure']='runtime error or duration stop';save();break
            report['gpu_ready']='MGPU-MARBLE-RUNTIME-v1 ready=1 service=1' in text or report.get('gpu_ready',False)
            report['hardware_renderer']=bool(re.search(r'redwood-gl: .*CoreAnimation.*; FD725;',text)) or report.get('hardware_renderer',False)
            report['hardware_timing_ready']=('marble-display: MDISPLAY-MARBLE-COUNTER-v2 ready=1' in text or
                                             'marble-display: runtime ready=1' in text or
                                             report.get('hardware_timing_ready',False))
            if report['hardware_renderer'] and not report.get('announced'):
                print(f'GPU renderer initialized; visible desktop still requires capture verification; automatic stop at {budget} seconds',flush=True)
                report['announced']=True
            if a.scene.startswith('native-') and report['hardware_renderer']:
                marks=re.findall(r'rwtouch: mark=([\w-]+) at_us=(\d+)',text)
                report['native_marks']=list(dict.fromkeys(report.get('native_marks',[])+[mark for mark,_ in marks]))
                for mark,_ in marks:
                    if mark.endswith('-end') and mark not in native_captured and time.monotonic()-start<78:
                        print(run('python','tools/marble-ui-regression.py','capture',mark,
                                  '--out',a.out/'ui','--settle','0',timeout=12),flush=True)
                        native_captured.add(mark)
                report['native_captured']=sorted(native_captured)
                report['native_sequence_complete']=('complete' in report['native_marks'])
            if (a.exercise and not a.scene.startswith('native-') and report['hardware_renderer'] and 12<=time.monotonic()-start<78 and
                    time.monotonic()>=action_at):
                if not ui_ready:
                    from PIL import Image,ImageChops,ImageStat
                    ui_action('capture','ready',settle=0)
                    current=Image.open(a.out/'ui/ready.png').convert('RGB')
                    reference=Image.open(ROOT/'out/ui-gmu-hfi/ready2.png').convert('RGB')
                    # Retain the broad historical delta as evidence, but do
                    # not let wallpaper/widgets postpone the input sequence.
                    box=(50,750,1010,1750)
                    difference=ImageStat.Stat(ImageChops.difference(current.crop(box),reference.crop(box)))
                    delta=sum(difference.mean)/3
                    gate=settings_target(current,Image.open(a.settings_template).convert('RGB')) if a.scene=='locale' or a.desktop_template_gate else layout_gate(current,reference)
                    if a.scene=='locale' and gate['passed']:
                        actions[0]=('tap','locale-settings',tuple(gate['coords']))
                    ui_ready=gate['passed'];report['ui_ready_delta']=delta
                    report['ui_layout_gate']=gate
                    if ui_ready:
                        current.save(a.out/'ui/desktop-confirmed.png')
                        print('Desktop glyph verified; starting bounded synthetic gestures',flush=True)
                    action_at=time.monotonic()+action_gap
                elif action_index<len(actions):
                    kind,name,coords=actions[action_index]
                    if name=='settings-after-control':
                        target=settings_target(Image.open(a.out/'ui/control-close.png').convert('RGB'),Image.open(a.settings_template).convert('RGB'))
                        if not target['passed']:raise RuntimeError('no confirmed desktop before Settings tap; no blind control-module input')
                        coords=tuple(target['coords']);report['settings_after_control_target']=target
                    print(ui_action(kind,name,coords),flush=True)
                    if a.desktop_template_gate and a.scene in ('panel-focus','control-focus','control-home') and name in ('notifications-close','control-close','control-backdrop','settings-home'):
                        visible=settings_target(Image.open(a.out/'ui'/(name+'.png')).convert('RGB'),Image.open(a.settings_template).convert('RGB'))
                        report.setdefault('panel_return_visual',{})[name]=visible
                    action_index+=1;action_at=time.monotonic()+action_gap
                    report['ui_actions_sent']=action_index
                    report['ui_actions_complete']=action_index==len(actions)
                    report['ui_sequence_complete']=report['ui_actions_complete']
                else:report['ui_actions_complete']=True;report['ui_sequence_complete']=True
                if a.desktop_template_gate and a.scene in ('panel-focus','control-focus','control-home'):
                    returns=report.get('panel_return_visual',{})
                    report['ui_sequence_complete']=report.get('ui_actions_complete',False) and len(returns)==2 and all(x['passed'] for x in returns.values())
            if (a.daily_buttons and not a.manual_ten_minute and a.scene not in ('power','native-power') and
                    (report.get('ui_sequence_complete') or report.get('native_sequence_complete') or
                     time.monotonic()-start>=(78 if a.scene.startswith('native-') or a.scene=='control-home' else 65)) and
                    not report.get('idle_blackout_requested') and time.monotonic()-start<82):
                print(ui_action('key','idle-blackout',(1,),settle=0),flush=True)
                report['idle_blackout_requested']=True
                from PIL import Image
                report['idle_blackout_verified']=not bool(Image.open(a.out/'ui/idle-blackout.png').convert('RGB').getbbox())
                deadline=time.monotonic()+8
                while not report['idle_blackout_verified'] and time.monotonic()<deadline:
                    time.sleep(.25)
                    ui_action('capture','idle-blackout-pending',settle=0)
                    report['idle_blackout_verified']=not bool(Image.open(a.out/'ui/idle-blackout-pending.png').convert('RGB').getbbox())
                save()
                if not report['idle_blackout_verified']:
                    raise RuntimeError('idle blackout capture remains visible')
                report['exercise_finished_early']=True
                break
            save()
            if not a.manual_ten_minute and not captured and time.monotonic()-start>=40:
                print(run('python','tools/marble-ui-regression.py','capture','preview','--out',a.out,'--settle','0',timeout=15),flush=True)
                captured=True
            print(f'preview {time.monotonic()-start:.1f}s die={report.get("latest_die_mc")} GPU={report.get("hardware_renderer")}',flush=True)
            time.sleep(1)
    except DiscoveryComplete:
        pass
    except Exception as exc:
        report['error']=str(exc);save();print('Preview error:',exc,flush=True)
    finally:
        devices=run(MARBLE_FASTBOOT,'devices')
        if 'xnuloader' not in devices:
            # An early reset can disappear from both USB personalities while
            # Android boots. Observe the transition before declaring it lost;
            # never invent a successful resource release after such a reset.
            deadline=time.monotonic()+45
            while MARBLE_ANDROID_SERIAL not in run(MARBLE_ADB,'devices') and time.monotonic()<deadline:
                time.sleep(1)
                devices=run(MARBLE_FASTBOOT,'devices')
                if 'xnuloader' in devices:break
        if 'xnuloader' in devices:
            state=fb('getvar','result')
            if 'result: running' in state:
                fb('oem','stop');deadline=time.monotonic()+35
                while time.monotonic()<deadline:
                    state=fb('getvar','result')
                    if 'result: running' not in state:break
                    time.sleep(1)
            report['loader_state']=state;save()
            if 'result: returned ' in state:
                fb('oem','log') # restore composed log staging after the last UI screenshot
                fb('get_staged',a.out/'returned.log')
                text=(a.out/'returned.log').read_bytes().split(b'\0PMOC')[0].decode(errors='replace')
                checks={
                    'gpu_release':r'^marble-gpu-runtime: released PASS=1 draws=0 wraps=0 active=0\r?$',
                    'gx_restore':r'^marble-gmu-gx: stopped rc=0 halted=1 .*collapsed=1 restored=1 phase=3\r?$',
                    'context_restore':r'^marble-gpu-context: stopped PASS=1 reset=1 .*roots_zero=1 .*tlb_sync=1 irq_fault=0 ',
                    'cpu_restore':r'marble-cpucap: restored PASS=1 active=0 dirty=0 changed=0',
                    'event_restore':r'marble-efi-idle: restored PASS=1 active=0 dirty=0 failed=0'}
                if a.pacing=='hardware':
                    checks['display_bus_restore']=r'marble-display-bus: restored PASS=1 active=0 held=0 uncertain=0 dirty=0'
                if report.get('brightness_lease_requested') or a.brightness_backend:
                    checks['brightness_restore']=r'marble-brightness: restored PASS=1 original=\d+ level=\d+ active=0 dirty=0 changes=\d+'
                report['restoration']={k:bool(re.search(v,text,re.M)) for k,v in checks.items()}
                report['hardware_timing_ready']=(bool(re.search(r'marble-display: (?:runtime|MDISPLAY-MARBLE-COUNTER-v2) ready=1',text)) or
                                                  report.get('hardware_timing_ready',False))
                reports=re.findall(r'marble-display: runtime ready=1 hz=(\d+) frame=(\d+) waits=(\d+) failures=(\d+) notifications=(\d+) presents=(\d+) overruns=(\d+) max_copy_us=(\d+)',text)
                if reports:
                    report['display_runtime']=dict(zip(('hz','frame','waits','failures','notifications','presents','overruns','max_copy_us'),map(int,reports[-1])))
                report['unsynchronized_fallbacks']=text.count('unsynchronized fallback')
                if a.pacing=='hardware':
                    runtime=report.get('display_runtime',{})
                    errors=[]
                    if not report['hardware_timing_ready']:errors.append('hardware counter readiness not observed')
                    if not runtime.get('presents',0):errors.append('no blank-window runtime presentations recorded')
                    if runtime.get('failures',0):errors.append('counter wait failures')
                    if runtime.get('overruns',0):errors.append('copy crossed blank boundary')
                    if report['unsynchronized_fallbacks']:errors.append('unsynchronized fallback')
                    report['display_pacing_acceptance']=dict(passed=not errors,errors=errors,
                        scope='counter polling and blank-window CPU copy only; not optical scanout or IRQ/pageflip')
                report['returned']=True;save()
                if not all(report['restoration'].values()):
                    raise RuntimeError('release incomplete: leaving loader for diagnosis')
                if a.record_seconds:
                    fb('oem','recording');fb('get_staged',a.out/'recording.bin',timeout=60)
                print(fb('reboot'),flush=True)
            elif any(s in state for s in ('result: idle','result: not-started ')):
                if 'result: idle' in state:fb('oem','log')
                fb('get_staged',a.out/'refused.log')
                if report.get('xo_rpmh_hold_requested'):
                    audit=module('xo_rpmh_recovery','tools/marble-xo-rpmh-report.py')
                    evidence=audit.inspect((a.out/'refused.log').read_text(errors='replace'))
                    report['xo_rpmh_recovery_report']=evidence;save()
                    if evidence['reset_safe'] or evidence['clean_refusal']:
                        report['xo_rpmh_explicit_reset_requested']=True;save();print(fb('reboot'),flush=True)
                    else:
                        print('XO hold uncertain; keep watchdog and observe Android recovery without further driver calls',flush=True)
                        deadline=time.monotonic()+55
                        while 'YOUR_ANDROID_SERIAL\tdevice' not in run(MARBLE_ADB,'devices') and time.monotonic()<deadline:time.sleep(1)
                        if 'YOUR_ANDROID_SERIAL\tdevice' not in run(MARBLE_ADB,'devices'):raise RuntimeError('XO hold watchdog recovery not observed')
                        report['xo_rpmh_watchdog_recovery_observed']=True;save()
                elif report.get('xo_npa_test_requested'):
                    audit=module('xo_npa_recovery','tools/marble-xo-npa-report.py')
                    evidence=audit.inspect((a.out/'refused.log').read_text(errors='replace'))
                    report['xo_npa_recovery_report']=evidence;save()
                    if evidence['cleanup_verified'] or evidence['clean_refusal']:
                        report['xo_npa_explicit_reset_requested']=True;save();print(fb('reboot'),flush=True)
                    else:
                        print('XO provider lifecycle uncertain; retain static definitions and watchdog; observe Android recovery',flush=True)
                        deadline=time.monotonic()+55
                        while 'YOUR_ANDROID_SERIAL\tdevice' not in run(MARBLE_ADB,'devices') and time.monotonic()<deadline:time.sleep(1)
                        if 'YOUR_ANDROID_SERIAL\tdevice' not in run(MARBLE_ADB,'devices'):raise RuntimeError('XO provider watchdog recovery not observed')
                        report['xo_npa_watchdog_recovery_observed']=True;save()
                elif report.get('adsp_owned_boot_requested'):
                    audit=module('adsp_owned_recovery','tools/marble-adsp-owned-report.py')
                    evidence=audit.inspect((a.out/'refused.log').read_text(errors='replace'),1,scm_peak=a.adsp_control_boot,wire=a.adsp_control_boot,raw_shutdown=a.adsp_control_boot,held=a.adsp_control_boot,control=a.adsp_control_boot,apps=a.adsp_apps_open,gpr=a.adsp_spf_state,amdb=a.adsp_amdb_info,amdb_cycle=a.adsp_amdb_load_cycle,tdm_graph=a.adsp_tdm_graph_cycle,tdm_config=a.adsp_tdm_config_cycle,audio_clock=a.adsp_audio_clock_cycle,tdm_run=a.adsp_tdm_run_cycle,tdm_pcm_graph=a.adsp_tdm_pcm_graph_cycle,audio_dma=a.audio_dma_info,pcm_memory=a.pcm_memory_cycle,pcm_data=(a.audio_pcm_periods if a.audio_pcm_repeat else a.pcm_data_cycle),tdm_pins=a.tdm_pins_cycle,mfc=a.audio_input_format=='ios48-mfc')
                    report['adsp_owned_boot_recovery_report']=evidence;save()
                    if evidence['reset_safe'] or evidence['clean_refusal']:
                        report['adsp_owned_boot_explicit_reset_requested']=True;save();print(fb('reboot'),flush=True)
                    else:
                        print('DSP stop/ownership uncertain; retain power, bandwidth and watchdog; observe Android recovery without another driver call',flush=True)
                        deadline=time.monotonic()+55
                        while 'YOUR_ANDROID_SERIAL\tdevice' not in run(MARBLE_ADB,'devices') and time.monotonic()<deadline:time.sleep(1)
                        if 'YOUR_ANDROID_SERIAL\tdevice' not in run(MARBLE_ADB,'devices'):raise RuntimeError('owned DSP watchdog recovery not observed')
                        report['adsp_owned_boot_watchdog_recovery_observed']=True;save()
                elif report.get('adsp_owned_stage_requested'):
                    audit=module('adsp_owned_recovery','tools/marble-adsp-owned-report.py')
                    evidence=audit.inspect((a.out/'refused.log').read_text(errors='replace'),qmp=a.adsp_qmp_stage or a.adsp_scm_qmp_stage,scm_peak=a.adsp_scm_stage or a.adsp_scm_qmp_stage or a.adsp_scm_wire_stage or a.adsp_scm_channel_stage or a.adsp_scm_bounded_stage or a.adsp_scm_ipcc_stage,wire=a.adsp_scm_wire_stage or a.adsp_scm_channel_stage or a.adsp_scm_bounded_stage or a.adsp_scm_ipcc_stage,wire_messages=not a.adsp_scm_channel_stage,raw_shutdown=a.adsp_scm_bounded_stage or a.adsp_scm_ipcc_stage,ipcc_ack=a.adsp_scm_ipcc_stage and not a.adsp_scm_held_stage,held=a.adsp_scm_held_stage)
                    report['adsp_owned_recovery_report']=evidence;save()
                    if evidence['reset_safe'] or evidence['clean_refusal']:
                        report['adsp_owned_explicit_reset_requested']=True;save();print(fb('reboot'),flush=True)
                    else:
                        print('ICB ownership uncertain; retain watchdog and observe Android recovery without another driver call',flush=True)
                        deadline=time.monotonic()+55
                        while 'YOUR_ANDROID_SERIAL\tdevice' not in run(MARBLE_ADB,'devices') and time.monotonic()<deadline:time.sleep(1)
                        if 'YOUR_ANDROID_SERIAL\tdevice' not in run(MARBLE_ADB,'devices'):raise RuntimeError('ICB watchdog recovery not observed')
                        report['adsp_owned_watchdog_recovery_observed']=True;save()
                elif report.get('adsp_dependency_cycle_requested'):
                    audit=module('adsp_dependency_recovery','tools/marble-adsp-dependency-report.py')
                    evidence=audit.inspect((a.out/'refused.log').read_text(errors='replace'))
                    report['adsp_dependency_recovery_report']=evidence;save()
                    if evidence['reset_safe'] or evidence['clean_refusal']:
                        report['adsp_dependency_explicit_reset_requested']=True;save();print(fb('reboot'),flush=True)
                    else:
                        print('ICB ownership uncertain; retain watchdog and observe Android recovery without another driver call',flush=True)
                        deadline=time.monotonic()+55
                        while 'YOUR_ANDROID_SERIAL\tdevice' not in run(MARBLE_ADB,'devices') and time.monotonic()<deadline:time.sleep(1)
                        if 'YOUR_ANDROID_SERIAL\tdevice' not in run(MARBLE_ADB,'devices'):raise RuntimeError('ICB watchdog recovery not observed')
                        report['adsp_dependency_watchdog_recovery_observed']=True;save()
                elif report.get('icb_run_bandwidth_cycle_requested'):
                    audit=module('icb_run_bandwidth_recovery','tools/marble-icb-run-bandwidth-report.py')
                    evidence=audit.inspect((a.out/'refused.log').read_text(errors='replace'),a.icb_run_scm_bandwidth_cycle)
                    report['icb_run_bandwidth_recovery_report']=evidence;save()
                    if evidence['reset_safe'] or evidence['clean_refusal']:
                        report['icb_run_bandwidth_explicit_reset_requested']=True;save();print(fb('reboot'),flush=True)
                    else:
                        print('ICB ownership uncertain; retain watchdog and observe Android recovery without another driver call',flush=True)
                        deadline=time.monotonic()+55
                        while 'YOUR_ANDROID_SERIAL\tdevice' not in run(MARBLE_ADB,'devices') and time.monotonic()<deadline:time.sleep(1)
                        if 'YOUR_ANDROID_SERIAL\tdevice' not in run(MARBLE_ADB,'devices'):raise RuntimeError('ICB watchdog recovery not observed')
                        report['icb_run_bandwidth_watchdog_recovery_observed']=True;save()
                elif report.get('icb_run_cycle_requested'):
                    audit=module('icb_run_recovery','tools/marble-icb-run-report.py')
                    evidence=audit.inspect((a.out/'refused.log').read_text(errors='replace'))
                    report['icb_run_recovery_report']=evidence;save()
                    if evidence['reset_safe'] or evidence['clean_refusal']:
                        report['icb_run_explicit_reset_requested']=True;save();print(fb('reboot'),flush=True)
                    else:
                        print('ICB ownership uncertain; retain watchdog and observe Android recovery without another driver call',flush=True)
                        deadline=time.monotonic()+55
                        while 'YOUR_ANDROID_SERIAL\tdevice' not in run(MARBLE_ADB,'devices') and time.monotonic()<deadline:time.sleep(1)
                        if 'YOUR_ANDROID_SERIAL\tdevice' not in run(MARBLE_ADB,'devices'):raise RuntimeError('ICB watchdog recovery not observed')
                        report['icb_run_watchdog_recovery_observed']=True;save()
                elif report.get('icb_bandwidth_cycle_requested'):
                    audit=module('icb_bandwidth_recovery','tools/marble-icb-bandwidth-report.py')
                    evidence=audit.inspect((a.out/'refused.log').read_text(errors='replace'))
                    report['icb_bandwidth_recovery_report']=evidence;save()
                    if evidence['reset_safe'] or evidence['clean_refusal']:
                        report['icb_bandwidth_explicit_reset_requested']=True;save();print(fb('reboot'),flush=True)
                    else:
                        print('ICB ownership uncertain; retain watchdog and observe Android recovery without another driver call',flush=True)
                        deadline=time.monotonic()+55
                        while 'YOUR_ANDROID_SERIAL\tdevice' not in run(MARBLE_ADB,'devices') and time.monotonic()<deadline:time.sleep(1)
                        if 'YOUR_ANDROID_SERIAL\tdevice' not in run(MARBLE_ADB,'devices'):raise RuntimeError('ICB watchdog recovery not observed')
                        report['icb_bandwidth_watchdog_recovery_observed']=True;save()
                elif report.get('icb_zero_cycle_requested'):
                    audit=module('icb_zero_recovery','tools/marble-icb-zero-report.py')
                    evidence=audit.inspect((a.out/'refused.log').read_text(errors='replace'))
                    report['icb_zero_recovery_report']=evidence;save()
                    if evidence['cleanup_verified'] or evidence['clean_refusal']:
                        report['icb_explicit_reset_requested']=True;save();print(fb('reboot'),flush=True)
                    else:
                        print('ICB ownership uncertain; retain watchdog and observe Android recovery without another driver call',flush=True)
                        deadline=time.monotonic()+55
                        while 'YOUR_ANDROID_SERIAL\tdevice' not in run(MARBLE_ADB,'devices') and time.monotonic()<deadline:time.sleep(1)
                        if 'YOUR_ANDROID_SERIAL\tdevice' not in run(MARBLE_ADB,'devices'):raise RuntimeError('ICB watchdog recovery not observed')
                        report['icb_watchdog_recovery_observed']=True;save()
                elif report.get('rpmh_zero_cycle_requested'):
                    audit=module('rpmh_recovery','tools/marble-rpmh-native-report.py')
                    evidence=audit.inspect((a.out/'refused.log').read_text(errors='replace'))
                    report['rpmh_recovery_report']=evidence;save()
                    if evidence['cleanup_verified'] or evidence['clean_refusal']:
                        report['rpmh_explicit_reset_requested']=True;save();print(fb('reboot'),flush=True)
                    else:
                        print('RPMh ownership uncertain; retain watchdog and observe Android recovery without another driver call',flush=True)
                        deadline=time.monotonic()+55
                        while 'YOUR_ANDROID_SERIAL\tdevice' not in run(MARBLE_ADB,'devices') and time.monotonic()<deadline:time.sleep(1)
                        if 'YOUR_ANDROID_SERIAL\tdevice' not in run(MARBLE_ADB,'devices'):raise RuntimeError('RPMh watchdog recovery not observed')
                        report['rpmh_watchdog_recovery_observed']=True;save()
                elif report.get('qmp_wire_cycle_requested'):
                    audit=module('qmp_wire_recovery','tools/marble-qmp-wire-report.py')
                    evidence=audit.inspect((a.out/'refused.log').read_text(errors='replace'),tpl=a.qmp_tpl_cycle,smccc=a.qmp_smccc_cycle,ipcc=a.qmp_ipcc_cycle)
                    report['qmp_wire_recovery_report']=evidence;save()
                    if evidence['clean_refusal'] or (evidence['reset_safe'] and report.get('qmp_wire_post_return_preserved')):
                        report['qmp_wire_explicit_reset_requested']=True;save();print(fb('reboot'),flush=True)
                    else:
                        print('QMP wire state uncertain; retain watchdog and observe Android recovery without further driver calls',flush=True)
                        deadline=time.monotonic()+45
                        while 'YOUR_ANDROID_SERIAL\tdevice' not in run(MARBLE_ADB,'devices') and time.monotonic()<deadline:time.sleep(1)
                        if 'YOUR_ANDROID_SERIAL\tdevice' not in run(MARBLE_ADB,'devices'):raise RuntimeError('QMP wire watchdog recovery not observed')
                        report['qmp_wire_watchdog_recovery_observed']=True;save()
                elif report.get('qmp_native_cycle_requested'):
                    audit=module('qmp_native_recovery','tools/marble-qmp-native-report.py')
                    evidence=audit.inspect((a.out/'refused.log').read_text(errors='replace'),a.qmp_native_peer_close,a.qmp_native_settled_close,mailbox_load_state=a.qmp_mailbox_load_state_cycle)
                    report['qmp_native_recovery_report']=evidence;save()
                    if evidence['cleanup_verified'] or evidence['clean_refusal']:
                        report['qmp_native_explicit_reset_requested']=True;save();print(fb('reboot'),flush=True)
                    else:
                        print('Native QMP cleanup uncertain; retain watchdog and callback storage; observe Android recovery without further driver calls',flush=True)
                        deadline=time.monotonic()+45
                        while 'YOUR_ANDROID_SERIAL\tdevice' not in run(MARBLE_ADB,'devices') and time.monotonic()<deadline:time.sleep(1)
                        if 'YOUR_ANDROID_SERIAL\tdevice' not in run(MARBLE_ADB,'devices'):raise RuntimeError('QMP watchdog recovery not observed')
                        report['qmp_native_watchdog_recovery_observed']=True;save()
                elif report.get('smp2p_prepare_requested'):
                    evidence=inspect_smp((a.out/'refused.log').read_text(errors='replace'))
                    if evidence['passed']:
                        report['smp2p_explicit_cold_reset_requested']=True;save();print(fb('reboot'),flush=True)
                    elif evidence['clean_refusal']:
                        report['smp2p_clean_refusal']=True;save();print(fb('reboot'),flush=True)
                    else:
                        print('SMEM preparation uncertain; retain recovery watchdog and observe Android without more driver calls',flush=True)
                        deadline=time.monotonic()+45
                        while 'YOUR_ANDROID_SERIAL\tdevice' not in run(MARBLE_ADB,'devices') and time.monotonic()<deadline:time.sleep(1)
                        if 'YOUR_ANDROID_SERIAL\tdevice' not in run(MARBLE_ADB,'devices'):raise RuntimeError('SMEM watchdog recovery not observed')
                        report['smp2p_watchdog_recovery_observed']=True;save()
                elif report.get('npa_client_cycle_requested'):
                    text=(a.out/'refused.log').read_text(errors='replace')
                    clean=npa.inspect(text,2 if a.npa_votes or a.npa_native_release or a.pil_powered_stage else 0)['cleanup_verified']
                    report['npa_cleanup_verified']=clean;save()
                    native_reset=(a.pil_powered_stage and inspect_held(text)['passed']) or (a.npa_native_release and npa.inspect_native_release(text)['passed'])
                    if native_reset:
                        report['npa_explicit_cold_reset_requested']=True;save()
                        print('Qualified native release-to-zero; original state is not restored in place. Cold resetting firmware before further driver work.',flush=True)
                        print(fb('reboot'),flush=True)
                    elif not clean:
                        print('NPA cleanup uncertain; retain firmware watchdog and observe Android recovery, without another protocol call',flush=True)
                        deadline=time.monotonic()+45
                        while 'YOUR_ANDROID_SERIAL\tdevice' not in run(MARBLE_ADB,'devices') and time.monotonic()<deadline:time.sleep(1)
                        if 'YOUR_ANDROID_SERIAL\tdevice' not in run(MARBLE_ADB,'devices'):
                            raise RuntimeError('NPA cleanup uncertain and watchdog recovery not observed; loader retained')
                        report['npa_watchdog_recovery_observed']=True;save()
                    else:print(fb('reboot'),flush=True)
                else:print(fb('reboot'),flush=True)
            else:raise RuntimeError('normal return missing; no blind reboot')
            screen_off(a.out)
        elif MARBLE_ANDROID_SERIAL in run(MARBLE_ADB,'devices'):
            screen_off(a.out)
            if report.get('phone_reboot_requested'):
                report['android_reappeared_independently']=True
                report['restoration_verified']=False
            else:
                report['android_remained_online']=True
                report['restoration_not_required_no_boot']=True
        else:raise RuntimeError('phone not found: inspect saved state')
        _,after=battery_snapshot(a.out,'battery-after')
        report['battery_after_c']=after['temperature_c'];report['android_screen_off']=True;save()
        if a.i2c_inventory and report.get('i2c_inventory_requested'):
            boot_id=run(MARBLE_ADB,'-s',MARBLE_ANDROID_SERIAL,'shell','cat','/proc/sys/kernel/random/boot_id').strip()
            report['android_boot_id_after_sha256']=hashlib.sha256(boot_id.encode()).hexdigest()
            changed=bool(re.fullmatch(r'[0-9a-fA-F-]{36}',boot_id)) and report['android_boot_id_after_sha256']!=report.get('android_boot_id_before_sha256')
            report['i2c_inventory_with_reset_passed']=changed and after['temperature_c']<38 and report.get('i2c_inventory',{}).get('passed',False) and (not a.i2c_config_snapshot or report.get('i2c_runtime_config_capture_passed'));save()
        if a.xo_rpmh_hold and report.get('xo_rpmh_hold_requested'):
            boot_id=run(MARBLE_ADB,'-s',MARBLE_ANDROID_SERIAL,'shell','cat','/proc/sys/kernel/random/boot_id').strip()
            report['android_boot_id_after_sha256']=hashlib.sha256(boot_id.encode()).hexdigest()
            changed=bool(re.fullmatch(r'[0-9a-fA-F-]{36}',boot_id)) and report['android_boot_id_after_sha256']!=report.get('android_boot_id_before_sha256')
            report['xo_rpmh_cold_reset_verified']=changed
            report['xo_rpmh_hold_with_reset_passed']=changed and report.get('xo_rpmh_explicit_reset_requested',False) and after['temperature_c']<38 and report.get('xo_rpmh_report',{}).get('passed',False);save()
        if a.xo_npa_test and report.get('xo_npa_test_requested'):
            boot_id=run(MARBLE_ADB,'-s',MARBLE_ANDROID_SERIAL,'shell','cat','/proc/sys/kernel/random/boot_id').strip()
            report['android_boot_id_after_sha256']=hashlib.sha256(boot_id.encode()).hexdigest()
            changed=bool(re.fullmatch(r'[0-9a-fA-F-]{36}',boot_id)) and report['android_boot_id_after_sha256']!=report.get('android_boot_id_before_sha256')
            report['xo_npa_cold_reset_verified']=changed
            report['xo_npa_with_reset_passed']=changed and report.get('xo_npa_explicit_reset_requested',False) and after['temperature_c']<38 and report.get('xo_npa_report',{}).get('passed',False);save()
        if a.adsp_owned_boot and report.get('adsp_owned_boot_requested'):
            boot_id=run(MARBLE_ADB,'-s',MARBLE_ANDROID_SERIAL,'shell','cat','/proc/sys/kernel/random/boot_id').strip()
            report['android_boot_id_after_sha256']=hashlib.sha256(boot_id.encode()).hexdigest()
            changed=bool(re.fullmatch(r'[0-9a-fA-F-]{36}',boot_id)) and report['android_boot_id_after_sha256']!=report.get('android_boot_id_before_sha256')
            report['adsp_owned_boot_cold_reset_verified']=changed
            report['adsp_owned_boot_with_reset_passed']=changed and report.get('adsp_owned_boot_explicit_reset_requested',False) and after['temperature_c']<38 and report.get('adsp_owned_boot_report',{}).get('passed',False);save()
        if a.adsp_owned_stage and report.get('adsp_owned_stage_requested'):
            boot_id=run(MARBLE_ADB,'-s',MARBLE_ANDROID_SERIAL,'shell','cat','/proc/sys/kernel/random/boot_id').strip()
            report['android_boot_id_after_sha256']=hashlib.sha256(boot_id.encode()).hexdigest()
            changed=bool(re.fullmatch(r'[0-9a-fA-F-]{36}',boot_id)) and report['android_boot_id_after_sha256']!=report.get('android_boot_id_before_sha256')
            report['adsp_owned_cold_reset_verified']=changed
            report['adsp_owned_with_reset_passed']=changed and report.get('adsp_owned_explicit_reset_requested',False) and after['temperature_c']<38 and report.get('adsp_owned_report',{}).get('passed',False) and (not (a.adsp_scm_wire_stage or a.adsp_scm_channel_stage or a.adsp_scm_bounded_stage or a.adsp_scm_ipcc_stage) or report.get('adsp_scm_wire_post_return_preserved',False));save()
        if a.adsp_dependency_cycle and report.get('adsp_dependency_cycle_requested'):
            boot_id=run(MARBLE_ADB,'-s',MARBLE_ANDROID_SERIAL,'shell','cat','/proc/sys/kernel/random/boot_id').strip()
            report['android_boot_id_after_sha256']=hashlib.sha256(boot_id.encode()).hexdigest()
            changed=bool(re.fullmatch(r'[0-9a-fA-F-]{36}',boot_id)) and report['android_boot_id_after_sha256']!=report.get('android_boot_id_before_sha256')
            report['adsp_dependency_cold_reset_verified']=changed
            report['adsp_dependency_with_reset_passed']=changed and report.get('adsp_dependency_explicit_reset_requested',False) and after['temperature_c']<38 and report.get('adsp_dependency_report',{}).get('passed',False);save()
        if a.icb_run_bandwidth_cycle and report.get('icb_run_bandwidth_cycle_requested'):
            boot_id=run(MARBLE_ADB,'-s',MARBLE_ANDROID_SERIAL,'shell','cat','/proc/sys/kernel/random/boot_id').strip()
            report['android_boot_id_after_sha256']=hashlib.sha256(boot_id.encode()).hexdigest()
            changed=bool(re.fullmatch(r'[0-9a-fA-F-]{36}',boot_id)) and report['android_boot_id_after_sha256']!=report.get('android_boot_id_before_sha256')
            report['icb_run_bandwidth_cold_reset_verified']=changed
            report['icb_run_bandwidth_with_reset_passed']=changed and report.get('icb_run_bandwidth_explicit_reset_requested',False) and after['temperature_c']<38 and report.get('icb_run_bandwidth_report',{}).get('passed',False);save()
        if a.icb_run_cycle and report.get('icb_run_cycle_requested'):
            boot_id=run(MARBLE_ADB,'-s',MARBLE_ANDROID_SERIAL,'shell','cat','/proc/sys/kernel/random/boot_id').strip()
            report['android_boot_id_after_sha256']=hashlib.sha256(boot_id.encode()).hexdigest()
            changed=bool(re.fullmatch(r'[0-9a-fA-F-]{36}',boot_id)) and report['android_boot_id_after_sha256']!=report.get('android_boot_id_before_sha256')
            report['icb_run_cold_reset_verified']=changed
            report['icb_run_with_reset_passed']=changed and report.get('icb_run_explicit_reset_requested',False) and after['temperature_c']<38 and report.get('icb_run_report',{}).get('passed',False);save()
        if a.icb_bandwidth_cycle and report.get('icb_bandwidth_cycle_requested'):
            boot_id=run(MARBLE_ADB,'-s',MARBLE_ANDROID_SERIAL,'shell','cat','/proc/sys/kernel/random/boot_id').strip()
            report['android_boot_id_after_sha256']=hashlib.sha256(boot_id.encode()).hexdigest()
            changed=bool(re.fullmatch(r'[0-9a-fA-F-]{36}',boot_id)) and report['android_boot_id_after_sha256']!=report.get('android_boot_id_before_sha256')
            report['icb_bandwidth_cold_reset_verified']=changed
            report['icb_bandwidth_with_reset_passed']=changed and report.get('icb_bandwidth_explicit_reset_requested',False) and after['temperature_c']<38 and report.get('icb_bandwidth_report',{}).get('passed',False);save()
        if a.icb_zero_cycle and report.get('icb_zero_cycle_requested'):
            boot_id=run(MARBLE_ADB,'-s',MARBLE_ANDROID_SERIAL,'shell','cat','/proc/sys/kernel/random/boot_id').strip()
            report['android_boot_id_after_sha256']=hashlib.sha256(boot_id.encode()).hexdigest()
            changed=bool(re.fullmatch(r'[0-9a-fA-F-]{36}',boot_id)) and report['android_boot_id_after_sha256']!=report.get('android_boot_id_before_sha256')
            report['icb_cold_reset_verified']=changed
            report['icb_zero_with_reset_passed']=changed and report.get('icb_explicit_reset_requested',False) and after['temperature_c']<38 and report.get('icb_zero_report',{}).get('passed',False);save()
        if a.rpmh_zero_cycle and report.get('rpmh_zero_cycle_requested'):
            boot_id=run(MARBLE_ADB,'-s',MARBLE_ANDROID_SERIAL,'shell','cat','/proc/sys/kernel/random/boot_id').strip()
            report['android_boot_id_after_sha256']=hashlib.sha256(boot_id.encode()).hexdigest()
            changed=bool(re.fullmatch(r'[0-9a-fA-F-]{36}',boot_id)) and report['android_boot_id_after_sha256']!=report.get('android_boot_id_before_sha256')
            report['rpmh_cold_reset_verified']=changed
            report['rpmh_zero_with_reset_passed']=changed and report.get('rpmh_explicit_reset_requested',False) and after['temperature_c']<38 and report.get('rpmh_zero_report',{}).get('passed',False);save()
        if a.qmp_native_cycle and report.get('qmp_native_cycle_requested'):
            boot_id=run(MARBLE_ADB,'-s',MARBLE_ANDROID_SERIAL,'shell','cat','/proc/sys/kernel/random/boot_id').strip()
            report['android_boot_id_after_sha256']=hashlib.sha256(boot_id.encode()).hexdigest()
            changed=bool(re.fullmatch(r'[0-9a-fA-F-]{36}',boot_id)) and report['android_boot_id_after_sha256']!=report.get('android_boot_id_before_sha256')
            report['qmp_native_cold_reset_verified']=changed
            report['qmp_native_cycle_with_reset_passed']=changed and after['temperature_c']<38 and report.get('qmp_native_report',{}).get('passed',False) and (not a.qmp_mailbox_load_state_cycle or report.get('qmp_native_explicit_reset_requested',False))
            report['qmp_original_restoration_verified']=report.get('qmp_native_report',{}).get('original_restoration',False)
            report['qmp_channel_closure_verified']=report.get('qmp_native_report',{}).get('cleanup_verified',False);save()
        if a.qmp_wire_cycle and report.get('qmp_wire_cycle_requested'):
            boot_id=run(MARBLE_ADB,'-s',MARBLE_ANDROID_SERIAL,'shell','cat','/proc/sys/kernel/random/boot_id').strip()
            report['android_boot_id_after_sha256']=hashlib.sha256(boot_id.encode()).hexdigest()
            changed=bool(re.fullmatch(r'[0-9a-fA-F-]{36}',boot_id)) and report['android_boot_id_after_sha256']!=report.get('android_boot_id_before_sha256')
            report['qmp_wire_cold_reset_verified']=changed
            report['qmp_wire_with_reset_passed']=changed and report.get('qmp_wire_explicit_reset_requested',False) and after['temperature_c']<38 and report.get('qmp_wire_report',{}).get('passed',False) and report.get('qmp_wire_post_return_preserved',False);save()
        if a.smp2p_prepare and report.get('smp2p_explicit_cold_reset_requested'):
            boot_id=run(MARBLE_ADB,'-s',MARBLE_ANDROID_SERIAL,'shell','cat','/proc/sys/kernel/random/boot_id').strip()
            report['android_boot_id_after_sha256']=hashlib.sha256(boot_id.encode()).hexdigest()
            changed=bool(re.fullmatch(r'[0-9a-fA-F-]{36}',boot_id)) and report['android_boot_id_after_sha256']!=report.get('android_boot_id_before_sha256')
            report['smp2p_cold_reset_verified']=changed
            report['smp2p_prepare_with_reset_passed']=changed and after['temperature_c']<38 and report['smp2p_prepare_report']['passed']
            report['smp2p_prepare_report']['cold_reset_verified']=changed;save()
        if (a.npa_native_release or a.pil_powered_stage) and report.get('npa_explicit_cold_reset_requested'):
            boot_id=run(MARBLE_ADB,'-s',MARBLE_ANDROID_SERIAL,'shell','cat','/proc/sys/kernel/random/boot_id').strip()
            report['android_boot_id_after_sha256']=hashlib.sha256(boot_id.encode()).hexdigest()
            changed=bool(re.fullmatch(r'[0-9a-fA-F-]{36}',boot_id)) and report['android_boot_id_after_sha256']!=report.get('android_boot_id_before_sha256')
            report['npa_cold_reset_verified']=changed
            report['npa_native_release_with_reset_passed']=changed and after['temperature_c']<38 and report['npa_native_release_report']['passed']
            report['npa_native_release_report']['cold_reset_verified']=changed;save()
            if a.pil_powered_stage:
                report['pil_powered_stage_with_reset_passed']=report['npa_native_release_with_reset_passed'];save()
                if a.pil_bounded_boot:
                    report['pil_bounded_boot_with_reset_passed']=report['pil_powered_stage_with_reset_passed'];save()
                if a.pil_smp2p_boot:
                    report['pil_smp2p_boot_with_reset_passed']=report['pil_powered_stage_with_reset_passed'];save()
        if (a.out/'returned.log').exists():
            # The loader's final header retains all TSENS records even if the
            # kernel's own console ring wrapped. Audit the raw return directly.
            checked=thermal.inspect(a.out/'returned.log')
            (a.out/'thermal-report.json').write_text(json.dumps(checked,indent=2)+'\n')
            report['thermal_report_passed']=checked['passed'];report['thermal_report_errors']=checked['errors']
            save()
        print(json.dumps(report,indent=2),flush=True)
    if a.i2c_inventory and not report.get('i2c_inventory_with_reset_passed'):raise SystemExit(1)
    if a.qup_core_keepalive and not report.get('qup_core_keepalive',{}).get('passed'):raise SystemExit(1)
    if a.audio_i2c_window and not report.get('audio_i2c_window',{}).get('passed'):raise SystemExit(1)
    if a.audio_i2c_hold and not report.get('audio_i2c_hold',{}).get('passed'):raise SystemExit(1)
    if a.tdm_pins_cycle and not report.get('adsp_tdm_pins_cycle',{}).get('passed'):raise SystemExit(1)
    if a.pcm_data_cycle and not report.get('adsp_pcm_data_cycle',{}).get('passed'):raise SystemExit(1)
    if a.pcm_memory_cycle and not report.get('adsp_pcm_memory_cycle',{}).get('passed'):raise SystemExit(1)
    if a.audio_map_cycle and not report.get('adsp_audio_map_cycle',{}).get('passed'):raise SystemExit(1)
    if a.audio_context_info and not report.get('adsp_audio_context_inventory',{}).get('passed'):raise SystemExit(1)
    if a.audio_dma_info and not report.get('adsp_audio_dma_inventory',{}).get('passed'):raise SystemExit(1)
    if a.adsp_tdm_run_cycle and not report.get('adsp_tdm_run_cycle',{}).get('passed'):raise SystemExit(1)
    if a.adsp_audio_clock_cycle and not report.get('adsp_audio_clock_cycle',{}).get('passed'):raise SystemExit(1)
    if a.adsp_tdm_graph_cycle and not report.get('adsp_tdm_graph_cycle',{}).get('passed'):raise SystemExit(1)
    if a.adsp_amdb_load_cycle and not report.get('adsp_amdb_load_cycle',{}).get('passed'):raise SystemExit(1)
    if a.adsp_amdb_info and not report.get('adsp_amdb_info',{}).get('passed'):raise SystemExit(1)
    if a.adsp_spf_state and not (a.adsp_amdb_info or a.adsp_amdb_load_cycle or a.adsp_tdm_graph_cycle or a.adsp_audio_clock_cycle or a.adsp_tdm_run_cycle) and not report.get('adsp_spf_state',{}).get('passed'):raise SystemExit(1)
    if report.get('error'):raise SystemExit(1)
    if a.xo_rpmh_hold and not report.get('xo_rpmh_hold_with_reset_passed'):raise SystemExit(1)
    if a.xo_npa_test and not report.get('xo_npa_with_reset_passed'):raise SystemExit(1)
    if a.adsp_owned_boot and not report.get('adsp_owned_boot_with_reset_passed'):raise SystemExit(1)
    if a.adsp_owned_stage and not report.get('adsp_owned_with_reset_passed'):raise SystemExit(1)
    if a.adsp_dependency_cycle and not report.get('adsp_dependency_with_reset_passed'):raise SystemExit(1)
    if a.icb_run_bandwidth_cycle and not report.get('icb_run_bandwidth_with_reset_passed'):raise SystemExit(1)
    if a.icb_run_cycle and not report.get('icb_run_with_reset_passed'):raise SystemExit(1)
    if a.icb_bandwidth_cycle and not report.get('icb_bandwidth_with_reset_passed'):raise SystemExit(1)
    if a.icb_zero_cycle and not report.get('icb_zero_with_reset_passed'):raise SystemExit(1)
    if a.adsp_dependency_info and (not a.discovery_only or a.pil_driver or a.pil_files or any(v for k,v in vars(a).items()
            if isinstance(v,bool) and k not in ('adsp_dependency_info','discovery_only'))):
        p.error('dependency inventory requires isolated discovery')
    if a.rpmh_zero_cycle and not report.get('rpmh_zero_with_reset_passed'):raise SystemExit(1)
    if a.qmp_native_cycle and not report.get('qmp_native_cycle_with_reset_passed'):raise SystemExit(1)
    if a.qmp_wire_cycle and not report.get('qmp_wire_with_reset_passed'):raise SystemExit(1)
    if a.glink_smem_info and (not report.get('glink_smem_inventory',{}).get('passed') or not report.get('android_screen_off')):raise SystemExit(1)
    if a.smp2p_prepare and not report.get('smp2p_prepare_with_reset_passed'):raise SystemExit(1)
    if (a.npa_native_release or a.pil_powered_stage) and not report.get('npa_native_release_with_reset_passed'):raise SystemExit(1)

if __name__=='__main__':main()
