"""Run public offline regressions and available models from 75 C checks.

No phone commands are issued. Optional tests needing private firmware or
locally built images are individually listed, skipped, and counted.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def cases(suite):
    for item in suite:
        if isinstance(item, unittest.TestSuite):
            yield from cases(item)
        else:
            yield item


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--python-only', action='store_true')
    p.add_argument('--c-only', action='store_true')
    p.add_argument('--cc', default=os.environ.get('CC'))
    p.add_argument('--report', type=Path, default=ROOT/'out/host-tests.json')
    a = p.parse_args()
    if a.python_only and a.c_only:
        p.error('choose at most one subset')
    report = {'hardware_tested': False}
    passed = True
    os.chdir(ROOT)
    sys.path.insert(0, str(ROOT/'tools'))
    # Controller unit tests mock these executable names; this does not invoke them.
    os.environ.setdefault('MARBLE_ADB', 'adb.exe')
    os.environ.setdefault('MARBLE_FASTBOOT', 'fastboot.exe')
    if not a.c_only:
        local = json.loads((ROOT/'tools/tests/local-input-tests.json').read_text())
        suite = unittest.defaultTestLoader.discover(str(ROOT/'tools/tests'), 'test_marble*.py')
        for test in cases(suite):
            required = local.get(test.id())
            if required and not (ROOT/required['required_file']).is_file():
                method = test._testMethodName
                setattr(test, method, unittest.skip(required['reason']+' Input: '+required['required_file'])(getattr(test, method)))
        result = unittest.TextTestRunner(verbosity=1).run(suite)
        passed = result.wasSuccessful()
        report['python'] = dict(run=result.testsRun, skipped=len(result.skipped),
                                failures=len(result.failures), errors=len(result.errors))
    if not a.python_only:
        compiler = a.cc or shutil.which('clang') or shutil.which('gcc')
        if not compiler:
            p.error('C models require clang/gcc on PATH or --cc')
        models = json.loads((ROOT/'tools/tests/host-models.json').read_text())
        local_models = json.loads((ROOT/'tools/tests/host-model-local-inputs.json').read_text())
        skipped_models = []
        with tempfile.TemporaryDirectory(prefix='marble-models-') as tmp:
            for model in models:
                local = local_models.get(model, {})
                if any(not (ROOT/f).is_file() for f in local.get('required_files', [])):
                    skipped_models.append(model)
                    print('SKIP local firmware model:', model, flush=True)
                    continue
                exe = Path(tmp)/(Path(model).stem+('.exe' if os.name == 'nt' else ''))
                subprocess.run([compiler, '-O2', '-Wall', '-Wextra', model, '-o', str(exe)], check=True)
                subprocess.run([str(exe), *local.get('arguments', [])], check=True)
        report['C_models'] = dict(passed=len(models)-len(skipped_models), skipped=skipped_models,
                                  compiler=Path(compiler).name)
    report['passed'] = passed
    a.report.parent.mkdir(parents=True, exist_ok=True)
    a.report.write_text(json.dumps(report, indent=2)+'\n', encoding='utf8')
    print(json.dumps(report, indent=2), flush=True)
    if not passed:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
