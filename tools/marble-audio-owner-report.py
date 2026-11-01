"""Require real XNU FIFO ownership and PAS/unmap/free closure; no sound claim."""
import re
def inspect(text,board=False,periods=2,pcm=False,signal_oracle=None,tpl_sampling=False,usb_poll_lease=False,board_held=False,refill=False,qmp_close_snapshot=False,native_link_only=False,stream_window=False,runtime_sampling=False,usb_qmp_lease=False,fast_handshake=False,handshake_ms=10,warmup_ms=0,prelaunch=False,display_dma_only=False):
 errors=[]
 pcm_data_valid=False
 if stream_window and not (periods==24 and pcm and board and board_held and native_link_only and qmp_close_snapshot and not refill):return dict(passed=False,errors=["stream24 requires distinct v2 held native producer profile"])
 if stream_window and signal_oracle is None:return dict(passed=False,errors=["stream24 requires independent oracle"])
 if native_link_only and not ((refill or stream_window) and qmp_close_snapshot):return dict(passed=False,errors=["native-only link requires explicit refill/coherent-close profile"])
 if qmp_close_snapshot and not (refill or stream_window):return dict(passed=False,errors=["QMP close snapshot requires explicit refill"])
 if periods not in (2,8) and not (stream_window and periods==24):return dict(passed=False,errors=["unqualified owner period count"])
 if pcm and (not board or (periods!=8 and not stream_window)):return dict(passed=False,errors=["PCM requires eight muted board periods"])
 if refill and not (board_held and pcm and board and periods==8):return dict(passed=False,errors=["refill requires held muted PCM8"])
 if refill and signal_oracle is None:return dict(passed=False,errors=["refill requires independent signal oracle"])
 if board_held and not (board and pcm and (periods==8 or stream_window)):return dict(passed=False,errors=["board hold requires muted PCM8"] )
 if signal_oracle is not None and not pcm:return dict(passed=False,errors=["signal oracle requires PCM profile"])
 if signal_oracle is not None and not isinstance(signal_oracle,dict):return dict(passed=False,errors=["invalid signal oracle type"])
 def rows(prefix):
  return [dict(re.findall(r'(\w+)=([^\s]+)',line[line.find(prefix):])) for line in text.splitlines() if prefix in line]
 def one(prefix,**expected):
  found=rows(prefix)
  if len(found)!=1 or any(found[0].get(k)!=str(v) for k,v in expected.items()):errors.append('missing/invalid '+prefix);return {}
  return found[0]
 if display_dma_only and not (board and stream_window and periods==24):return dict(passed=False,errors=['audio DMA audit requires muted board stream24'])
 dma_modes=rows('marble-display-memory: mode=')
 if display_dma_only:
  begins=rows('marble-display-memory: begin ');ends=rows('marble-display-memory: complete ');exact=rows('marble-display-memory: exact_single_GOP_disjoint=')
  if len(begins)<2 or not len(dma_modes)==len(begins)==len(ends)==len(exact):errors.append('missing audio display DMA audit lifecycle')
  for m in dma_modes:
   if any(m.get(k)!=v for k,v in dict(mode='audio-DMA-only',bus_lease='1',timing_qualified='0',samples='2',fields='66',interval_us='20000').items()):errors.append('invalid audio display DMA audit mode')
  for b in begins:
   if any(b.get(k)!='1' for k in ('exact_DT_offsets',)) or b.get('payload_access')!='0' or b.get('register_writes')!='0':errors.append('invalid display DMA audit access')
  for e in ends:
   if any(e.get(k)!=v for k,v in dict(stable='1',demura_enabled_samples='0',overlapping_pipe_samples='0',bus_restored='1',audit_only='1',payload_write_approved='0').items()):errors.append('failed display DMA audit or lease restoration')
  if any(e.get('exact_single_GOP_disjoint')!='1' for e in exact):errors.append('display GOP isolation not proved')
  # Require each ordered audit to include all 66 actual register fields in
  # both samples. Missing/duplicate pipe rows cannot substitute for reads.
  blocks=re.findall(r'marble-display-memory: mode=audio-DMA-only[^\n]*\n(.*?)marble-display-memory: complete [^\n]*',text,re.S)
  if len(blocks)!=len(dma_modes):errors.append('unordered display DMA audits')
  for block in blocks:
   for sample in range(2):
    ds=re.findall(r'marble-display-memory: sample='+str(sample)+r' dspp=(\d+) demura_status=([0-9a-f]+) fetch_enabled=(\d+)',block)
    ps=re.findall(r'marble-display-memory: sample='+str(sample)+r' pipe=(\d+) size=([0-9a-f]+) rect1=([0-9a-f]+) strides=([0-9a-f]+),([0-9a-f]+) addresses=([0-9a-f]+),([0-9a-f]+),([0-9a-f]+),([0-9a-f]+) conservative_overlap=(\d+)',block)
    if [d[0] for d in ds]!=['0','1'] or any(d[2]!='0' for d in ds) or [q[0] for q in ps]!=list(map(str,range(8))) or any(q[-1]!='0' for q in ps):errors.append('incomplete display DMA register samples')
 elif dma_modes:errors.append('unexpected audio DMA-only display audit')
 if prelaunch and not (fast_handshake and handshake_ms==100 and warmup_ms==100):return dict(passed=False,errors=['prelaunch requires explicit original100ms cadence'])
 if warmup_ms not in (0,100) or (warmup_ms and not fast_handshake):return dict(passed=False,errors=['unqualified startup warmup'])
 if handshake_ms not in (10,50,100):return dict(passed=False,errors=['unqualified handshake interval'])
 if fast_handshake and not usb_qmp_lease:return dict(passed=False,errors=['fast handshake requires bounded QMP lease'])
 if usb_qmp_lease and not (usb_poll_lease and (runtime_sampling or fast_handshake) and qmp_close_snapshot):return dict(passed=False,errors=['QMP lease requires explicit USB/runtime/coherent-close profile'])
 if usb_poll_lease:
  if not board or tpl_sampling:errors.append('USB poll lease requires explicit board profile without diagnostic sampler')
  one('aud-usb-poll-preflight: ',PASS=1,timer_writes=0,DSP_start=0)
  lease=one('aud-usb-poll-lease: ',scope='PAS-XNU-unmap-free-QMP-v3' if usb_qmp_lease else 'PAS-XNU-unmap-free-v2',qualified=1,attempted=1,cancelled=1,resume_attempted=1,restored=1,failed=0,period_100ns=1000000,cancel_status=0,resume_status=0,cadence_rebased=1,timer_API_only=1,CloseEvent_calls=0,pending_removals=0,charger_writes=0,IRQ_changes=0,full_PAS_limit_us=1000000)
  if usb_qmp_lease:
   close_pos=text.find('marble-qmp-hold: complete PASS=1 ');resume_pos=text.find('aud-usb-poll-lease: ')
   if close_pos<0 or resume_pos<=close_pos:errors.append('QMP must close before poll resume')
  metadata_stop=one('marble-pil-PAS: phase=metadata-shutdown ',status=0,transport=1,secure=0,dirty=0,PASS=1,auth_reset=1,dsp_start=1)
  later=one('aud-usb-poll-later: ',PASS=1,checked=1,binding=1,period_100ns=1000000,notify_link=0,scheduler_progress=1,timer_writes=0,callback_calls=0)
  try:
   if not 0<int(lease['hold_us'])<1000000 or int(lease['event'],16)<0x80000000 or int(lease['callback'],16)<0x80004f70:raise ValueError('lease bounds')
   if not int(lease['hold_us'])<=int(metadata_stop['elapsed_us'])<1000000:raise ValueError('outer metadata-stop bound')
   positions=[text.find(p) for p in ('aud-usb-poll-preflight: ','marble-pil-boot: auth ','marble-smccc: PAS_stop ','aud-owner-fw: phase=DMA-released-after-XNU-unmap ','aud-usb-poll-lease: ','marble-pil-PAS: phase=metadata-shutdown ')]
   if min(positions)<0 or positions!=sorted(positions):raise ValueError('lease lifecycle ordering')
   if not 0<int(later['resume_trigger'],16)<int(later['later_trigger'],16) or int(later['timer_link'],16)<0x80000000 or text.find('aud-usb-poll-later: ')<text.find('aud-owner-session: '):raise ValueError('poll did not make later progress')
  except (KeyError,ValueError):errors.append('invalid USB poll pause/resume evidence')
 elif rows('aud-usb-poll-lease: '):errors.append('unexpected foreign USB timer lease')
 if prelaunch:
  one('aud-owner-prelaunch: ',PASS=1,phase='before-QMP-channel',before_QMP_ON=1,cap_inactive=1,cap_result=0,CPU_ON_calls=1,deferred_cap_service=1,cap_request=1,limit_us=500000,DSP_start=0,FIFO_writes=0)
  try:
   if not 0<int(rows('aud-owner-prelaunch: ')[0]['elapsed_us'])<500000:raise ValueError('prelaunch deadline')
  except (IndexError,KeyError,ValueError):errors.append('invalid prelaunch wait bound')
  one('aud-owner-prelaunch-reuse: ',PASS=1,CPU_ON_calls=1,no_second_CPU_ON=1,after_QMP_ON=1)
  positions=[text.find(p) for p in ('marble-pil-held: loaded ','aud-owner-prelaunch: ','marble-qmp-wire: opened ','marble-qmp-hold: on ','aud-owner-prelaunch-reuse: ','aud-owner-fw: phase=kernel-ready-before-AUTH ','marble-pil-boot: auth ')]
  if min(positions)<0 or positions!=sorted(positions):errors.append('prelaunch/ROM/QMP/READY/AUTH ordering')
 elif rows('aud-owner-prelaunch: '):errors.append('unexpected early CPU launch')
 if fast_handshake:
  one('aud-owner-handshake: ',service_us=handshake_ms*1000,warmup_us=warmup_ms*1000,initial_ms=300,later_service_us=2000,minimum_runtime_us=300000,maximum_runtime_us=600000,real_replies_required=1,FIFO_owner='XNU',IRQ_changes=0)
 elif rows('aud-owner-handshake: '):errors.append('unexpected fast handshake profile')
 if runtime_sampling and (not stream_window or tpl_sampling):errors.append('runtime sampler requires stream24 without PAS sampler')
 if runtime_sampling or fast_handshake or usb_qmp_lease:
  for phase in ('typed-complete','runtime-exit'):
   deadline=one('aud-runtime-deadline: phase='+phase+' ',limit_us=600000,within=1,includes_final_thermal=1,round_only=0)
   try:
    if not 300000<=int(deadline['elapsed_us'])<600000:raise ValueError('actual runtime')
   except (KeyError,ValueError):errors.append('invalid actual runtime deadline')
  one('aud-runtime-deadline: phase=runtime-exit ',typed_complete=1,sampler_closed_before_stop=1)
  positions=[text.find(p) for p in (('marble-pil-boot: auth ','aud-tpl-sampler: ','aud-runtime-deadline: phase=runtime-exit ','marble-smccc: PAS_stop ') if runtime_sampling else ('marble-pil-boot: auth ','aud-runtime-deadline: phase=runtime-exit ','marble-smccc: PAS_stop '))]
  if min(positions)<0 or positions!=sorted(positions):errors.append('runtime scope/cleanup order')
  if not runtime_sampling:one('aud-runtime-deadline: phase=runtime-exit ',sampler_required=0)
 if not (runtime_sampling or tpl_sampling) and (rows('aud-tpl-sampler: ') or rows('aud-tpl-sample: ')):errors.append('unexpected own diagnostic timer')
 if tpl_sampling or runtime_sampling:
  sampler=one('aud-tpl-sampler: ',prepared=1,armed=1,released=1,active=0,truncated=0,arm_status=0,cancel_status=0,close_status=0,period_100ns=100000,notify_tpl=16,foreign_timer_changes=0,callback_IO=0)
  try:
   count=int(sampler['count']);samples=rows('aud-tpl-sample: ')
   if not 0<=count<128 or len(samples)!=count or [int(s['index']) for s in samples]!=list(range(count)):raise ValueError('sampler count')
   times=[int(s['at_us']) for s in samples]
   if times!=sorted(times) or any(not 0<=t<2000000 for t in times) or any(s.get('valid')!='1' for s in samples):raise ValueError('sampler snapshots')
  except (KeyError,ValueError):errors.append('invalid own-timer sampling/cleanup evidence')
 phases=[('kernel-ready-before-AUTH',1,2,0,0,0,0,0),('DMA-granted',2,2,0,0,0,0,0),
         ('configured-real-START-proof',3,4,0,0,1,0,0),('retired-real-UNMAP-proof',3,6,periods,periods,1,0,0),
         ('PAS-off-wait-XNU-unmap',4,6,periods,periods,1,1,0),('DMA-released-after-XNU-unmap',5,8,periods,periods,1,1,1)]
 epochs=[]
 for phase,state,seq,submitted,completed,configured,stopped,restored in phases:
  row=one('aud-owner-fw: phase='+phase+' ',state=state,accepted_sequence=seq,submitted=submitted,completed=completed,
          configured=configured,PAS_stopped=stopped,restored=restored,failed=0,active=0 if restored else 1,
          GLINK_owner='XNU',EFI_FIFO_writes=0,audio_acceptance=0)
  epochs.append(row.get('epoch'))
 kernel=one('marble-audio-owner-kernel: ABI=',ABI='MAUDIO-OWNER-XNU-STREAM24-MUTED-v2' if stream_window else 'MAUDIO-OWNER-XNU-PCM8-MUTED-v1' if pcm else 'MAUDIO-OWNER-XNU-ZERO8-v1' if periods==8 else 'MAUDIO-OWNER-XNU-ZERO-v1',phase='ready',GLINK_owner='XNU',zero=int(not pcm),HAL_sink=0)
 try:
  epoch_valid=len(set(epochs+[kernel.get('epoch')]))==1 and 0<int(epochs[0],16)<1<<64
 except (TypeError,ValueError):epoch_valid=False
 if not epoch_valid:errors.append('kernel/EFI session epochs differ')
 one('marble-audio-owner-kernel: phase=retired ',submitted=periods,completed=periods,published=periods,RX=142+2*(periods-2),TX=23+2*(periods-2),zero=int(not pcm))
 one('marble-audio-owner-kernel: phase=complete ',PASS=1,submitted=periods,completed=periods,DMA_unmapped=1,PAS_off=1,route_restored=1,DMA_owner_released=1,HAL_sink=0,audio_acceptance=0)
 one('aud-owner-board: ',profile='muted-Music-PCM24' if stream_window else 'muted-Music-PCM8' if pcm else 'muted-Music' if board else 'disconnected',zero_only=int(not pcm),TDM_pin_writes=int(board),amplifier_commands=int(board),audible=0)
 if pcm:
  feed=one('marble-audio-owner-feed: ',source='redwood-device-write',frames=5888 if stream_window else 1792 if refill else 2048,guard='original',HPF=1,gain_db=0,extra_divisor=16,peak_bound=64,HAL_sink=0,audible=0)
  first=one('aud-owner-PCM-first: ',bytes=1024,tail_zero=1,audible=0)
  pages=rows('marble-audio-owner-PCM: ')
  try:
   if len(pages)!=periods or [int(p['period']) for p in pages]!=list(range(periods)):raise ValueError('period count/order')
   peaks=[];nonzero=0
   for page in pages:
    peak=int(page['peak']);n=int(page['nonzero']);digest=int(page['hash'],16)
    if not 0<=peak<=64 or not 0<=n<=512 or (peak==0)!=(n==0) or not 0<=digest<1<<32 or any(page.get(k)!=v for k,v in dict(bytes='1024',tail_zero='1',audible='0').items()):raise ValueError('page bound')
    peaks.append(peak);nonzero+=n
   if not nonzero or int(feed['peak'])!=max(peaks):raise ValueError('source/data mismatch or silence')
   if any(first.get(k)!=pages[0].get(k) for k in ('peak','nonzero','hash')):raise ValueError('EFI/XNU first page mismatch')
   if signal_oracle is not None:
    expected=dict(pattern='triangle1k-stream24-gap' if stream_window else 'triangle1k-refill-gap' if refill else 'triangle1k',frames=5888 if stream_window else 1792 if refill else 2048,rate=48000,bits=16,channels=2,raw_peak=504,guard='original',HPF=True,extra_divisor=16,peak_bound=64)
    if any(signal_oracle.get(k)!=v for k,v in expected.items()) or len(signal_oracle.get('pages',[]))!=periods:raise ValueError('signal oracle header')
    if stream_window and any(signal_oracle.get(k)!=v for k,v in dict(output_frames=6144,silent_periods=[2],prebuffer_frames=512,late_frames=5376,periods=24,producer_writes=19,max_queue_frames=2048).items()):raise ValueError('stream24 oracle schedule')
    if refill and any(signal_oracle.get(k)!=v for k,v in dict(output_frames=2048,silent_periods=[2],prebuffer_frames=512,late_frames=1280).items()):raise ValueError('refill oracle schedule')
    for actual,wanted in zip(pages,signal_oracle['pages']):
     if any(actual.get(k)!=str(wanted.get(k)) for k in ('period','peak','nonzero','hash')) or not ((int(actual['nonzero'])==0 and int(actual['peak'])==0) if (refill or stream_window) and actual.get('period')=='2' else 400<int(actual['nonzero'])<=512):raise ValueError('signal/original-guard oracle mismatch')
   pcm_data_valid=True
  except (KeyError,ValueError,TypeError,AttributeError):errors.append('invalid/missing/mismatched bounded PCM data evidence')
  positions=[text.find(p) for p in ('aud-owner-fw: phase=DMA-granted ','aud-owner-PCM-first: ','aud-owner-fw: phase=configured-real-START-proof ')]
  if min(positions)<0 or positions!=sorted(positions):errors.append('PCM first page not validated before DATA grant');pcm_data_valid=False
 elif rows('marble-audio-owner-PCM: ') or rows('aud-owner-PCM-first: '):errors.append('unexpected nonzero PCM profile')
 if board:
  one('tp: acquired ',PASS=1,active=1,writes=6)
  one('tp: end ',PASS=1,active=0,restored=1,writes=12,PAS_stopped=0,amp_changes=0)
  one('aw: window end ',PASS=1,config_active=0,clock_active=0)
  one('aw: end ',PASS=1,window_passed=1,DSP_passed=1,DSP_dirty=0,config_active=0,clock_active=0,dirty=0)
  for device in (() if board_held else ('34','35')):
   for kind,extra in [('analog',dict(ready=1)),('digital',dict(locked=1,AMPPD_retained=0))]:
    one(f'marble-aw-{kind}: complete device={device} ',PASS=1,restored=1,dirty=0,HMUTE_retained=1,attenuation_db=90,speaker_switch_writes=0,**extra)
 one('aud-owner-session: ',PASS=1,launched=1,protocol_completed=1,DMA_restored=1,HAL_sink=0,cold_reset_required=1)
 one('marble-pil-boot: auth ',status=0,transport=1,secure=0,accepted=1,hold_limit_ms=600,payload_access=0)
 pas=one('marble-smccc: PAS_stop ',PASS=1,fn='42000206',PAS_id=1,outcome=0,raw_a0=0,raw_a1=0,WAITQ_replays=0)
 runtime=one('marble-pil-boot: observed ',thermal_ok=1,runtime_ok=1,link_invalid=0)
 mapping=one('marble-audio-map: result ',PASS=1,stage=9,status=0,context=5,allocated=1,programmed=1,translated=1,
             disabled=1,restored=1,freed=1,retained=0,stream_writes=8,translation_mode=2,PCM_requests=periods)
 one('marble-smp2p-boot: stopped ',PASS=1,ready_seen=1,stop_ack_seen=1,stop_clear=1,PAS_shutdown_allowed=1)
 one('marble-pil-held: restored ',PASS=1,original_restored=1,backup_retained=0,shutdown_before_restore=1)
 one('marble-adsp-owned: complete ',PASS=1,ROM_stage_restored=1,LPI_released=1,LPI_active=0,thermal_ok=1,cold_reset_required=1,storage_access=0)
 one('marble-cpucap: restored ',PASS=1,active=0,dirty=0)
 one('marble-efi-idle: restored ',PASS=1,active=0,dirty=0)
 try:
  if not 0<int(pas['elapsed_us'])<1000000 or not 1<=int(pas['calls'])<=128:raise ValueError('PAS')
  if not 300000<=int(runtime['hold_us'])<600000 or not 0<=int(runtime['die_mc'])<55000:raise ValueError('runtime')
  pa=int(mapping['physical'],16);allocation=int(mapping['allocation'],16)
  if not 0<allocation<(1<<36)-16384 or allocation&4095 or pa!=allocation+12288 or (int(mapping['par'],16)>>12)&0xfffffffff!=pa:raise ValueError('DMA')
 except (KeyError,ValueError):errors.append('invalid numeric PAS/runtime/DMA evidence')
 order=['aud-owner-fw: phase=kernel-ready-before-AUTH ','marble-pil-boot: auth ','aud-owner-fw: phase=DMA-granted ',
        'aud-owner-fw: phase=configured-real-START-proof ','aud-owner-fw: phase=retired-real-UNMAP-proof ',
        'marble-smp2p-boot: stopped ','marble-smccc: PAS_stop ','aud-owner-fw: phase=PAS-off-wait-XNU-unmap ',
        'marble-audio-map: result ','aud-owner-fw: phase=DMA-released-after-XNU-unmap ','marble-pil-held: restored ','aud-owner-session: PASS=1']
 positions=[text.find(p) for p in order]
 if min(positions)<0 or positions!=sorted(positions):errors.append('cross-core PAS/unmap/free/resource ordering')
 if board:
  board_order=('aud-owner-fw: phase=DMA-granted ','tp: acquired ','aud-owner-fw: phase=configured-real-START-proof ','aw: window end ','aud-owner-fw: phase=retired-real-UNMAP-proof ','tp: end ','marble-smccc: PAS_stop ','aud-owner-session: PASS=1') if board_held else ('aud-owner-fw: phase=DMA-granted ','tp: acquired ','aw: window end ','aud-owner-fw: phase=configured-real-START-proof ','aud-owner-fw: phase=retired-real-UNMAP-proof ','tp: end ','marble-smccc: PAS_stop ','aud-owner-session: PASS=1')
  positions=[text.find(p) for p in board_order]
  if min(positions)<0 or positions!=sorted(positions):errors.append('muted board configuration/retirement/restore ordering')
 if board_held:
  for device in ('34','35'):
   one('aud-board-held: phase=begin device='+device+' ',PASS=1,status='311',HMUTE=1,attenuation_db=90,audible=0)
   end=one('aud-board-held: phase=finish device='+device+' ',PASS=1,powered_down=1,reset_restored=1,bus_clean=1,aux_restored=1,HMUTE=1,audible=0)
   try:
    if not 0<int(end['held_us'])<600000:raise ValueError('hold')
   except (KeyError,ValueError):errors.append('invalid amplifier hold bound')
  one('aud-board-held: phase=DATA-granted ',devices=2,HMUTE=1,attenuation_db=90,STOP_allowed=0,audible=0)
  one('aud-board-held: phase=drained ',PASS=1,submitted=periods,completed=periods,STOP_allowed=0,audible=0)
  one('aud-board-held: phase=quiet-ACK ',flags=7,submitted=periods,completed=periods,STOP_allowed=1,config_active=0,clock_active=0,audible=0)
  one('marble-audio-board-gate: phase=drained ',submitted=periods,completed=periods,pending=0,STOP_sent=0,ABI='MAUDIO-BOARD-QUIET24-v2' if stream_window else 'MAUDIO-BOARD-QUIET-v1',audible=0)
  one('marble-audio-board-gate: phase=quiet-ACK ',submitted=periods,completed=periods,flags=7,STOP_allowed=1,audible=0)
  # Cross-core log lines may race publication. Enforce ordering within each
  # producer, and physical begin/grant/finish on the EFI producer.
  for order in [
   ['aud-board-held: phase=begin device=34 ','aud-board-held: phase=begin device=35 ','aud-owner-fw: phase=configured-real-START-proof ','aud-board-held: phase=DATA-granted ','aud-board-held: phase=drained ','aud-board-held: phase=finish device=35 ','aud-board-held: phase=finish device=34 ','aw: window end ','aud-board-held: phase=quiet-ACK ','aud-owner-fw: phase=retired-real-UNMAP-proof '],
   ['marble-audio-owner-PCM: period='+str(periods-1)+' ','marble-audio-board-gate: phase=drained ','marble-audio-board-gate: phase=quiet-ACK ','marble-audio-owner-kernel: phase=retired ']]:
   pos=[text.find(marker) for marker in order]
   if min(pos)<0 or pos!=sorted(pos):errors.append('DATA/board quiet/STOP lifecycle ordering')
 elif rows('aud-board-held: ') or rows('marble-audio-board-gate: '):errors.append('unexpected held board profile')
 if refill:
  one('marble-audio-refill: phase=waiting ',ABI='MAUDIO-REFILL8-MUTED-v1',submitted=3,completed=3,pending=0,feed=512,consumed=512,silence=256,queued=0,audible=0)
  one('marble-audio-refill: phase=resumed ',feed=1792,queued=1280,consumed=512,silence=256,pending=0,audible=0)
  one('marble-audio-refill-test: phase=observed-empty ',submitted=3,completed=3,feed=512,consumed=512,silence=256,queued=0,pending=0,audible=0)
  one('marble-audio-refill-test: phase=retired ',PASS=1,input_frames=1792,output_frames=2048,periods=8,consumed=1792,silence=256,queued=0,RAM_copy=1,HAL_sink=0,audible=0)
  # PID1 console output and the retained kernel printf ring are collected
  # separately. Their text positions are not a shared clock. Enforce each
  # producer's order, plus its exact independently observed queue counters.
  orders=[['marble-audio-owner-PCM: period=2 ','marble-audio-refill: phase=waiting ','marble-audio-owner-feed: ','marble-audio-refill: phase=resumed ','marble-audio-owner-PCM: period=3 ','marble-audio-owner-PCM: period=7 ','marble-audio-board-gate: phase=drained '],
          ['marble-audio-refill-test: phase=observed-empty ','marble-audio-refill-test: phase=retired ']]
  for order in orders:
   positions=[text.find(p) for p in order]
   if min(positions)<0 or positions!=sorted(positions):errors.append('live refill producer/consumer ordering')
 elif rows('marble-audio-refill: ') or rows('marble-audio-refill-test: '):errors.append('unexpected live refill profile')
 if stream_window:
  one('marble-audio-stream: phase=waiting ',ABI='MAUDIO-STREAM24-MUTED-v2',submitted=3,completed=3,pending=0,feed=512,consumed=512,silence=256,queued=0,audible=0)
  resumed=one('marble-audio-stream: phase=resumed ',consumed=512,silence=256,pending=0,audible=0)
  stats=one('marble-audio-stream: phase=retired ',input_frames=5888,output_frames=6144,periods=24,consumed=5888,silence=256,queued=0,head=5888,tail=5888,wraps=1,writes=19,HAL_sink=0,audible=0)
  one('marble-audio-stream-test: phase=observed-empty ',submitted=3,completed=3,feed=512,consumed=512,silence=256,queued=0,pending=0,audible=0)
  one('marble-audio-stream-test: phase=retired ',PASS=1,input_frames=5888,output_frames=6144,periods=24,consumed=5888,silence=256,queued=0,RAM_copy=1,flow_control=1,writes=19,HAL_sink=0,audible=0)
  try:
   if not 1024<=int(resumed['queued'])<=2048 or int(resumed['feed'])!=512+int(resumed['queued']) or not 1024<=int(stats['max_queued'])<=2048:raise ValueError('queue watermarks')
  except (KeyError,ValueError):errors.append('invalid stream24 producer watermarks')
  for order in [
   ['marble-audio-owner-PCM: period=2 ','marble-audio-stream: phase=waiting ','marble-audio-stream: phase=resumed ','marble-audio-owner-PCM: period=3 ','marble-audio-owner-feed: ','marble-audio-owner-PCM: period=23 ','marble-audio-board-gate: phase=drained ','marble-audio-board-gate: phase=quiet-ACK ','marble-audio-stream: phase=retired ','marble-audio-owner-kernel: phase=retired '],
   ['marble-audio-stream-test: phase=observed-empty ','marble-audio-stream-test: phase=retired ']]:
   pos=[text.find(m) for m in order]
   if min(pos)<0 or pos!=sorted(pos):errors.append('stream24 producer/wrap/retirement ordering')
 elif rows('marble-audio-stream: ') or rows('marble-audio-stream-test: '):errors.append('unexpected stream24 profile')
 if qmp_close_snapshot:
  close=one('aud-qmp-close-snapshot: ',PASS=1,failed=0,close_acks=2,limit_us=750000,double_read=1,transition_validator='unchanged',remote_owned_writes=0)
  one('marble-qmp-wire: complete ',PASS=1,dirty=0,descriptor_restored=1,native_context_unchanged=1,Clock_unchanged=1,DAIF_unchanged=1,AP_writes=38,kicks=6,ACKs=2,thermal_ok=1,native_event_calls=0)
  # Older complete records do not carry remote_owned_writes; their close
  # ACK records do. Validate the actual two AP acknowledgements explicitly.
  ack=rows('marble-qmp-wire: close_ack ')
  if len(ack)!=2 or any(a.get('index')!=str(i) or a.get('observed_remote')!=v or a.get('AP_ack')!=v or a.get('offset')!='18' or a.get('remote_state_writes')!='0' for i,(a,v) in enumerate(zip(ack,('ffff0000','ffff')))):
   errors.append('invalid QMP observed close acknowledgements')
  try:
   if not 6<=int(close['reads'])<=4096 or not 0<=int(close['unstable'])<=int(close['reads']) or not 0<int(close['elapsed_us'])<750000:raise ValueError('close bounds')
  except (KeyError,ValueError):errors.append('invalid QMP coherent close bounds')
 elif rows('aud-qmp-close-snapshot: '):errors.append('unexpected QMP close snapshot profile')
 if native_link_only:
  one('aud-owner-link-gate: ',PASS=1,phase='pre-AUTH',XNU_READY=1,EFI_register_calls=0,EFI_deregister_calls=0,FIFO_owner='XNU',DSP_link_inferred=0)
  if rows('marble-glink: register ') or rows('marble-glink: deregister '):errors.append('legacy EFI link observer unexpectedly registered')
  pos=[text.find(p) for p in ['aud-owner-fw: phase=kernel-ready-before-AUTH ','aud-owner-link-gate: ','marble-pil-boot: auth ']]
  if min(pos)<0 or pos!=sorted(pos):errors.append('native-only pre-AUTH READY gate ordering')
 elif rows('aud-owner-link-gate: '):errors.append('unexpected native-only link gate')
 faults=('panic(cpu','EARLY KERNEL EXCEPTION','phase=failed','ACK-missing-retain','tr: q ','pd: q ','marble-thermal: STOP requested','marble-cpucap: runtime request changed','aud-qmp-close-reject: ')
 if not board:faults+=('marble-aw-digital: complete ','tp: acquired ')
 if any(s in text for s in faults):errors.append('fault/temperature sampling gap/concurrent FIFO owner or unexpected physical output')
 return dict(passed=not errors,errors=errors,audio_acceptance=False,HAL_sink=False,physical_output=False,board_configured=bool(board),board_held=bool(board_held),native_link_only=bool(native_link_only),live_refill=bool(refill and not errors),underrun_recovered=bool((refill or stream_window) and not errors),stream_window_verified=bool(stream_window and not errors),bounded_PCM=bool(pcm),PCM_data_verified=pcm_data_valid,signal_oracle_matched=signal_oracle is not None and pcm_data_valid,tpl_sampling_required=bool(tpl_sampling),runtime_sampling_required=bool(runtime_sampling),usb_qmp_lease_required=bool(usb_qmp_lease),fast_handshake_required=bool(fast_handshake),display_dma_only_required=bool(display_dma_only),continuous_playback=False,
             scope=('muted Music/OTP/TDM board closure; ' if board else 'disconnected board; ')+'real XNU GLINK owner, EFI power/SMMU owner, bounded '+('device-write PCM' if pcm else 'zero')+' periods; PAS then XNU unmap ACK before table/page free')
