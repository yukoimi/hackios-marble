"""Reject broken userspace shader, mapped BO and completion evidence."""
import importlib.util,sys,tempfile
from pathlib import Path
s=importlib.util.spec_from_file_location('service',Path(__file__).parents[1]/'marble-gpu-service-report.py')
m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
p=Path(sys.argv[1]);assert m.inspect(p)['passed']
text=p.read_text(encoding='utf8');lines=text.splitlines(keepends=True)
indices=[i for i,v in enumerate(lines) if v.startswith(('marble-gpu-service: MGPU-',
 'marble-gpu-service: submit ','marble-gpu-service: work ',
 'marble-gl-test: PASS=','gl_test: freedreno;','marble-gpu-live-map: map='))]
with tempfile.TemporaryDirectory() as td:
 q=Path(td)/'changed.log'
 for i in indices:
  for value in (''.join(lines[:i]+lines[i+1:]),text+lines[i]):
   q.write_text(value,encoding='utf8');assert not m.inspect(q)['passed'],lines[i]
 mutations=[('hardware=1','hardware=0'),('bad=0 error=0 renderer=FD725','bad=1 error=0 renderer=FD725'),
  ('renderer=FD725','renderer=softpipe'),('fence=70','fence=69'),
  ('submissions=3 pos=1087','submissions=2 pos=1087'),('leaves=48 generation=175','leaves=52 generation=175'),
  ('checked=15564','checked=15563'),('hw.activecpu 4','hw.activecpu 1')]
 for old,new in mutations:
  assert old in text,old;q.write_text(text.replace(old,new),encoding='utf8')
  assert not m.inspect(q)['passed'],old
 for marker in ('panic(cpu 0): injected failure','completion timeout','SYSTEM_RESET'):
  q.write_text(text+'\n'+marker+'\n',encoding='utf8');assert not m.inspect(q)['passed'],marker
 # A correct final fence cannot compensate for submitting after teardown.
 i=next(i for i,v in enumerate(lines) if v.startswith('marble-gpu-service: work '))
 changed=lines.copy();value=changed.pop(i);changed.append(value)
 q.write_text(''.join(changed),encoding='utf8');assert not m.inspect(q)['passed']
print('service report rejects missing/duplicate',len(indices),'records,',len(mutations),'corruptions, failure markers and stop reordering')
