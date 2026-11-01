"""Derive only marble's exact Gen7_4_0 HWCG table from locked Xiaomi source."""
import argparse,hashlib,json,re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--check',action='store_true');a=p.parse_args()
 src=ROOT.parent/'upstream-marble/gpu';manifest=json.loads((src/'sources.json').read_text(encoding='utf8'))
 assert manifest['commit']=='48952ed36228217531482b39d5bef13e7fd808ec'
 for name in ('adreno-gpulist.h','gen7_reg.h','adreno_gen7.c'):
  record=next(v for v in manifest['files'] if v['path'].endswith('/'+name))
  assert hashlib.sha256((src/name).read_bytes()).hexdigest()==record['sha256'],name
 text=(src/'adreno-gpulist.h').read_text(encoding='utf8');regs=(src/'gen7_reg.h').read_text(encoding='utf8')
 core=re.search(r'adreno_gpu_core_gen7_4_0\s*=\s*\{(.*?)\n\};',text,re.S).group(1)
 assert '.hwcg = gen7_0_0_hwcg_regs,' in core and 'qcom,adreno-gpu-gen7-4-0' in core
 table=re.search(r'gen7_0_0_hwcg_regs\[\]\s*=\s*\{(.*?)\n\};',text,re.S).group(1)
 symbols=dict((n,int(v,0)) for n,v in re.findall(r'^#define\s+(GEN7_\w+)\s+(0x[0-9a-fA-F]+)\s*$',regs,re.M))
 pairs=re.findall(r'\{\s*(GEN7_\w+),\s*(0x[0-9a-fA-F]+)\s*\}',table)
 assert len(pairs)==53 and len({n for n,v in pairs})==53
 lines=['/* Generated from locked Xiaomi Gen7_4_0 -> gen7_0_0_hwcg_regs.',' * Source SPDX: GPL-2.0-only; commit '+manifest['commit']+'.',' * Register/value facts only; no A740 table or live hardware evidence. */','#ifndef MARBLE_GEN7_HWCG_H','#define MARBLE_GEN7_HWCG_H','static const uint32_t marble_gen7_hwcg[][2]={']
 lines += [f'    {{0x{symbols[n]:05x},0x{int(v,0):08x}}}, /* {n} */' for n,v in pairs]
 lines += ['};','#define MARBLE_GEN7_HWCG_COUNT 53']
 assert '.gbif = gen7_0_0_gbif_regs,' in core
 gbif=re.search(r'gen7_0_0_gbif_regs\[\]\s*=\s*\{(.*?)\n\};',text,re.S).group(1)
 bus=re.findall(r'\{\s*(GEN7_\w+),\s*(0x[0-9a-fA-F]+)\s*\}',gbif)
 assert len(bus)==5
 lines += ['/* First four registers are CX state: snapshot and restore after drain. */','static const uint32_t marble_gen7_gbif[][2]={']
 lines += [f'    {{0x{symbols[n]:05x},0x{int(v,0):08x}}}, /* {n} */' for n,v in bus]
 lines += ['};','#define MARBLE_GEN7_GBIF_COUNT 5']
 start=(src/'adreno_gen7.c').read_text(encoding='utf8').split('int gen7_start(')[1].split('return 0;')[0]
 constants=re.findall(r'kgsl_regwrite\(device,\s*(GEN7_\w+),\s*(0x[0-9a-fA-F]+)\s*\)',start)
 wanted=['GEN7_UCHE_GBIF_GX_CONFIG','GEN7_RBBM_PERFCTR_GPU_BUSY_MASKED',
  'GEN7_UCHE_TRAP_BASE_LO','GEN7_UCHE_TRAP_BASE_HI','GEN7_UCHE_WRITE_THRU_BASE_LO',
  'GEN7_UCHE_WRITE_THRU_BASE_HI','GEN7_UCHE_CACHE_WAYS','GEN7_RBBM_PERFCTR_CNTL']
 facts=dict(constants);assert all(n in facts for n in wanted)
 lines += ['/* Unconditional literal writes from the locked gen7_start(). */','static const uint32_t marble_gen7_uche[][2]={']
 lines += [f'    {{0x{symbols[n]:05x},0x{int(facts[n],0):08x}}}, /* {n} */' for n in wanted]
 cmdq='FIELD_PREP(GENMASK(19,16),6)|FIELD_PREP(GENMASK(15,12),6)|FIELD_PREP(GENMASK(11,8),9)|BIT(3)|BIT(2)|FIELD_PREP(GENMASK(1,0),2)'
 assert 'kgsl_regwrite(device,GEN7_UCHE_CMDQ_CONFIG,'+cmdq+');' in re.sub(r'\s+','',start)
 lines += [f'    {{0x{symbols["GEN7_UCHE_CMDQ_CONFIG"]:05x},0x0006690e}}, /* GEN7_UCHE_CMDQ_CONFIG */','};','#define MARBLE_GEN7_UCHE_COUNT 9','#endif','']
 output='\n'.join(lines);path=ROOT/'xnu/files/bsd/dev/marble_gpu/marble_gen7_hwcg.h'
 if a.check:assert path.read_text(encoding='utf8')==output
 else:path.write_text(output,encoding='utf8')
 print('locked Gen7_4_0 HWCG facts:',len(pairs),'CHECK' if a.check else 'GENERATED')
if __name__=='__main__':main()
