#!/usr/bin/env python3
"""Marble RAM-only test controller. No flash, erase, unlock or partition commands."""
from marble_local import FASTBOOT as MARBLE_FASTBOOT
import argparse
from datetime import datetime
import hashlib
import json
import re
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time
from marble_smp import KERNEL_SHA, patched_kernel, write_kernel
from marble_source import verify_kernel
from marble_android import verify_battery_preflight
from marble_audio_refill_profile import DISK_SHA256 as REFILL_DISK_SHA, STREAM_DISK_SHA256 as STREAM_DISK_SHA

ROOT=Path(__file__).resolve().parents[1]

def blocking_owner_completion(reply):
    completion=re.findall(r'audio-run-result=([^\r\n]+)',reply)
    if len(completion)!=1:raise RuntimeError('missing/duplicate blocking owner completion')
    result=completion[0].strip()
    if result.startswith('running'):raise RuntimeError('blocking completion still running')
    return result

def verify_boot_image(image,manifest,hardware_status=None):
    if hardware_status is not None:
        if hardware_status.get('image_sha256')!=hashlib.sha256(image).hexdigest():
            raise ValueError('hardware status belongs to another image')
        if hardware_status.get('boot_allowed') is False or hardware_status.get('hardware_status','').startswith('FAILED'):
            raise ValueError('boot candidate failed hardware acceptance; original build manifest preserved')
    if manifest.get('hardware_status','').startswith('FAILED'):
        raise ValueError('boot candidate is recorded as hardware-failed; choose a corrected image')
    if hashlib.sha256(image).hexdigest()!=manifest['output_sha256']:
        raise ValueError('boot image differs from manifest')

