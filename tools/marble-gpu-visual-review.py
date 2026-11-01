"""Supplement the retained software-reference failure with GPU reference evidence."""
import hashlib, importlib.util, json
from pathlib import Path


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    root = Path(__file__).resolve().parents[1]
    composition = load('composition', root / 'tools/marble-gpu-composition-report.py')
    suite = load('suite', root / 'tools/marble-ui-suite.py')
    current = root / 'out/ui-gpu-accelerated-basic'
    reference = root / 'out/ui-gpu-composition'
    checks = []
    for name in ('settings', 'scroll', 'home', 'menu', 'dismiss'):
        raw = composition.raw_capture(current, name)
        ref_raw = composition.raw_capture(reference, name)
        delta = composition.image_difference(current / (name + '.png'), reference / (name + '.png'))
        registered, shift = suite.scroll_difference(current / (name + '.png'), reference / (name + '.png')) if name == 'scroll' else (delta['mean_error'], 0)
        checks.append(dict(name=name, capture=raw, reference_capture=ref_raw,
                           **delta, registered_mean_error=registered,
                           vertical_shift=shift, gpu_reference_passed=registered < 12))
    bad = root / 'out/ui-gpu-sysmem-pages/library-three.png'
    good = root / 'out/ui-gpu-copy-pages/00-library.png'
    negative = composition.image_difference(bad, good)
    negative.update(captured_defect=str(bad.relative_to(root)),
                    sha256=hashlib.sha256(bad.read_bytes()).hexdigest(),
                    rejected=not negative['passed'])
    software = json.loads((current / 'report.json').read_text())
    report = dict(
        gpu_reference_passed=all(v['gpu_reference_passed'] for v in checks),
        checks=checks, software_reference_report_retained=software,
        captured_black_block_negative_control=negative,
        visual_review='Latest basic contact sheet and default three-round page contact sheet reviewed. No reproduced black strips in these settled captures. Four-item Settings menu remains visible; blur/background differ from the software reference.',
        limits='Independent GPU appearance comparison; does not override the original software-reference failure. Sampled framebuffer and synthetic input only; transient rendering, physical touch, optical output and temperature are not certified.')
    path = root / 'out/marble-gpu-visual-review.json'
    path.write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
    print(json.dumps(report, indent=2))
    if not report['gpu_reference_passed'] or not negative['rejected']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
