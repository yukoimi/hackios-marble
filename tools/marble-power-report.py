"""Audit read-only ADSP power discovery, never accept DSP/audio functionality."""
import argparse,json,re
from pathlib import Path

def inspect(text):
    def rows(prefix):
        return [dict(re.findall(r'(\w+)=([^\s]+)',s)) for s in text.splitlines() if s.startswith(prefix)]
    def one(prefix):
        r=rows(prefix)
        return r[0] if len(r)==1 else {}
    def exact(row,values):return all(row.get(k)==str(v) for k,v in values.items())
    errors=[]
    q=one('marble-qmp: complete ');d=one('marble-qmp: descriptor ');n=one('marble-power-resource: complete ')
    qualified=one('marble-qmp: qualified ');final=one('marble-adsp-power: complete ')
    if not exact(qualified,dict(native_exact=1,address='c300000',bytes=1024,identity=1,protocol_calls=0)):errors.append('native QMP address/mapping not qualified')
    if not exact(d,dict(stable=1,valid=1,magic='4d41494c',version=1)):errors.append('QMP descriptor not stable/valid')
    if not exact(q,dict(PASS=1,descriptor_reads=120,length_reads=8,payload_reads=0,MMIO_writes=0,IPCC_writes=0,channel_open_calls=0,DSP_start=0)):errors.append('read-only QMP extent proof missing')
    if not exact(n,dict(PASS=1,terminated=1,protocol_calls=0,clients_created=0,resource_votes=0,DSP_start=0)):errors.append('native resource graph not bounded/complete')
    if not exact(final,dict(PASS=1,qmp=1,resources=1,clock_enable=0,clock_disable=0,votes=0,PAS_calls=0,DSP_start=0,storage_access=0)):errors.append('final read-only boundary missing')
    resources=rows('marble-power-resource: index=');xo=[r for r in resources if r.get('name')=='/pmic/client/xo']
    placeholders=rows('marble-power-resource: placeholder ')
    try:
        total=len(resources)+len(placeholders)
        if not 1<=int(n['visited'])<=96 or total!=int(n['visited']):raise ValueError('graph count')
        if sorted(int(r['index']) for r in resources+placeholders)!=list(range(total)):raise ValueError('graph indices')
        if not 0<=len(xo)<=1 or len(xo)!=int(n['xo_matches']) or any(r.get('read_mask')!='f' for r in resources):raise ValueError('resource evidence')
        if any(not exact(r,dict(definition=0,read_mask=1)) for r in placeholders):raise ValueError('placeholder evidence')
        extents=[]
        for side in ('local','remote'):
            off=int(d[side+'_offset'],16);size=int(d[side+'_bytes']);length=int(q[side+'_message_bytes'])
            if off<0x3c or off&3 or size<68 or size&3 or off>1024 or size>1024-off or not 0<=length<=size-4:raise ValueError('mailbox extent')
            extents.append((off,off+size))
        if not (extents[0][1]<=extents[1][0] or extents[1][1]<=extents[0][0]):raise ValueError('overlapping mailboxes')
    except (KeyError,ValueError) as e:errors.append('invalid resource/mailbox values: '+str(e))
    prefixes=['marble-qmp: qualified ','marble-qmp: descriptor ','marble-qmp: complete ','marble-power-resource: complete ','marble-adsp-power: complete ']
    positions=[text.find(x) for x in prefixes]
    if min(positions)<0 or positions!=sorted(positions):errors.append('inventory sequence missing or reordered')
    return dict(passed=not errors,errors=errors,xo=xo,qmp=d,resources=resources,placeholders=placeholders,
        dsp_started=False,audio_acceptance=False,resource_leases_verified=False,
        scope='mapped firmware metadata and QMP descriptor only; no ownership, rail voltage or working audio proof')

