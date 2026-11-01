"""Pin Mu SMEM transport allocation, runtime GET and receive dispatch code."""
import argparse,importlib.util,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def render():
 s=importlib.util.spec_from_file_location('smem_glink_digest',ROOT/'tools/marble-icb-bandwidth-code.py')
 m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
 m.INPUTS=[('glink','Glink','9fbbf72bc75995c01dd13a31b50eb83634d548202552b600cadbe57bcea443dc',[(0x5b70,0xc0),(0x5c30,0x510),(0x65e4,0x498),(0x6ab8,0x1c)])]
 code,meta=m.render()
 return code.replace('Exact Mu vector/complete and ICB type3 conversion code.','Exact Mu GLink SMEM runtime GET/RX and boot allocation paths.').replace('marble_icb_bandwidth_','marble_glink_smem_'),meta
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--write',action='store_true');a=p.parse_args();code,meta=render();path=ROOT/'xnuloader/marble_glink_smem_code.h'
 if a.write:path.write_text(code,encoding='utf8')
 elif path.read_text()!=code:raise ValueError('GLink SMEM code pins differ')
 print('PASS',sum(len(v) for v in json.loads(meta).values()),'native GLink SMEM code ranges')
if __name__=='__main__':main()
