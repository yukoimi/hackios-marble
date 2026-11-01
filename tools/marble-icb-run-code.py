"""Pin the published native ICB post-init/RUN transition and RPMh completion pump."""
import argparse,importlib.util,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def render():
 s=importlib.util.spec_from_file_location('icb_code_generator',ROOT/'tools/marble-icb-bandwidth-code.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
 m.INPUTS=[('icb','ICB','6cd27554f8edcf3bbe41473448354e55cd57461f0c29abbe18a5a8b6fdca596a',[(0x13f4,0x18),(0x2aa4,0xa8),(0x4088,0xd4),(0x4244,0x5c),(0x5900,0x2a4),(0x8e78,0xd0)]),
 ('rpmh','Rpmh','7f917c9e9c64ba11bee99f3d56f5474d39dac210f5a2dfad2059dd1d4afcd3c6',[(0x17e4,0x18),(0x2d00,0x138),(0x5a64,0xb0),(0x4978,0x168)])]
 code,meta=m.render();return code.replace('marble_icb_bandwidth_','marble_icb_run_'),meta
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--write',action='store_true');a=p.parse_args();code,meta=render()
 for path,content in [(ROOT/'xnuloader/marble_icb_run_code.h',code),(ROOT/'out/adsp-native-dependencies-v2-plan/icb-run-code-ranges.json',meta)]:
  if a.write:path.write_text(content,encoding='utf8')
  elif path.read_text(encoding='utf8')!=content:raise ValueError('native post-init code mismatch')
 print('PASS 10 native ICB post-init and RPMh completion code ranges')
if __name__=='__main__':main()
