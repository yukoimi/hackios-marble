"""Read-only Android boot v3/v4, gzip+FDT and MBR/FAT12 inspection (stdlib only)."""
from pathlib import Path
import hashlib, json, lzma, re, struct, sys, zlib

source = Path(sys.argv[1])
out = Path(sys.argv[2]) if len(sys.argv) > 2 else Path.cwd() / 'image-inspection'
out.mkdir(exist_ok=True)
b = source.read_bytes()
assert b[:8] == b'ANDROID!'
ks, rs, osver, hs, *rest = struct.unpack_from('<9I', b, 8)
version = rest[-1]
assert version in (3, 4)
ko = 4096
ro = (ko + ks + 4095) // 4096 * 4096
k, r = b[ko:ko+ks], b[ro:ro+rs]
z = zlib.decompressobj(31)
fw = z.decompress(k) + z.flush()
dtb = z.unused_data
(out/'uefi-bootshim.bin').write_bytes(fw)
(out/'BootShim.bin').write_bytes(fw[:0x70])
(out/'SILICIUM_UEFI.fd').write_bytes(fw[0x70:])
(out/'marble.dtb').write_bytes(dtb)
(out/'ramdisk.bin').write_bytes(r)
summary = dict(file=str(source), size=len(b), sha256=hashlib.sha256(b).hexdigest(),
               header_version=version, header_size=hs, kernel_offset=ko, kernel_size=ks,
               ramdisk_offset=ro, ramdisk_size=rs, uncompressed_firmware_size=len(fw),
               gzip_consumed=len(k)-len(dtb), appended_dtb_size=len(dtb))
signature_size=struct.unpack_from('<I',b,1580)[0] if version==4 else 0
trailer=b[ro+rs:]
summary.update(signature_size=signature_size,trailer_size=len(trailer),trailer_nonzero_bytes=sum(x!=0 for x in trailer))

