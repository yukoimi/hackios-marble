"""Check compiled native ABI signatures against exact supplied Mu driver bytes."""
import hashlib,importlib.util,json,re,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def verify(name,sha,ranges):
    data=(ROOT/'out/peripheral-power-audit-v1-plan'/f'{name}Dxe.efi').read_bytes()
    if hashlib.sha256(data).hexdigest()!=sha:raise ValueError('unqualified native driver: '+name)
    pe=struct.unpack_from('<I',data,60)[0]
    if data[pe:pe+4]!=b'PE\0\0' or struct.unpack_from('<H',data,pe+4)[0]!=0xaa64:raise ValueError('not ARM64 PE')
    section=pe+24+struct.unpack_from('<H',data,pe+20)[0]
    sections=[struct.unpack_from('<IIII',data,section+40*i+8) for i in range(struct.unpack_from('<H',data,pe+6)[0])]
    header=(ROOT/f'xnuloader/marble_{name.lower()}_code.h').read_text()
    for address,size in ranges:
        matches=[data[off+address-va:off+address-va+size] for vs,va,n,off in sections if va<=address and address+size<=va+min(vs,n)]
        if len(matches)!=1 or len(matches[0])!=size:raise ValueError('instruction extent')
        pattern=rf'marble_{name.lower()}_code_{address:x}\[\]=\{{([^}}]+)\}};'
        found=re.findall(pattern,header)
        if len(found)!=1:raise ValueError('signature missing/duplicate')
        words=[int(x,16) for x in re.findall(r'0x([0-9a-f]+)u',found[0])]
        if words!=list(struct.unpack('<'+'I'*(size//4),matches[0])):raise ValueError('signature differs from supplied native code')
    return len(ranges)

def verify_digests(header,arrays):
    text=(ROOT/header).read_text();total=0
    for symbol,driver,sha in arrays:
        data=(ROOT/f'out/peripheral-power-audit-v1-plan/{driver}Dxe.efi').read_bytes()
        if hashlib.sha256(data).hexdigest()!=sha:raise ValueError('unqualified digest driver: '+driver)
        pe=struct.unpack_from('<I',data,60)[0];at=pe+24+struct.unpack_from('<H',data,pe+20)[0]
        sections=[struct.unpack_from('<IIII',data,at+40*i+8) for i in range(struct.unpack_from('<H',data,pe+6)[0])]
        body=re.findall(re.escape(symbol)+r'\[\]=\{(.*?)\n\};',text,re.S)
        if len(body)!=1:raise ValueError('missing/duplicate digest array')
        rows=re.findall(r'\{0x([0-9a-f]+),(\d+),\{([^}]+)\}\}',body[0])
        if not rows:raise ValueError('empty digest array')
        for address,size,digest in rows:
            address=int(address,16);size=int(size)
            matches=[data[off+address-va:off+address-va+size] for vs,va,n,off in sections if va<=address and address+size<=va+min(vs,n)]
            expected=bytes(int(x,16) for x in re.findall(r'0x([0-9a-f]{2})',digest))
            if len(matches)!=1 or len(expected)!=32 or hashlib.sha256(matches[0]).digest()!=expected:raise ValueError('digest code mismatch: '+symbol)
            total+=1
    return total

if __name__=='__main__':
    n=verify('Rpmh','7f917c9e9c64ba11bee99f3d56f5474d39dac210f5a2dfad2059dd1d4afcd3c6',
        [(0x15ec,0x7c),(0x16ec,0x44),(0x1760,0x54),(0x294c,0xf4),(0x2ac0,0xbc),
         (0x3dc8,0x140),(0x5498,0x44),(0x81dc,0xc4),(0x82ac,0xc4),(0x659c,0x28)])
    n+=verify('ICB','6cd27554f8edcf3bbe41473448354e55cd57461f0c29abbe18a5a8b6fdca596a',[(0x42a0,0x38),(0x43fc,0x34)])
    n+=verify_digests('xnuloader/marble_icb_lease_code.h',[
        ('marble_icb_lease_npa_code','Npa','9ced898ce160f69aede9ce1ad93dfef9223e9c280dc3ec99616861eae56e9c09'),
        ('marble_icb_lease_icb_code','ICB','6cd27554f8edcf3bbe41473448354e55cd57461f0c29abbe18a5a8b6fdca596a')])
    n+=verify_digests('xnuloader/marble_xo_npa_code.h',[
        ('marble_xo_npa_code','Npa','9ced898ce160f69aede9ce1ad93dfef9223e9c280dc3ec99616861eae56e9c09')])
    generated={'marble-rpmh-xo-code':'marble_xo_rpmh_code.h','marble-icb-bandwidth-code':'marble_icb_bandwidth_code.h','marble-icb-run-code':'marble_icb_run_code.h','marble-qmp-code':'marble_qmp_code.h','marble-ipcc-code':'marble_ipcc_code.h','marble-glink-smem-code':'marble_glink_smem_code.h'}
    for name,header in generated.items():
        spec=importlib.util.spec_from_file_location(name,ROOT/f'tools/{name}.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
        code,metadata=m.render()
        if (ROOT/'xnuloader'/header).read_text()!=code:raise ValueError('generated header differs: '+header)
        rows=json.loads(metadata);n+=len(rows) if isinstance(rows,list) else sum(len(v) for v in rows.values())
    print('PASS',n,'native instruction ranges against pinned Mu drivers')
