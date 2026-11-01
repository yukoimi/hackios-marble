#!/usr/bin/env python3
"""Refuse incomplete stage13 evidence and changed original-source table words."""
import argparse
import importlib.util
from pathlib import Path
import re
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('good', type=Path)
parser.add_argument('older', type=Path, nargs='+')
args = parser.parse_args()
spec = importlib.util.spec_from_file_location('votes', Path(__file__).parents[1] / 'marble-gmu-votes-report.py')
report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(report)
oracle = report.load_oracle(report.ROOT / 'out/marble-gmu-votes-oracle.json')
assert report.inspect(args.good, oracle)['passed']
for old in args.older:
    assert not report.inspect(old, oracle)['passed'], old
text = args.good.read_bytes().decode('utf8', 'replace')
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / 'corrupt.log'
    changes = [
        ('cpu_only=1', 'cpu_only=0'), ('queue_writes=0', 'queue_writes=1'),
        ('hardware_votes=0', 'hardware_votes=1'), ('reset_released=0', 'reset_released=1'),
        ('enabled_OPPs=0', 'enabled_OPPs=1'), ('payload name=performance id=4', 'payload name=performance id=3'),
        ('pending=0 sequence=0', 'pending=0 sequence=1'), ('restored=1 skipped=0', 'restored=0 skipped=0'),
        ('name=performance offset=8 ', 'name=performance offset=0 '),
    ]
    for before, after in changes:
        assert before in text, before
        path.write_text(text.replace(before, after))
        assert not report.inspect(path, oracle)['passed'], before
    words = 0
    pattern = r'^marble-gmu-votes: words name=\w+ offset=\d+ data=([0-9a-f]+)$'
    for match in re.finditer(pattern, text, re.M):
        for offset in range(match.start(1), match.end(1), 8):
            changed = text[:offset] + ('1' if text[offset] != '1' else '0') + text[offset+1:]
            path.write_text(changed)
            assert not report.inspect(path, oracle)['passed'], words
            words += 1
    assert words == 217, words
    lines = text.splitlines(keepends=True)
    new_lines = [i for i, line in enumerate(lines) if line.startswith('marble-gmu-votes: ')]
    for index in new_lines:
        path.write_text(''.join(lines[:index] + lines[index+1:]))
        assert not report.inspect(path, oracle)['passed'], index
    path.write_text(text + '\n' + lines[new_lines[-1]])
    assert not report.inspect(path, oracle)['passed']
print('Stage13 report: 217 changed payload words, 9 boundary mutations, missing/duplicate records and earlier stages refused PASS')
