#!/usr/bin/env python3
"""Ensure incomplete or corrupted hardware records cannot pass stage12 QA."""
import argparse
import importlib.util
from pathlib import Path
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('good', type=Path)
parser.add_argument('older', type=Path, nargs='+')
args = parser.parse_args()
spec = importlib.util.spec_from_file_location('preflight', Path(__file__).parents[1] / 'marble-gmu-preflight-report.py')
report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(report)
assert report.inspect(args.good)['passed']
for old in args.older:
    assert not report.inspect(old)['passed'], old
text = args.good.read_bytes().decode('utf8', 'replace')
changes = [
    ('offset=0x9318 value=0', 'offset=0x9314 value=0'),
    ('offset=0x931c value=0', 'offset=0x931c value=0x602'),
    ('clocks PASS=1 stable=1', 'clocks PASS=1 stable=0'),
    ('guarded=1 reads=16 writes=0', 'guarded=0 reads=16 writes=0'),
    ('bytes=131072 stable=1', 'bytes=131072 stable=0'),
    ('reads=65536 writes=0', 'reads=32768 writes=0'),
    ('version=0x1 standalone=0', 'version=0x1 standalone=1'),
    ('address=0x30050', 'address=0x50050'),
    ('name=gfx.lvl found=1', 'name=gfx.lvl found=0'),
    ('name=CN0 slave=5', 'name=CN0 slave=3'),
    ('aux=e022020004000500', 'aux=e0220200040005'),
    ('BCM name=SH0 status=0 unit=4200000', 'BCM name=SH0 status=0 unit=4200001'),
    ('clocks_writes=0 db_writes=0', 'clocks_writes=1 db_writes=0'),
    ('votes=0 notifications=0 reset_released=0', 'votes=1 notifications=0 reset_released=0'),
    ('votes=0 notifications=0 reset_released=0', 'votes=0 notifications=0 reset_released=1'),
    ('images_intact=1 layout_ok=1', 'images_intact=0 layout_ok=1'),
    ('observed=1 restored=1 skipped=0', 'observed=1 restored=0 skipped=0'),
]
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / 'changed.log'
    for before, after in changes:
        assert before in text, before
        path.write_text(text.replace(before, after), encoding='utf8')
        assert not report.inspect(path)['passed'], (before, after)
    path.write_text(text + '\nmarble-gmu-preflight: clock offset=0x9318 value=0\n', encoding='utf8')
    assert not report.inspect(path)['passed']
print('Stage12 report: valid evidence accepted; older/incomplete logs and 18 corrupt/duplicate records refused PASS')
