"""Audit EFI event waits and restoration; not a power-state residency meter."""
import argparse
import json
import re
from pathlib import Path


def inspect(text, phase='restored'):
    errors=[]
    rows=[]
    for match in re.finditer(r'^marble-efi-idle: ([a-z-]+) (.+)$',text,re.M):
        values=dict(re.findall(r'(\w+)=(\w+)',match[2]))
        try:
            row={k:int(v,16 if k in ('saved','now') else 10) for k,v in values.items()}
        except ValueError:
            errors.append('invalid event-wait metadata');continue
        rows.append(dict(phase=match[1],**row))
    selected=[row for row in rows if row['phase']==phase]
    required={'PASS','active','dirty','failed','saved','now','period_us','waits','wait_us',
              'max_wait_us','immediate','late','max_poll_gap_us'}
    if len(selected)!=1 or not required.issubset(selected[0]):
        errors.append('one complete final event-wait record required')
        row=None
    else:
        row=selected[0]
        if any(row[k] for k in ('active','dirty','failed','late')) or row['PASS']!=1:
            errors.append('event wait fault or incomplete restoration')
        if row['now']!=row['saved']:errors.append('CNTKCTL was not restored to the saved value')
        if not 1<=row['period_us']<=500:errors.append('event period outside 1..500 us')
        if row['waits']<64 or row['wait_us']<row['period_us']*8 or row['immediate']>=row['waits']:
            errors.append('insufficient measured WFE wait time')
        # Register period is logged rounded down; allow one us rounding.
        if row['max_wait_us']>(row['period_us']+1)*4:errors.append('late WFE wakeup')
        if phase=='probe' and row['waits']!=64:errors.append('probe did not complete 64 waits')
    if any(r['phase']=='runtime-fault' or r.get('PASS')!=1 for r in rows):
        errors.append('an earlier event-wait fault was recorded')
    if phase=='restored' and not any(r['phase']=='acquired' and r.get('PASS')==1 for r in rows):
        errors.append('run did not acquire an event-stream lease')
    return dict(passed=not errors,errors=errors,phase=phase,record=row,
        scope='elapsed time across WFE instructions and observed poll gaps; not hardware C-state residency or measured power',
        battery_temperature=False)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('log',type=Path);p.add_argument('--phase',choices=('probe','restored'),default='restored')
    p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    text=a.log.read_bytes().split(b'\0PMOC')[0].decode('utf8',errors='replace')
    report=inspect(text,a.phase)
    a.out.write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
    print(json.dumps(report,indent=2));raise SystemExit(0 if report['passed'] else 1)


if __name__=='__main__':main()
