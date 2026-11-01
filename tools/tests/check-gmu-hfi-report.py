import importlib.util
from pathlib import Path
import sys,tempfile
spec=importlib.util.spec_from_file_location('hfi',Path(__file__).parents[1]/'marble-gmu-hfi-report.py')
report=importlib.util.module_from_spec(spec);spec.loader.exec_module(report)
path=Path(sys.argv[1]);assert report.inspect(path)['passed'];text=path.read_text(encoding='utf8')
lines=text.splitlines(keepends=True);indices=[i for i,s in enumerate(lines) if s.startswith('marble-gmu-hfi: ')]
with tempfile.TemporaryDirectory() as td:
    p=Path(td)/'changed.log'
    for i in indices:
        for s in (''.join(lines[:i]+lines[i+1:]),text+lines[i]):
            p.write_text(s,encoding='utf8');assert not report.inspect(p)['passed']
    changes=[('header=0x10304','header=0x304'),('returned=0x103b04','returned=0x203b04'),
        ('read=220 write=220','read=216 write=220'),('acks=2','acks=1'),
        ('ready=1 commands=2','ready=0 commands=2'),('notify=2','notify=0'),
        ('pending=0 poisoned=0','pending=0x20a003 poisoned=1')]
    for old,new in changes:
        assert old in text
        p.write_text(text.replace(old,new),encoding='utf8');assert not report.inspect(p)['passed']
print(f'HFI report: {len(indices)} missing/duplicate records, {len(changes)} malformed/stale/unconsumed/failed ACK cases refused PASS')
