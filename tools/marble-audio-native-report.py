"""Audit real EFI-owned native endpoint evidence, never iOS playback acceptance."""
import re

def inspect(text,require_proof=False):
 errors=[]
 def rows(prefix):return [dict(re.findall(r'(\w+)=([^\s]+)',s)) for s in text.splitlines() if s.startswith(prefix)]
 def one(prefix,**expected):
  found=rows(prefix)
  if len(found)!=1 or any(found[0].get(k)!=str(v) for k,v in expected.items()):
   errors.append('missing/invalid '+prefix);return {}
  return found[0]
 begin=one('aud-native: phase=begin ',active=1,failed=0,submitted=0,completed=0,published=0,configured=0,owner='EFI',zero=1,XNU_sink=0,audio_acceptance=0)
 retired=one('aud-native: phase=progress active=1 failed=0 stream=8 ',pending=0,started=1,submitted=2,completed=2,published=2,configured=1,PAS_stopped=0,restored=0)
 stop=one('aud-native: phase=stopped ',active=0,failed=0,submitted=2,completed=2,published=2,configured=1,PAS_stopped=1,restored=1,owner='EFI',zero=1,XNU_sink=0,audio_acceptance=0)
 auth=one('marble-pil-boot: auth ',status=0,transport=1,secure=0,accepted=1,hold_limit_ms=600)
 runtime=one('marble-pil-boot: observed ',thermal_ok=1,runtime_ok=1,link_invalid=0)
 pas=one('marble-smccc: PAS_stop ',PASS=1,fn='42000206',arginfo=1,PAS_id=1,WAITQ_replays=0,DSP_start=1)
 one('marble-smp2p-boot: stopped ',PASS=1,ready_seen=1,stop_ack_seen=1,stop_clear=1,PAS_shutdown_allowed=1)
 one('marble-pil-held: shutdown ',status=0,transport=1,secure=0,verified=1)
 one('marble-qmp-hold: complete ',PASS=1,ON_before_PAS=1,PAS_before_OFF=1,mailbox_ACKs=2,IPCC_clears=2,wire_preserved=1)
 one('marble-pil-held: restored ',PASS=1,original_restored=1,backup_retained=0,shutdown_before_restore=1)
 one('marble-pil-header: complete ',PASS=1,metadata_clear=1,auth_reset=1,dsp_start=1)
 one('marble-pil-ramfs: released ',PASS=1,installed=0,dirty=0,file_writes=0,block_reads=0,block_writes=0)
 one('marble-glink: released ',PASS=1,active=0,dirty=0,invalid=0)
 pins=one('tp: end ',PASS=1,active=0,restored=1,writes=12,PAS_stopped=0,amp_changes=0)
 mapping=one('marble-audio-map: result ',PASS=1,stage=9,status=0,context=5,allocated=1,programmed=1,translated=1,disabled=1,restored=1,freed=1,retained=0,translation_mode=2,stream_writes=8,PCM_requests=2)
 final=one('marble-adsp-owned: complete ',PASS=1,ROM_stage_restored=1,LPI_released=1,LPI_active=0,thermal_ok=1,watchdog_armed=1,cold_reset_required=1,storage_access=0,audio_acceptance=0)
 amp=one('aw: end ',PASS=1,window_passed=1,DSP_passed=1,DSP_dirty=0,config_active=0,clock_active=0,dirty=0)
 if require_proof or rows('aud-owner-proof: '):
  configure=one('aud-owner-proof: phase=configure ',flags=15,MAP_typed=1,START_typed=1,START_TX=1,submitted=1,completed=0,audible=0)
  retired_proof=one('aud-owner-proof: phase=retired ',flags=63,MAP_typed=1,START_typed=1,UNMAP_typed=1,UNMAP_TX=1,submitted=2,completed=2,audible=0)
  try:
   if not 0<int(configure['epoch'],16)==int(retired_proof['epoch'],16)<1<<64:raise ValueError('epoch mismatch')
  except (ValueError,KeyError):errors.append('native resource-owner proof epoch absent/mismatched')
  positions=[text.find(x) for x in ('aud-native: phase=progress active=1 failed=0 stream=3 ',
             'aud-owner-proof: phase=configure ','aud-native: phase=progress active=1 failed=0 stream=8 ',
             'aud-owner-proof: phase=retired ','tp: end ','marble-smccc: PAS_stop ')]
  if min(positions)<0 or positions!=sorted(positions):errors.append('owner proof is not between real START/UNMAP and resource release')
 for device in ('34','35'):
  for kind,extra in [('analog',dict(ready=1)),('digital',dict(locked=1,AMPPD_retained=0))]:
   one(f'marble-aw-{kind}: complete device={device} ',PASS=1,restored=1,dirty=0,HMUTE_retained=1,attenuation_db=90,speaker_switch_writes=0,**extra)
 try:
  if not 0<=int(auth['elapsed_us'])<1000000 or not 300000<=int(runtime['hold_us'])<750000 or not 0<=int(runtime['die_mc'])<55000:raise ValueError('runtime bound')
  if not 1<=int(pas['calls'])<=128 or not 0<=int(pas['elapsed_us'])<1000000 or not 10<=int(stop['RX'])<=256 or not 10<=int(stop['TX'])<=64:raise ValueError('PAS/FIFO bound')
  allocation=int(mapping['allocation'],16);physical=int(mapping['physical'],16)
  if allocation&4095 or not 0<allocation<(1<<36)-16384 or physical!=allocation+12288 or (int(mapping['par'],16)>>12)&0xfffffffff!=physical:raise ValueError('DMA translation')
 except (ValueError,KeyError):errors.append('numeric runtime/DMA/PAS evidence unavailable or out of bound')
 order=['aud-native: phase=begin ','marble-pil-boot: auth ','marble-audio-context: complete PASS=1',
        'aud-native: phase=progress active=1 failed=0 stream=8 ','tp: end ',
        'marble-smp2p-boot: stopped ','marble-smccc: PAS_stop ',
        'marble-audio-map: result ','aud-native: phase=stopped ',
        'marble-pil-held: shutdown ','marble-pil-held: restored ','marble-adsp-owned: complete ','aw: end ']
 positions=[text.find(x) for x in order]
 if min(positions)<0 or positions!=sorted(positions) or len(set(positions))!=len(positions):errors.append('native resource/START/protocol/PAS/restore order')
 if 'failure-retain' in text or 'panic(cpu' in text or 'EARLY KERNEL EXCEPTION' in text or 'tr: q ' in text or 'pr: retired ' in text:errors.append('fault or concurrent legacy FIFO owner')
 return dict(passed=not errors,errors=errors,reset_safe=not errors,audio_acceptance=False,continuous_playback=False,XNU_sink=False,
             scope='real DSP, XNU protocol engine hosted by EFI owner, two zero periods and full shutdown; no cross-core handoff or audible playback')