def inspect_xo(text):
    def one(prefix):
        rows=[dict(re.findall(r'(\w+)=([^\s]+)',s)) for s in text.splitlines() if s.startswith(prefix)]
        return rows[0] if len(rows)==1 else {}
    def exact(row,values):return all(row.get(k)==str(v) for k,v in values.items())
    errors=[];start=one('marble-adsp-xo: snapshot_begin ');resource=one('marble-adsp-xo: resource ');final=one('marble-adsp-xo: complete ')
    if not exact(start,dict(address='80860000',bytes=131072,pages=32,allocated=0,snapshots=2,votes=0,MMIO_writes=0,DSP_start=0)):errors.append('XO RAM extent/allocation missing')
    if not exact(resource,dict(name='xo.lvl',stable=1,parsed=1,qualified=1,slave=3,issue='0,0,0',checksum_authenticated=0)):errors.append('stable parsed XO ARC resource missing')
    if not exact(final,dict(PASS=1,read_bytes=262144,free_status=0,resource_votes=0,native_clock_calls=0,RPMh_calls=0,NPA_calls=0,QMP_calls=0,PAS_calls=0,DSP_start=0,storage_access=0,lease_accepted=0)):errors.append('XO read-only/free boundary missing')
    try:
        addr=int(resource['address'],16)
        if not 0<addr<=0xffffffff or addr&3 or ((addr>>16)&7)!=3:raise ValueError('ARC address')
        if not 0<=int(resource['aux_bytes'])<=65535 or not 0<=int(resource['version'])<=65535:raise ValueError('directory metadata')
        if not 0<int(final['elapsed_us'])<1000000:raise ValueError('snapshot budget')
    except (KeyError,ValueError) as exc:errors.append('invalid XO values: '+str(exc))
    positions=[text.find(p) for p in ('marble-adsp-xo: snapshot_begin ','marble-adsp-xo: resource ','marble-adsp-xo: complete ')]
    if min(positions)<0 or positions!=sorted(positions):errors.append('snapshot order')
    return dict(passed=not errors,errors=errors,resource=resource,resource_leases_verified=False,dsp_started=False,audio_acceptance=False,
        scope='two bounded RAM command database snapshots and existing parser only; no physical clock state, authenticated checksum or usable XO lease proof')

def inspect_icb(text):
    def rows(label):return [dict(re.findall(r'(\w+)=([^\s]+)',s)) for s in text.splitlines() if s.startswith('marble-adsp-icb: '+label+' ')]
    snapshots=rows('snapshot');finals=rows('complete');routes=rows('route');errors=[]
    snap=snapshots[0] if len(snapshots)==1 else {};final=finals[0] if len(finals)==1 else {}
    def exact(row,**values):return all(row.get(k)==str(v) for k,v in values.items())
    if not exact(snap,exact_abi=1,Init_calls=0,first=1,second=1,stable=1):errors.append('stable native snapshots missing')
    if not exact(final,PASS=1,native_calls=0,clients_created=0,votes=0,MMIO_writes=0,NPA_calls=0,RPMh_calls=0,PAS_calls=0,DSP_start=0,storage_access=0,bandwidth_lease=0):errors.append('read-only boundary missing')
    try:
        if not 1<=int(snap['trees'])<=8 or not 1<=int(snap['nodes'])<=4096 or len(routes)!=int(snap['nodes']):raise ValueError('tree/node limits')
        if not 0<int(final['elapsed_us'])<1000000:raise ValueError('snapshot budget')
        seen=set()
        for i,r in enumerate(routes):
            key=(int(r['tree']),int(r['master']),int(r['slave']))
            if int(r['index'])!=i or not 0<=key[0]<int(snap['trees']) or not 0<=key[1]<=1024 or not 0<=key[2]<=1024 or key in seen:raise ValueError('route identity')
            seen.add(key)
            if not 1<=int(r['hops'])<=16 or r.get('hardware_identity_verified')!='0':raise ValueError('unverified hardware mapping or hops')
    except (KeyError,ValueError) as exc:errors.append('invalid metadata: '+str(exc))
    order=['marble-adsp-icb: snapshot ']+['marble-adsp-icb: route index='+str(i)+' ' for i in range(len(routes))]+['marble-adsp-icb: complete ']
    positions=[text.find(p) for p in order]
    if min(positions)<0 or positions!=sorted(positions):errors.append('missing or reordered routes')
    return dict(passed=not errors,errors=errors,routes=routes,bandwidth_lease_verified=False,dsp_started=False,audio_acceptance=False,
        scope='two native route-tree RAM snapshots; firmware endpoint numbers only, not Android IDs or crypto hardware mapping proof')

