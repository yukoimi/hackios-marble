"""Preserve the bounded GPU display candidate, keeping visual uncertainty explicit."""
import argparse, importlib.util, json, shutil, subprocess, tarfile
from datetime import datetime, timezone
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('checkpoint',ROOT/'tools/marble-checkpoint.py')
checkpoint=importlib.util.module_from_spec(spec);spec.loader.exec_module(checkpoint)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('directory',type=Path);a=p.parse_args()
    target=a.directory.resolve()
    report=json.loads((ROOT/'out/marble-display-v2-report.json').read_text())
    if report['passed'] is not True or len(report['runs'])!=6:
        raise ValueError('six audited display runs required; visual acceptance remains separate')
    items=[
        'out/marble-display-loader-boot.img','out/marble-display-loader-boot.json',
        'out/linux-build/mach-marble-gpu-service-v2','out/linux-build/mach-marble-gpu-service-v2.json',
        'out/gpu-service-v2-source.tar.gz','out/gpu-display-v2-userland-source.tar.gz',
        'out/marble-root-display-v2-launchd.apfs','out/marble-root-display-v2-full.apfs',
        'out/marble-root-display-v2-pages.apfs','out/marble-root-animation-nofetch-shared-retest.apfs',
        'out/marble-root-pages-nofetch-shared.apfs','out/marble-display-v2-report.json',
        'out/display-v2-animation-comparison.png','out/marble-display-v2-device-state.json',
    ]
    for directory in ['tools','docs','out/linux-build/userland-display-v2','out/display-v2-normal']:
        items += [str(path.relative_to(ROOT)) for path in (ROOT/directory).rglob('*')
                  if path.is_file() and '__pycache__' not in path.parts]
    for run in report['runs']:
        directory=Path(run['log']).parent
        if not directory.is_absolute():directory=ROOT/directory
        items += [str(path.relative_to(ROOT)) for path in directory.rglob('*') if path.is_file()]
    for item in items:
        if not (ROOT/item).is_file():raise FileNotFoundError(item)
    system=ROOT/'out/marble-system.apfs'
    external=dict(path=str(system),bytes=system.stat().st_size,sha256=checkpoint.digest(system),copied=False)
    target.mkdir(parents=True,exist_ok=False)
    for item in sorted(set(items)):
        destination=target/item;destination.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(ROOT/item,destination)
    with tarfile.open(target/'host-sources.tar.gz','w:gz') as tf:
        for name,(path,mode) in sorted(checkpoint.sources().items()):
            if not path.is_file():raise FileNotFoundError(path)
            info=tf.gettarinfo(str(path),arcname=name);info.uid=info.gid=0;info.uname=info.gname=''
            info.mode=int(mode[-3:],8);tf.addfile(info,path.open('rb'))
    readme='''# Marble GPU display candidate

Temporary fastboot boot and RAM APFS only; no Android userdata access or partition changes.
GPU lifecycle/evidence checks passed in six bounded UI runs. Recorded transitions improved;
intermittent green/black artifacts are NOT certified eliminated. No optical, physical touch,
in-test temperature or hardware-vblank certification. Keep the earlier baseline separately.

From the restored workspace with the original dependencies and an already unlocked marble:

    python tools/marble-run.py boot --image out/marble-display-loader-boot.img
    python tools/marble-run.py ramdisk out/marble-system.apfs
    python tools/marble-run.py run --kernel out/linux-build/mach-marble-gpu-service-v2 --ramdisk out/marble-root-display-v2-launchd.apfs --cpus 4 --seconds 180 --fb-threads 2 --input-mailbox --gpu-probe 17 --gpu-runtime --gpu-composition --detach

Enter HackiOS USB loader if the EFI menu appears. Touch-probe runs automatically before XNU.
Wait for returned, save the log, confirm GPU release, then reboot to Android.
The 2.6GB system APFS is an external dependency; see manifest for its original path/hash.
Validate every copied file with tools/marble-checkpoint.py DIRECTORY --verify.
'''
    (target/'RESTORE.md').write_text(readme,encoding='utf8')
    manifest=dict(created_utc=datetime.now(timezone.utc).isoformat(),
                  policy='temporary boot and RAM APFS; no Android userdata access',
                  renderer='FD725 / Mesa 25.0.7 / CoreAnimation GLES',
                  profile='shared inputs/outputs, fetch withheld; legacy software pacing',
                  audited_runs=6,visual_pass=None,transient_artifacts_pass=None,
                  limits='Bounded recorded source surfaces; no optical/physical-touch/in-test-temperature certification',
                  external_dependencies=[external],files={})
    for path in sorted(target.rglob('*')):
        if path.is_file():manifest['files'][path.relative_to(target).as_posix()]=dict(bytes=path.stat().st_size,sha256=checkpoint.digest(path))
    (target/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf8')
    subprocess.run(['python',str(ROOT/'tools/marble-checkpoint.py'),str(target),'--verify'],check=True)
    print('Saved display candidate:',target)


if __name__=='__main__':main()
