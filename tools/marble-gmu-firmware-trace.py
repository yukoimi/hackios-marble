#!/usr/bin/env python3
"""Offline M3 firmware control-flow trace; does not simulate real GPU MMIO."""
from pathlib import Path
import collections,json,struct,sys
shadow="--mmio-shadow" in sys.argv
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT.parent/'toolchains/python-libs'))
from unicorn import *
from unicorn.arm_const import *
fw=(ROOT.parent/'marble-baselines/20261024-gmu-votes/firmware/gmu_gen70000.bin').read_bytes()
u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS);u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M3)
u.mem_map(0,0x4000);u.mem_map(0x10004000,0x4000)
if shadow:u.mem_map(0x18000000,0x100000)
blocks=[];off=0
while off<len(fw):
    address,n,kind,value=struct.unpack_from('<4I',fw,off);off+=16
    blocks.append((address,n,kind,value,off))
    if not n and kind in (1,7) and address>=0x4000 and not 0x10004000<=address<0x10008000:u.mem_map(address,(value+4095)&~4095)
    off+=n
for address,n,kind,value,off in blocks:
    if n:u.mem_write(address,fw[off:off+n])
# Cortex-M private peripherals are model-only backing, NOT the GPU/AHB bridge.
u.mem_map(0xe000e000,0x2000)
sp,pc=struct.unpack('<2I',u.mem_read(0,8));u.reg_write(UC_ARM_REG_SP,sp)
pcs=collections.deque(maxlen=32);accesses=[];instructions=0
def code(uc,address,size,data):
    global instructions
    instructions+=1;pcs.append(address)
def invalid(uc,access,address,size,value,data):
    accesses.append(dict(access=access,address=hex(address),size=size,value=hex(value)))
    return False
def mmio(uc,access,address,size,value,data):
    if 0x18000000<=address<0x18100000:
        accesses.append(dict(access=access,address=hex(address),size=size,value=hex(value),pc=hex(uc.reg_read(UC_ARM_REG_PC))))
        if access==UC_MEM_READ:uc.emu_stop()
u.hook_add(UC_HOOK_MEM_WRITE|UC_HOOK_MEM_READ,mmio)
u.hook_add(UC_HOOK_CODE,code);u.hook_add(UC_HOOK_MEM_INVALID,invalid)
error=None
try:u.emu_start(pc,0xffffffff,count=200000)
except UcError as e:error=str(e)
print(json.dumps(dict(scope='offline control flow only; private peripheral backing is synthetic; no hardware completion claim',sp=hex(sp),entry=hex(pc),instructions=instructions,error=error,invalid=accesses,pc_tail=[hex(v) for v in pcs],registers={name:hex(u.reg_read(reg)) for name,reg in [('r0',UC_ARM_REG_R0),('r1',UC_ARM_REG_R1),('r2',UC_ARM_REG_R2),('r3',UC_ARM_REG_R3),('lr',UC_ARM_REG_LR),('sp',UC_ARM_REG_SP),('pc',UC_ARM_REG_PC)]}),indent=2))
