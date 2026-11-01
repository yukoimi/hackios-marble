#!/usr/bin/env python3
"""Execute the assembled local-only M3 payload; refuse any nonlocal access."""
from pathlib import Path
import hashlib,re,struct,sys
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT.parent/'toolchains/python-libs'))
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS,UC_HOOK_MEM_WRITE,UC_HOOK_MEM_READ
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_PC,UC_ARM_REG_PRIMASK,UC_CPU_ARM_CORTEX_M3
code=(ROOT/'out/marble-gmu-dma.bin').read_bytes()
source=(ROOT/'xnu/files/bsd/dev/marble_gpu/marble_gmu_diagnostic.h').read_text(encoding='utf8')
array=re.search(r'marble_dma_code\[\]=\{([^}]+)\}',source)[1]
words=[int(v.strip(),0) for v in array.split(',')]
assert code==struct.pack('<16I',*words) and len(code)==64
u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS);u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M3)
u.mem_map(0,0x5000);u.mem_map(0x60000000,0x1000);u.mem_write(0x4000,struct.pack('<I',0x1234abcd));u.mem_map(0x10004000,0x4000);u.mem_write(0x40,code)
u.reg_write(UC_ARM_REG_SP,0x10007f80);writes=[];reads=[]
def write(uc,access,address,size,value,data):
    assert (address,size,value) in [(0x10004000,4,0x4d475052),(0x10004004,4,0x1234abcd),(0x60000000,4,0xdec0de01),(0x10004008,4,0xdec0de01),(0x1000400c,4,0x4d475044)];writes.append((address,size,value))
def read(uc,access,address,size,value,data):
    assert (0x40<=address and address+size<=0x80) or address in (0x4000,0x60000000);reads.append((address,size))
u.hook_add(UC_HOOK_MEM_WRITE,write);u.hook_add(UC_HOOK_MEM_READ,read)
u.emu_start(0x41,0x80,count=100)
assert writes==[(0x10004000,4,0x4d475052),(0x10004004,4,0x1234abcd),(0x60000000,4,0xdec0de01),(0x10004008,4,0xdec0de01),(0x1000400c,4,0x4d475044)]
assert [a for a,n in reads if a in (0x4000,0x60000000)]==[0x4000,0x60000000]
assert u.reg_read(UC_ARM_REG_PC)==0x64 and u.reg_read(UC_ARM_REG_PRIMASK)==1
print('Cortex-M3 DMA diagnostic: declared code read, scratch RAM write/read, local progress, IRQ masked and bounded loop; assembled kernel bytes match PASS',hashlib.sha256(code).hexdigest())
