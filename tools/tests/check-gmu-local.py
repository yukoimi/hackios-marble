#!/usr/bin/env python3
"""Execute the assembled local-only M3 payload; refuse any nonlocal access."""
from pathlib import Path
import hashlib,re,struct,sys
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT.parent/'toolchains/python-libs'))
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS,UC_HOOK_MEM_WRITE,UC_HOOK_MEM_READ
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_PC,UC_ARM_REG_PRIMASK,UC_CPU_ARM_CORTEX_M3
code=(ROOT/'out/marble-gmu-local.bin').read_bytes()
source=(ROOT/'xnu/files/bsd/dev/marble_gpu/marble_gmu_diagnostic.h').read_text(encoding='utf8')
array=re.search(r'marble_local_code\[\]=\{([^}]+)\}',source)[1]
words=[int(v.strip(),0) for v in array.split(',')]
assert code==struct.pack('<6I',*words) and len(code)==24
u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS);u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M3)
u.mem_map(0,0x4000);u.mem_map(0x10004000,0x4000);u.mem_write(0x40,code)
u.reg_write(UC_ARM_REG_SP,0x10007f80);writes=[];reads=[]
def write(uc,access,address,size,value,data):
    assert (address,size,value)==(0x10004000,4,0x4d475055);writes.append((address,size,value))
def read(uc,access,address,size,value,data):
    assert 0x40<=address and address+size<=0x58;reads.append((address,size))
u.hook_add(UC_HOOK_MEM_WRITE,write);u.hook_add(UC_HOOK_MEM_READ,read)
u.emu_start(0x41,0x58,count=100)
assert len(writes)==1 and reads==[(0x50,4),(0x54,4)]
assert u.mem_read(0x10004000,4)==struct.pack('<I',0x4d475055)
assert u.reg_read(UC_ARM_REG_PC)==0x4c and u.reg_read(UC_ARM_REG_PRIMASK)==1
print('Cortex-M3 local payload: two ITCM literal reads, one DTCM marker write, IRQ disabled, bounded loop; no DDR/MMIO; assembled image matches kernel PASS',hashlib.sha256(code).hexdigest())
