"""Mutation tests for hardware evidence acceptance, not a hardware simulator."""
import importlib.util
from pathlib import Path
import sys,tempfile
spec=importlib.util.spec_from_file_location('boot',Path(__file__).parents[1]/'marble-gmu-boot-report.py')
report=importlib.util.module_from_spec(spec);spec.loader.exec_module(report)
good=Path(sys.argv[1]);assert report.inspect(good)['passed']
text=good.read_text(encoding='utf8');lines=text.splitlines(keepends=True)
indices=[i for i,s in enumerate(lines) if s.startswith('marble-gmu-boot: ')]
with tempfile.TemporaryDirectory() as directory:
    path=Path(directory)/'changed.log'
    for i in indices:
        for changed in (''.join(lines[:i]+lines[i+1:]),text+lines[i]):
            path.write_text(changed,encoding='utf8');assert not report.inspect(path)['passed'],lines[i]
    mutations=[('initialized=1 fw_result=0x100','initialized=0 fw_result=0'),
        ('stopped=1 ack=0x3','stopped=0 ack=0x3'),('ack=0x3','ack=0x1'),
        ('restored=1 retain=0','restored=0 retain=1'),
        ('exact SID5+SID405 quarantined','only SID5 quarantined'),
        ('writes=55 stage=17','writes=55 stage=16'),
        ('=== 36 checks, 0 failed ===','=== 36 checks, 1 failed ==='),
        ('hw.activecpu 4','hw.activecpu 1'),
        ('before=0x4052 after=0x4052','before=0x4052 after=0x4053')]
    for old,new in mutations:
        assert old in text,old
        path.write_text(text.replace(old,new),encoding='utf8');assert not report.inspect(path)['passed'],old
    path.write_text(text+'\npanic(cpu 0)\n',encoding='utf8');assert not report.inspect(path)['passed']
print(f'GMU boot report: {len(indices)} missing/duplicate records and {len(mutations)+1} altered success/cleanup/CPU records refused PASS')
