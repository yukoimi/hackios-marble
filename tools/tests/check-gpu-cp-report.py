import importlib.util,sys,tempfile
from pathlib import Path
spec=importlib.util.spec_from_file_location('cp',Path(__file__).parents[1]/'marble-gpu-cp-report.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
p=Path(sys.argv[1]);assert m.inspect(p)['passed'];text=p.read_text(encoding='utf8');lines=text.splitlines(keepends=True)
indices=[i for i,s in enumerate(lines) if s.startswith(('marble-gpu-cp: ','marble-gpu-context: '))]
with tempfile.TemporaryDirectory() as td:
 q=Path(td)/'changed.log'
 for i in indices:
  for changed in (''.join(lines[:i]+lines[i+1:]),text+lines[i]):
   q.write_text(changed,encoding='utf8');assert not m.inspect(q)['passed'],lines[i]
 changes=[('completed=1 words=21','completed=0 words=21'),('rptr=21 wptr=21','rptr=20 wptr=21'),
  ('fence=0x1 marker=0x4d475031','fence=0 marker=0x4d475031'),
  ('marker=0x4d475031 expected=','marker=0 expected='),('scratch=0x4d475031 status=','scratch=0 status='),
  ('irq=0 fault=0 halt_requested=1','irq=1 fault=0 halt_requested=1'),
  ('halt_requested=1 halt_observed=0','halt_requested=0 halt_observed=0'),
  ('halt_observed=0 halt=0x1','halt_observed=1 halt=0x1'),
  ('CX-bus-restore PASS=1 changed=1 restored=1','CX-bus-restore PASS=1 changed=1 restored=0'),
  ('LPAC1=0x401','LPAC1=0x402'),('routes_faulted=4','routes_faulted=2'),
  ('roots_zero=1','roots_zero=0'),('=== 36 checks, 0 failed ===','=== 36 checks, 1 failed ===')]
 for old,new in changes:
  assert old in text,old;q.write_text(text.replace(old,new),encoding='utf8');assert not m.inspect(q)['passed'],old
print(f'CP report refuses {len(indices)} missing/duplicate records and {len(changes)} corrupted DMA/LPAC/drain/restore cases')
