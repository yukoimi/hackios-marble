#!/usr/bin/env python3
"""Build RAM APFS images on Windows without native Unix symlinks.

Input: upstream ios165 extraction, normalized 7z technical listings, prebuilt/userland.
Apple files stay local. This tool never communicates with a phone.
"""
import argparse
import hashlib
import importlib.util
import plistlib
from pathlib import Path
import shutil
import stat
import subprocess
import sys
from marble_windows import extracted_path

ROOT = Path(__file__).resolve().parents[1]

# Exact firmware recovered from the user's ROM. These files remain in the
# support RAM disk; this does not install firmware into Android partitions.
GPU_FIRMWARE = {
    'gmu_gen70000.bin': (57272, 'b2d3f0e2a98fd6259ab9ffef16df63265542e6cc9a970d53d476e823d432042a'),
    'a730_sqe.fw': (74676, 'e67d1829f57fc8326d806234b932dc3b78e28a9940df34c70752f17d5f38b7aa'),
    'a730_zap.mdt': (4044, 'b957876634e6a9eab17ae39912ea3b4cb5ba811d0720b04ba0590e118c3cd46a'),
    'a730_zap.b00': (148, '9cd65a08fa2ce72c05a52aad95951f749f52fc310613a2857e270b463a641a7f'),
    'a730_zap.b01': (2236, '1fc98f97cf354b9704b20d0848f7b2739638539a57d88ecf5cc71ca1c5c4e6b9'),
    'a730_zap.b02': (3896, '41a11d0a7c9a6dd523bc1da3fdbeb887f04a9295072391fcdd4b8b71f713fedc'),
}

def gpu_firmware(directory):
    result = {}
    for name, (size, digest) in GPU_FIRMWARE.items():
        data = (directory / name).read_bytes()
        if len(data) != size or hashlib.sha256(data).hexdigest() != digest:
            raise ValueError('GPU firmware differs from the pinned ROM: ' + name)
        result[name] = data
    return result

