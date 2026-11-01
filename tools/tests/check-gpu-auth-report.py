"""Mutation checks for the PAS and secure hardware evidence parsers."""
import importlib.util,sys,tempfile
from pathlib import Path
def module(name):
 s=importlib.util.spec_from_file_location(name,Path(__file__).parents[1]/('marble-gpu-'+name+'-report.py'))
 m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
kind=sys.argv[1];m=module(kind);p=Path(sys.argv[2]);assert m.inspect(p)['passed']
text=p.read_text(encoding='utf8');lines=text.splitlines(keepends=True)
indices=[i for i,s in enumerate(lines) if s.startswith(('marble-gpu-zap: ','marble-gpu-secure: ','marble-gpu-blit: ','marble-gpu-runtime: '))]
with tempfile.TemporaryDirectory() as td:
 q=Path(td)/'changed.log'
 for i in indices:
  for changed in (''.join(lines[:i]+lines[i+1:]),text+lines[i]):
   q.write_text(changed,encoding='utf8');assert not m.inspect(q)['passed'],lines[i]
 mutations=[('command=0x201 status=0 answer=0 ','command=0x201 status=-1 answer=0 '),
  ('command=0x202 status=0 answer=0 ','command=0x202 status=0 answer=1 '),
  ('command=0x205 status=0 answer=0 ','command=0x205 status=-1 answer=0 '),
  ('file_bytes=2236 segment=1','file_bytes=3896 segment=2'),('readback=1','readback=0'),
  ('unloaded=1 retain=0 phase=7','unloaded=0 retain=1 phase=7')]
 if kind in ('secure','blit','runtime'):mutations += [('after=0 submitted=1','after=1 submitted=1'),('shadow=28 fence=2','shadow=21 fence=1')]
 if kind in ('blit','runtime'):mutations += [('pixels=1024 guards=3056','pixels=1023 guards=3056'),('pixel0=0xffff8000','pixel0=0xa5c39e71'),('shadow=167 fence=3','shadow=167 fence=2')]
 if kind=='runtime':mutations += [('stop_requested=1','stop_requested=0'),('draws=32 wraps=1','draws=32 wraps=0'),('ready=1 service=0','ready=1 service=1'),('pixels=1024 guards=3056 fault=0','pixels=1024 guards=3056 fault=1')]
 for old,new in mutations:
  assert old in text,old;q.write_text(text.replace(old,new),encoding='utf8');assert not m.inspect(q)['passed'],old
 # A copied payload before successful MEM_SETUP must not be accepted.
 payload=next(s for s in lines if s.startswith('marble-gpu-zap: payload '))
 changed=text.replace(payload,'').replace('marble-gpu-zap: before-scm command=0x202',payload+'marble-gpu-zap: before-scm command=0x202',1)
 q.write_text(changed,encoding='utf8');assert not m.inspect(q)['passed']
print(kind,'report refuses',len(indices),'missing/duplicate records and',len(mutations)+1,'corrupt/reordered evidence cases')
