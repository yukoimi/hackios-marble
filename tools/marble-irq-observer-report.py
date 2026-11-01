"""Decode read-only non-secure GIC snapshots; never imply an IRQ was handled."""
import argparse,json,re
from pathlib import Path
def inspect(text):
 def rows(prefix):return [dict(re.findall(r'(\w+)=([^\s]+)',line)) for line in text.splitlines() if line.startswith(prefix)]
 def exact(row,**values):return all(row.get(k)==str(v) for k,v in values.items())
 errors=[];qualified=rows('marble-irq-observer: qualified ');snapshots=rows('marble-irq-observer: snapshot ');spis=rows('marble-irq-observer: SPI ');phases={}
 try:
  if len(qualified)!=1:raise ValueError('one GIC qualification required')
  q=qualified[0];r=int(q['redistributor'],16);banks=int(q['banks']);mpidr=int(q['MPIDR'],16)
  if not exact(q,distributor=17100000,stride=40000,IPCC_SPI=261,IRQ_writes=0,IAR_reads=0) or not 0x17180000<=r<=0x17340000 or (r-0x17180000)%0x40000 or not 9<=banks<=32 or q['architecture'] not in ('3','4'):raise ValueError('DT/GIC geometry or read-only contract')
  if not 0<=mpidr<2**64:raise ValueError('MPIDR')
  expected=[0,1,2,3] if rows('marble-smccc: interrupted ') else [0,1,3]
  if [int(row['phase']) for row in snapshots]!=expected:raise ValueError('missing/duplicate/out-of-order phases')
  for row in snapshots:
   phase=int(row['phase']);enabled=int(row['PPI_enabled'],16);pending=int(row['PPI_pending'],16);active=int(row['PPI_active'],16)
   if not exact(row,IRQ_writes=0,IAR_reads=0) or any(not 0<=v<=0xffffffff for v in (enabled,pending,active)):raise ValueError('PPI snapshot')
   entries=[s for s in spis if s.get('phase')==str(phase)]
   if len([s for s in entries if s.get('bank')=='8'])!=1:raise ValueError('IPCC bank missing or duplicated')
   seen=set();p=[i for i in range(32) if pending&(1<<i)];a=[i for i in range(32) if active&(1<<i)];ep=[i for i in p if enabled&(1<<i)]
   for entry in entries:
    bank=int(entry['bank']);en=int(entry['enabled'],16);pn=int(entry['pending'],16);ac=int(entry['active'],16)
    if bank in seen or not 1<=bank<banks or any(not 0<=v<=0xffffffff for v in (en,pn,ac)):raise ValueError('SPI bank snapshot')
    seen.add(bank)
    p.extend(bank*32+i for i in range(32) if pn&(1<<i));a.extend(bank*32+i for i in range(32) if ac&(1<<i));ep.extend(bank*32+i for i in range(32) if pn&en&(1<<i))
   phases[str(phase)]=dict(pending_IRQs=p,active_IRQs=a,enabled_pending_IRQs=ep,IPCC_pending=261 in p,IPCC_active=261 in a)
  if any(s.get('phase') not in phases for s in spis):raise ValueError('SPI record outside a snapshot')
 except (KeyError,ValueError) as exc:errors.append('invalid read-only GIC evidence: '+str(exc))
 return dict(passed=not errors,errors=errors,phases=phases,IRQ_writes=0,IAR_reads=0,
  scope='observed non-secure GIC view only; no acknowledge/EOI, IRQ handling, masking, clearing, priority or configuration changes')
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('log',type=Path);a=p.parse_args();r=inspect(a.log.read_text(errors='replace'));print(json.dumps(r,indent=2));raise SystemExit(not r['passed'])