def payload(kernel,ramdisk,core,seconds,verbose=False,input_mailbox=False,cpus=1,source_profile=None,diagnostics=False,fb_threads=1,gpu_probe=0,gpu_runtime=False,gpu_composition=False,display_pacing='legacy',record_seconds=0,blank_display=False,cpu_cap=False,efi_idle=False,short_thermal=False,gpu_preview=False,record_start=35,daily_buttons=False,record_mb=96,record_step=6,brightness_backend=False,host_epoch=None,render_scale=3,manual_ten_minute=False,audio_queue_probe=False,audio_transport_probe=False,audio_owner_package=None,audio_owner_periods=2,audio_owner_pcm=False,audio_board_held=False,audio_owner_refill=False,audio_owner_stream=False):
    if audio_owner_stream and (audio_owner_refill or hashlib.sha256(ramdisk).hexdigest()!=STREAM_DISK_SHA):raise ValueError('stream24 requires its pinned producer disk and distinct mode')
    if audio_owner_refill and hashlib.sha256(ramdisk).hexdigest()!=REFILL_DISK_SHA:
        raise ValueError('live refill requires its independently pinned producer disk')
    if cpus not in (1,2,4) or core not in range(4,8) or not 1<=seconds<=3600:
        raise ValueError('invalid marble CPU or run-time profile')
    if manual_ten_minute and not(gpu_preview and seconds==600 and display_pacing=='hardware'):
        raise ValueError('manual preview requires exactly 600 seconds, GPU preview and hardware pacing')
    maximum=600 if manual_ten_minute else 90
    if gpu_runtime and seconds>maximum:
        raise ValueError('GPU runtime is capped at 90 seconds after the reported heating incident')
    if gpu_preview and not (source_profile and cpu_cap and efi_idle and cpus==4 and
            30<=seconds<=maximum and gpu_probe==17 and gpu_runtime and gpu_composition and
            input_mailbox and not blank_display and not short_thermal and not verbose and
            (hashlib.sha256(kernel).hexdigest()=='de4f2fe9e573522fa1580d95e7069567ccc6599e4766f3cf5c3655e8d80aa6d4' or
             (b'MDISPLAY-MARBLE-COUNTER-v2\0' in kernel and source_profile.get('gpu_preview_kernel_abi')=='display-counter-v2'))):
        raise ValueError('GPU preview requires the explicit CPU-cap kernel, both leases, four cores, touch and 30..90 second GPU composition')
    if source_profile:
        verify_kernel(kernel,source_profile)
        if not gpu_preview and gpu_runtime and source_profile.get('gpu_status','').startswith('FAILED'):
            raise ValueError('GPU startup for this candidate is recorded as failed; do not repeat it without a corrected build')
        if not gpu_preview and source_profile.get('idle_only_after_thermal_stop') and not (
                blank_display and len(ramdisk)<=0x400000 and not gpu_probe and not gpu_runtime and not gpu_composition):
            raise ValueError('this candidate hit its startup temperature guard; only the audited small black idle profile remains enabled')
    else:
        expected=(ROOT/'prebuilt/mach.development.bcm2837').read_bytes()
        if hashlib.sha256(expected).hexdigest()!=KERNEL_SHA:
            raise ValueError('upstream prebuilt has changed')
        if cpus>1: expected,_=patched_kernel(expected)
        if kernel!=expected:
            raise ValueError('kernel differs from the verified CPU profile')
    if fb_threads not in (1,2,4) or (not source_profile and (diagnostics or fb_threads!=1)):
        raise ValueError('display workers and CPU telemetry require an explicit source build')
    if render_scale not in (2,3) or (render_scale==2 and not(source_profile and gpu_composition and gpu_preview)):
        raise ValueError('2x rendering requires the bounded source GPU preview; supported scales are 2 and 3')
    args=('-v ' if verbose else '')+f'bootcpu={core} cpus={cpus} rd=md0 gl=0 touchscreen=0 buttons=0 cpufreq=0 gpu=0 wifi=0 adsp=0 ufs=0 ufs_write=0 ufs_native=0 var_store=0 battery=0 metal=0 ddr=0 loader_return_after={seconds}'
    if host_epoch is not None:
        if type(host_epoch) is not int or not 1577836800<=host_epoch<=4102444799 or not source_profile:
            raise ValueError('host clock requires a source kernel and a UTC epoch in 2020..2099')
        args+=f' epoch={host_epoch}'
    if source_profile:
        args+=f' fb_threads={fb_threads}'
    if render_scale==2:args+=' fbscale=2'
    if cpu_cap:
        if not source_profile or seconds>maximum or b'MCPU-MARBLE-CAP-CHECK-v2\0' not in kernel:
            raise ValueError('CPU request cap requires its matching source kernel and a 1..90 second budget')
        args+=' marble_cpu_cap=1'
    if source_profile and source_profile.get('requires_cpu_cap') and not cpu_cap:
        raise ValueError('this thermal candidate requires the restored CPU request lease')
    if efi_idle:
        if not cpu_cap or not source_profile or seconds>maximum:
            raise ValueError('EFI event wait requires the source CPU cap profile and a 1..90 second budget')
        args+=' marble_efi_idle=1'
    if short_thermal:
        if not (cpu_cap and efi_idle and source_profile and seconds==18 and blank_display and
                not input_mailbox and not gpu_probe and not gpu_runtime and not gpu_composition and
                (hashlib.sha256(ramdisk).hexdigest()=='15eb3220b7369c5ca1eef1fbc43847569c5413b5bfc796f0cd15bbbb862a0ddd' or (audio_queue_probe and b'MAUDIO-QUEUE-PROBE-v1' in kernel and (hashlib.sha256(ramdisk).hexdigest()=='82a15700ea358e1f3c492437e71ad9c6a2ffe4c21c8c1635452df6304566494e' or (audio_owner_pcm and b'MAUDIO-OWNER-XNU-PCM8-MUTED-v1' in kernel and (hashlib.sha256(ramdisk).hexdigest()=='8361b8538d88d32cd5f50138c96d06d18262f5c78c77f6d2d8c0a842b9bdf579' or (audio_owner_refill and b'MAUDIO-REFILL8-MUTED-v1' in kernel and hashlib.sha256(ramdisk).hexdigest()==REFILL_DISK_SHA) or (audio_owner_stream and b'MAUDIO-OWNER-XNU-STREAM24-MUTED-v2' in kernel and hashlib.sha256(ramdisk).hexdigest()==STREAM_DISK_SHA))))))):
            raise ValueError('short thermal window is only for the pinned 18-second black sleeping-init experiment')
        args+=' marble_thermal_short=1'
    if audio_queue_probe:
        if not (source_profile and b'MAUDIO-QUEUE-PROBE-v1' in kernel and blank_display and
                len(ramdisk)<=0x400000 and seconds<=30 and not gpu_probe and not gpu_runtime and not gpu_composition and not input_mailbox):
            raise ValueError('audio queue probe requires its source ABI, small static black RAM disk and <=30s without GPU/touch')
        args+=' marble_audio_queue=1'
    if audio_transport_probe:
        if not (audio_queue_probe and short_thermal and b'MAUDIO-NATIVE-GLINK-RO-v1' in kernel):
            raise ValueError('native SMEM transport probe requires explicit queue, short18 and its kernel ABI')
        args+=' marble_audio_transport=1'
    if gpu_preview:args+=' marble_gpu_preview=1'
    if manual_ten_minute:args+=' marble_gpu_manual=1'
    if diagnostics:
        args+=' touch_lat=1 cpuload=1000 marble_cpu_probe=1 marble_idle_probe=1'
    if blank_display:
        if not source_profile or verbose or gpu_composition:
            raise ValueError('black idle display requires source kernel, quiet console and no GPU composition')
        args+=' fb_blank=1'
    if display_pacing not in ('legacy','immediate','vsync','hardware') or not 0<=record_seconds<=120 or not 0<=record_start<=120:
        raise ValueError('invalid bounded display profile')
    if (display_pacing!='legacy' or record_seconds) and not source_profile:
        raise ValueError('display profiles require an explicit source kernel')
    if display_pacing!='legacy':args+=' fb_beat=0'
    if display_pacing=='vsync':args+=' ca_vbl=1'
    if display_pacing=='hardware':
        if b'MDISPLAY-MARBLE-COUNTER-v2\0' not in kernel:
            raise ValueError('hardware display pacing requires the timing-counter source candidate')
        args+=' ca_vbl=1 fb_hw_vsync=1'
    if record_mb not in (96,192) or record_step not in (6,8):
        raise ValueError('unsupported bounded recording memory/sample profile')
    if record_seconds:args+=f' rec_secs={record_seconds} rec_start={record_start} rec_mb={record_mb} rec_step={record_step} rec_ring=1'
    if gpu_probe:
        if not source_profile or gpu_probe not in range(1,18) or b'MGPU-MARBLE-PREFLIGHT-v1' not in kernel:
            raise ValueError('GPU preflight requires an explicit source kernel with its board adapter')
        if gpu_probe>=3 and b'MGPU-MARBLE-POWER-v1' not in kernel:
            raise ValueError('CX transaction requires the kernel power adapter')
        if gpu_probe>=4 and b'MGPU-MARBLE-CONTEXT-v2' not in kernel:
            raise ValueError('context diagnostic requires its kernel adapter')
        if gpu_probe>=6 and b'MGPU-MARBLE-TBU-v1' not in kernel:
            raise ValueError('TBU probe requires its kernel adapter')
        if gpu_probe>=7 and b'MGPU-MARBLE-ECATS-v1' not in kernel:
            raise ValueError('ECATS requires its kernel adapter')
        if gpu_probe>=8 and b'MGPU-MARBLE-GMU-MEMORY-v1' not in kernel:
            raise ValueError('GMU memory probe requires its kernel adapter')
        if gpu_probe>=9 and b'MGPU-MARBLE-GMU-TCM-v1' not in kernel:
            raise ValueError('TCM probe requires its kernel adapter')
        if gpu_probe>=11 and b'MGPU-MARBLE-GMU-RESOURCES-v1' not in kernel:
            raise ValueError('GMU log/HFI resources require their kernel adapter')
        if gpu_probe>=12 and b'MGPU-MARBLE-GMU-PREFLIGHT-v1' not in kernel:
            raise ValueError('GMU clock/command DB probe requires its kernel adapter')
        if gpu_probe>=13 and b'MGPU-MARBLE-GMU-VOTES-v1' not in kernel:
            raise ValueError('GMU CPU vote encoding requires its kernel adapter')
        if gpu_probe>=14 and b'MGPU-MARBLE-GMU-CLOCKS-v1' not in kernel:
            raise ValueError('GMU clock transaction requires its kernel adapter')
        if gpu_probe>=15 and b'MGPU-MARBLE-GMU-RPMH-v1' not in kernel:
            raise RuntimeError('kernel lacks marble RPMh read transaction')
        if gpu_probe>=16 and b'MGPU-MARBLE-GMU-DEPENDENCIES-v1' not in kernel:
            raise RuntimeError('kernel lacks dependency read transaction')
        if gpu_probe>=17 and b'MGPU-MARBLE-GMU-BOOT-v1' not in kernel:
            raise RuntimeError('kernel lacks bounded GMU boot transaction')
        args+=f' marble_gpu_probe={gpu_probe}'
    if gpu_runtime:
        if not source_profile or gpu_probe!=17 or b'MGPU-MARBLE-RUNTIME-v1' not in kernel:
            raise ValueError('GPU runtime requires its explicit source kernel and probe17')
        args+=' marble_gpu_runtime=1'
    if gpu_composition:
        if not gpu_runtime or b'MGPU-MARBLE-SERVICE-v1' not in kernel:
            raise ValueError('GPU composition requires the explicit userspace GPU service kernel')
        args=args.replace(' gl=0 ',' gl=1 ',1)
    if brightness_backend:
        if not (input_mailbox and source_profile and cpu_cap and efi_idle and
                display_pacing=='hardware' and not short_thermal and not blank_display):
            raise ValueError('brightness backend requires RAM mailbox, source kernel, both leases and hardware pacing')
        args+=' marble_brightness=1'
    if daily_buttons and not (input_mailbox and source_profile):
        raise ValueError('daily buttons require an explicit source kernel and v2 RAM input mailbox')
    if input_mailbox:
        if len(ramdisk)>0x800000: raise ValueError('support disk overlaps reserved RAM input mailbox')
        ramdisk+=bytes(0x800000-len(ramdisk))
        header=struct.pack('<Q4I5Q',0x314843554f544d,2 if daily_buttons else 1,32,128,0,0,0,0,0,0)
        ramdisk+=header+bytes(0x4000-len(header))
    entries=[('kernel',kernel),('ramdisk',ramdisk)]
    if audio_owner_refill and (not audio_board_held or not audio_owner_pcm or b'MAUDIO-REFILL8-MUTED-v1' not in kernel):raise ValueError('live refill requires explicit held PCM8 and refill kernel ABI')
    if audio_board_held and (not audio_owner_pcm or b'MAUDIO-BOARD-QUIET-v1' not in kernel):raise ValueError('board hold requires explicit PCM8 and board gate kernel ABI')
    if audio_owner_stream and not (audio_owner_pcm and audio_board_held and audio_owner_periods==24 and b'MAUDIO-OWNER-XNU-STREAM24-MUTED-v2' in kernel):raise ValueError('stream24 requires separate v2 kernel and held PCM24 profile')
    if audio_owner_pcm and ((audio_owner_periods!=8 and not (audio_owner_stream and audio_owner_periods==24)) or audio_owner_package is None):raise ValueError('PCM probe requires eight periods and owner package')
    if (audio_owner_periods not in (2,8) and not (audio_owner_stream and audio_owner_periods==24)) or (audio_owner_periods!=2 and audio_owner_package is None):raise ValueError('explicit owner period count requires owner package')
    if audio_owner_package is not None:
        if not (audio_queue_probe and short_thermal and cpus==1 and seconds==18 and not audio_transport_probe and source_profile and b'MAUDIO-OWNER-XNU-ZERO-v1' in kernel):raise ValueError('audio owner requires the isolated single-core zero18 kernel/queue profile')
        if len(audio_owner_package)!=30180144 or hashlib.sha256(audio_owner_package).hexdigest()!='ce5a2ee6534ea6fe4e5d2272252989854a3e0277893f49c87d63376458f7d237':raise ValueError('audio owner package differs from pinned MADSP001 bundle')
        if audio_owner_periods==8:
            if b'MAUDIO-OWNER-XNU-ZERO8-v1' not in kernel:raise ValueError('eight periods require ZERO8 source-kernel ABI')
            args+=' marble_audio_owner_periods=8'
        if audio_owner_stream:args+=' marble_audio_owner_periods=24'
        if audio_owner_pcm:
            if b'MAUDIO-OWNER-XNU-PCM8-MUTED-v1' not in kernel:raise ValueError('PCM probe requires source-kernel PCM8-MUTED ABI')
            args+=' marble_audio_owner_pcm=1'
        if audio_board_held:args+=' marble_audio_board_gate=1'
        if audio_owner_refill:args+=' marble_audio_refill=1'
        if audio_owner_stream:args+=' marble_audio_stream=1'
        args+=' marble_audio_owner=1';entries.append(('audio-dsp',audio_owner_package))
    entries.append(('cmdline',args.encode()+b'\0'))
    body=b''
    for name,data in entries:
        body+=struct.pack('<16sII',name.encode(),len(data),0)+data+bytes(-len(data)%16)
    body+=bytes(24)
    if len(body)+8>0x3f00000: raise ValueError('payload exceeds loader buffer')
    return struct.pack('<II',0x50554e58,len(body))+body

