"""Pin native receive-ID/clear ordering without invoking its callback dispatcher."""
import argparse,importlib.util,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def render():
 s=importlib.util.spec_from_file_location('ipcc_digest_generator',ROOT/'tools/marble-icb-bandwidth-code.py')
 m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
 m.INPUTS=[('ipcc','IPCC','e91f717f5a3a3e73701324fc4d8476416712af21a8a569a05fce0bb4befb30c1',[(0x3120,0x17c),(0x2784,0x10)])]
 code,meta=m.render()
 return code.replace('Exact Mu vector/complete and ICB type3 conversion code.','Exact Mu IPCC receive/clear order and MMIO access primitives.').replace('marble_icb_bandwidth_','marble_'),meta
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--write',action='store_true');a=p.parse_args();code,meta=render();path=ROOT/'xnuloader/marble_ipcc_code.h'
 if a.write:path.write_text(code,encoding='utf8')
 elif path.read_text()!=code:raise ValueError('IPCC code pins differ')
 print('PASS',sum(len(v) for v in json.loads(meta).values()),'native IPCC receive/clear code ranges')
if __name__=='__main__':main()
