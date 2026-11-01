import importlib.util,sys,tempfile
from pathlib import Path
spec=importlib.util.spec_from_file_location('gpu',Path(__file__).parents[1]/'marble-gpu-context-report.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
p=Path(sys.argv[1]);assert m.inspect(p)['passed'];text=p.read_text(encoding='utf8');lines=text.splitlines(keepends=True)
indices=[i for i,s in enumerate(lines) if s.startswith(('marble-gpu-context: ','marble-gmu-core: ','marble-gmu-start: ','marble-gmu-gx: ','marble-gmu-vote: '))]
with tempfile.TemporaryDirectory() as td:
 q=Path(td)/'changed.log'
 for i in indices:
  for changed in (''.join(lines[:i]+lines[i+1:]),text+lines[i]):
   q.write_text(changed,encoding='utf8');assert not m.inspect(q)['passed'],lines[i]
 changes=[('sid=0 iova=0x100000','sid=5 iova=0x100000'),('SQE_skip=4','SQE_skip=0'),
  ('held=1 commands=1','held=0 commands=1'),('sent=0x50010a','sent=0x40010a'),
  ('returned=0x50010a','returned=0x50010b'),('roots_zero=1','roots_zero=0'),
  ('routes_faulted=2','routes_faulted=1'),('irq_fault=0 writes=32','irq_fault=1 writes=32'),
  ('bank=1 asid=0x43','bank=0 asid=0x42'),('halted=1 halt_ack=0','halted=0 halt_ack=0')]
 for old,new in changes:
  assert old in text,old;q.write_text(text.replace(old,new),encoding='utf8');assert not m.inspect(q)['passed'],old
print(f'GPU report refuses {len(indices)} missing/duplicate records and {len(changes)} corrupted stream/START/cleanup cases')
