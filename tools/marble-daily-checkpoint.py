"""Snapshot the current GPU/display/daily baseline without claiming full acceptance."""
import argparse
import importlib.util
import json
from datetime import datetime, timezone
from pathlib import Path
import shutil
import tarfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('directory', type=Path)
    p.add_argument('--profile', choices=('power-v1','vsync-v13','daily-progress-v2','daily-progress-v3','animation-v4','animation-v5'), default='power-v1')
    p.add_argument('--evidence', action='append', type=Path, default=[])
    a = p.parse_args()
    spec = importlib.util.spec_from_file_location('checkpoint', ROOT/'tools/marble-checkpoint.py')
    checkpoint = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(checkpoint)
    target = a.directory.resolve()
    target.mkdir(parents=True, exist_ok=False)
    paths = [
        'out/marble-daily-buttons-v5-loader-boot.img',
        'out/marble-daily-buttons-v5-loader-boot.json',
        'out/loader-marble-daily-buttons-v5.efi',
        'out/linux-build/mach-marble-daily-power-v1',
        'out/linux-build/mach-marble-daily-power-v1.json',
        'out/linux-build/mach-marble-vsync-v10',
        'out/marble-root-daily-buttons-v1.apfs',
        'out/linux-build/userland-daily-v1',
        'out/daily-buttons-v3-gpu-live',
        'out/daily-buttons-v4-gpu-live',
        'out/daily-buttons-v5-gpu-live',
        'out/daily-power-v1-gpu-live',
        'out/daily-power-v1-plan',
        'out/daily-buttons-v5-plan',
        'out/n12turbo-rom/daily-material/firmware/manifest.json',
    ]
    validation_path='out/daily-power-v1-gpu-live/exercise.json'
    if a.profile in ('vsync-v13','daily-progress-v2','daily-progress-v3'):
        paths=[
            'out/marble-daily-gesture-v1-loader-boot.img',
            'out/marble-daily-gesture-v1-loader-boot.json',
            'out/loader-marble-daily-gesture-v1.efi',
            'out/linux-build/mach-marble-vsync-v13',
            'out/linux-build/mach-marble-vsync-v13.json',
            'out/marble-root-daily-buttons-v1.apfs',
            'out/linux-build/userland-daily-v1',
            'out/vsync-v13-plan', 'out/daily-gesture-v1-plan',
            'out/daily-dsi-v1-plan',
            'out/marble-daily-dsi-v1-loader-boot.img',
            'out/marble-daily-dsi-v1-loader-boot.json',
            'out/loader-marble-daily-dsi-v1.efi',
            'out/daily-dsi-read-v1-plan', 'out/daily-dsi-read-v2-plan', 'out/daily-dsi-dim-v1-plan',
            'out/marble-daily-dsi-read-v1-loader-boot.img',
            'out/marble-daily-dsi-read-v1-loader-boot.json',
            'out/loader-marble-daily-dsi-read-v1.efi',
            'out/marble-daily-dsi-read-v2-loader-boot.img',
            'out/marble-daily-dsi-read-v2-loader-boot.json',
            'out/loader-marble-daily-dsi-read-v2.efi',
            'out/marble-daily-dsi-dim-v1-loader-boot.img',
            'out/marble-daily-dsi-dim-v1-loader-boot.json',
            'out/loader-marble-daily-dsi-dim-v1.efi',
            'out/daily-display-fw-v1-plan', 'out/daily-dsp-v1-plan',
            'out/n12turbo-rom/daily-material/firmware/manifest.json',
        ]
        validation_path='out/vsync-v13-settings-focus-fb2-live/exercise.json'
        if a.profile in ('daily-progress-v2','daily-progress-v3'):
            paths += [
                'out/daily-brightness-runtime-v1-plan',
                'out/marble-daily-brightness-runtime-v1-loader-boot.img',
                'out/marble-daily-brightness-runtime-v1-loader-boot.json',
                'out/loader-marble-daily-brightness-runtime-v1.efi',
                'out/daily-brightness-api-v1-plan', 'out/daily-brightness-api-v2-plan',
                'out/marble-root-brightness-observer-v1.apfs',
                'out/marble-root-brightness-observer-v2.apfs',
                'out/marble-root-brightness-inventory-v1.apfs',
                'out/linux-build/userland-brightness-observer-v1',
                'out/linux-build/userland-brightness-observer-v2',
                'out/daily-brightness-api-v3-plan',
                'out/marble-root-brightness-requests-v1.apfs',
                'out/linux-build/userland-brightness-observer-v3',
                'out/daily-snapshot-v4-plan',
                'out/marble-daily-snapshot-v4-loader-boot.img',
                'out/marble-daily-snapshot-v4-loader-boot.json',
                'out/loader-marble-daily-snapshot-v4.efi',
                'out/daily-glink-v1-plan',
                'out/marble-daily-glink-v1-loader-boot.img',
                'out/marble-daily-glink-v1-loader-boot.json',
                'out/loader-marble-daily-glink-v1.efi',
                'out/vsync-v17-plan',
                'out/linux-build/mach-marble-vsync-v17',
                'out/linux-build/mach-marble-vsync-v17.json',
                'out/linux-build/vsync-v17-kernel.log',
                'out/daily-brightness-api-v4-plan', 'out/daily-brightness-api-v5-plan',
                'out/linux-build/userland-brightness-observer-v4',
                'out/linux-build/userland-brightness-observer-v5',
                'out/marble-root-brightness-requests-v2.apfs',
                'out/marble-root-brightness-hit-v1.apfs',
            ]
            for version,kernel in ((1,14),(2,15),(3,16)):
                paths += [f'out/daily-snapshot-v{version}-plan',
                    f'out/marble-daily-snapshot-v{version}-loader-boot.img',
                    f'out/marble-daily-snapshot-v{version}-loader-boot.json',
                    f'out/loader-marble-daily-snapshot-v{version}.efi',
                    f'out/linux-build/mach-marble-vsync-v{kernel}',
                    f'out/linux-build/mach-marble-vsync-v{kernel}.json']
            if a.profile == 'daily-progress-v3':
                paths += [
                    'out/daily-brightness-api-v6-plan', 'out/daily-brightness-api-v7-plan',
                    'out/daily-brightness-backend-v1-plan', 'out/daily-brightness-backend-v2-plan',
                    'out/daily-brightness-backend-v3-plan', 'out/daily-brightness-backend-v4-plan',
                    'out/daily-brightness-backend-support-v3-plan',
                    'out/daily-brightness-clock-v1-plan', 'out/marble-root-brightness-backend-v3.apfs',
                ]
                validation_path='out/vsync-v17-settings-clock-v1-live/exercise.json'
    if a.profile in ('animation-v4','animation-v5'):
        paths=[
            'out/daily-brightness-clock-v1-plan',
            'out/marble-root-brightness-backend-v3.apfs',
            'out/marble-root-animation-marked-v1.apfs',
            'out/marble-root-animation-marked-v2.apfs',
            'out/marble-root-scan-v1.apfs',
            'out/marble-root-nofetch-sysmem-v1.apfs',
            'out/linux-build/userland-brightness-backend-v3',
            'out/eagl-scan-v2-plan',
            'out/eagl-publish-v3-plan',
            'out/eagl-output-timing-v1-plan',
            'out/eagl-frame-errors-v1-plan',
            'out/eagl-fetch-copy-v1-plan',
            'out/marble-root-publish-v3.apfs',
            'out/marble-root-publish-v3-shared-fetch.apfs',
            'out/marble-root-output-timing-v1-fetch.apfs',
            'out/marble-root-frame-errors-v1-fetch.apfs',
            'out/marble-root-publish-v3-shared-sysmem.apfs',
            'out/marble-root-fetch-copy-v1.apfs',
            'out/animation-cadence-20261028.json',
            'out/animation-cadence-sysmem-20261028.json',
        ]
        for version in ('overlap-v2','queue-v3','cache-v4'):
            paths += [f'out/linux-build/mach-marble-gpu-{version}',
                      f'out/linux-build/mach-marble-gpu-{version}.json']
        paths += ['out/linux-build/gpu-cache-v4-kernel.log',
                  'out/linux-build/gpu-queue-v3-kernel.log']
        validation_path='out/gpu-cache-v4-settings-live/exercise.json'
        if a.profile=='animation-v5':
            paths += [
                'out/eagl-backdrop-grow-v1-plan', 'out/eagl-copy-timing-v1-plan',
                'out/eagl-copy-timing-v2-plan', 'out/marble-root-fetch-copy-v1-frame-errors.apfs',
                'out/marble-root-fetch-copy-grow-v1.apfs', 'out/marble-root-fetch-copy-timing-v1.apfs',
                'out/marble-root-fetch-copy-grow-timing-v2.apfs',
            ]
            validation_path='out/gpu-cache-v4-fetch-copy-frame-errors-animation-live/exercise.json'
    for name in paths:
        src, dst = ROOT/name, target/name
        dst.parent.mkdir(parents=True, exist_ok=True)
        if src.is_dir():
            shutil.copytree(src, dst)
        else:
            shutil.copyfile(src, dst)
    external={}
    for directory in a.evidence:
        directory=directory.resolve()
        if not directory.is_relative_to(ROOT/'out') or not directory.is_dir():
            raise ValueError('evidence must be a directory inside out')
        for src in sorted(directory.rglob('*')):
            if not src.is_file():continue
            # Preserve compact evidence in the checkpoint, and pin the original
            # large recording/framebuffer downloads without copying them again.
            if src.name=='recording.bin' or src.suffix=='.bgra':
                external[str(src)]=dict(bytes=src.stat().st_size,sha256=checkpoint.digest(src))
                continue
            dst=target/src.relative_to(ROOT)
            dst.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(src,dst)
    sources=checkpoint.sources()
    required={path.relative_to(ROOT).as_posix() for path in (ROOT/'xnuloader').glob('marble_*.h')}
    missing=required-sources.keys()
    if missing:raise RuntimeError('recovery source headers missing from Git whitelist: '+', '.join(sorted(missing)))
    with tarfile.open(target/'sources.tar.gz', 'w:gz') as tf:
        for name, (path, mode) in sorted(sources.items()):
            if not path.is_file():
                raise RuntimeError('missing source: '+str(path))
            info = tf.gettarinfo(str(path), arcname=name)
            info.mode = 0o755 if mode == '100755' else 0o644
            if mode == '120000':
                info.type = tarfile.SYMTYPE
                info.linkname = path.read_text().strip() if not path.is_symlink() else str(path.readlink())
                info.size = 0
                tf.addfile(info)
            else:
                with path.open('rb') as f:
                    tf.addfile(info, f)
    (target/'kernel.patch').write_bytes(checkpoint.git(ROOT, 'diff', '--binary', 'HEAD'))
    data = dict(created_utc=datetime.now(timezone.utc).isoformat(),
        policy='fastboot boot only; RAM APFS; no Android userdata access',
        profile=a.profile,
        source_scope='current merged source snapshot; historical build manifests identify each binary revision',
        validation=json.loads((ROOT/validation_path).read_text()),
        limits=['physical button edges untested', 'volume state not verified',
                'single synthetic wake/unlock passed; physical key edges untested', 'pixel black only; no panel power or suspend',
                'brightness, battery, audio, network, sensors incomplete',
                'counter polling and blank-window copy; no IRQ/pageflip or optical proof'],
        external_dependencies=external, files={})
    if a.profile in ('daily-progress-v3','animation-v4','animation-v5'):
        data['limits'] = [
            'physical buttons and real suspend/wake not fully validated',
            'brightness is a limited experimental bridge; automatic brightness unimplemented',
            'session clock only; RTC and timezone unchanged',
            'battery, audio, network and sensors incomplete',
            'counter polling and blank-window copy; no IRQ/pageflip or optical proof',
        ]
        if a.profile in ('animation-v4','animation-v5'):
            data['limits'] += [
                'nofetch exits approximately 21-23 completed RAM compositions per second; shared-fetch selected exit approximately 42; stable 60 FPS unachieved',
                'native Home test is not reliable; queue-v3 first exit was missed and comparison excluded',
                'sysmem is an experiment; refer to its own evidence before use',
                'current source includes compact GL statistics and scan profiling beyond cache-v4 kernel changes',
                'native shared-fetch rejected: Control Center black triangles reproduced with both default tiling and sysmem',
                'fetch-copy is a separate candidate; host pixel tests are not Adreno/display validation',
            ]
        if a.profile=='animation-v5':
            data['limits'] += [
                'fetch-copy selected exits approximately 31-38 completed RAM compositions per second; no stable 60 FPS claim',
                'grow-v1 test stopped at die 55 C after two actions; thermal acceptance failed and immediate retry was not attempted',
                'grow/PBO-fix and copy-cost diagnostics v2 built and host-tested only; device performance and rendering unqualified',
                'default nofetch profile retained; native-fetch with black triangles remains rejected',
            ]
    for name in ('out/marble-system.apfs',):
        src = ROOT/name
        data['external_dependencies'][str(src)] = dict(bytes=src.stat().st_size, sha256=checkpoint.digest(src))
    for path in sorted(target.rglob('*')):
        if path.is_file():
            data['files'][path.relative_to(target).as_posix()] = dict(bytes=path.stat().st_size, sha256=checkpoint.digest(path))
    (target/'manifest.json').write_text(json.dumps(data, indent=2)+'\n', encoding='utf8')
    print('Saved', len(data['files']), 'files:', target)


if __name__ == '__main__':
    main()