def module(name, path):
    s=importlib.util.spec_from_file_location(name,path)
    m=importlib.util.module_from_spec(s); s.loader.exec_module(m)
    return m

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--mode',choices=['cli','launchd'],default='cli')
    p.add_argument('--support-only',action='store_true')
    p.add_argument('--support-output',type=Path,help='save the support image to a new path without overwriting a baseline')
    p.add_argument('--system-output',type=Path,help='new alternate system image; preserves the baseline')
    p.add_argument('--localization-overlay',type=Path,help='size/hash-verified output of marble-localization.py')
    p.add_argument('--language',choices=['en','zh-Hans'],default='en')
    p.add_argument('--wallpaper-2x',action='store_true',help='add stock Stripe wallpaper for 414x896@2 and select its blue variant in the alternate RAM system')
    p.add_argument('--brightness-observer',action='store_true',help='log stock iOS brightness requests using the selected diagnostic/backend build')
    p.add_argument('--brightness-service',action='store_true',help='include the root RAM-only request broker; requires backend observer and explicit runtime opt-in')
    p.add_argument('--brightness-ui-inventory',action='store_true',help='also inspect SpringBoard brightness method ABIs; read only')
    p.add_argument('--input-selftest',action='store_true',help='run the raw RAM mailbox check in CLI mode')
    p.add_argument('--cpu-test',action='store_true',help='run CPU counts and concurrent arithmetic in CLI mode')
    p.add_argument('--input-qos',action='store_true',help='enable backboardd input QoS and timing diagnostics')
    p.add_argument('--programs',type=Path,help='use explicitly source-built userspace executables and library overrides')
    p.add_argument('--gpu-firmware',type=Path,help='include six hash-verified ROM firmware files in the support RAM disk')
    p.add_argument('--cpu-stress',type=int,default=0,help='CLI only: bounded 1..300 second arithmetic/memory/timer stress')
    p.add_argument('--idle-seconds',type=int,default=0,help='CLI only: sleep 1..30 seconds for temperature/return-path observation, without a stress workload')
    p.add_argument('--gpu-test',action='store_true',help='CLI only: source-built Mesa hardware triangle and pixel check')
    p.add_argument('--gpu-sysmem',action='store_true',help='launchd only: compare Mesa sysmem rendering with default GMEM tiling')
    p.add_argument('--gpu-profile',choices=['default','shared','sysmem','nofetch-sysmem','copy','copy-input','copy-output','nofetch','nofetch-copy-input'],default='default',help='bounded compositor comparison; all rendering stays on the GPU')
    p.add_argument('--gpu-output-timing',action='store_true',help='log fullsize surface publication timing; diagnostic, not panel FPS')
    p.add_argument('--gpu-error-poll',choices=['call','frame'],help='experimental GL debug polling frequency; application error queries unchanged')
    p.add_argument('--gpu-fetch-copy',action='store_true',help='candidate: CA fetch shaders sample a separate GPU attachment copy')
    p.add_argument('--gpu-backdrop-grow',action='store_true',help='candidate: retain GPU snapshot capacity across smaller attachments')
    p.add_argument('--gpu-present-scale',action='store_true',help='candidate: sequence-bound GLES final scaling helper; matching source kernel required')
    p.add_argument('--gpu-scale-audit',action='store_true',help='diagnostic: bounded CPU source/output pixel comparisons in the final scaling helper')
    p.add_argument('--gpu-scale-delay',type=int,choices=(0,15,30),default=0,help='test only: defer the scaling client GPU open to isolate initialization ordering')
    p.add_argument('--gpu-scale-noblit',action='store_true',help='candidate: use Mesa existing GPU blitter fallback in the scale process, isolating its hardware 2D path')
    p.add_argument('--gpu-scale-await-compositor',action='store_true',help='candidate: open the scale GPU client only after CoreAnimation completes its first frame')
    p.add_argument('--ui-layout-diagnostic',action='store_true',help='log UIScreen and status bar provider ABIs in SpringBoard; no drawing hooks')
    p.add_argument('--ui-geometry-2x',action='store_true',help='normalize the 414x896@2 exclusion shape to the stock Split828 provider')
    p.add_argument('--ui-wallpaper-inspect',action='store_true',help='bounded native wallpaper view hierarchy evidence; no appearance changes')
    p.add_argument('--ui-wallpaper-static',type=Path,help='explicit static PNG fallback in the native wallpaper window; no managed Poster service required')
    p.add_argument('--ui-open-settings',action='store_true',help='test only: ABI-checked native SpringBoard app launch notification for locale acceptance')
    p.add_argument('--ui-app-wait',action='store_true',help='test only: bounded Preferences main-thread samples and console routing')
    p.add_argument('--ui-ready-gate',action='store_true',help='test only: require SpringBoard did-finish-launch notification before native gesture sequence')
    p.add_argument('--ui-statusbar-inspect',action='store_true',help='bounded read-only status-bar view/layer alpha inspection for the remaining top-left black background')
    p.add_argument('--ui-statusbar-gradient-alpha',action='store_true',help='candidate: correct opaque flags only for the qualified native status-bar gradient with transparent colors')
    p.add_argument('--ui-gesture-inspect',action='store_true',help='bounded Control Center UIWindow event/recognizer diagnostics; original event dispatch is preserved')
    p.add_argument('--ui-control-local-pan',action='store_true',help='candidate: a fresh visible Control Center signal classifies bottom touches for its native local dismissal pan')
    p.add_argument('--ui-wallpaper-reset',action='store_true',help='ask the native SpringBoard wallpaper controller to reload defaults in RAM after startup')
    p.add_argument('--ui-wallpaper-image',type=Path,help='native wallpaper setter input PNG from local stock assets; copied only to support RAM disk')
    p.add_argument('--ui-wallpaper-color',action='store_true',help='diagnostic: native ColorPoster background, isolated from unsupported legacy image setter')
    p.add_argument('--media-compat',action='store_true',help='restore upstream mediaserverd compatibility injection for portrait prewarm and HEIC software decode')
    p.add_argument('--gesture-test',action='store_true',help='launchd only: replay six bounded swipes after 45 seconds; keep hands off')
    p.add_argument('--display-test',action='store_true',help='launchd only: bounded marked page, Settings scrolling and app animations')
    p.add_argument('--display-case',choices=['full','animation','pages','panels','power','manual','locale'],default='full',help='short repeated transitions, locale acceptance, or one unlock in manual mode')
    p.add_argument('--display-warmup',type=int,choices=range(25,61),default=55)
    p.add_argument('--display-rounds',type=int,choices=(1,2,3),default=3)
    p.add_argument('--panel-warmup',type=int,choices=range(20,46),default=45,help='bounded panels test warmup before marked gestures')
    p.add_argument('--panel-rounds',type=int,choices=(1,2),default=1)
    a=p.parse_args()
    if a.support_output and a.support_output.exists():p.error('--support-output already exists')
    if a.gpu_scale_audit and not a.gpu_present_scale:p.error('--gpu-scale-audit requires --gpu-present-scale')
    if a.gpu_scale_delay and not a.gpu_present_scale:p.error('--gpu-scale-delay requires --gpu-present-scale')
    if a.gpu_scale_noblit and not a.gpu_present_scale:p.error('--gpu-scale-noblit requires --gpu-present-scale')
    if a.gpu_scale_await_compositor and (not a.gpu_present_scale or a.gpu_scale_delay):p.error('compositor gate requires scale helper and no fixed delay')
    if a.system_output and a.system_output.exists():p.error('--system-output already exists')
    if a.localization_overlay and not a.system_output:p.error('localization overlay requires a new --system-output')
    if a.wallpaper_2x and not a.system_output:p.error('wallpaper compatibility requires a new --system-output')
    if a.brightness_ui_inventory and not a.brightness_observer:p.error('--brightness-ui-inventory requires --brightness-observer')
    if a.brightness_service and not(a.brightness_observer and a.brightness_ui_inventory and a.programs):
        p.error('brightness service requires observer, SpringBoard injection and source programs')
    if a.idle_seconds and (a.mode!='cli' or not 1<=a.idle_seconds<=30 or a.cpu_stress or a.cpu_test or a.gpu_test):
        p.error('idle observation requires CLI mode, 1..30 seconds and no CPU/GPU stress options')
    firmware = gpu_firmware(a.gpu_firmware) if a.gpu_firmware else {}
    if a.gpu_sysmem:
        if a.gpu_profile!='default':p.error('choose --gpu-profile or --gpu-sysmem')
        a.gpu_profile='sysmem'
    if a.gpu_profile!='default' and (a.mode!='launchd' or not a.programs):
        p.error('GPU compositor comparison requires launchd mode and source programs')
    if (a.gpu_output_timing or a.gpu_error_poll) and (a.mode!='launchd' or not a.programs):
        p.error('GPU diagnostics require launchd mode and source programs')
    if a.gpu_fetch_copy and (a.mode!='launchd' or not a.programs or a.gpu_profile not in ('shared','sysmem')):
        p.error('GPU fetch-copy requires launchd, source programs and shared/sysmem profile')
    if a.gpu_backdrop_grow and (a.mode!='launchd' or not a.programs):
        p.error('GPU backdrop storage candidate requires launchd and source programs')
    if a.gpu_present_scale and (a.mode!='launchd' or not a.programs or not (a.programs/'marble-gpu-scale').is_file()):
        p.error('GPU final scale candidate requires launchd and its source-built helper')
    if (a.ui_layout_diagnostic or a.ui_geometry_2x) and (a.mode!='launchd' or not a.programs or not (a.programs/'marble-ui-layout.dylib').is_file()):
        p.error('UI layout diagnostic requires its source-built library and launchd')
    if a.ui_wallpaper_reset and not a.ui_geometry_2x:p.error('wallpaper reset requires --ui-geometry-2x')
    if a.ui_wallpaper_inspect and not a.ui_geometry_2x:p.error('wallpaper inspection requires --ui-geometry-2x')
    if a.ui_open_settings and not (a.ui_geometry_2x and a.display_case=='locale' and a.display_test):p.error('native launch test requires geometry shim and locale display test')
    if a.ui_app_wait and not a.ui_open_settings:p.error('Preferences sampler requires native locale launch test')
    if a.ui_wallpaper_static:
        if not a.ui_geometry_2x or a.ui_wallpaper_reset or a.ui_wallpaper_image or a.ui_wallpaper_color:p.error('static wallpaper needs geometry shim and no other wallpaper experiment')
        from PIL import Image
        with Image.open(a.ui_wallpaper_static) as image:
            if image.format!='PNG' or image.size!=(828,1792):p.error('static fallback requires a local 828x1792 PNG')
    if a.ui_wallpaper_image and (not a.ui_geometry_2x or a.ui_wallpaper_reset):p.error('wallpaper image requires geometry shim and no reset option')
    if a.ui_wallpaper_image and not (a.programs/'marble-wallpaper').is_file():p.error('wallpaper image requires the separate native helper')
    if a.ui_wallpaper_color and (not a.ui_geometry_2x or a.ui_wallpaper_image or a.ui_wallpaper_reset):p.error('choose only one native wallpaper experiment with geometry shim')
    if a.media_compat and (a.mode!='launchd' or not a.programs or not (a.programs/'libredwood-media.dylib').is_file()):p.error('media compatibility requires the explicitly rebuilt library')
    if a.cpu_stress and (a.mode!='cli' or not 1<=a.cpu_stress<=300 or not a.programs):
        p.error('CPU stress requires CLI mode, --programs, and 1..300 seconds')
    if a.gesture_test and (a.mode!='launchd' or not a.programs):
        p.error('gesture test requires launchd mode and --programs')
    if a.display_test and (a.mode!='launchd' or not a.programs or a.gesture_test):
        p.error('display test requires launchd mode, --programs, and no --gesture-test')
    if a.ui_ready_gate and (not a.display_test or not a.ui_geometry_2x):p.error('UI ready gate requires native display test and geometry library')
    if a.ui_statusbar_inspect and not a.ui_geometry_2x:p.error('status-bar inspection requires the geometry library')
    if a.ui_statusbar_gradient_alpha and not a.ui_geometry_2x:p.error('gradient alpha candidate requires the geometry library')
    if a.ui_gesture_inspect and not a.ui_geometry_2x:p.error('gesture diagnostics require the geometry library')
    if a.ui_control_local_pan:
        if not (a.ui_geometry_2x and a.programs):p.error('Control Center classification requires the explicit UI/input source build')
        for name in ('marble-ui-layout.dylib','libredwood.dylib'):
            if b'ui-control-local-pan' not in (a.programs/name).read_bytes():p.error('Control Center producer/consumer marker absent in '+name)
    sys.path.insert(0,str(ROOT/'tools/apfs'))
    import mkapfs as apfs
    select=module('marble_select',ROOT/'ios165/select-system.py')
    out=ROOT/'out'; ios=ROOT/'ios165'; pre=ROOT/'prebuilt/userland'
    programs=a.programs.resolve() if a.programs else pre
    support=out/('marble-support-'+a.mode); support.mkdir(exist_ok=True)
    # Reusing an output directory must never retain a previous GPU payload.
    if (support/'fw').exists(): shutil.rmtree(support/'fw')
    def put(path,data):
        f=support/path; f.parent.mkdir(parents=True,exist_ok=True)
        f.write_bytes(data.encode() if isinstance(data,str) else data)
    for name, data in firmware.items(): put('fw/'+name,data)
    for d in ['sbin','bin','etc','lib','share','LaunchDaemons','private/var','dev','empty','xpc','hal']:
        (support/d).mkdir(parents=True,exist_ok=True)
    # A reused staging directory must not silently enable an old candidate.
    (support/'bin/marble-gpu-scale').unlink(missing_ok=True)
    (support/'LaunchDaemons/com.marble.gpu-scale.plist').unlink(missing_ok=True)
    if a.gpu_present_scale:
        put('bin/marble-gpu-scale',(programs/'marble-gpu-scale').read_bytes())
        put('LaunchDaemons/com.marble.gpu-scale.plist',plistlib.dumps({
            'Label':'com.marble.gpu-scale','ProgramArguments':['/redwood/bin/marble-gpu-scale'],
            'RunAtLoad':True,'LaunchOnlyOnce':True,'ProcessType':'Interactive','Nice':-20,
            'EnvironmentVariables':dict({'DYLD_LIBRARY_PATH':'/redwood/lib'},**({'MARBLE_SCALE_AUDIT':'1'} if a.gpu_scale_audit else {}),**({'MARBLE_SCALE_START_DELAY':str(a.gpu_scale_delay)} if a.gpu_scale_delay else {}),**({'FD_MESA_DEBUG':'noblit'} if a.gpu_scale_noblit else {}),**({'MARBLE_SCALE_WAIT_COMPOSITOR':'1'} if a.gpu_scale_await_compositor else {})),
            'StandardOutPath':'/dev/console','StandardErrorPath':'/dev/console'}))
    for name in ['init','child','hello_dyn','svc_test','surf_test','mg_test','cpu_test']:
        put(('sbin/' if name=='init' else 'bin/')+name,(programs/name).read_bytes())
    for f in pre.glob('*.dylib'): put('lib/'+f.name,f.read_bytes())
    if a.programs:
        for f in programs.glob('*.dylib'): put('lib/'+f.name,f.read_bytes())
    bridge=(programs if a.programs else out)/'marble-touch-bridge'
    if bridge.exists(): put('bin/marble-touch-bridge',bridge.read_bytes())
    put('etc/motd','Darwin on marble: RAM-only userspace diagnostics\n')
    put('share/pattern.bin',bytes((i*31+(i>>8))&255 for i in range(300000)))
    skel=subprocess.check_output([sys.executable,'ios165/mkvarskel.py','ios165/rootfs.slt'],cwd=ROOT).decode().replace('\r\n','\n')
    skel=skel.replace('l\t/usr/share/zoneinfo\t','l\t/usr/share/zoneinfo.default\t')
    var=ROOT/'userland/var'
    if var.exists():
        shutil.copytree(var,support/'var',dirs_exist_ok=True)
        for f in sorted(var.rglob('*')):
            if f.is_file():
                rel=f.relative_to(var).as_posix(); owner=501 if rel.startswith('mobile/') else 0
                skel+=f'f\t644\t{owner}\t{owner}\t/private/var/{rel}\t/var/{rel}\n'
    global_preferences=support/'var/mobile/Library/Preferences/.GlobalPreferences.plist'
    global_preferences.unlink(missing_ok=True)
    if a.language=='zh-Hans':
        old_zone='l\t/var/db/timezone/zoneinfo/US/Pacific\t/private/var/db/timezone/localtime'
        if old_zone not in skel:raise RuntimeError('stock timezone skeleton differs; no silent timezone patch')
        # Keep the stock TZDIR prefix. CoreFoundation derives the zone name
        # from this link; a direct zoneinfo.default path is readable by libc
        # but fails its name lookup and falls back to GMT on 20F66.
        skel=skel.replace(old_zone,'l\t/var/db/timezone/zoneinfo/Asia/Shanghai\t/private/var/db/timezone/localtime')
        put('var/mobile/Library/Preferences/.GlobalPreferences.plist',plistlib.dumps({
            'AppleLanguages':['zh-Hans-CN','en'],'AppleLocale':'zh_CN',
            'AppleKeyboards':['zh_Hans-Pinyin@sw=Pinyin;hw=Automatic','en_US@sw=QWERTY;hw=Automatic']}))
        skel+='f\t644\t501\t501\t/private/var/mobile/Library/Preferences/.GlobalPreferences.plist\t/var/mobile/Library/Preferences/.GlobalPreferences.plist\n'
    put('etc/var.skel',skel)
    put('etc/inject.conf','/redwood/lib/libredwood.dylib backboardd containermanagerd containermanagerd_system\n/redwood/lib/libredwood-sb.dylib SpringBoard\n')
    if a.input_qos:
        shim=(programs if a.programs else out)/'marble-input-qos.dylib'
        if not shim.exists(): raise RuntimeError('build the input QoS shim first')
        put('lib/marble-input-qos.dylib',shim.read_bytes())
        # One rule per process: init sets exec_inject from this file. Keep the
        # original injection for other processes and append to backboardd's list.
        put('etc/inject.conf','/redwood/lib/marble-input-qos.dylib:/redwood/lib/libredwood.dylib backboardd\n/redwood/lib/libredwood.dylib containermanagerd containermanagerd_system\n/redwood/lib/libredwood-sb.dylib SpringBoard\n')
    if a.brightness_observer:
        shim=programs/'marble-brightness-observer.dylib'
        if a.mode!='launchd' or not shim.exists():raise RuntimeError('brightness observer requires launchd and its built dylib')
        put('lib/marble-brightness-observer.dylib',shim.read_bytes())
        conf=(support/'etc/inject.conf').read_text()
        conf='\n'.join('/redwood/lib/marble-brightness-observer.dylib:'+line if line.endswith(' backboardd') else line for line in conf.splitlines())+'\n'
        # Without input-qos the original rule also lists container managers;
        # keep the diagnostic in backboardd alone.
        if not a.input_qos:
            conf=conf.replace('/redwood/lib/libredwood.dylib backboardd containermanagerd containermanagerd_system',
                '/redwood/lib/marble-brightness-observer.dylib:/redwood/lib/libredwood.dylib backboardd\n/redwood/lib/libredwood.dylib containermanagerd containermanagerd_system')
        if a.brightness_ui_inventory:
            conf=conf.replace('/redwood/lib/libredwood-sb.dylib SpringBoard',
                '/redwood/lib/marble-brightness-observer.dylib:/redwood/lib/libredwood-sb.dylib SpringBoard')
        put('etc/inject.conf',conf)
    (support/'etc/ui-geometry-2x').unlink(missing_ok=True)
    (support/'etc/ui-wallpaper-reset').unlink(missing_ok=True)
    (support/'etc/ui-wallpaper-color').unlink(missing_ok=True)
    (support/'etc/ui-wallpaper-inspect').unlink(missing_ok=True)
    if a.ui_wallpaper_inspect:put('etc/ui-wallpaper-inspect','native-view-tree\n')
    (support/'etc/ui-open-settings').unlink(missing_ok=True)
    if a.ui_open_settings:put('etc/ui-open-settings','native-locale-acceptance-only\n')
    (support/'etc/ui-app-wait').unlink(missing_ok=True)
    if a.ui_app_wait:put('etc/ui-app-wait','bounded-unsuspended-thread-samples\n')
    (support/'etc/ui-ready-gate').unlink(missing_ok=True)
    if a.ui_ready_gate:put('etc/ui-ready-gate','native-did-finish-launch\n')
    (support/'etc/ui-statusbar-inspect').unlink(missing_ok=True)
    if a.ui_statusbar_inspect:put('etc/ui-statusbar-inspect','bounded-readonly-alpha\n')
    (support/'etc/ui-statusbar-gradient-alpha').unlink(missing_ok=True)
    if a.ui_statusbar_gradient_alpha:put('etc/ui-statusbar-gradient-alpha','qualified-transparent-colors-only\n')
    (support/'etc/ui-gesture-inspect').unlink(missing_ok=True)
    if a.ui_gesture_inspect:put('etc/ui-gesture-inspect','bounded-control-window-events-only\n')
    (support/'etc/ui-control-local-pan').unlink(missing_ok=True)
    if a.ui_control_local_pan:put('etc/ui-control-local-pan','fresh-visible-overlay-bottom-classification-only\n')
    (support/'etc/ui-wallpaper-static').unlink(missing_ok=True)
    (support/'share/wallpaper-static.png').unlink(missing_ok=True)
    if a.ui_wallpaper_static:
        put('etc/ui-wallpaper-static','native-secure-window-static-fallback\n')
        put('share/wallpaper-static.png',a.ui_wallpaper_static.read_bytes())
    if a.ui_wallpaper_color:put('etc/ui-wallpaper-color','native-blue-color\n')
    (support/'share/wallpaper.png').unlink(missing_ok=True)
    if a.ui_wallpaper_image:
        data=a.ui_wallpaper_image.read_bytes()
        if not data.startswith(b'\x89PNG\r\n\x1a\n') or len(data)>16*1024*1024:raise RuntimeError('wallpaper must be a PNG under 16 MiB')
        put('share/wallpaper.png',data)
    (support/'LaunchDaemons/com.marble.wallpaper.plist').unlink(missing_ok=True)
    (support/'bin/marble-wallpaper').unlink(missing_ok=True)
    if a.ui_wallpaper_image:
        put('bin/marble-wallpaper',(programs/'marble-wallpaper').read_bytes())
        put('LaunchDaemons/com.marble.wallpaper.plist',plistlib.dumps({
            'Label':'com.marble.wallpaper','ProgramArguments':['/redwood/bin/marble-wallpaper'],
            'UserName':'mobile','RunAtLoad':True,'LaunchOnlyOnce':True,'ProcessType':'Background',
            'StandardOutPath':'/dev/console','StandardErrorPath':'/dev/console'}))
    if a.ui_wallpaper_reset:put('etc/ui-wallpaper-reset','native-default-restore\n')
    if a.ui_layout_diagnostic or a.ui_geometry_2x:
        conf=(support/'etc/inject.conf').read_text()
        conf=conf.replace('/redwood/lib/libredwood-sb.dylib SpringBoard','/redwood/lib/marble-ui-layout.dylib:/redwood/lib/libredwood-sb.dylib SpringBoard')
        if a.ui_geometry_2x:
            put('etc/ui-geometry-2x','Split828\n')
            conf+='/redwood/lib/marble-ui-layout.dylib Preferences\n'
        if len(conf.encode())>=512:raise RuntimeError('inject.conf exceeds kernel capacity')
        put('etc/inject.conf',conf)
    if a.media_compat:
        conf=(support/'etc/inject.conf').read_text()+'/redwood/lib/libredwood-media.dylib mediaserverd\n'
        if len(conf.encode())>=512:raise RuntimeError('inject.conf exceeds kernel capacity')
        put('etc/inject.conf',conf)
    put('etc/rc',(ROOT/('userland/rc.ios' if a.mode=='cli' else 'userland/rc.launchd')).read_bytes().replace(b'\r\n',b'\n'))
    if a.idle_seconds:put('etc/rc',f'sleep {a.idle_seconds}\n'.encode()+(support/'etc/rc').read_bytes())
    if a.input_selftest:
        if a.mode!='cli' or not bridge.exists(): raise RuntimeError('input selftest requires CLI mode and a built bridge')
        rc=(support/'etc/rc').read_bytes()
        put('etc/rc',b'/redwood/bin/marble-touch-bridge --selftest\n'+rc)
    if a.cpu_test:
        if a.mode!='cli': raise RuntimeError('CPU benchmark requires CLI mode')
        put('etc/rc',b'/redwood/bin/cpu_test 1\n'+(support/'etc/rc').read_bytes())
    if a.cpu_stress:
        put('bin/marble-cpu-stress',(programs/'marble-cpu-stress').read_bytes())
        put('etc/rc',f'/redwood/bin/marble-cpu-stress {a.cpu_stress}\n'.encode()+(support/'etc/rc').read_bytes())
    if a.gpu_test:
        if a.mode!='cli' or not a.programs:raise RuntimeError('GPU test requires CLI mode and source programs')
        put('bin/marble-gl-test',(programs/'marble-gl-test').read_bytes())
        put('etc/rc',b'/redwood/bin/marble-gl-test\n'+(support/'etc/rc').read_bytes())
    put('etc/rc.jobs',(ROOT/'userland/rc.jobs').read_bytes().replace(b'\r\n',b'\n'))
    if a.mode=='launchd':
        jobs='''com.apple.notifyd com.apple.cfprefsd.xpc.daemon com.apple.cfprefsd.xpc.daemon.system
com.apple.logd com.apple.syslogd com.apple.distnoted.xpc.daemon com.apple.diagnosticd com.apple.fseventsd
com.apple.containermanagerd com.apple.containermanagerd.system com.apple.runningboardd
com.apple.backboardd com.apple.mobile.usermanagerd com.apple.mobilegestalt.xpc com.apple.lsd
com.apple.powerd com.apple.analyticsd com.apple.logd_helper com.apple.iconservices.iconservicesagent
com.apple.pluginkit.pkd com.apple.SpringBoard'''.split()
        subprocess.run([sys.executable,'userland/mkjobs.py','--rest','ios165/rootfs',str(support/'LaunchDaemons'),
                        *jobs],cwd=ROOT,check=True)
        put('LaunchDaemons/com.redwood.demo.plist',(ROOT/'userland/com.redwood.demo.plist').read_bytes())
        if a.ui_layout_diagnostic:
            job=support/'LaunchDaemons/com.apple.SpringBoard.plist';config=plistlib.loads(job.read_bytes())
            config.setdefault('EnvironmentVariables',{})['MARBLE_UI_LAYOUT']='1'
            job.write_bytes(plistlib.dumps(config))
        if a.gpu_profile!='default' or a.gpu_output_timing or a.gpu_error_poll or a.gpu_backdrop_grow or a.gpu_scale_await_compositor:
            job=support/'LaunchDaemons/com.apple.backboardd.plist'
            config=plistlib.loads(job.read_bytes())
            env=config.setdefault('EnvironmentVariables',{})
            env['MARBLE_GL_PROFILE']=a.gpu_profile
            if a.gpu_scale_await_compositor:env['MARBLE_GL_START_SIGNAL']='1'
            if a.gpu_output_timing:env['MARBLE_GL_OUTPUT_TIMING']='1'
            if a.gpu_error_poll:env['MARBLE_GL_ERROR_POLL']=a.gpu_error_poll
            if a.gpu_fetch_copy:env['MARBLE_GL_FETCH_COPY']='1'
            if a.gpu_backdrop_grow:env['MARBLE_GL_BACKDROP_GROW']='1'
            if a.gpu_profile in ('sysmem','nofetch-sysmem'):env['FD_MESA_DEBUG']='sysmem'
            job.write_bytes(plistlib.dumps(config,fmt=plistlib.FMT_BINARY))
        put('etc/marble-gpu-profile',a.gpu_profile+'\n')
        if bridge.exists():
            put('LaunchDaemons/com.marble.touch-bridge.plist','''<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>Label</key><string>com.marble.touch-bridge</string>
<key>ProgramArguments</key><array><string>/redwood/bin/marble-touch-bridge</string></array>
<key>RunAtLoad</key><true/><key>LaunchOnlyOnce</key><true/>
<key>ProcessType</key><string>Interactive</string>
<key>Nice</key><integer>-20</integer>
<key>StandardOutPath</key><string>/dev/console</string>
<key>StandardErrorPath</key><string>/dev/console</string>
</dict></plist>
''')
        if a.brightness_service:
            put('bin/marble-brightness-service',(programs/'marble-brightness-service').read_bytes())
            put('LaunchDaemons/com.marble.brightness-service.plist',plistlib.dumps({
                'Label':'com.marble.brightness-service','ProgramArguments':['/redwood/bin/marble-brightness-service'],
                'RunAtLoad':True,'LaunchOnlyOnce':True,'ProcessType':'Interactive',
                'StandardOutPath':'/dev/console','StandardErrorPath':'/dev/console'}))
        else:
            (support/'bin/marble-brightness-service').unlink(missing_ok=True)
            (support/'LaunchDaemons/com.marble.brightness-service.plist').unlink(missing_ok=True)
        put('etc/rc.jobs','/redwood/bin/cpu_test\n/redwood/bin/hello_dyn started by launchd\n/bin/ps -A\n/usr/bin/vm_stat\n')
        if a.gpu_present_scale:
            put('etc/rc.jobs',(support/'etc/rc.jobs').read_bytes()+
                b'/redwood/bin/mg_test main-screen-width main-screen-height main-screen-scale main-screen-class ArtworkDeviceSubType\n')
        gesture_job=support/'LaunchDaemons/com.marble.gesture-test.plist'
        if a.gesture_test or a.display_test:
            # Historical coordinate tests retain their old layout; locale
            # acceptance opens prefs: explicitly because app registration can
            # change the icon arrangement during startup.
            settings_x=663
            put('bin/rwtouch',(programs/'rwtouch').read_bytes())
            args=['/redwood/bin/rwtouch','wait','45']
            for _ in range(3):
                args+=['swipe','.85','.55','.15','.55','500','wait','2',
                       'swipe','.15','.55','.85','.55','500','wait','2']
            if a.display_test:
                args=['/redwood/bin/rwtouch','wait',str(a.display_warmup),'mark','pages-begin']
                for _ in range(3):
                    args+=['swipe','.85','.55','.15','.55','500','wait','.7',
                           'swipe','.15','.55','.85','.55','500','wait','.7']
                args+=['mark','pages-end','wait','4','tap',str(settings_x/1080),str(1700/2400),
                       'wait','3','mark','settings-begin']
                for _ in range(4):
                    args+=['swipe','.5','.8','.5','.3','700','wait','.6',
                           'swipe','.5','.3','.5','.8','700','wait','.6']
                args+=['mark','settings-end','wait','4','swipe','.5','.99','.5','.4','400','wait','2',
                       'mark','animation-begin']
                for _ in range(2):
                    args+=['tap',str(settings_x/1080),str(1700/2400),'wait','1.5',
                           'swipe','.5','.99','.5','.4','400','wait','1.5']
                args+=['mark','animation-end','wait','4','mark','complete']
                if a.display_case=='manual':
                    args=['/redwood/bin/rwtouch','wait','25','notify','com.redwood.unlock',
                          'wait','4','mark','manual-ready-end']
                elif a.display_case=='animation':
                    args=['/redwood/bin/rwtouch','wait',str(a.display_warmup),
                          'notify','com.redwood.unlock','wait','3','mark','native-ready-end','wait','3','mark','animation-begin']
                    for i in range(a.display_rounds):
                        # Let cold desktop/app startup settle before comparing
                        # transitions. Home avoids an ambiguous bottom-edge
                        # swipe being delivered as a Settings scroll.
                        args+=['mark',f'open-{i}-begin',
                               'tap',str(settings_x/1080),str(1700/2400),'wait','3.5',
                               'mark',f'open-{i}-end','mark',f'exit-{i}-begin',
                               'button','home','wait','3', 'mark',f'exit-{i}-end']
                    args+=['mark','animation-end','wait','4','mark','complete']
                elif a.display_case=='pages':
                    args=['/redwood/bin/rwtouch','wait',str(a.display_warmup),
                          'notify','com.redwood.unlock','wait','3','mark','native-ready-end','wait','3','mark','pages-begin']
                    for _ in range(a.display_rounds):
                        # Today -> home -> page two -> App Library -> home.
                        args+=['swipe','.15','.5','.85','.5','500','wait','.8',
                               'swipe','.85','.5','.15','.5','500','wait','.8',
                               'swipe','.85','.5','.15','.5','500','wait','.8',
                               'swipe','.85','.5','.15','.5','500','wait','.8',
                               'swipe','.15','.5','.85','.5','500','wait','.8',
                               'swipe','.15','.5','.85','.5','500','wait','.8']
                    args+=['mark','pages-end','wait','4','mark','complete']
                elif a.display_case=='panels':
                    args=['/redwood/bin/rwtouch','wait',str(a.panel_warmup),
                          'notify','com.redwood.unlock','wait','3','mark','native-ready-end','wait','3','mark','panels-begin']
                    for i in range(a.panel_rounds):
                        args+=['mark',f'notification-{i}-begin','swipe','.12','.015','.12','.85','650','wait','1.5',
                               'mark',f'notification-{i}-end','wait','1.5',
                               'mark',f'notification-close-{i}-begin','swipe','.5','.999','.5','.30','500','wait','1.5',
                               'mark',f'notification-close-{i}-end','wait','1.5',
                               'mark',f'control-{i}-begin','swipe','.9','.015','.9','.80','650','wait','1.5',
                               'mark',f'control-{i}-end','wait','1.5',
                               'mark',f'control-close-{i}-begin','swipe','.5','.999','.5','.30','500','wait','1.5',
                               'mark',f'control-close-{i}-end','wait','1.5']
                    args+=['mark','panels-end','wait','4','mark','complete']
                elif a.display_case=='power':
                    # Device-clock gestures isolate HID/UI wake behavior from
                    # the variable latency of sixteen separate USB commands.
                    # Each settled mark leaves time for a host capture.
                    args=['/redwood/bin/rwtouch','wait',str(a.display_warmup),
                          'mark','native-idle-end','wait','4',
                          'button','lock','wait','2','mark','native-black-end','wait','5',
                          'button','lock','wait','1','mark','native-wake-end','wait','1',
                          'swipe','.5','.99','.5','.33','350','wait','2',
                          'mark','native-unlock-end','wait','5',
                          'tap',str(settings_x/1080),str(1700/2400),'wait','3',
                          'mark','native-settings-end','wait','5',
                          'button','lock','wait','2','mark','native-final-black-end',
                          'wait','5','mark','complete']
                elif a.display_case=='locale':
                    args=['/redwood/bin/rwtouch','wait',str(a.display_warmup),
                          'notify','com.redwood.unlock','wait','3','mark','native-ready-end','wait','6',
                          'settings','wait','5','notify','com.marble.test.layers','mark','locale-settings-end','wait','3',
                          'swipe','.5','.8','.5','.4','500','wait','2','mark','locale-scroll-end','wait','3',
                          'button','home','wait','3','swipe','.5','.4','.5','.8','500','wait','3',
                          'mark','locale-keyboard-end','wait','3','button','home','wait','2','mark','complete']
                    if a.ui_open_settings:
                        at=args.index('settings');args[at:at+1]=['notify','com.marble.test.open-settings']
                if a.ui_ready_gate:
                    if args[1]!='wait':raise RuntimeError('native display test has no initial warmup')
                    args[1:3]=['wait-state','com.marble.ui.springboard-ready','50','wait','1']
                put('etc/marble-display-test.json',__import__('json').dumps(args,indent=2)+'\n')
            put('LaunchDaemons/com.marble.gesture-test.plist',plistlib.dumps({
                'Label':'com.marble.gesture-test','ProgramArguments':args,
                'RunAtLoad':True,'LaunchOnlyOnce':True,'ProcessType':'Interactive',
                'StandardOutPath':'/dev/console','StandardErrorPath':'/dev/console'}))
        elif gesture_job.exists():
            gesture_job.unlink()
        if not a.display_test:
            (support/'etc/marble-display-test.json').unlink(missing_ok=True)
        if len(list((support/'LaunchDaemons').glob('*.plist')))<380:
            raise RuntimeError('mkjobs.py lost on-demand jobs: apply tools/marble-mkjobs.patch to the userland checkout')
    tree=apfs.read_tree(str(support))
    def permissions(node,path=''):
        node.mode=(node.mode&~0o7777)|(0o755 if stat.S_ISDIR(node.mode) or path.startswith(('sbin/','bin/','lib/')) else 0o644)
        for n,c in node.children.items(): permissions(c,path+'/'+n if path else n)
    permissions(tree)
    def image(tree,path,name):
        spool=open(str(path)+'.spool','w+b')
        v=apfs.Volume(0,name,tree,True,1683900000*1000000000,spool,[])
        c=apfs.Container([v],256,path.name); c.layout(); c.write(str(path))
        spool.close(); Path(str(path)+'.spool').unlink()
        print(f'{path}: {path.stat().st_size} bytes; {v.counts}',flush=True)
    image(tree,a.support_output or out/f'marble-root-{a.mode}.apfs','redwood')
    if a.support_only: return
    root_manifest=out/'marble-system.manifest'
    root_manifest.write_text((ios/'system.manifest').read_text(encoding='utf8'),encoding='utf8',newline='\n')
    tree=apfs.read_tree(str(ios/'rootfs'))
    apfs.apply_manifest(tree,str(root_manifest))
    def directory(root,path):
        for part in path.split('/'):
            if part: root=root.children.setdefault(part,apfs.Node(part,stat.S_IFDIR|0o755))
        return root
    def link(path,target):
        parent,_,leaf=path.rpartition('/')
        directory(tree,parent).children[leaf]=apfs.Node(leaf,stat.S_IFLNK|0o777,target=target.encode())
    # Restore names containing characters that NTFS cannot represent (e.g. "*.example.com").
    # Keep the host's sanitized path only as the node's backing data source.
    for source in (ios/'system.files').read_text(encoding='utf8').splitlines():
        file=extracted_path(ios/'rootfs',source)
        if not file.is_file(): raise RuntimeError('missing extracted input '+str(file))
        if file.name!=source.rsplit('/',1)[-1]:
            parent,_,leaf=source.rpartition('/')
            node=directory(tree,parent)
            node.children.pop(file.name,None)
            node.children[leaf]=apfs.Node(leaf,stat.S_IFREG|0o644,source=str(file))
    # Move the apps selected from Apple's /var template to the system applications.
    private=tree.children.get('private')
    staged=private.children.get('var') if private else None
    if staged:
        apps=staged.children.get('staged_system_apps')
        if apps: directory(tree,'Applications').children.update(apps.children)
        # /private/var is a fresh ramfs made by init, never Android userdata.
        private.children['var']=apfs.Node('var',stat.S_IFDIR|0o755)
    apfs.apply_manifest(tree,str(root_manifest))
    if a.localization_overlay:
        import json
        overlay=a.localization_overlay.resolve()
        verified=json.loads((overlay/'verified.json').read_text())
        for item in verified['files']:
            source=extracted_path(overlay/'rootfs',item['source'])
            if not source.is_file() or source.stat().st_size!=item['bytes'] or hashlib.sha256(source.read_bytes()).hexdigest()!=item['sha256']:
                raise RuntimeError('localization resource differs: '+item['path'])
            parent,_,leaf=item['path'].rpartition('/')
            directory(tree,parent).children[leaf]=apfs.Node(leaf,stat.S_IFREG|0o644,source=str(source))
        apfs.apply_manifest(tree,str(overlay/'system.manifest'))
    if a.wallpaper_2x:
        # This IPSW includes 375/393/430-point wallpaper classes, but no 414@2.
        # Reuse Apple's procedural stripe renderer and its parameter tables.
        # No synthetic CA layer or frame overlay is inserted into SpringBoard.
        import copy
        wallpaper=ios/'rootfs/Library/Wallpaper'
        stripe=wallpaper/'Collections/Stripe.wallpaperCollection/Wallpapers'
        generated=(a.system_output.parent/(a.system_output.stem+'-wallpaper'))
        generated.mkdir(parents=True,exist_ok=False)
        for source in sorted(stripe.glob('*375w-812h@3x~iphone.wallpaper')):
            rel=source.relative_to(ios/'rootfs').as_posix();parent,_,name=rel.rpartition('/')
            original=directory(tree,parent).children[name]
            alias=copy.deepcopy(original);alias.name=name.replace('375w-812h@3x','414w-896h@2x')
            config=plistlib.loads((source/'Wallpaper.plist').read_bytes())
            config['logicalScreenClass']='414w-896h@2x~iphone'
            backing=generated/(alias.name+'.plist');backing.write_bytes(plistlib.dumps(config,fmt=plistlib.FMT_BINARY))
            alias.children['Wallpaper.plist'].source=str(backing)
            directory(tree,parent).children[alias.name]=alias
        backing=generated/'DefaultWallpapers~iphone.plist'
        defaults=plistlib.loads((wallpaper/'DefaultWallpapers~iphone.plist').read_bytes())
        defaults['default']={'wallpaperIdentifier':7390,'collectionIdentifier':'2939274C-F444-4AA8-806E-6E3FAFBA79B1'}
        backing.write_bytes(plistlib.dumps(defaults,fmt=plistlib.FMT_BINARY))
        directory(tree,'Library/Wallpaper').children[backing.name].source=str(backing)
        print('Alternate system: stock blue Stripe wallpaper, added 414x896@2 class',flush=True)
    library=directory(tree,'System/Library')
    for n in ['LaunchDaemons','NanoLaunchDaemons','LaunchAngels']: library.children.pop(n,None)
    directory(tree,'System/Library/xpc').children.pop('launchd.plist',None)
    link('System/Library/LaunchDaemons','/redwood/LaunchDaemons')
    sbin=directory(tree,'sbin')
    sbin.children['launchd.ios']=sbin.children.pop('launchd'); sbin.children['launchd.ios'].name='launchd.ios'
    sbin.children['launchd']=apfs.Node('launchd',stat.S_IFREG|0o755,source=str(pre/'stage0'))
    for d in ['redwood','dev','private/var','System/Volumes/iOS']: directory(tree,d)
    # Build cryptex ownership and symlinks from 7z metadata; no host symlink privilege required.
    for dirname,listing,dest in [('cryptex','cryptex-os.slt','OS'),('cryptex-app','cryptex-app.slt','App')]:
        child=apfs.read_tree(str(ios/dirname)); child.name=dest
        manifest=out/(dirname+'.manifest')
        lines=[]
        for e in select.entries(str(ios/listing)):
            mode=select.mode_bits(e['Mode'])
            if mode is None or e.get('Alternate Stream')=='+': continue
            lines.append(f"{mode:o}\t{e.get('User ID') or 0}\t{e.get('Group ID') or 0}\t{e['Path']}\t{e.get('Symbolic Link','')}\n")
        manifest.write_text(''.join(lines),encoding='utf8',newline='\n')
        apfs.apply_manifest(child,str(manifest))
        directory(tree,'System/Cryptexes').children[dest]=child
    # Check critical inputs before compressing the multi-GB image.
    for f in ['usr/lib/dyld','sbin/launchd','bin/ps']:
        if (ios/'rootfs'/f).stat().st_size<100: raise RuntimeError('missing input '+f)
    image(tree,a.system_output or out/'marble-system.apfs','System')

if __name__=='__main__': main()