def main():
    if hasattr(sys.stdout,'reconfigure'):sys.stdout.reconfigure(errors='backslashreplace')
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--fastboot',default=shutil.which('fastboot') or MARBLE_FASTBOOT)
    p.add_argument('--serial',help='optional exact fastboot serial')
    sub=p.add_subparsers(dest='command',required=True)
    b=sub.add_parser('boot',help='temporary fastboot boot; phone must already be in bootloader')
    b.add_argument('--image',type=Path,default=ROOT/'out/marble-userspace-boot.img')
    sub.add_parser('status')
    sub.add_parser('touch-probe',help='probe volatile touch controller registers; kernel must be idle')
    rd=sub.add_parser('ramdisk',help='upload APFS system image into loader-owned RAM')
    rd.add_argument('image',type=Path)
    r=sub.add_parser('run')
    r.add_argument('--ramdisk',type=Path,default=ROOT/'prebuilt/userland/hello')
    r.add_argument('--core',type=int,choices=range(4,8),default=7)
    r.add_argument('--cpus',type=int,choices=(1,2,4),default=1,help='XNU big cores; CPU0 stays with EFI/USB')
    r.add_argument('--seconds',type=int,default=30)
    r.add_argument('--selftest',action='store_true')
    r.add_argument('--verbose',action='store_true',help='show kernel console on the framebuffer')
    r.add_argument('--daily-buttons',action='store_true',help='opt into PMIC observer buttons through the v2 RAM mailbox; use the matching bridge')
    r.add_argument('--brightness-backend',action='store_true',help='opt into bounded RAM/DCS brightness requests with panel readback and exact exit restoration')
    r.add_argument('--host-clock',action='store_true',help='initialize the RAM session clock from the host UTC clock; no RTC or persistent writes')
    r.add_argument('--input-mailbox',action='store_true',help='append the EFI input mailbox to the support RAM disk')
    r.add_argument('--detach',action='store_true',help='return after starting XNU; use status and snapshot for follow-up')
    r.add_argument('--kernel',type=Path,help='explicit source-built candidate; requires matching JSON manifest and new loader')
    r.add_argument('--diagnostics',action='store_true',help='source kernel only: per-core load, display timing, read-only EPSS')
    r.add_argument('--fb-threads',type=int,choices=(1,2,4),default=1)
    r.add_argument('--render-scale',type=int,choices=(2,3),default=3,help='source GPU only: opt into original 2x logical rendering and panel upscale; 3 keeps baseline sharpness')
    r.add_argument('--blank-display',action='store_true',help='black framebuffer for a headless idle test; panel power unchanged')
    r.add_argument('--cpu-cap',action='store_true',help='marble big/prime request cap around 1.2 GHz, restored after all XNU cores stop; no little/voltage writes')
    r.add_argument('--efi-idle',action='store_true',help='opt-in local counter event wait; requires successful loader-only oem idle-probe and CPU cap')
    r.add_argument('--short-thermal',action='store_true',help='pinned 18-second sleeping init only: die start below 50 C; stop at 55 C or 10 seconds of consecutive >=50 C samples; not battery telemetry')
    r.add_argument('--gpu-preview',action='store_true',help='explicit bounded GPU preview with qualified EFI wait, CPU cap and fresh Android battery reading; preserves old failed-profile records')
    r.add_argument('--manual-ten-minute',action='store_true',help='explicit manual GPU preview only: exactly 600 seconds; existing temperature guards retained')
    r.add_argument('--audio-owner-pcm',action='store_true')
    r.add_argument('--audio-board-held',action='store_true')
    r.add_argument('--audio-owner-refill',action='store_true')
    r.add_argument('--audio-owner-stream',action='store_true')
    r.add_argument('--audio-owner-native-link',action='store_true')
    r.add_argument('--audio-owner-blocking-log',action='store_true',help='strict matching board firmware returns final state; fetch staged log without an extra state query')
    r.add_argument('--audio-owner-periods',type=int,choices=(2,8,24),default=2)
    r.add_argument('--audio-owner-package',type=Path,help='explicit pinned MADSP001 bundle for the combined XNU/EFI zero2 owner smoke')
    r.add_argument('--audio-transport-probe',action='store_true',help='read-only native SMEM/GLINK view, no FIFO/IPCC writes or DSP start')
    r.add_argument('--audio-queue-probe',action='store_true',help='explicit RAM-only kernel audio sink test; no DSP or playback')
    r.add_argument('--battery-preflight',type=Path,help='fresh Android battery JSON for the short thermal experiment')
    r.add_argument('--display-pacing',choices=('legacy','immediate','vsync','hardware'),default='legacy',help='legacy/software modes or explicit source hardware-counter polling; no panel clock change')
    r.add_argument('--record-seconds',type=int,default=0,help='0..120 seconds of reduced-resolution frame recording into a bounded RAM ring')
    r.add_argument('--record-start',type=int,default=35,help='0..120 seconds after the first presented frame')
    r.add_argument('--record-mb',type=int,choices=(96,192),default=96)
    r.add_argument('--record-step',type=int,choices=(6,8),default=6)
    r.add_argument('--gpu-runtime',action='store_true',help='retain audited GPU leases; worker or userspace service according to the explicit kernel')
    r.add_argument('--gpu-composition',action='store_true',help='service kernel only: enable the existing EAGL/Mesa CoreAnimation adapter')
    r.add_argument('--gpu-probe',type=int,choices=range(1,18),default=0,help='source only: 1 firmware/tables, 2 read clocks, 3 transient CX, 4 read context banks, 5 program/audit/release CB0 (ATS only if supported); 6 read gfx0 TBU, 7 ECATS with SID5, 8 firmware RAM and privileged ECATS, 9 read-only TCM stability, 10 TCM load/readback/restore, 11 stopped KGSL HFI/log resources, 12 read-only GMU clocks and command DB, 13 CPU-only Gen7 candidate tables, 14 bounded GMU/HUB clock transaction, 15 restored RPMh ARC read transaction, 16 retained ARC/BCM request adequacy, 17 bounded GMU firmware boot/reset/drain')
    sub.add_parser('snapshot',help='fetch a live log without stopping XNU')
    sub.add_parser('reboot',help='return to Android without changing partitions')
    a=p.parse_args()
    if a.command=='run' and not 1<=a.seconds<=3600: p.error('seconds must be 1..3600')
    if a.command=='run' and a.selftest and a.cpus>1: p.error('SMP cannot run on the EFI selftest core')
    if a.command=='run' and (a.short_thermal or a.gpu_preview):
        if not a.battery_preflight:p.error('windowed thermal experiment needs --battery-preflight')
        verify_battery_preflight(a.battery_preflight)
    devices=subprocess.check_output([a.fastboot,'devices'],text=True).splitlines()
    serials=[x.split()[0] for x in devices if x.split()]
    if a.serial:
        if a.serial not in serials: p.error('requested phone is not connected in fastboot')
    elif len(serials)==1: a.serial=serials[0]
    else: p.error('exactly one phone required, or use --serial')
    def fb(*cmd,timeout=30):
        r=subprocess.run([a.fastboot,'-s',a.serial,*map(str,cmd)],capture_output=True,text=True,timeout=timeout)
        output=r.stdout+r.stderr
        if r.returncode: raise RuntimeError(output)
        return output
    def get(name):
        text=fb('getvar',name)
        for line in text.splitlines():
            if line.startswith(name+':'): return line.split(':',1)[1].strip()
        raise RuntimeError('missing getvar '+name+': '+text)
    if a.command=='boot':
        status=a.image.parent/'hardware-status.json'
        manifest=json.loads(a.image.with_suffix('.json').read_text())
        verify_boot_image(a.image.read_bytes(),manifest,json.loads(status.read_text()) if status.is_file() else None)
        if get('product')!='marble' or get('unlocked')!='yes':
            raise RuntimeError('requires an already-unlocked marble; this tool cannot unlock')
        print(fb('boot',a.image,timeout=60)); return
    if get('product')!='xnuloader' or get('platform')!='marble' or get('storage-policy')!='ram-only-no-block-io':
        raise RuntimeError('not the marble RAM-only loader')
    if a.command=='status':
        print('marble RAM-only loader:',get('result')); return
    if a.command=='snapshot':
        log=ROOT/'out'/(datetime.now().strftime('%Y%m%d-%H%M%S')+'-marble-live.log')
        # A finished run already staged its complete kernel log. Preserve it.
        if get('result').startswith('running'): fb('oem','log')
        fb('get_staged',log)
        print(log); return
    if a.command=='reboot': print(fb('reboot')); return
    if get('result').startswith('running'): raise RuntimeError('a kernel is already running')
    if a.command=='touch-probe':
        result=subprocess.run([a.fastboot,'-s',a.serial,'oem','touch-probe'],capture_output=True,text=True,timeout=30)
        print(result.stdout+result.stderr)
        log=ROOT/'out/marble-touch-probe.log'
        fb('oem','log'); fb('get_staged',log)
        print(log.read_text(errors='replace'))
        return
    if a.command=='ramdisk':
        if not a.image.is_file() or not a.image.stat().st_size: raise RuntimeError('empty or missing image')
        print(fb('oem','clearscreen'))
        print(fb('oem','ramdisk')); print(fb('stage',a.image,timeout=1200)); return
    out=ROOT/'out'; out.mkdir(exist_ok=True)
    file=out/'marble-run.bin'
    kernel=(ROOT/'prebuilt/mach.development.bcm2837').read_bytes()
    profile=None
    if a.kernel:
        if get('marble-source-kernel')!='gic-dt-stride-v1':
            raise RuntimeError('source kernel requires the new loader')
        profile=json.loads(a.kernel.with_suffix('.json').read_text())
        if profile.get('hardware_status','').startswith('FAILED'):
            raise RuntimeError('this candidate is recorded as hardware-failed; choose the corrected candidate')
        kernel=a.kernel.read_bytes()
        verify_kernel(kernel,profile)
    elif a.cpus>1:
        if get('marble-smp')!='big-cores-1,2,4-gic256k-v1':
            raise RuntimeError('SMP requires the new marble loader; temporarily boot its image first')
        _,kernel=write_kernel()
    if a.gpu_probe and get('marble-gpu-preflight')!='readonly-v1':
        raise RuntimeError('GPU preflight requires its matching marble loader')
    if a.gpu_probe>=3 and get('marble-gpu-power')!='cx-restore-v1':
        raise RuntimeError('CX transaction requires its matching marble loader')
    if a.gpu_probe>=4 and get('marble-gpu-context')!='cb0-ats-release-v2':
        raise RuntimeError('context diagnostic requires its matching marble loader')
    if a.gpu_probe>=6 and get('marble-gpu-tbu')!='gfx0-v1':
        raise RuntimeError('TBU probe requires its matching loader windows')
    if a.gpu_probe>=8 and get('marble-gpu-memory')!='six-ranges-v1':
        raise RuntimeError('GMU memory probe requires its matching loader policy')
    if a.gpu_probe>=9 and get('marble-gpu-tcm')!='restore-v1':
        raise RuntimeError('TCM probe requires its matching loader policy')
    if a.gpu_probe>=11 and get('marble-gpu-resources')!='log-hfi-v1':
        raise RuntimeError('GMU log/HFI resources require their matching loader policy')
    if a.gpu_probe>=12 and get('marble-gmu-preflight')!='clocks-cmddb-readonly-v1':
        raise RuntimeError('GMU clock/command DB probe requires its matching loader')
    if a.gpu_probe>=13 and get('marble-gmu-votes')!='gen7-cpu-tables-v1':
        raise RuntimeError('GMU CPU tables require their matching loader policy')
    if a.gpu_probe>=14 and get('marble-gmu-clocks')!='gpll0-restore-v1':
        raise RuntimeError('GMU clock transaction requires its matching loader')
    if a.gpu_probe>=15 and get('marble-gmu-rpmh')!='arc-read-restore-v1':
        raise RuntimeError('RPMh read transaction requires matching loader')
    if a.gpu_probe>=17 and get('marble-gmu-boot')!='fw-init-reset-drain-v1':
        raise RuntimeError('GMU boot requires matching CX_MISC window and loader')
    if (a.display_pacing!='legacy' or a.record_seconds) and get('marble-display-test')!='clock-record-v1':
        raise RuntimeError('display profile requires its matching bounded loader policy')
    if a.display_pacing=='hardware' and get('marble-display-timing')!='firmware-ahb-lease-counter-v2':
        raise RuntimeError('hardware timing requires the firmware display-bus lease loader')
    if get('marble-thermal')!='tsens-v2-stop-55c-v1':
        raise RuntimeError('run requires the TSENS temperature-stop loader; do not reuse the earlier unmonitored loader')
    if a.cpu_cap and (a.selftest or get('marble-cpucap')!='big-prime-restore-1p2ghz-v2'):
        raise RuntimeError('CPU cap requires its matching loader and off-core run')
    if a.efi_idle and (a.selftest or get('marble-efi-idle') not in ('event-stream-500us-v1','event-stream-500us-v2') or get('marble-efi-idle-ready')!='yes'):
        raise RuntimeError('EFI wait requires matching loader, successful oem idle-probe and off-core run')
    if a.audio_owner_blocking_log and not (a.audio_owner_package and a.audio_board_held and not a.detach and get('marble-audio-run-log')=='blocking-result-log-v1'):raise RuntimeError('blocking log requires matching explicit owner board firmware')
    if a.audio_owner_package:
        expected='XNU-FIFO-EFI-resources-PCM24-v2-muted-ACK-before-free' if a.audio_owner_stream else 'XNU-FIFO-EFI-resources-PCM8-muted-ACK-before-free-v1' if a.audio_owner_pcm else 'XNU-FIFO-EFI-resources-zero'+str(a.audio_owner_periods)+'-ACK-before-free-v1'
        if get('marble-audio-owner')!=expected:raise RuntimeError('combined audio owner capability absent')
        if a.audio_owner_native_link and (not (a.audio_owner_refill or a.audio_owner_stream) or get('marble-audio-link-owner')!='XNU-only-no-EFI-observer-v1'):raise RuntimeError('native-only link profile requires matching refill loader')
        if (a.audio_owner_refill or a.audio_owner_stream) and get('marble-audio-qmp-close')!='coherent2-unchanged-codec-750ms-v1':raise RuntimeError('live refill requires coherent QMP close profile')
        if a.audio_owner_stream and get('marble-audio-owner-stream')!='STREAM24-v2-128ms-wrap4096-600ms-diagnostic':raise RuntimeError('stream24 loader capability absent')
        if a.audio_owner_refill and get('marble-audio-refill')!='PCM8-live-producer-gap1-readonly-status-v1':raise RuntimeError('live refill capability absent')
        if a.audio_board_held and get('marble-audio-board-held')!=('PCM24-v2-HMUTE-DRAIN-QUIET-before-STOP' if a.audio_owner_stream else 'PCM8-HMUTE-DRAIN-QUIET-before-STOP-v1'):raise RuntimeError('explicit board hold capability absent')
    if a.audio_transport_probe and get('marble-audio-transport')!='SMEM-readonly-native18-v1':
        raise RuntimeError('native SMEM probe requires its dedicated readonly loader')
    if a.audio_queue_probe and get('marble-audio-queue')!='locked-RAM4096-static18-noDSP-v1':
        raise RuntimeError('RAM audio queue probe requires its dedicated bounded loader')
    if a.short_thermal and get('marble-thermal-short')!='short18-die50-10s-hard55-v1':
        raise RuntimeError('short thermal window requires its matching bounded loader')
    if a.gpu_preview and get('marble-gpu-preview')!='gpu90-die50-60s-hard55-v2':
        raise RuntimeError('GPU preview requires its matching windowed-temperature EFI wait loader')
    if a.manual_ten_minute and get('marble-gpu-manual-preview')!='manual600-die50-60s-hard55-v1':
        raise RuntimeError('ten-minute preview requires its explicit duration-policy loader')
    if a.render_scale==2 and get('marble-render-scale')!='2x-preview-v1':
        raise RuntimeError('2x render candidate requires its strictly bounded scale-policy loader')
    print(fb('oem','clearscreen'))
    if a.daily_buttons and get('marble-buttons')!='observer-v7-mailbox-v2':
        raise RuntimeError('daily buttons require the qualified observer firmware')
    if a.brightness_backend and get('marble-brightness')!='ram-dcs-readback-restore-v1':
        raise RuntimeError('brightness requests require the qualified RAM/DCS loader')
    if a.host_clock and get('marble-host-clock')!='epoch-2020-2099-v1':
        raise RuntimeError('host clock requires its matching loader')
    epoch=int(time.time()) if a.host_clock else None
    file.write_bytes(payload(kernel,a.ramdisk.read_bytes(),a.core,a.seconds,a.verbose,a.input_mailbox,a.cpus,profile,a.diagnostics,a.fb_threads,a.gpu_probe,a.gpu_runtime,a.gpu_composition,a.display_pacing,a.record_seconds,a.blank_display,a.cpu_cap,a.efi_idle,a.short_thermal,a.gpu_preview,a.record_start,a.daily_buttons,a.record_mb,a.record_step,a.brightness_backend,epoch,a.render_scale,a.manual_ten_minute,a.audio_queue_probe,a.audio_transport_probe,a.audio_owner_package.read_bytes() if a.audio_owner_package else None,a.audio_owner_periods,a.audio_owner_pcm,a.audio_board_held,a.audio_owner_refill,a.audio_owner_stream))
    if epoch is not None:print(f'RAM session host UTC epoch={epoch}; persistent RTC unchanged')
    # A freshly booted EFI has not probed touch yet. Besides readying Goodix,
    # this finishes the existing loader's RPMh handoff before the GPU leases TCS0.
    # mt_probe is cached/idempotent; a failed probe must prevent this run.
    if a.input_mailbox:
        print(fb('oem','touch-probe'))
    print(fb('oem','thermal-gpu-preview-probe' if a.gpu_preview else 'thermal-short-probe' if a.short_thermal else 'thermal-probe'))
    print(fb('stage',file))
    run_reply=fb('oem','selftest' if a.selftest else 'run');print(run_reply)
    if a.detach:
        state=get('result')
        if not state.startswith('running'):
            raise RuntimeError('XNU did not remain running: '+state+'; fetch the staged log before retrying')
        print('XNU started; automatic return after '+str(a.seconds)+' seconds.'); return
    deadline=time.monotonic()+a.seconds+45
    while True:
        if a.audio_owner_blocking_log:
            result=blocking_owner_completion(run_reply)
            break
        result=get('result')
        if not result.startswith('running'): break
        if time.monotonic()>deadline: raise RuntimeError('kernel did not return: '+result)
        time.sleep(1)
    # Fetch the staged run log directly: "oem log" would replace its kernel section.
    log=out/(datetime.now().strftime('%Y%m%d-%H%M%S')+'-marble.log')
    fb('get_staged',log)
    print(result+'; log: '+str(log))
    data=log.read_bytes().split(b'\0PMOC')[0]
    print(data.decode('utf8',errors='replace'))
    if not result.startswith(('returned ','selftest-ok ')) or b'panic(cpu' in data or b'EARLY KERNEL EXCEPTION' in data:
        raise RuntimeError('kernel did not complete normally; preserved log: '+str(log))

if __name__=='__main__': main()
