#!/usr/bin/env python3
"""RPMh report rejects false completion, changed identity and missing cleanup."""
import importlib.util
from pathlib import Path
import sys,tempfile
spec=importlib.util.spec_from_file_location('rpmh',Path(__file__).parents[1]/'marble-gmu-rpmh-report.py')
r=importlib.util.module_from_spec(spec);spec.loader.exec_module(r)
oracle=r.clocks.votes.load_oracle(r.clocks.votes.ROOT/'out/marble-gmu-votes-oracle.json')
good=Path(sys.argv[1]);assert r.inspect(good,oracle)['passed'];text=good.read_text(encoding='utf8')
lines=text.splitlines(keepends=True);indices=[i for i,s in enumerate(lines) if s.startswith('marble-gmu-rpmh: ')]
assert len(indices)==16
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory)/'changed.log'
 for i in indices:
  p.write_text(''.join(lines[:i]+lines[i+1:]),encoding='utf8');assert not r.inspect(p,oracle)['passed']
 for before,after in [('rail_writes=0','rail_writes=1'),('completed=8','completed=7'),('reads=8','reads=7'),('restored=1 retain=0','restored=0 retain=1'),('status=0x10101','status=0x10001'),('index=1 name=cx.lvl address=0x30000 data=0x5','index=1 name=cx.lvl address=0x30000 data=0x4'),('name=cx.lvl address=0x30000','name=mx.lvl address=0x30000'),('offset=0 before=0x1 after=0x1','offset=0 before=0x1 after=0')]:
  assert before in text,before
  p.write_text(text.replace(before,after),encoding='utf8');assert not r.inspect(p,oracle)['passed'],before
 p.write_text(text+lines[indices[0]],encoding='utf8');assert not r.inspect(p,oracle)['passed']
 for old in sys.argv[2:]:assert not r.inspect(Path(old),oracle)['passed']
print('RPMh report: missing/duplicate responses, 8 completion/identity/state/restore corruptions and failed/older runs refused PASS')
