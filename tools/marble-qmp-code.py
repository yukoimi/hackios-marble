"""Pin the supplied GlinkDxe QMP TX path and its completion ownership."""
import argparse, importlib.util, json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def render():
    s=importlib.util.spec_from_file_location('qmp_digest_generator',ROOT/'tools/marble-icb-bandwidth-code.py')
    m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
    m.INPUTS=[('glink','Glink','9fbbf72bc75995c01dd13a31b50eb83634d548202552b600cadbe57bcea443dc',[
        (0x152c,0x38),(0x27c0,0x12c),(0x3534,0xfc),(0x3854,0x9c),
        (0x3948,0x74),(0x7000,0x104),(0x72e0,0x424),(0x3484,0xb0),
        (0x3084,0x1c0),(0x2f4c,0x34),(0x4e90,0x44),(0x3244,0x84),(0x50e8,0xc)])]
    code,meta=m.render()
    return code.replace('Exact Mu vector/complete and ICB type3 conversion code.','Exact Mu QMP TX, mailbox ACK and client completion code.').replace('marble_icb_bandwidth_','marble_qmp_'),meta

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--write',action='store_true');a=p.parse_args()
    code,meta=render();path=ROOT/'xnuloader/marble_qmp_code.h'
    if a.write:path.write_text(code,encoding='utf8')
    elif path.read_text()!=code:raise ValueError('QMP code pins differ')
    print('PASS',sum(len(v) for v in json.loads(meta).values()),'native QMP TX code ranges')
if __name__=='__main__':main()
