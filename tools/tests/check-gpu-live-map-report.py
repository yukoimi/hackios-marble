"""Reject missing, duplicate, corrupt and reordered live hardware evidence."""
import importlib.util,sys,tempfile
from pathlib import Path
s=importlib.util.spec_from_file_location('live',Path(__file__).parents[1]/'marble-gpu-live-map-report.py')
m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
p=Path(sys.argv[1]);assert m.inspect(p)['passed']
text=p.read_text(encoding='utf8');lines=text.splitlines(keepends=True)
indices=[i for i,s in enumerate(lines) if s.startswith(('marble-gpu-live-map: ','marble-gpu-runtime: '))]
with tempfile.TemporaryDirectory() as td:
 q=Path(td)/'changed.log'
 for i in indices:
  for changed in (''.join(lines[:i]+lines[i+1:]),text+lines[i]):
   q.write_text(changed,encoding='utf8');assert not m.inspect(q)['passed'],lines[i]
 mutations=[('pages=258 aperture=0x20000000 leaves=0','pages=258 aperture=0x20000000 leaves=131072'),
  ('generation=129 leaves=48','generation=128 leaves=48'),('leaves=52 generation=2','leaves=51 generation=2'),
  ('published=1 tlb_removed=1','published=0 tlb_removed=0'),('iova=0x2003000','iova=0x2004000'),
  ('cycles=64 map=64 unmap=64 ecats=128 draws=64','cycles=63 map=64 unmap=63 ecats=128 draws=64'),
  ('pos=871 fence=67','pos=871 fence=66'),('irq_fault=0 writes=174','irq_fault=0 writes=172')]
 for old,new in mutations:
  assert old in text;q.write_text(text.replace(old,new),encoding='utf8');assert not m.inspect(q)['passed'],old
 for i in indices[1:-1:4]:
  changed=lines.copy();changed[i],changed[i+1]=changed[i+1],changed[i]
  q.write_text(''.join(changed),encoding='utf8');assert not m.inspect(q)['passed']
print('live report refuses missing/duplicate',len(indices),'records,',len(mutations),'corrupt fields and reordered batches')
