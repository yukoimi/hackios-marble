"""Archive the Chinese/static-wallpaper and peripheral work, including failures.

This is an in-progress checkpoint, not a claim that peripherals or 60 FPS work.
"""
import argparse,hashlib,importlib.util,json,shutil,tarfile
from datetime import datetime,timezone
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('directory',type=Path);a=p.parse_args();target=a.directory.resolve()
    if not target.is_relative_to(ROOT/'out') or target.exists():p.error('new directory under out required')
    spec=importlib.util.spec_from_file_location('ui_checkpoint_base',ROOT/'tools/marble-checkpoint.py');base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
    source_entries=base.sources()
    # Fail before copying gigabytes if a new loader header is still hidden by
    # the inherited /xnuloader/* ignore rule. Build snapshots alone are not
    # a substitute for a complete current-source archive.
    required={x.relative_to(ROOT).as_posix() for x in (ROOT/'xnuloader').glob('marble_*.h')}
    missing=required-source_entries.keys()
    if missing:raise ValueError('source closure missing: '+', '.join(sorted(missing)))
    evidence=['out/daily-adc-v4-discovery-live','out/daily-adc-read-v5-cool-discovery-live',
        'out/daily-adc-read-v6-cool-discovery-live','out/daily-pil-install-v7-discovery-live',
        'out/daily-pil-config-v8-discovery-live','out/daily-pil-ramfs-v9c-discovery-live',
        'out/daily-pil-adsp-v10b-cycle-live','out/daily-pil-memory-v11b-discovery-live',
        'out/daily-pil-header-v12b-discovery-live','out/daily-pil-elf-v13-discovery-live',
        'out/ui-wallpaper-tree-v15-panels-live','out/ui-wallpaper-static-v16-panels-live',
        'out/ui-locale-v17-live','out/ui-locale-v18b-live','out/ui-locale-v19-live','out/ui-locale-v20-live','out/ui-locale-v21b-live',
        'out/ui-app-scene-v22-live','out/ui-app-layer-v23-live','out/ui-app-host-v24b-live','out/ui-cpuscale-v25-live',
        'out/ui-refresh-v26-live','out/ui-pixels-v27-live','out/ui-shared-vm-v28-live',
        'out/daily-scm-inventory-v14-live','out/daily-scm-availability-v15-live',
        'out/daily-pil-metadata-v16-live','out/daily-pil-metadata-v16b-live','out/daily-pil-PAS-metadata-v17-live',
        'out/ui-delay-v29-live','out/ui-gate-v31-live','out/ui-panels-v32-live','out/ui-ready-v33-live',
        'out/daily-pil-PAS-memory-v18-live','out/daily-pil-PAS-memory-v18b-live','out/daily-pil-PAS-memory-v18c-live',
        'out/daily-pil-segment-v19b-live','out/daily-pil-all-segments-v20c-live','out/daily-pil-all-segments-v20e-live',
        'out/daily-pil-all-segments-v20f-live','out/daily-display-memory-v21b-live',
        'out/daily-pil-display-qualified-v22-live','out/daily-pil-proxy-config-v24-live','out/daily-pil-proxy-resources-v25b-live',
        'out/daily-npa-inventory-v26-live','out/daily-npa-resources-v27-live','out/daily-npa-resources-v27b-live','out/daily-npa-resources-v27c-live',
        'out/ui-desktopgate-v34-live','out/ui-statusbar-v35-live','out/ui-gradient-v36-live',
        'out/ui-gradient-ready-v37-live','out/ui-gradient-v38b-live','out/ui-control-v39b-live',
        'out/ui-control-lean-v40-live','out/ui-control-state-v41b-live','out/ui-control-edge-v41c-live',
        'out/ui-control-local-v42-live','out/ui-control-home-v42b-live',
        'out/daily-npa-clients-v28-live','out/daily-npa-votes-v29-live','out/daily-npa-readback-v30b-live',
        'out/daily-npa-vcs-v31-live','out/daily-npa-native-release-v32-live',
        'out/daily-pil-powered-v33e-live','out/daily-pil-bounded-boot-v34b-live',
        'out/daily-smp2p-prepare-v35b-live','out/daily-smp2p-notify-v36-live',
        'out/daily-pil-smp2p-stage-v37-live','out/daily-pil-smp2p-boot-v38-live',
        'out/upstream-display-panels-v1-live','out/upstream-display-panel-focus-v2-live',
        'out/upstream-display-pages-v3-live']
    for name in evidence:
        report=json.loads((ROOT/name/'exercise.json').read_text())
        if not report.get('android_screen_off'):raise ValueError('unfinished/unconfirmed phone recovery: '+name)
    target.mkdir(parents=True)
    directories=['out/gpu-scale-v4-layout-plan','out/daily-adc-read-v6-plan','out/daily-pil-ramfs-v9c-plan',
        'out/daily-pil-adsp-v10b-plan','out/daily-pil-memory-v11b-plan','out/daily-pil-header-v12b-plan',
        'out/daily-pil-elf-v13-plan','out/ui-wallpaper-static-v16-plan','out/ui-locale-native-v20-plan','out/ui-locale-wait-v21b-plan','out/scm-native-v1-plan',
        'out/pil-noauto-v2-plan','out/pil-header-only-v3-reloc-verified-plan','out/pil-ram-files-v1-plan',
        'out/ios-zh-v1','out/pmic-direct-v1-plan','out/pil-reuse-v1-plan',
        'out/ui-app-scene-v22-plan','out/ui-app-layer-v23-plan','out/ui-app-host-v24b-plan',
        'out/scale-import-refresh-v26-plan','out/scale-live-pixels-v27-plan','out/gpu-shared-vm-v28-plan',
        'out/scale-deferred-open-v29-plan','out/daily-scm-inventory-v14-plan','out/daily-scm-availability-v15-plan',
        'out/daily-pil-metadata-v16-plan','out/daily-pil-PAS-metadata-v17-plan','out/daily-pil-PAS-memory-v18-plan',
        'out/eagl-scale-gate-v31-plan','out/scale-compositor-gate-v31b-plan','out/ui-ready-v33-plan',
        'out/daily-pil-segment-v19-plan','out/daily-pil-segment-v19b-plan',
        'out/daily-pil-all-segments-v20-plan','out/daily-pil-all-segments-v20b-plan','out/daily-pil-all-segments-v20c-plan',
        'out/daily-pil-all-segments-v20d-plan','out/daily-pil-all-segments-v20e-plan','out/daily-pil-all-segments-v20f-plan',
        'out/display-demura-audit-v1-plan','out/daily-display-memory-v21-plan','out/daily-display-memory-v21b-plan','out/daily-display-memory-v21c-plan',
        'out/daily-pil-display-qualified-v22-plan','out/daily-pil-word-check-v23-plan','out/daily-pil-proxy-config-v24-plan',
        'out/daily-pil-proxy-resources-v25-plan','out/daily-pil-proxy-resources-v25b-plan','out/npa-abi-v1-plan','out/npa-vcs-audit-v1-plan',
        'out/daily-npa-inventory-v26-plan','out/daily-npa-resources-v27-plan','out/daily-npa-resources-v27b-plan','out/daily-npa-resources-v27c-plan',
        'out/ui-statusbar-v35-plan','out/ui-gradient-alpha-v36-plan','out/ui-gradient-ready-v37-plan','out/ui-gradient-stops-v38-plan',
        'out/ui-gradient-timezone-v38b-plan','out/ui-control-observer-v39-plan','out/ui-timezone-v1-plan','out/wallpaper-static-v2-plan',
        'out/ui-control-state-v41-plan','out/ui-control-state-v41b-plan','out/ui-control-local-v42-plan',
        'out/daily-npa-clients-v28-plan','out/daily-npa-votes-v29-plan','out/daily-npa-safe-v30-plan','out/daily-npa-readback-v30b-plan',
        'out/daily-npa-vcs-v31-plan','out/daily-npa-native-release-v32-plan',
        'out/daily-pil-powered-v33-plan','out/daily-pil-powered-v33b-plan',
        'out/daily-pil-powered-v33c-plan','out/daily-pil-powered-v33d-plan','out/daily-pil-powered-v33e-plan',
        'out/daily-pil-bounded-boot-v34-plan','out/daily-pil-bounded-boot-v34b-plan',
        'out/dsp-smp2p-v1-plan','out/ipcc-native-v1-plan',
        'out/daily-smp2p-prepare-v35-plan','out/daily-smp2p-prepare-v35b-plan',
        'out/daily-smp2p-notify-v36-plan','out/daily-pil-smp2p-stage-v37-plan',
        'out/daily-pil-smp2p-boot-v38-plan','out/daily-upstream-audit-v39-plan',
        'out/upstream-docs-20261029','out/upstream-display-bounds-v1-plan',
        'out/upstream-display-bounds-v2-plan','out/upstream-display-bounds-v3-plan']
    names=['docs/MARBLE-UI-PERIPHERALS-20261029.md','docs/MARBLE-UPSTREAM-DOCS-20261029.md','out/marble-system-zh-v1.apfs',
        'out/marble-root-ui-locale-v19.apfs','out/marble-root-ui-locale-v20.apfs','out/marble-root-ui-locale-v21b.apfs',
        'out/marble-root-ui-media-v9.apfs','out/ui-settings-glyph-2x-v1.png',
        'out/marble-root-ui-scene-v22.apfs','out/marble-root-ui-layer-v23.apfs','out/marble-root-ui-host-v24b.apfs',
        'out/marble-root-ui-cpuscale-v25.apfs','out/marble-root-ui-refresh-v26.apfs','out/marble-root-ui-pixels-v27.apfs','out/marble-root-ui-delay-v29.apfs',
        'out/marble-root-ui-gate-v31.apfs','out/marble-root-ui-panels-v32.apfs','out/marble-root-ui-ready-v33.apfs',
        'out/marble-root-ui-desktopgate-v34.apfs','out/marble-root-ui-statusbar-v35.apfs','out/marble-root-ui-gradient-v36.apfs',
        'out/marble-root-ui-gradient-ready-v37.apfs','out/marble-root-ui-gradient-v38b.apfs',
        'out/marble-root-ui-control-v39.apfs','out/marble-root-ui-control-v39b.apfs',
        'out/marble-root-ui-control-lean-v40.apfs','out/marble-root-ui-control-state-v41b.apfs','out/marble-root-ui-control-local-v42.apfs',
        'out/daily-pil-config-v8-report.json','out/ui-media-v14-panel-comparison.json',
        'out/peripheral-material-v2-plan/catalog.json','out/wallpaper-static-v1-plan/source.json',
        'out/wallpaper-static-v1-plan/stock-background-2x.png']
    for directory in directories:names += [x.relative_to(ROOT).as_posix() for x in (ROOT/directory).rglob('*') if x.is_file()]
    # Firmware inputs are local and private; include only the exact files from
    # the packed/verified ROM manifest, rather than the complete Android ROM.
    material=ROOT/'out/n12turbo-rom/daily-material/firmware'
    names.append('out/n12turbo-rom/daily-material/firmware/manifest.json')
    for row in json.loads((ROOT/'out/pil-ram-files-v1-plan/manifest.json').read_text())['files']:
        names.append((material/'image'/row['name']).relative_to(ROOT).as_posix())
    for name in sorted(set(names)):
        source=ROOT/name
        if not source.is_file():raise FileNotFoundError(source)
        destination=target/name;destination.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,destination)
    with tarfile.open(target/'sources.tar.gz','w:gz',compresslevel=3) as tf:
        for name,(path,mode) in sorted(source_entries.items()):
            info=tf.gettarinfo(str(path),arcname=name);info.uid=info.gid=0;info.uname=info.gname='';info.mode=0o755 if mode=='100755' else 0o644
            if mode=='120000':
                info.type=tarfile.SYMTYPE;info.linkname=str(path.readlink()) if path.is_symlink() else path.read_text().strip();info.size=0;tf.addfile(info)
            else:
                with path.open('rb') as stream:tf.addfile(info,stream)
    with tarfile.open(target/'sources.tar.gz','r:gz') as tf:
        if not required<=set(tf.getnames()):raise ValueError('loader header missing from finished source archive')
    evidence_files={}
    with tarfile.open(target/'evidence.tar.gz','w:gz',compresslevel=3) as tf:
        for directory in evidence:
            for path in sorted((ROOT/directory).rglob('*')):
                if not path.is_file():continue
                name=path.relative_to(ROOT).as_posix();evidence_files[name]=dict(bytes=path.stat().st_size,sha256=base.digest(path));tf.add(path,arcname=name,recursive=False)
    with tarfile.open(target/'evidence.tar.gz','r:gz') as tf:
        for item in tf:
            h=hashlib.sha256();stream=tf.extractfile(item)
            for block in iter(lambda:stream.read(4*1024*1024),b''):h.update(block)
            if h.hexdigest()!=evidence_files[item.name]['sha256']:raise ValueError('evidence changed: '+item.name)
    (target/'evidence-files.json').write_text(json.dumps(evidence_files,indent=2)+'\n')
    data=dict(created_utc=datetime.now(timezone.utc).isoformat(),status='IN PROGRESS; upstream hakios branch pinned; G2/G5/G7/G10 display fixes tested on panels/pages; ADSP PAS shutdown still unverified; peripherals/60 FPS incomplete',
        policy='temporary fastboot boot; RAM images; no Android userdata access; failed DSP cycle disabled',
        tested=['Chinese desktop','static fallback wallpaper/Dock background','idle ADC temperature and voltage','native no-AUTO PIL installation/config',
            'native ROM RAM filesystem reads','native ELF parser and noncacheable authentication metadata',
            'PAS1 metadata/memory setup and shutdown without DSP execution','native first ADSP2 segment load/readback/restore',
            'Chinese Settings visible with GPU final scale; v29 locale sequence completed',
            'native SpringBoard finished-launch notification and test consumer handshake',
            'resident ADSP/SLPI proxy exact ABI inventory; event methods not called'],
        newer_verified=['v24 complete ADSP2 native load/readback/BSS/restore and PAS shutdown; no DSP execution',
            'v25b configured LPI resources and vote256 read without actual votes','v26 all 54 native NPA table slots matched',
            'v34-v38 real Chinese Control Center visible, notification close verified',
            'v39b wallpaper top black area removed through source-image crop; no status bar hiding',
            'v39b libc and NSTimeZone both Asia/Shanghai, native interface date/time visibly correct',
            'v41b native widgets finished asynchronously; weather/calendar text actually visible',
            'v42 bottom-edge Control Center dismiss ended and returned to desktop; v42b Settings then normal home returned too',
            'v28/v30b zero-request NPA client creation/reverse destruction and full original-state restoration',
            'v31 exact VCS object bindings/default min0/max480/init256 verified without protocol calls',
            'v32 native requests256/release-to-zero independently verified; original restoration remains false; explicit cold reset and Android boot ID change verified',
            'v33e full ROM stage under held power; noexec PAS shutdown before exact memory restore',
            'v35b/v36 AP-owned SMP2P initialization and native-qualified IPCC notification; explicit cold reset verified',
            'v37 combines SMP2P prep with noexec powered ROM stage and stop-before-restore',
            'v38 secure auth accepted and fresh ready/stop ACK observed; PAS shutdown FAILED; not accepted as restoration',
            'upstream display bounds: 450 clipped render pixel comparisons under ASan/UBSan; offset/extreme/allocation failure cases passed',
            'new display kernel: notifications/Control Center close returned to verified desktop, six page gestures completed, 489 reduced completed-composition frames visually reviewed'],
        limitations=['native Poster missing; static cropped fallback remains','Control Center routing compatibility verified by synthetic HID; no mechanical/optical or physical unattended touch proof',
            'historical UI GPU tests hit die55 guard; new bounds panel/page runs remained below the guard',
            'DSP v10b/v34b/v38 shutdown/reset cause remains unexplained; all executing OEM paths disabled; retained-reset draft offline only',
            'NPA v29 original-state restoration failed; old vote mode disabled; v32 separately accepts native release only with mandatory verified cold reset, no DSP startup',
            'audio/Wi-Fi/Bluetooth/sensors/SOC/suspend incomplete'],
        files={str(x.relative_to(target)):dict(bytes=x.stat().st_size,sha256=base.digest(x)) for x in sorted(target.rglob('*')) if x.is_file()})
    (target/'manifest.json').write_text(json.dumps(data,indent=2)+'\n')
    print('Saved in-progress checkpoint:',target,flush=True)
if __name__=='__main__':main()
