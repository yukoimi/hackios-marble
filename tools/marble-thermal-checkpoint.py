"""Save the temperature-guarded GPU candidate without asserting acceptance."""
import argparse, importlib.util, json, shutil, subprocess, tarfile
from datetime import datetime, timezone
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('checkpoint',ROOT/'tools/marble-checkpoint.py')
checkpoint=importlib.util.module_from_spec(spec);spec.loader.exec_module(checkpoint)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('directory',type=Path)
    p.add_argument('--evidence',action='append',type=Path,default=[])
    p.add_argument('--efi-idle',action='store_true',help='also preserve the selected EFI event-wait revision and references')
    p.add_argument('--efi-idle-revision',choices=('v1','v2','v3'),default='v1')
    a=p.parse_args();target=a.directory.resolve()
    items=set(map(Path,[
        'out/marble-thermal-monitor-loader-boot.img','out/marble-thermal-monitor-loader-boot.json',
        'out/loader-marble-thermal.efi','out/marble-root-thermal-idle.apfs',
        'out/marble-thermal-idle-minimal.apfs','out/marble-thermal-idle-minimal.json',
        'out/linux-build/marble-thermal-idle',
        'out/marble-idle-probe-loader-boot.img','out/marble-idle-probe-loader-boot.json',
        'out/loader-marble-idle-probe.efi',
        'out/linux-build/mach-marble-idle-fix','out/linux-build/mach-marble-idle-fix.json',
        'out/idle-fix-source.tar.gz','out/linux-build/kernel-idle-fix.log',
        'out/marble-root-panels-scissor-short.apfs',
        'out/marble-root-panels-scissor.apfs','out/panels-scissor-candidate.json',
        'out/linux-build/mach-marble-gpu-service-v2','out/linux-build/mach-marble-gpu-service-v2.json',
        'out/gpu-service-v2-source.tar.gz','out/gpu-panels-scissor-userland-source.tar.gz',
        'out/marble-cpucap-v2-loader-boot.img','out/marble-cpucap-v2-loader-boot.json',
        'out/loader-marble-cpucap-v2.efi','out/cpucap-v3-source.tar.gz','out/cpucap-reference.tar.gz',
        'out/linux-build/mach-marble-cpucap-v3','out/linux-build/mach-marble-cpucap-v3.json',
        'out/linux-build/kernel-cpucap-v3.log',
    ]))
    if a.efi_idle:
        revision=a.efi_idle_revision
        items.update(map(Path,[
            f'out/loader-marble-efi-idle-{revision}.efi',f'out/marble-efi-idle-{revision}-loader-boot.img',
            f'out/marble-efi-idle-{revision}-loader-boot.json','out/efi-idle-v1-reference.tar.gz',
        ]))
    directories=[ROOT/'tools',ROOT/'docs',ROOT/'out/linux-build/userland-panels-scissor',
                 ROOT/'out/backdrop-host-test']
    for directory in a.evidence:
        directory=directory.resolve()
        if not directory.is_relative_to(ROOT/'out') or not directory.is_dir():
            raise ValueError('evidence must be an existing directory inside out')
        directories.append(directory)
    for directory in directories:
        items.update(path.relative_to(ROOT) for path in directory.rglob('*')
                     if path.is_file() and '__pycache__' not in path.parts)
    for item in items:
        if not (ROOT/item).is_file():raise FileNotFoundError(item)
    target.mkdir(parents=True,exist_ok=False)
    for item in sorted(items):
        destination=target/item;destination.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(ROOT/item,destination)
    with tarfile.open(target/'host-sources.tar.gz','w:gz') as tf:
        entries=checkpoint.sources()
        # Merged userspace sources are ignored by the kernel repository.
        # Include new local files as well as those tracked by its upstream.
        for path in (ROOT/'userland/src').rglob('*'):
            if path.is_file():
                name=path.relative_to(ROOT).as_posix()
                entries.setdefault(name,(path,'100644'))
        for name,(path,mode) in sorted(entries.items()):
            info=tf.gettarinfo(str(path),arcname=name)
            info.uid=info.gid=0;info.uname=info.gname='';info.mode=int(mode[-3:],8)
            if mode=='120000':
                info.type=tarfile.SYMTYPE
                info.linkname=str(path.readlink()) if path.is_symlink() else path.read_text().strip()
                info.size=0;tf.addfile(info)
            elif info.isfile():
                with path.open('rb') as f:tf.addfile(info,f)
            else:tf.addfile(info)
    (target/'kernel.patch').write_bytes(checkpoint.git(ROOT,'diff','--binary','HEAD'))
    (target/'RESTORE.md').write_text('''# Temperature-guarded marble experiment

This is a recovery point for an experiment, not GPU/VSync/daily-use acceptance.
Keep all earlier baselines. Verify with tools/marble-checkpoint.py DIRECTORY --verify.
Only temporary fastboot boot and RAM APFS; no partition or Android /data access.

Latest CPU-cap experiment: read docs/MARBLE-CPU-THERMAL.md first.
Use marble-cpucap-v2-loader-boot.img with mach-marble-cpucap-v3 only with
--cpu-cap. The 18-second four-core small black idle test passed at 39.2 C.
Full GPU-disabled iOS startup triggered the conservative 45 C die stop at
46.3 C; CPU requests restored and Android screen OFF. Battery after return
was 30.9 C, not a continuous in-iOS measurement. Further startup/GPU attempts
on this same candidate are blocked; only the small black idle profile remains.
Do not delete manifest failure fields to retry. No new GPU acceptance.
Source/reference archives and exact original refusal/stop logs are retained.
For the audited idle profile only:
tools/marble-thermal-idle-test.py --out out/NEW-DIRECTORY --cpus 4 --cpu-cap
  --kernel out/linux-build/mach-marble-cpucap-v3

Earlier idle-handshake experiment (retained independently):
Use out/marble-idle-probe-loader-boot.img with the matching manifest.
The system APFS is an external dependency with its original path/hash below.
First validate the minimal static idle image without GPU (one core, 18 seconds,
out/linux-build/mach-marble-idle-fix, opt-in diagnostics, black framebuffer):
tools/marble-thermal-idle-test.py --out out/NEW-EVIDENCE-DIRECTORY.
It needs no 2.6 GB upload and must use a fresh loader without a resident disk.
It is not hardware-qualified if the cold preflight refuses to start.
The UP initial-signal handshake fixes SIGPdisabled blocking WFI. The first
diagnostic candidate trapped on CNTP_CTL_EL0 and is NOT a usable kernel.
Only virtual timer registers already used by the working kernel are now read.
Black framebuffer pixels do not mean DSI/panel power-off has been implemented.
The later GPU startup reached 53.8 C before visual acceptance, host stop was
requested and the device reappeared in Android without a complete GPU release
log. Its manifest blocks further GPU runtime; do not re-run it or mark it PASS.
The host 45 C stop is reactive sampling, not a guaranteed maximum temperature.
Android must be left screen-off after testing. GPU runs require a fresh idle
loader, valid CPU/GPU readings strictly below 40 C,
runtime samples below 55 C, and at most 90 seconds; never bypass these guards.
After stop, require GPU drained/released active=0 and loader returned before reboot.
Return to Android for cooling and offline review. Never automatically retry a thermal stop.

The scissor-copy EAGL candidate preserves vibrant colour matrices and backdrop blur.
Host Mesa tests are not Adreno/optical/thermal certification. See evidence JSON and
docs/MARBLE-GPU.md for the exact observed outcomes and outstanding failures.
No hardware-vblank or new daily-use hardware support is claimed by this checkpoint.
''',encoding='utf8')
    system=ROOT/'out/marble-system.apfs'
    manifest=dict(created_utc=datetime.now(timezone.utc).isoformat(),
        policy='temporary boot; RAM only; no Android userdata access',
        experiment='post-CPU_ON reversible big/prime request cap; short idle PASS; iOS CPU-only conservative guard stop; no new GPU run',
        gpu_complete=False,hardware_vsync_complete=False,daily_functions_complete=False,
        visual_pass=None,transient_artifacts_pass=None,
        evidence=[str(path.relative_to(ROOT)) for path in directories[4:]],
        external_dependencies=[dict(path=str(system),bytes=system.stat().st_size,
            sha256=checkpoint.digest(system),copied=False)],files={})
    if a.efi_idle:
        manifest['experiment']='EFI local event-stream wait experiment; read selected evidence for actual qualification; CPU-cap history retained'
        manifest['efi_idle']=dict(hardware_tested='see evidence; not inferred by checkpoint tool',revision=a.efi_idle_revision,
            loader=f'out/marble-efi-idle-{a.efi_idle_revision}-loader-boot.img',
            reference='docs/MARBLE-EFI-IDLE.md',
            unchanged_kernel='out/linux-build/mach-marble-cpucap-v3',
            preserves_stopped_profile=True)
        with (target/'RESTORE.md').open('a',encoding='utf8') as f:
            f.write(f'''
Selected EFI wait revision: {a.efi_idle_revision}. Read docs/MARBLE-EFI-IDLE.md
and the selected experiment evidence before use. This snapshot does not unlock
the stopped iOS/GPU profile. No new kernel is inferred by the checkpoint tool.
Only the loader-only probe and the small black sleeping-init profile are in
scope. v3 additionally supports the explicit --short-thermal --battery-preflight
profile, pinned to 18 seconds and the exact sleeping-init RAM disk. The old
40/45 C results remain evidence of the earlier experimental rules; they do not
establish battery overheating. Preserve all earlier recovery points and logs.
''')
    for path in sorted(target.rglob('*')):
        if path.is_file():manifest['files'][path.relative_to(target).as_posix()]=dict(
            bytes=path.stat().st_size,sha256=checkpoint.digest(path))
    (target/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf8')
    subprocess.run(['python',str(ROOT/'tools/marble-checkpoint.py'),str(target),'--verify'],check=True)
    print(target)


if __name__=='__main__':main()