# Flattened device tree. Save every property as strings or big-endian cells.
magic,total,so,stro,_,_,_,_,strsz,ssz = struct.unpack_from('>10I',dtb)
assert magic == 0xd00dfeed and total == len(dtb)
strings=dtb[stro:stro+strsz]
nodes={}; stack=[]; at=so
while at < so+ssz:
    token=struct.unpack_from('>I',dtb,at)[0]; at+=4
    if token == 1:
        end=dtb.index(0,at); name=dtb[at:end].decode(); at=(end+4)&~3
        stack.append(name); nodes['/'.join(stack) or '/']={}
    elif token == 2: stack.pop()
    elif token == 3:
        n,off=struct.unpack_from('>II',dtb,at);at+=8
        key=strings[off:strings.index(0,off)].decode()
        value=dtb[at:at+n];at=(at+n+3)&~3
        if (value and value[-1] == 0 and value[0] >= 32
                and all(x==0 or 32<=x<127 for x in value)
                and all(value.rstrip(b'\0').split(b'\0'))):
            decoded=value.rstrip(b'\0').decode().split('\0')
        elif len(value)%4 == 0: decoded=[f'0x{x:x}' for x in struct.unpack('>'+str(len(value)//4)+'I',value)]
        else: decoded={'hex':value.hex()}
        nodes['/'.join(stack) or '/'][key]=decoded
    elif token == 4: pass
    elif token == 9: break
    else: raise ValueError(token)
(out/'marble-dtb.json').write_text(json.dumps(nodes,ensure_ascii=False,indent=2),encoding='utf8')

# MBR and FAT12. Follow allocated FAT chains, including directories.
parts=[]; files=[]
for i in range(4):
    off=446+16*i
    typ=r[off+4]; lba,sectors=struct.unpack_from('<II',r,off+8)
    if not typ: continue
    parts.append(dict(type=hex(typ),lba=lba,sectors=sectors))
    v=r[lba*512:(lba+sectors)*512]
    bps=struct.unpack_from('<H',v,11)[0]; spc=v[13]
    reserved=struct.unpack_from('<H',v,14)[0]; fats=v[16]
    roots=struct.unpack_from('<H',v,17)[0]; spf=struct.unpack_from('<H',v,22)[0]
    root=(reserved+fats*spf)*bps; data=root+((roots*32+bps-1)//bps)*bps
    fat=v[reserved*bps:(reserved+spf)*bps]
    def cluster_bytes(cluster):
        result=bytearray(); seen=set()
        while 2 <= cluster < 0xff8:
            if cluster in seen: raise ValueError('FAT loop')
            seen.add(cluster)
            pos=data+(cluster-2)*spc*bps;result.extend(v[pos:pos+spc*bps])
            pos=cluster*3//2; val=int.from_bytes(fat[pos:pos+2],'little')
            cluster=(val>>4) if cluster&1 else (val&0xfff)
        return bytes(result)
    def directory(raw,prefix=''):
        long_parts={}
        for p in range(0,len(raw),32):
            e=raw[p:p+32]
            if not e or e[0] == 0: break
            if e[0]==0xe5:
                long_parts={}
                continue
            if e[11] == 15:
                fragment=e[1:11]+e[14:26]+e[28:32]
                long_parts[e[0]&0x1f]=fragment.decode('utf-16le').split('\0')[0].rstrip('\uffff')
                continue
            name=e[:8].decode('ascii',errors='replace').rstrip(); ext=e[8:11].decode('ascii',errors='replace').rstrip()
            if ext: name+='.'+ext
            if long_parts:
                name=''.join(long_parts[i] for i in sorted(long_parts))
                long_parts={}
            if name in ('.','..') or e[11]&8: continue
            path=prefix+'/'+name; c=struct.unpack_from('<H',e,26)[0]; size=struct.unpack_from('<I',e,28)[0]
            files.append(dict(path=path,size=size,directory=bool(e[11]&16),cluster=c))
            content=cluster_bytes(c)
            if e[11]&16: directory(content,path)
            else:
                target=out/'fat-files'/path.lstrip('/');target.parent.mkdir(parents=True,exist_ok=True)
                target.write_bytes(content[:size])
    directory(v[root:root+roots*32])
summary['partitions']=parts;summary['fat_files']=files

# Standard guided LZMA section inside the compact UEFI firmware volume.
fv=fw[0x70:]
assert fv[0x28:0x2c] == b'_FVH'
assert struct.unpack_from('<Q',fv,0x20)[0] == len(fv)
hdr=struct.unpack_from('<H',fv,0x30)[0];pos=(hdr+7)&~7
while pos+24<=len(fv):
    h=fv[pos:pos+24]
    if h in (b'\xff'*24,b'\0'*24):break
    size=int.from_bytes(h[20:23],'little')
    if size<24:break
    s=pos+24
    while s+4<=pos+size:
        n=int.from_bytes(fv[s:s+3],'little');typ=fv[s+3]
        if n<4:break
        if typ==2 and fv[s+4:s+20].hex() == '98584eee143959429d6edc7bd79403cf':
            dataoff=struct.unpack_from('<H',fv,s+20)[0]
            inner=lzma.decompress(fv[s+dataoff:s+n])
            target=out/'firmware';target.mkdir(exist_ok=True)
            (target/'inner-sections.bin').write_bytes(inner)
            ascii_strings=[x.decode('ascii') for x in re.findall(rb'[\x20-\x7e]{6,}',inner)]
            wide_strings=[x.decode('utf-16le') for x in re.findall(rb'(?:[\x20-\x7e]\x00){6,}',inner)]
            (target/'ascii-strings.txt').write_text('\n'.join(ascii_strings),encoding='utf8')
            (target/'utf16-strings.txt').write_text('\n'.join(wide_strings),encoding='utf8')
            summary['firmware_inner_size']=len(inner)
            summary['firmware_markers']=[x for x in ascii_strings+wide_strings if re.search(r'(?i)unixnu|xnu:|marble|Poco_F5',x)]
            regions={}
            for label in (b'UEFI FD\0',b'TZApps Reserved\0',b'Display Reserved\0'):
                off=inner.find(label)
                regions[label.rstrip(b'\0').decode()]=dict(base=hex(struct.unpack_from('<Q',inner,off+32)[0]),size=hex(struct.unpack_from('<Q',inner,off+40)[0]))
            summary['embedded_memory_regions']=regions
        s=(s+n+3)&~3
    pos=(pos+size+7)&~7
(out/'summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps(summary,ensure_ascii=False,indent=2))
for path,props in nodes.items():
    if (path in ('/','/memory','/memory@80000000') or re.search(r'/cpu@|interrupt-controller@|psci|framebuffer|splash|touch|goodix|focaltech|novatek|synaptics|kgsl-3d0@|gmu@|wifi@|wlan@',path)):
        print(path,json.dumps(props,ensure_ascii=False))