def inspect_dependencies(text):
    base=inspect_icb(text);errors=list(base['errors'])
    def rows(prefix):return [dict(re.findall(r'(\w+)=([^\s]+)',s)) for s in text.splitlines() if s.startswith(prefix)]
    def one(prefix):
        r=rows(prefix);return r[0] if len(r)==1 else {}
    def exact(row,**values):return all(row.get(k)==str(v) for k,v in values.items())
    snap=one('marble-adsp-dependencies: snapshot ');end=one('marble-adsp-dependencies: complete ')
    npa=one('marble-adsp-dependencies: npa ');hops=rows('marble-adsp-hop: ')
    plugin=rows('marble-adsp-dependencies: plugin_word ');clock=rows('marble-adsp-dependencies: clock_word ')
    if not exact(snap,first=1,second=1,stable=1,firmware_master=23,firmware_slave=0,hops=4):errors.append('dependency snapshots incomplete')
    if not exact(end,PASS=1,native_calls=0,clients_created=0,votes=0,MMIO_writes=0,DSP_start=0,storage_access=0,lease_accepted=0):errors.append('dependency read-only boundary incomplete')
    try:
        if len(hops)!=8:raise ValueError('endpoint count')
        for i,row in enumerate(hops):
            if not exact(row,index=i//2,side=i%2,metadata_only=1) or not row.get('name') or not row.get('bus'):raise ValueError('endpoint sequence/name')
            if not 0<=int(row['id'])<=1024 or not 0<=int(row['width'])<=1024 or not 0<=int(row['bus_base'],16)<2**64:raise ValueError('endpoint metadata bounds')
        for count,group in ((8,plugin),(20,clock)):
            if len(group)!=count or [int(r['index']) for r in group]!=list(range(count)) or any(not 0<=int(r['value'],16)<2**64 for r in group):raise ValueError('native metadata extent')
        if any(not 0<int(npa[k],16)<2**64 for k in ('resource','definition','plugin')):raise ValueError('NPA identity')
        if not 0<=int(npa['state'])<2**32:raise ValueError('NPA state')
    except (KeyError,ValueError) as exc:errors.append('invalid dependency metadata: '+str(exc))
    prefixes=['marble-adsp-icb: complete ','marble-adsp-dependencies: snapshot ','marble-adsp-hop: index=0 side=0 ',
        'marble-adsp-dependencies: npa ','marble-adsp-dependencies: plugin_word index=0 ',
        'marble-adsp-dependencies: clock_word index=0 ','marble-adsp-dependencies: complete ']
    pos=[text.find(p) for p in prefixes]
    if min(pos)<0 or pos!=sorted(pos):errors.append('dependency sequence')
    return dict(passed=not errors,errors=errors,endpoints=hops,npa=npa,plugin=plugin,clock=clock,
        hardware_identity_verified=False,resource_leases_verified=False,dsp_started=False,audio_acceptance=False,
        scope='stable named native endpoint/bus and NPA/Clock metadata; no function calls, hardware reads or lease acceptance')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('log',type=Path);g=p.add_mutually_exclusive_group();g.add_argument('--xo',action='store_true');g.add_argument('--icb',action='store_true');g.add_argument('--dependencies',action='store_true');a=p.parse_args()
    r=(inspect_dependencies if a.dependencies else inspect_icb if a.icb else inspect_xo if a.xo else inspect)(a.log.read_text(errors='replace'));print(json.dumps(r,indent=2));raise SystemExit(not r['passed'])
