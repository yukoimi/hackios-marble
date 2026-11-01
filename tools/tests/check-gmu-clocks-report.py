#!/usr/bin/env python3
"""Reject stale, incomplete, changed-rate and un-restored clock evidence."""
import importlib.util
from pathlib import Path
import sys
import tempfile
spec=importlib.util.spec_from_file_location('clocks',Path(__file__).parents[1]/'marble-gmu-clocks-report.py')
report=importlib.util.module_from_spec(spec);spec.loader.exec_module(report)
oracle=report.votes.load_oracle(report.votes.ROOT/'out/marble-gmu-votes-oracle.json')
good=Path(sys.argv[1]);assert report.inspect(good,oracle)['passed']
text=good.read_text(encoding='utf8')
lines=text.splitlines(keepends=True)
indices=[i for i,line in enumerate(lines) if line.startswith('marble-gmu-clocks: ')]
assert len(indices)==16
with tempfile.TemporaryDirectory() as directory:
    p=Path(directory)/'changed.log'
    for i in indices:
        p.write_text(''.join(lines[:i]+lines[i+1:]),encoding='utf8')
        assert not report.inspect(p,oracle)['passed']
    for old,new in [('hz=199999810','hz=200000000'),('full=1365367','full=1365366'),
                    ('measured=1','measured=0'),('configured=1','configured=0'),
                    ('retain=0','retain=1'),('reset_released=0','reset_released=1'),
                    ('before=0xe0257f after=0xe0257f','before=0xe0257f after=0xe1257f')]:
        assert old in text,old
        p.write_text(text.replace(old,new),encoding='utf8')
        assert not report.inspect(p,oracle)['passed'],old
    p.write_text(text+lines[indices[0]],encoding='utf8');assert not report.inspect(p,oracle)['passed']
    for old in sys.argv[2:]:assert not report.inspect(Path(old),oracle)['passed']
print('Stage14 report: 16 missing records, duplicate record, 7 counter/state/register corruptions and older stages refused PASS')
