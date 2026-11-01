"""Require the complete dependency-held no-execution ROM stage and recovery boundary."""
import argparse,importlib.util,json,re
from pathlib import Path
def load(name,file):
 s=importlib.util.spec_from_file_location(name,Path(__file__).with_name(file));m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
run=load('owned_run','marble-icb-run-report.py');xo=load('owned_xo','marble-xo-rpmh-report.py')
bw=load('owned_bw','marble-icb-bandwidth-report.py');smp=load('owned_smp','marble-smp2p-report.py')
def inspect(text,execute=0,qmp=False,scm_peak=False,wire=False,wire_messages=True,raw_shutdown=False,ipcc_ack=False,held=False,control=False,apps=False,gpr=False,amdb=False,amdb_cycle=False,tdm_graph=False,tdm_config=False,audio_clock=False,tdm_run=False,tdm_pcm_graph=False,audio_dma=False,pcm_memory=False,pcm_data=False,tdm_pins=False,mfc=False):
 errors=[]
 if mfc and not (tdm_pcm_graph and pcm_data==2):errors.append("MFC requires explicit two-request PCM graph contract")
 if bool("mfc: path " in text)!=bool(mfc):errors.append("MFC topology requires an exact explicit format contract")
 if tdm_pins and not pcm_data:errors.append("TDM pins require explicit silent PCM contract")
 if not tdm_pins and "tp: begin " in text:errors.append("TDM pin writes require explicit contract")
 if pcm_data and not pcm_memory:errors.append("PCM submission requires explicit shared-memory contract")
 if not pcm_data and "pd: q " in text:errors.append("PCM submission requires explicit contract")
 if pcm_memory and not (tdm_pcm_graph and audio_dma):errors.append("memory lifecycle requires explicit PCM graph/DMA contract")
 if not pcm_memory and "pm: lease " in text:errors.append("memory lifecycle requires explicit contract")
 if audio_dma and not tdm_pcm_graph:errors.append('DMA inventory requires explicit PCM graph contract')
 if not audio_dma and 'marble-audio-dma: begin ' in text:errors.append('DMA inventory requires explicit read-only contract')
 if tdm_pcm_graph and not tdm_run:errors.append('PCM graph requires explicit TDM run contract')
 if not tdm_pcm_graph and 'tr: topology ' in text:errors.append('PCM graph requires explicit topology contract')
 if tdm_run and (not gpr or amdb or amdb_cycle or tdm_graph or audio_clock):errors.append('TDM run requires exclusive GPR contract')
 if not tdm_run and 'tr: end ' in text:errors.append('TDM run requires explicit contract')
 if audio_clock and (not gpr or amdb or amdb_cycle or tdm_graph):errors.append('audio clock requires explicit exclusive GPR contract')
 if not audio_clock and 'prm: complete ' in text:errors.append('audio clock requires explicit contract')
 if tdm_config and not tdm_graph:errors.append('TDM calibration requires explicit graph contract')
 if not tdm_config and 'marble-tdm-graph: configuration ' in text:errors.append('TDM calibration requires explicit contract')
 if tdm_graph and (not gpr or amdb or amdb_cycle):errors.append('TDM graph requires explicit exclusive GPR contract')
 if not tdm_graph and 'marble-tdm-graph: complete ' in text:errors.append('TDM graph requires explicit contract')
 if amdb_cycle and (not gpr or amdb):errors.append('library cycle requires explicit exclusive GPR contract')
 if not amdb_cycle and 'marble-amdb-cycle: complete ' in text:errors.append('library cycle requires explicit contract')
 if amdb and not gpr:errors.append('AMDB requires the explicit GPR application contract')
 if not amdb and 'marble-amdb: complete ' in text:errors.append('AMDB requires explicit metadata contract')
 if gpr and not apps:errors.append("GPR requires explicit application query contract")
 if apps and not control:errors.append('adsp_apps requires the explicit DSP control contract')
 if control and not (execute==1 and held and raw_shutdown and wire and scm_peak and not qmp and not ipcc_ack):errors.append('control link requires the explicit held SCM DSP boot contract')
 if not control and 'marble-glink-control: begin ' in text:errors.append('DSP control service requires an explicit contract')
 if held and ((execute and not control) or not wire or not raw_shutdown or not scm_peak or ipcc_ack):errors.append('held load_state requires the explicit independent no-exec SCM/raw-stop contract')
 if not held and 'marble-qmp-hold: on ' in text:errors.append('held load_state requires an explicit combined contract')
 if wire and ((execute and not control) or qmp or not scm_peak):errors.append('AP wire requires the independent SCM no-execution contract')
 if not wire and 'marble-qmp-wire: qualified ' in text:errors.append('AP wire side effects require an explicit combined contract')
 if raw_shutdown and (not wire or not wire_messages or not scm_peak or (execute and not control)):errors.append('bounded shutdown requires the explicit SCM/AP-wire no-exec contract')
 if not raw_shutdown and 'marble-smccc: PAS_stop ' in text:errors.append('bounded shutdown requires an explicit combined contract')
 if ipcc_ack and not raw_shutdown:errors.append('IPCC acknowledgement requires the bounded no-exec stop contract')
 if not (ipcc_ack or held) and 'marble-qmp-ipcc: qualified ' in text:errors.append('IPCC acknowledgement requires an explicit combined contract')
 if not qmp and 'marble-qmp-load: qualified ' in text:errors.append('QMP side effects require the explicit combined contract')
 markers=['marble-adsp-owned: begin contract=RUN-XO-LPI-BW-ROM-noexec-reset-v1 package_bytes=30180144 package_fingerprints=2 DSP_start=0\n',
  'marble-adsp-owned: phase=install retained_watchdog=1\n','marble-adsp-owned: phase=smp2p DSP_off=1\n',
  'marble-adsp-owned: phase=lpi configured_level=256\n','marble-adsp-owned: phase=xo LPI_clients=2 LPI_level=256\n',
  'marble-adsp-owned: phase=bandwidth baseline=RUN_XO_LPI_held\n',
  'marble-adsp-owned: phase=ROM-noexec bandwidth_held=1 XO_held=1 LPI_clients=2 DSP_start=0\n']
 if execute:
  markers[0]=markers[0].replace('ROM-noexec-reset','ROM-boot-stop-reset')
  markers[-1]=markers[-1].replace('ROM-noexec','ROM-boot')
 if scm_peak:markers[0]=markers[0].replace('LPI-BW-ROM','LPI-SCM-BW-ROM')
 pos=[text.find(s) for s in markers]
 if min(pos)<0 or pos!=sorted(pos) or any(text.count(s)!=1 for s in markers):errors.append('owned no-exec phase boundary missing')
 native=text[pos[0]:pos[1]] if min(pos[:2])>=0 else ''
 xotext=text[pos[4]:pos[5]] if min(pos[4:6])>=0 else ''
 bwtext=text[pos[5]:] if pos[5]>=0 else ''
 metadata=bw.power.inspect_dependencies(native)
 peak=0xffffffff*1000 if scm_peak else 1000000
 reports={'run':run.inspect(native),'xo':xo.inspect(xotext),'bandwidth':bw.inspect(bwtext,True,3 if scm_peak else 2,metadata,15000000,execute,peak,control_execute=control),
  'powered_ROM':smp.inspect_boot(text) if execute else smp.inspect_powered_stage(text)}
 if qmp:
  qmp_report=load('owned_qmp','marble-qmp-native-report.py')
  reports['qmp']=qmp_report.inspect(text,settled_close=True,mailbox_load_state=True,owned=True)
  stages=['marble-npa-client: request index=1 ',markers[4],markers[-1],
    'marble-pil-held: loaded ','marble-qmp-load: tx_complete index=0 ',
    'marble-qmp-load: tx_complete index=1 ','marble-qmp-native: close_state ',
    'marble-pil-held: shutdown ','marble-pil-held: restored ','marble-icb-bw: completed ','marble-npa-held: returned ']
  indices=[text.find(s) for s in stages]
  if min(indices)<0 or indices!=sorted(indices):errors.append('LPI/QMP/XO/ROM release ordering missing')
 if wire:
  wire_report=load('owned_wire','marble-qmp-wire-report.py')
  reports['wire']=wire_report.inspect(text,messages=wire_messages,ipcc=ipcc_ack)
  stages=['marble-pil-held: loaded ','marble-pil-progress: AP-wire-post-load-before address=1',
    'marble-qmp-wire: qualified ','marble-qmp-wire: complete PASS=1',
    'marble-pil-progress: AP-wire-noexec-unwind-return address=1',
    'marble-pil-held: shutdown ','marble-pil-held: restored ','marble-icb-bw: completed ']
  indices=[text.find(s) for s in stages]
  if not held and (min(indices)<0 or indices!=sorted(indices)):errors.append('AP wire must unwind after ROM load and before PAS shutdown while leases are held')
 if pcm_memory:reports['PCM_memory']=load('pcm_memory_audit','marble-pcm-memory-report.py').inspect(text,pcm_data=pcm_data)
 if pcm_data:reports['PCM_data']=load('pcm_data_audit','marble-pcm-data-report.py').inspect(text,requests=int(pcm_data),mfc=mfc)
 if tdm_pins:reports['TDM_pins']=load('audio_pins_audit','marble-audio-pins-report.py').inspect(text)
 for k,v in reports.items():errors.extend(k+': '+e for e in v['errors'])
 def rows(prefix):return [dict(re.findall(r'(\w+)=([^\s]+)',l)) for l in text.splitlines() if l.startswith(prefix)]
 def exact(r,**v):return all(r.get(k)==str(x) for k,x in v.items())
 if raw_shutdown:
  raw=rows('marble-smccc: PAS_stop ')
  try:
   if len(raw)!=1 or not exact(raw[0],PASS=1,fn=42000206,arginfo=1,PAS_id=1,initial_a6=0,initial_a7=0,outcome=0,raw_a0=0,raw_a1=0,WAITQ_replays=0,auth_reset=0,DSP_start=execute) or not 1<=int(raw[0]['calls'])<=(128 if control else 64) or not 0<int(raw[0]['elapsed_us'])<(1000000 if control else 250000) or not 0<=int(raw[0]['previous_TPL'])<=8 or int(raw[0]['DAIF_before'],16)!=int(raw[0]['DAIF_after'],16):raise ValueError('raw shutdown result or register contract')
  except (KeyError,ValueError) as exc:errors.append('invalid bounded shutdown: '+str(exc))
  stages=['marble-qmp-hold: on PASS=1' if held else 'marble-qmp-wire: complete PASS=1','marble-pil-progress: PAS-metadata-shutdown-before','marble-smccc: PAS_stop ','marble-pil-progress: PAS-metadata-shutdown-return','marble-pil-held: shutdown ']
  indices=[text.find(s) for s in stages]
  if min(indices)<0 or indices!=sorted(indices):errors.append('bounded shutdown must follow wire return and precede verified cleanup')
  if ipcc_ack:
   stages=['marble-pil-held: loaded ','marble-qmp-ipcc: qualified ','marble-qmp-wire: complete ','marble-qmp-ipcc: complete ',
     'marble-pil-progress: QMP-IPCC-owned-ack address=1','marble-pil-progress: PAS-metadata-shutdown-before']
   indices=[text.find(s) for s in stages]
   if min(indices)<0 or indices!=sorted(indices):errors.append('owned IPCC acknowledgement must precede bounded PAS shutdown')
 if held:
  def one(prefix):
   found=rows(prefix);return found[0] if len(found)==1 else {}
  if not exact(one('marble-qmp-hold: on '),PASS=1,mailbox_ACKs=1,IPCC_clears=1,channel_open=1,PAS_stop_calls=0,DSP_start=0):errors.append('load_state ON and owned receive ACK missing before PAS')
  if not exact(one('marble-qmp-hold: stop '),PASS=1,on_held=1,mailbox_ACKs=1,IPCC_clears=1,DSP_start=execute):errors.append('PAS must stop while load_state ON is held')
  if not exact(one('marble-qmp-hold: complete '),PASS=1,ON_before_PAS=1,PAS_before_OFF=1,mailbox_ACKs=2,IPCC_clears=2,wire_preserved=1,DSP_start=execute,audio_acceptance=0):errors.append('load_state OFF/close/IRQ restoration missing')
  notifications=rows('marble-qmp-hold: notify ')
  if len(notifications)!=2 or any(not exact(row,stage=i+1,PASS=1,observed=0,after='ffffffff',clear_writes=i+1,foreign_clears=0,IRQ_enable_writes=0,GIC_writes=0) for i,row in enumerate(notifications)):errors.append('two separately handled QMP notifications required')
  if not exact(one('marble-pil-held: post_shutdown_gate '),PASS=1,PAS_stopped=1,payload_restore=0):errors.append('held lifecycle must complete before payload restoration')
  qualified=rows('marble-qmp-ipcc: qualified ')
  if len(qualified)!=1 or not exact(qualified[0],APPS_client=8,source_client=0,signal=0,base='ed18000',receive_offset=10,clear_offset='1c',code_ranges=2,before='ffffffff',IRQ_enable_writes=0,GIC_writes=0,native_callbacks=0):errors.append('initial empty IPCC ownership missing')
  order=['marble-pil-held: loaded ','marble-qmp-ipcc: qualified ','marble-qmp-wire: send_complete index=0 ','marble-qmp-hold: notify stage=1 ',
    'marble-qmp-hold: on ','marble-pil-progress: QMP-held-on-return address=1','marble-pil-held: post_load_gate ',
    'marble-pil-progress: PAS-metadata-shutdown-before','marble-qmp-hold: stop ','marble-smccc: PAS_stop ','marble-pil-held: shutdown ',
    'marble-qmp-wire: send_before index=1 ','marble-qmp-wire: complete ','marble-qmp-hold: notify stage=2 ',
    'marble-qmp-hold: complete ','marble-pil-held: post_shutdown_gate ','marble-pil-held: restored ']
  indices=[text.find(x) for x in order]
  if min(indices)<0 or indices!=sorted(indices):errors.append('required ordering ON -> PAS shutdown -> OFF/close -> payload restore missing')
 if control:
  inventory=load('control_smem_report','marble-glink-smem-report.py')
  reports['SMEM_before_execution']=inventory.inspect(text[:text.find(markers[0])])
  runtime=load('control_runtime_report','marble-glink-control-report.py')
  reports['control_link']=runtime.inspect(text,apps=apps,gpr=gpr,amdb=amdb,amdb_cycle=amdb_cycle,tdm_graph=tdm_graph,tdm_config=tdm_config,audio_clock=audio_clock,tdm_run=tdm_run,tdm_pcm_graph=tdm_pcm_graph,audio_dma=audio_dma,pcm_memory=pcm_memory,pcm_data=pcm_data,tdm_pins=tdm_pins,mfc=mfc)
  for name in ('SMEM_before_execution','control_link'):errors.extend(name+': '+e for e in reports[name]['errors'])
 for prefix,fields in [
  ('marble-pil: install_complete ',dict(PASS=1,watchdog_clear='ffffffffffffffff',watchdog_retained=1,reboot_required=1,functional_acceptance=0)),
  ('marble-adsp-dependency: simultaneous ',dict(PASS=1,XO_ack=1,bandwidth_ib=peak,LPI_clients=2,LPI_level=256,hold_us=10000,DSP_start=0)),
  ('marble-adsp-owned: action_return ',dict(outcome=1,bandwidth_release_allowed=1)),
  ('marble-adsp-owned: complete ',dict(PASS=1,ROM_stage_restored=1,LPI_released=1,LPI_active=0,XO_retained=1,thermal_ok=1,ICB_RUN_retained=1,SMEM_retained=1,watchdog_armed=1,cold_reset_required=1,original_hardware_restored=0,DSP_start=execute,storage_access=0,audio_acceptance=0))]:
  found=rows(prefix)
  if len(found)!=1 or not exact(found[0],**fields):errors.append('missing owned-stage record: '+prefix)
 order=['marble-icb-bw: requested ','marble-adsp-dependency: simultaneous ',markers[-1],
  'marble-pil-held: loaded ','marble-pil-held: shutdown ','marble-pil-held: restored ',
  'marble-pil-ramfs: released ','marble-adsp-owned: action_return ','marble-icb-bw: completed ',
  'marble-icb-bw: complete ','marble-npa-held: returned ','marble-npa-client: destroy index=1 ','marble-adsp-owned: complete ']
 indices=[text.find(s) for s in order]
 if min(indices)<0 or indices!=sorted(indices):errors.append('ROM stage must remain inside all power/clock/bandwidth leases')
 try:
  inner=rows('marble-icb-bw: complete ');outer=rows('marble-npa-held: returned ');end=rows('marble-npa-client: complete ')
  if len(inner)!=1 or len(outer)!=1 or len(end)!=1 or not 0<int(inner[0]['elapsed_us'])<=int(outer[0]['elapsed_us'])<=int(end[0]['elapsed_us'])<15000000:raise ValueError('nested elapsed budgets')
  if qmp:
   elapsed=rows('marble-qmp-native: complete ')
   if len(elapsed)!=1 or not 0<int(elapsed[0]['elapsed_us'])<=int(inner[0]['elapsed_us']):raise ValueError('QMP post-load lifetime must remain inside bandwidth lease')
  if wire:
   elapsed=rows('marble-qmp-wire: complete ')
   if len(elapsed)!=1 or not 0<int(elapsed[0]['elapsed_us'])<=int(inner[0]['elapsed_us']):raise ValueError('AP wire lifetime must remain inside bandwidth lease')
 except (KeyError,ValueError) as e:errors.append('invalid power lifetime: '+str(e))
 if execute:
  ready='marble-pil-progress: owned-dependencies-ready address=1'
  auth=text.find('marble-pil-boot: auth ')
  if text.count(ready)!=1 or not 0<=pos[-1]<text.find(ready)<auth:errors.append('new owned dependency authorization missing before secure auth')
 refusal=rows('marble-adsp-owned: refused ')
 clean=len(refusal)==1 and exact(refusal[0],clean=1,attempted=0,dirty=0) and not any(s in text for s in ('marble-icb-run: complete ','marble-pil: load_image '))
 if refusal and not clean:errors.append('contradictory refusal')
 return dict(passed=not errors,errors=errors,reset_safe=not errors,cleanup_verified=False,clean_refusal=clean,
  dependency_held_ROM_verified=not errors,original_hardware_restored=False,dsp_started=(True if not errors else None) if execute else False,
  qmp_held_ROM_verified=False,qmp_noexec_unwind_verified=qmp and not errors,
  AP_wire_noexec_unwind_verified=wire and wire_messages and not held and not errors,
  AP_channel_noexec_unwind_verified=wire and not wire_messages and not errors,
  bounded_PAS_shutdown_verified=raw_shutdown and not errors,
  owned_IPCC_PAS_shutdown_verified=ipcc_ack and raw_shutdown and not errors,
  load_state_held_through_shutdown_verified=held and not errors,
  GLINK_control_link_verified=control and not errors,
  adsp_apps_channel_verified=apps and not errors,
  scm_peak_held_ROM_verified=scm_peak and not errors,
  dsp_auth_attempted='marble-pil-progress: PAS-auth-reset-before' in text or 'marble-pil-boot: auth ' in text,
  dsp_shutdown_verified=bool(execute and not errors),audio_acceptance=False,
  scope=('native RUN/XO/LPI256/'+('Linux-SCM-logical-peak' if scm_peak else '1MB/s')+
    (' held during secure DSP auth, fresh ready/stop ACK and verified PAS shutdown/restore' if execute else ' held during ROM load/readback/PAS no-exec shutdown/restore'))+'; bandwidth and LPI released; remaining firmware/SMEM/XO state requires cold reset; no audio acceptance')
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('log',type=Path);a=p.parse_args();r=inspect(a.log.read_text(errors='replace'));print(json.dumps(r,indent=2));raise SystemExit(not r['passed'])
