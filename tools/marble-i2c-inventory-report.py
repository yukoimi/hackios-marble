"""Exact Mu I2C table/code discovery; controller use is a separate milestone."""
import re
CONFIG_SHA256='358db7a726eb6ed8945600f688d9d96575f12c55a590a418b80cae0ece51e452'
ROUTE_ADDRESSES=(0x880048,0x88004c,0x88007c,0x880258,0x880260,0x880264,0x88026c,0x880270,0x880278,0x880284,0x880288,0x880600,0x880610,0x880614,0x880630,0x880640,0x880644,0x880800,0x880804,0x88080c,0x880810,0x880814,0x880908,0x880e18,0x880e1c,0x12e018,0x12e01c,0x12e020,0x12e024,0x12e008,0x162010,0xf138000,0xf138004,0xf139000,0xf139004,0xf101000,0xf101004,0xf178000,0xf178004,0xf134000,0xf134004)
SETUP_ADDRESSES=(0x880e1c,0x880e18,0x88007c,0x880048,0x880278,0x880254,0x880260,0x880264,0x880284,0x880288,0x880258,0xf138000,0xf139000)
SETUP_VALUES=(0,0,0,0x71,0xa02c1a,0,0x7f8fe,0xffefe,0x7f8fe,0xffefe,0,7,7)
def inspect(text,dt=False,config=False,clock=False,cycle=False,registers=False,route=False,setup=False,amp=False,amp_registers=False,amp_quiet=False,amp_profile=False,amp_digital=False,amp_analog=False):
 def rows(prefix):return [dict(re.findall(r'(\w+)=([^\s]+)',l)) for l in text.splitlines() if l.startswith(prefix)]
 def exact(row,**kw):return all(row.get(k)==str(v) for k,v in kw.items())
 errors=[]
 digital_commands=0;analog_commands=0
 if amp_digital:
  if not amp_profile:errors.append('digital PLL requires Music profile')
  try:digital_commands=sum(int(r['commands']) for r in rows('marble-aw-digital: access '))
  except (KeyError,ValueError):errors.append('invalid digital command counters')
 elif 'marble-aw-digital:' in text or 'phase=digital-' in text:errors.append('digital PLL requires explicit contract')
 if amp_analog:
  if not amp_digital:errors.append('muted analog requires digital PLL')
  try:analog_commands=sum(int(r['commands']) for r in rows('marble-aw-analog: access '))
  except (KeyError,ValueError):errors.append('invalid analog command counters')
 elif 'marble-aw-analog:' in text or 'phase=analog-' in text:errors.append('muted analog requires explicit contract')
 if amp_profile and not amp_quiet:errors.append('Music profile requires quiet lifecycle')
 if not amp_profile and ('marble-aw-profile:' in text or 'phase=profile-' in text):errors.append('Music profile requires explicit contract')
 if amp_quiet and not amp_registers:errors.append('quiet write requires snapshot lifecycle')
 if not amp_quiet and ('marble-aw-quiet:' in text or 'phase=quiet-' in text):errors.append('quiet write requires explicit contract')
 if amp_registers and not amp:errors.append('register snapshot requires amplifier lifecycle')
 if not amp_registers and ('marble-aw-reg:' in text or 'phase=register-read' in text):errors.append('register snapshot requires explicit contract')
 if amp and not setup:errors.append('amplifier ID requires complete setup lifecycle contract')
 if not amp and any(line.startswith('marble-i2c-amp:') for line in text.splitlines()):errors.append('amplifier ID requires explicit contract')
 amp_writes=rows('marble-i2c-amp: write ')
 amp_gpio=sum(row.get('address') in ('f101000','f101004','f178000','f178004') for row in amp_writes) if amp else 0
 if setup and not route:errors.append('setup requires complete route snapshot contract')
 if not setup and any(line.startswith('marble-i2c-setup:') for line in text.splitlines()):errors.append('setup requires explicit contract')
 setup_writes=rows('marble-i2c-setup: write ')
 setup_gpio=sum(row.get('address') in ('f138000','f139000') for row in setup_writes) if setup else 0
 if route and not registers:errors.append('route snapshot requires controller snapshot contract')
 if not route and any(line.startswith('marble-i2c-route:') for line in text.splitlines()):errors.append('route snapshot requires explicit contract')
 identity=False
 if registers and not cycle:errors.append('register snapshot requires explicit clock lifecycle')
 if not registers and any(line.startswith('marble-i2c-registers:') for line in text.splitlines()):errors.append('register snapshot requires explicit contract')
 if cycle and not clock:errors.append('clock cycle requires explicit clock qualification')
 if not cycle and any(line.startswith(('marble-i2c-lease:','marble-i2c-temperature:')) for line in text.splitlines()):errors.append('clock cycle requires explicit contract')
 if clock and (dt or config):errors.append('clock lookup requires separate contract')
 if not clock and any(line.startswith(('marble-i2c-clock:','marble-i2c-board:')) for line in text.splitlines()):errors.append('clock lookup requires explicit contract')
 if config and not dt:errors.append("config snapshot requires explicit DT observation")
 if not config and rows("marble-i2c-config: observed "):errors.append("configuration snapshot requires explicit contract")
 if not dt and rows("marble-i2c-DT: observed "):errors.append("DT observation requires explicit contract")
 try:
  q=rows('marble-i2c: qualified ');end=rows('marble-i2c: complete ');slots=rows('marble-i2c: slot ')
  if len(q)!=1 or len(end)!=1 or len(slots)!=5:raise ValueError('missing/duplicate I2C evidence')
  q=q[0];end=end[0];base=int(q['base'],16);table=int(q['table'],16)
  if base<0x80000000 or base&4095 or table!=base+0xa0d8:raise ValueError('I2C image/table binding')
  if not exact(q,table_rva='a0d8',revision=10000,slots=5,code_rva='27f8',code_bytes=2196,code_digest=1,protocol_calls=0):raise ValueError('I2C code/revision qualification')
  for i,(row,rva) in enumerate(zip(slots,(0x27f8,0x2a8c,0x2ea4,0x2b9c,0x2ff8))):
   if not exact(row,index=i,invoked=0) or int(row['rva'],16)!=rva or int(row['pointer'],16)!=base+rva:raise ValueError('I2C method binding')
  if not exact(end,PASS=1,table_unchanged=1,client_head_unchanged=1,protocol_calls=0,instance_selected=0,controller_qualified=0,MMIO_reads=0,MMIO_writes=0,GPIO_writes=0,clock_calls=0,DSP_start=0,storage_access=0,audio_acceptance=0):raise ValueError('read-only/no-call contract')
  if not 0<=int(end['client_head'],16)<=0xffffffffffffffff:raise ValueError('invalid head observation')
  order=['marble-i2c: qualified ',*[f'marble-i2c: slot index={i} ' for i in range(5)],'marble-i2c: complete ']
  positions=[text.find(x) for x in order]
  if min(positions)<0 or positions!=sorted(positions):raise ValueError('I2C inventory ordering')
 except (KeyError,ValueError) as e:errors.append(str(e))
 if dt:
  try:
   q=rows('marble-i2c-DT: observed ');words=rows('marble-i2c-DT: word ');code=rows('marble-i2c-DT: code ');end=rows('marble-i2c-DT: complete ')
   if len(q)!=1 or len(words)!=33 or len(code)!=2 or len(end)!=1:raise ValueError('missing/duplicate DT observations')
   q=q[0];end=end[0]
   if not exact(q,words=33,calls=0,ABI_qualified=0) or int(q['table'],16)<0x80000000:raise ValueError('DT table observation')
   values=[]
   for i,row in enumerate(words):
    if not exact(row,index=i) or not 0<=int(row['value'],16)<=0xffffffffffffffff:raise ValueError('DT table words')
    values.append(int(row['value'],16))
   for i,row in zip((6,20),code):
    pointer=int(row['pointer'],16);sample=row['words'].split(',')
    if not exact(row,index=i,bytes=32,invoked=0) or pointer!=values[i] or pointer<0x80000000 or pointer&3 or len(sample)!=4 or any(not 0<=int(w,16)<=0xffffffffffffffff for w in sample):raise ValueError('DT method observations')
   if not exact(end,PASS=1,table_unchanged=1,cached_provider_unchanged=1,DT_queries=0,I2C_calls=0,MMIO_reads=0,MMIO_writes=0,GPIO_writes=0,allocation_calls=0,ABI_qualified=0,controller_qualified=0,audio_acceptance=0):raise ValueError('DT no-call observation contract')
   order=['marble-i2c: complete ','marble-i2c-DT: observed ',*[f'marble-i2c-DT: word index={i} ' for i in range(33)],'marble-i2c-DT: code index=6 ','marble-i2c-DT: code index=20 ','marble-i2c-DT: complete ']
   positions=[text.find(x) for x in order]
   if min(positions)<0 or positions!=sorted(positions):raise ValueError('DT observation ordering')
  except (KeyError,ValueError) as e:errors.append(str(e))
 if config:
  try:
   observed=rows('marble-i2c-config: observed ');header=rows('marble-i2c-config: header ');end=rows('marble-i2c-config: complete ')
   if len(observed)!=1 or len(header)!=1 or len(end)!=1:raise ValueError('missing/duplicate ROM snapshot')
   table_words=rows('marble-i2c-DT: word ')
   if len(table_words)!=33 or any(int(table_words[i]['value'],16)!=v for i,v in ((0,0x10001),(6,0xa70149f8),(20,0xa702620c))):raise ValueError('ROM DT implementation identity')
   if not exact(observed[0],pointer_slot='a7043aa0',code_ranges=2,DT_queries=0) or not 0x80000000<=int(observed[0]['pointer'],16)<=0x500000000-65536:raise ValueError('snapshot pointer/code qualification')
   if not exact(header[0],bytes=41865,magic='d00dfeed',payload_read=0):raise ValueError('snapshot header')
   if not exact(end[0],PASS=1,bytes=41865,pointer_read=1,pointer_unchanged=1,capture_only=1,configuration_qualified=0,DT_queries=0,I2C_calls=0,MMIO_reads=0,MMIO_writes=0,GPIO_writes=0,original_blob_writes=0,audio_acceptance=0) or end[0].get('ROM_blob_match') not in ('0','1'):raise ValueError('bounded capture-only contract')
   order=['marble-i2c-DT: complete ','marble-i2c-config: observed ','marble-i2c-config: header ','marble-i2c-config: complete ']
   positions=[text.find(x) for x in order]
   if min(positions)<0 or positions!=sorted(positions):raise ValueError('ROM snapshot ordering')
  except (KeyError,ValueError) as e:errors.append(str(e))
 if clock:
  try:
   board=rows('marble-i2c-board: candidate ');q=rows('marble-i2c-clock: qualified ');lookups=rows('marble-i2c-clock: lookup ');end=rows('marble-i2c-clock: complete ')
   if len(board)!=1 or len(q)!=1 or len(lookups)!=5 or len(end)!=1:raise ValueError('missing/duplicate clock evidence')
   if not exact(board[0],native_bytes=88,controller='880000',common='8c0000',QUP=2,core_index=16,se_index=0,pins='56,57',function=1,bus_khz=100,timing_se_khz=19200,hardware_calls=0):raise ValueError('board candidate contract')
   base=int(q[0]['base'],16)
   if base<0x80000000 or base&4095 or int(q[0]['table'],16)!=base+0x1b0f8 or not exact(q[0],table_rva='1b0f8',revision=10009,get_id_rva='14e4',code_bytes=68,code_digest=1):raise ValueError('clock implementation binding')
   names=('gcc_qupv3_wrap2_core_clk','gcc_qupv3_wrap2_core_2x_clk','gcc_qupv3_wrap_2_m_ahb_clk','gcc_qupv3_wrap_2_s_ahb_clk','gcc_qupv3_wrap2_s0_clk')
   ids=[]
   for i,(row,name) in enumerate(zip(lookups,names)):
    if not exact(row,index=i,name=name,status=0,clock_writes=0) or not 0<int(row['id'],16)<0xffffffffffffffff:raise ValueError('clock lookup failed or invalid')
    ids.append(int(row['id'],16))
   if len(set(ids))!=5:raise ValueError('clock IDs not distinct')
   if not exact(end[0],PASS=1,lookups=5,table_unchanged=1,I2C_head_unchanged=1,clock_writes=0,frequency_requests=0,I2C_calls=0,GPIO_writes=0,controller_qualified=0,audio_acceptance=0):raise ValueError('clock lookup ownership contract')
   positions=[text.find(s) for s in ('marble-i2c: complete ','marble-i2c-board: candidate ','marble-i2c-clock: qualified ',*[f'marble-i2c-clock: lookup index={i} ' for i in range(5)],'marble-i2c-clock: complete ')]
   if min(positions)<0 or positions!=sorted(positions):raise ValueError('clock lookup ordering')
  except (KeyError,ValueError) as e:errors.append(str(e))
 if cycle:
  try:
   temperatures=rows('marble-i2c-temperature: ')
   if len(temperatures)!=2:raise ValueError('clock cycle temperature samples missing/duplicate')
   for row,phase,limit in zip(temperatures,('before','after'),(50000,55000)):
    if not exact(row,phase=phase,valid=1,limit_mc=limit,PASS=1) or not -40000<=int(row['maximum_mc'])<limit:raise ValueError('clock cycle temperature guard')
   q=rows('marble-i2c-lease: qualified ');states=rows('marble-i2c-lease: state ');enables=rows('marble-i2c-lease: enable ');disables=rows('marble-i2c-lease: disable ');acquired=rows('marble-i2c-lease: acquired ');end=rows('marble-i2c-lease: complete ')
   if (len(q),len(states),len(enables),len(disables),len(acquired),len(end))!=(1,20,5,5,1,1):raise ValueError('missing/duplicate clock lifecycle evidence')
   if not exact(q[0],code_rva='1528',code_bytes=420,code_digest=1,metadata_rva='4458',metadata_bytes=148,metadata_digest=1,clocks=5,frequency_requests=0,controller_calls=0):raise ValueError('clock lifecycle code binding')
   ids=[int(r['id'],16) for r in rows('marble-i2c-clock: lookup ')]
   if len(ids)!=5:raise ValueError('clock lifecycle IDs missing')
   for j,row in enumerate(states):
    i=j%5;phase=('before','acquire','acquire','restore')[j//5]
    if not exact(row,phase=phase,index=i) or row['status'] not in ('0,0,0','0,0,8000000000000013') or int(row['id'],16)!=ids[i] or row['enabled'] not in ('0','1') or row['on'] not in ('0','1') or not 0<=int(row['hz'])<0xffffffff:raise ValueError('clock lifecycle state')
    if row['status']!='0,0,0' and row['hz']!='0':raise ValueError('unavailable frequency metadata must remain explicit')
    if 5<=j<10 or j>=15:
     if any(row[k]!=states[i][k] for k in ('enabled','on','hz','status')):raise ValueError('clock lifecycle state not preserved')
    if 10<=j<15 and row['enabled']!='1':raise ValueError('clock enable vote missing')
   for i,row in enumerate(enables):
    if not exact(row,index=i,status=0) or int(row['id'],16)!=ids[i]:raise ValueError('clock enable evidence')
   for i,row in zip(range(4,-1,-1),disables):
    if not exact(row,index=i,status=0) or int(row['id'],16)!=ids[i]:raise ValueError('clock reverse release evidence')
   if not exact(acquired[0],PASS=1,held='1f',uncertain=0,active=1,register_access_qualified=0):raise ValueError('five clock references not acquired')
   if not exact(end[0],PASS=1,acquired=1,restored=1,table_unchanged=1,I2C_head_unchanged=1,held=0,uncertain=0,active=0,dirty=0,hold_us=1000,frequency_requests=0,frequency_measurement=0,register_access_qualified=0,I2C_calls=0,GPIO_writes=setup_gpio+amp_gpio,controller_MMIO_reads=(16 if registers else 0)+(82 if route else 0),controller_MMIO_writes=(len(setup_writes)-setup_gpio if setup else 0)+(len(amp_writes)-amp_gpio if amp else 0),audio_acceptance=0):raise ValueError('clock release/ownership contract')
   expected=['marble-i2c-lease: qualified ',*[f'marble-i2c-lease: state phase=before index={i} ' for i in range(5)],*[f'marble-i2c-lease: state phase=acquire index={i} ' for i in range(5)]]
   for i in range(5):expected += [f'marble-i2c-lease: enable index={i} ',f'marble-i2c-lease: state phase=acquire index={i} ']
   expected += ['marble-i2c-lease: acquired ',*[f'marble-i2c-lease: disable index={i} ' for i in range(4,-1,-1)],*[f'marble-i2c-lease: state phase=restore index={i} ' for i in range(5)],'marble-i2c-lease: complete ']
   actual=[line for line in text.splitlines() if line.startswith('marble-i2c-lease:')]
   if len(actual)!=len(expected) or any(not line.startswith(prefix) for line,prefix in zip(actual,expected)) or text.find('marble-i2c-lease: qualified ')<text.find('marble-i2c-clock: complete ') or not text.find('marble-i2c-temperature: phase=before ')<text.find('marble-i2c: qualified ')<text.find('marble-i2c-lease: state phase=restore index=4 ')<text.find('marble-i2c-temperature: phase=after ')<text.find('marble-i2c-lease: complete '):raise ValueError('clock lifecycle ordering')
  except (KeyError,ValueError) as e:errors.append(str(e))
 if registers:
  try:
   maps=rows('marble-i2c-registers: translation ');reads=rows('marble-i2c-registers: read ');end=rows('marble-i2c-registers: complete ')
   if (len(maps),len(reads),len(end))!=(8,16,1):raise ValueError('missing/duplicate passive register evidence')
   addresses=(0x8c0004,0x880040,0x880060,0x880064,0x880068,0x88006c,0x880e24,0x880e28)
   for i,(row,address) in enumerate(zip(maps,addresses)):
    par=int(row['par'],16)
    if not exact(row,index=i,identity=1) or int(row['address'],16)!=address or par&1 or par&0x0000fffffffff000!=address&~4095:raise ValueError('passive register translation')
   values=[]
   for j,row in enumerate(reads):
    i=j%8;v=int(row['value'],16)
    if not exact(row,round=j//8,index=i,writes=0) or int(row['address'],16)!=addresses[i] or not 0<=v<=0xffffffff:raise ValueError('passive register address/value')
    values.append(v)
   if values[:8]!=values[8:]:raise ValueError('passive register snapshots differ')
   v=values[:8];proto=(v[4]>>8)&255;idle=not(v[1]&0x1001);fifo=not v[3]
   identity=bool(v[0] not in (0,0xffffffff) and proto==3 and idle and fifo and (v[6]>>16&63)>=2 and (v[7]>>16&63)>=2 and (v[6]>>24&63)==32 and (v[7]>>24&63)==32)
   if not exact(end[0],PASS=1,snapshot_captured=1,stable=1,protocol=proto,idle=int(idle),FIFO=int(fifo),identity_qualified=int(identity),MMIO_reads=16,MMIO_writes=0,I2C_transactions=0,GPIO_writes=0,firmware_loads=0,controller_writes=0,physical_chip_id_verified=0,audio_acceptance=0):raise ValueError('passive snapshot contract/identity')
   actual=[line for line in text.splitlines() if line.startswith('marble-i2c-registers:')]
   expected=[*[f'marble-i2c-registers: translation index={i} ' for i in range(8)],*[f'marble-i2c-registers: read round={r} index={i} ' for r in range(2) for i in range(8)],'marble-i2c-registers: complete ']
   if len(actual)!=len(expected) or any(not line.startswith(prefix) for line,prefix in zip(actual,expected)) or not text.find('marble-i2c-lease: acquired ')<text.find(expected[0])<text.find(expected[-1])<text.find('marble-i2c-lease: disable index=4 '):raise ValueError('passive snapshot lifecycle ordering')
  except (KeyError,ValueError) as e:errors.append(str(e))
 route_values=None
 if route:
  try:
   maps=rows('marble-i2c-route: translation ');reads=rows('marble-i2c-route: read ');end=rows('marble-i2c-route: complete ')
   if (len(maps),len(reads),len(end))!=(41,82,1):raise ValueError('missing/duplicate passive route evidence')
   for i,(row,address) in enumerate(zip(maps,ROUTE_ADDRESSES)):
    par=int(row['par'],16)
    if not exact(row,index=i,identity=1) or int(row['address'],16)!=address or par&1 or par&0x0000fffffffff000!=address&~4095:raise ValueError('route translation')
   values=[]
   for j,row in enumerate(reads):
    i=j%41;v=int(row['value'],16);mask=2 if i>=31 and not i&1 else 0xffffffff
    if not exact(row,round=j//41,index=i,writes=0) or int(row['address'],16)!=ROUTE_ADDRESSES[i] or int(row['mask'],16)!=mask or not 0<=v<=0xffffffff:raise ValueError('route address/value/mask')
    if j>=41 and (values[i]^v)&mask:raise ValueError('route preservation mismatch')
    values.append(v)
   if not exact(end[0],PASS=1,words=41,snapshots=2,MMIO_reads=82,MMIO_writes=0,I2C_transactions=0,GPIO_writes=0,clock_frequency_requests=0,bus_ready=0,physical_chip_id_verified=0,audio_acceptance=0):raise ValueError('passive route contract')
   actual=[line for line in text.splitlines() if line.startswith('marble-i2c-route:')]
   expected=[*[f'marble-i2c-route: translation index={i} ' for i in range(41)],*[f'marble-i2c-route: read round={r} index={i} ' for r in range(2) for i in range(41)],'marble-i2c-route: complete ']
   if len(actual)!=len(expected) or any(not line.startswith(prefix) for line,prefix in zip(actual,expected)) or not text.find('marble-i2c-registers: complete ')<text.find(expected[0])<text.find(expected[-1])<text.find('marble-i2c-lease: disable index=4 '):raise ValueError('route lifecycle ordering')
   route_values={f'{address:x}':f'{value:x}' for address,value in zip(ROUTE_ADDRESSES,values)}
  except (KeyError,ValueError) as e:errors.append(str(e))
 if setup:
  try:
   saved=rows('marble-i2c-setup: saved ');applied=rows('marble-i2c-setup: applied ');preserved=rows('marble-i2c-setup: preserved ');end=rows('marble-i2c-setup: complete ')
   if len(saved)!=13 or len(applied)!=1 or len(preserved)!=41 or len(end)!=1 or route_values is None or not identity:raise ValueError('missing/duplicate setup evidence or initial identity')
   rv={int(a,16):int(v,16) for a,v in route_values.items()}
   if any(rv[a] for a in (0x880258,0x880600,0x880610,0x880630,0x880640,0x880800,0x880804)) or rv[0x12e018]&0x3fff or any(rv[a]!=1 for a in (0xf138000,0xf139000,0xf101000,0xf178000,0xf134000)) or rv[0xf101004]&3 or rv[0xf178004]&3 or rv[0xf134004]&2:raise ValueError('setup idle/reset/parent preflight')
   originals=[];changed=[]
   for i,(row,address,desired) in enumerate(zip(saved,SETUP_ADDRESSES,SETUP_VALUES)):
    before=int(row['before'],16)
    if not exact(row,index=i) or int(row['address'],16)!=address or int(row['desired'],16)!=desired or not 0<=before<=0xffffffff or (address in rv and before!=rv[address]):raise ValueError('setup saved configuration binding')
    originals.append(before)
    if before!=desired:changed.append(i)
   if len(setup_writes)!=2*len(changed):raise ValueError('setup changed-only write count')
   expected=['marble-i2c-setup: saved ']*13
   for phase,indices in (('apply',changed),('restore',list(reversed(changed)))):
    group=[row for row in setup_writes if row.get('phase')==phase]
    if len(group)!=len(changed):raise ValueError('setup phase write count')
    for row,i in zip(group,indices):
     value=SETUP_VALUES[i] if phase=='apply' else originals[i]
     if int(row['address'],16)!=SETUP_ADDRESSES[i] or int(row['value'],16)!=value or int(row['observed'],16)!=value:raise ValueError('setup write/readback or reverse release')
    if phase=='apply':expected+=['marble-i2c-setup: write phase=apply ']*len(changed)+['marble-i2c-setup: applied ']
    else:expected+=['marble-i2c-setup: write phase=restore ']*len(changed)
   bits=sum(1<<i for i in changed)
   if not exact(applied[0],PASS=1,active=1,I2C_transactions=0) or int(applied[0]['changed'],16)!=bits or int(applied[0]['IOS'],16)&3!=3 or int(applied[0]['status'],16)&0x1001:raise ValueError('setup bus idle/high readback')
   for i,row in enumerate(preserved):
    mask=0 if amp and i==11 else 0xffffff7f if amp and i==12 else 2 if i>=31 and not i&1 else 0xffffffff
    if not exact(row,index=i) or int(row['mask'],16)!=mask or (int(row['value'],16)^rv[ROUTE_ADDRESSES[i]])&mask:raise ValueError('setup route/reset/clock config not preserved')
   if not exact(end[0],PASS=1,configured=1,restored=1,route_preserved=1,held=0,active=0,dirty=0,writes=len(setup_writes),bus_khz=100,I2C_transactions=(324+digital_commands+analog_commands if amp_profile else 54 if amp_quiet else 44 if amp_registers else 4) if amp else 0,reset_writes=amp_gpio,amplifier_register_writes=100 if amp_analog else 96 if amp_digital else 92 if amp_profile else 2 if amp_quiet else 0,firmware_loads=0,frequency_requests=0,audio_acceptance=0):raise ValueError('setup restoration contract')
   expected+=['marble-i2c-setup: preserved ']*41+['marble-i2c-setup: complete ']
   actual=[line for line in text.splitlines() if line.startswith('marble-i2c-setup:')]
   if len(actual)!=len(expected) or any(not line.startswith(prefix) for line,prefix in zip(actual,expected)) or not text.find('marble-i2c-route: complete ')<text.find('marble-i2c-setup: saved ')<text.find('marble-i2c-setup: complete ')<text.find('marble-i2c-lease: disable index=4 '):raise ValueError('setup lifecycle ordering')
  except (KeyError,ValueError) as e:errors.append(str(e))
 if amp:
  try:
   saved=rows('marble-i2c-amp: saved ');resets=rows('marble-i2c-amp: reset-saved ');transfers=rows('marble-i2c-amp: transfer ');ends=rows('marble-i2c-amp: complete ')
   if (len(saved),len(resets),len(transfers),len(ends))!=(12,2,2,2) or route_values is None:raise ValueError('missing/duplicate amplifier evidence')
   rv={int(a,16):int(v,16) for a,v in route_values.items()};aux=(0x88026c,0x880270,0x880614,0x88080c,0x880810,0x880814);desired=(1,2,0x7fc0347f,0,13,14)
   for n,device in enumerate((0x34,0x35)):
    gpio=0xf101000 if n==0 else 0xf178000;d=f'{device:x}'
    reset=resets[n];transfer=transfers[n];end=ends[n]
    if not exact(reset,device=d,cfg=1,io=0) or int(reset['gpio'],16)!=gpio:raise ValueError('amplifier reset snapshot')
    originals=[];changed=[]
    for i,(row,address,value) in enumerate(zip(saved[n*6:(n+1)*6],aux,desired)):
     old=int(row['value'],16)
     if not exact(row,device=d,index=i) or int(row['address'],16)!=address or old!=rv[address] or int(row['desired'],16)!=value:raise ValueError('amplifier auxiliary snapshot')
     originals.append(old)
     if old!=value:changed.append(i)
    writes=[r for r in amp_writes if r.get('device')==d]
    expected=[]
    for i in changed:expected.append(('prepare',aux[i],desired[i]))
    expected += [('reset-output',gpio,0x201),('reset-release',gpio+4,2)]
    prefix=writes[:len(expected)]
    if len(prefix)!=len(expected) or any((r.get('phase'),int(r['address'],16),int(r['value'],16))!=e for r,e in zip(prefix,expected)):raise ValueError('amplifier prepare/reset ordering')
    suffix=[('reset-assert',gpio+4,0),('reset-restore',gpio,1)]+[('restore',aux[i],originals[i]) for i in reversed(changed)]
    tail=writes[-len(suffix):]
    if len(tail)!=len(suffix) or any((r.get('phase'),int(r['address'],16),int(r['value'],16))!=e for r,e in zip(tail,suffix)):raise ValueError('amplifier reset/auxiliary release ordering')
    middle=writes[len(expected):-len(suffix)]
    if amp_registers:
     _inspect_aw2032_snapshot(text,device,[row for row in middle if row.get('phase')=='register-read'])
     middle=[row for row in middle if row.get('phase')!='register-read']
    if amp_quiet:
     _inspect_aw2032_quiet(text,device,[row for row in middle if row.get('phase') in ('quiet-write','quiet-read')])
     middle=[row for row in middle if row.get('phase') not in ('quiet-write','quiet-read')]
    if amp_profile:
     _inspect_aw2032_profile(text,device,[row for row in middle if row.get('phase') in ('profile-write','profile-read')])
     middle=[row for row in middle if row.get('phase') not in ('profile-write','profile-read')]
    if amp_digital:
     _inspect_aw2032_digital(text,device,[row for row in middle if row.get('phase') in ('digital-write','digital-read')],muted_analog=amp_analog)
     middle=[row for row in middle if row.get('phase') not in ('digital-write','digital-read')]
    if amp_analog:
     _inspect_aw2032_analog(text,device,[row for row in middle if row.get('phase') in ('analog-write','analog-read')])
     middle=[row for row in middle if row.get('phase') not in ('analog-write','analog-read')]
    commands=[];tx_words=0
    if not middle:raise ValueError('amplifier bus operations missing')
    for row in middle:
     address=int(row['address'],16);value=int(row['value'],16)
     if row.get('phase')!='transfer':raise ValueError('unexpected amplifier write phase')
     if address==0x880600:commands.append(value)
     elif address==0x880700:
      if value:raise ValueError('amplifier register pointer must be zero')
      tx_words+=1
     elif address==0x88026c:
      if value!=1:raise ValueError('amplifier TX length')
     elif address==0x880270:
      if value!=2:raise ValueError('amplifier RX length')
     elif address==0x88080c:
      if value not in (0,1):raise ValueError('amplifier TX watermark')
     elif address==0x880618:
      if not value or value&~0xffc0ffff:raise ValueError('amplifier foreign IRQ clear')
     elif address==0x880648:
      if not value or value&~0xc000000:raise ValueError('amplifier foreign secondary IRQ clear')
     else:raise ValueError('unapproved amplifier write address')
    if commands!=[(1<<27)|(device<<9)|4,(2<<27)|(device<<9)] or tx_words!=1:raise ValueError('ID-only repeated-start command sequence')
    operations=[(int(row['address'],16),int(row['value'],16)) for row in middle]
    wm=[(i,v) for i,(a,v) in enumerate(operations) if a==0x88080c]
    command_positions=[i for i,(a,v) in enumerate(operations) if a==0x880600];tx_position=next(i for i,(a,v) in enumerate(operations) if a==0x880700)
    if len(wm)!=2 or wm[0][1]!=1 or wm[1][1]!=0 or not command_positions[0]<wm[0][0]<tx_position<wm[1][0]<command_positions[1]:raise ValueError('amplifier FIFO watermark lifecycle')
    if not exact(transfer,device=d,PASS=1,attempted=1,commands=2,tx_words=1,rx_words=1,chip_id='2032',aborts=0,dirty=0) or int(transfer['raw_rx'],16)&0xffff!=0x3220 or not 1<=int(transfer['polls'])<=8000 or not 0<=int(transfer['elapsed_us'])<40000:raise ValueError('amplifier ID response/time bound')
    raw_irq=int(transfer['idle_IRQ'],16);irq_enable=int(transfer['idle_IRQ_enable'],16)
    if raw_irq not in (0,0x80) or irq_enable!=0x7fc0347f or raw_irq&irq_enable or rv[0x880614]&0x80:raise ValueError('amplifier masked RX summary contract')
    s_seen=int(transfer['S_IRQ_seen'],16);s_last=int(transfer['S_IRQ_last'],16);s_cleared=0
    for row in middle:
     if row['address']=='880648':s_cleared|=int(row['value'],16)
    if s_seen&~0xc000000 or s_last&~s_seen or s_cleared!=s_seen:raise ValueError('amplifier secondary FIFO event ownership')
    post_irq=int(end['post_IRQ'],16);post_enable=int(end['post_IRQ_enable'],16)
    if post_irq not in (0,0x80) or post_enable!=originals[2] or post_irq&post_enable:raise ValueError('amplifier post-restore enabled IRQs')
    if not exact(end,device=d,PASS=1,chip_id='2032',reset_restored=1,bus_clean=1,aux_restored=1,dirty=0,logical_quiesce=1,raw_IRQ_restored=int(not post_irq),cold_reset_required=1,amplifier_register_writes=50 if amp_analog else 48 if amp_digital else 46 if amp_profile else 1 if amp_quiet else 0,speaker_switch_writes=0,audio_acceptance=0):raise ValueError('amplifier reset/bus cleanup')
    start=text.find(f'marble-i2c-amp: saved device={d} index=0 ');release=text.find(f'marble-i2c-amp: write address={gpio+4:x} value=2 phase=reset-release device={d}');reply=text.find(f'marble-i2c-amp: transfer device={d} ');asserted=text.find(f'marble-i2c-amp: write address={gpio+4:x} value=0 phase=reset-assert device={d}');finish=text.find(f'marble-i2c-amp: complete device={d} ')
    if not text.find('marble-i2c-setup: applied ')<start<release<reply<asserted<finish<text.find('marble-i2c-setup: write phase=restore '):raise ValueError('amplifier lifecycle ordering')
   if len(amp_writes)!=sum(r.get('device') in ('34','35') for r in amp_writes) or amp_gpio!=8 or text.find('marble-i2c-amp: complete device=34 ')>text.find('marble-i2c-amp: saved device=35 index=0 '):raise ValueError('amplifier device ownership/count')
  except (KeyError,ValueError) as e:errors.append(str(e))
 return dict(passed=not errors,errors=errors,digital_commands=digital_commands,analog_commands=analog_commands,amplifier_muted_analog_verified=amp_analog and not errors,protocol_and_code_verified=not errors,clock_names_verified=clock and not errors,clock_lease_verified=cycle and not errors,clock_frequency_measured=False,passive_snapshot_captured=registers and not errors,controller_identity_verified=registers and identity and not errors,route_snapshot_captured=route and not errors,bus_configuration_verified=setup and not errors,route_values=route_values if route and not errors else None,DT_provider_observed=dt and not errors,DT_ABI_verified=False,ROM_snapshot_captured=config and not errors,raw_ROM_blob_match=(end[0].get('ROM_blob_match')=='1') if config and not errors else None,ROM_configuration_verified=False,controller_verified=amp and not errors,chip_id_verified=amp and not errors,amplifier_reset_registers_verified=amp_registers and not errors,amplifier_quiet_write_verified=amp_quiet and not errors,amplifier_Music_profile_verified=amp_profile and not errors,amplifier_digital_PLL_verified=amp_digital and not errors,audio_acceptance=False)


def _inspect_aw2032_snapshot(text,device,writes):
 d=f'{device:x}'
 regs=(1,3,4,5,6,7,8,0x66,0x78,0x7a)
 lines=text.splitlines()
 rows=[dict(re.findall(r'(\w+)=([^\s]+)',l)) for l in lines if l.startswith('marble-aw-reg: read ') and f'device={d} ' in l]
 ends=[dict(re.findall(r'(\w+)=([^\s]+)',l)) for l in lines if l.startswith('marble-aw-reg: complete ') and f'device={d} ' in l]
 if len(rows)!=10 or len(ends)!=1:raise ValueError('AW2032 snapshot cardinality')
 for i,(r,reg) in enumerate(zip(rows,regs)):
  if any(r.get(k)!=str(v) for k,v in dict(index=i,PASS=1,commands=2,tx_words=1,rx_words=1,aborts=0,dirty=0).items()) or int(r['reg'],16)!=reg or not 0<=int(r['value'],16)<=65535 or not 0<=int(r['elapsed_us'])<40000:raise ValueError('AW2032 register response')
 values={reg:int(row['value'],16) for reg,row in zip(regs,rows)}
 if values[4]&3!=3 or not values[5]&0x10:raise ValueError('AW2032 not powered down and muted')
 if any(ends[0].get(k)!=str(v) for k,v in dict(PASS=1,reads=10,powered_down=1,muted=1,amplifier_register_writes=0,speaker_switch_writes=0,audio_acceptance=0).items()):raise ValueError('AW2032 reset-state contract')
 commands=[];pointers=[];lens=[];rx=[];wm=[]
 for r in writes:
  a=int(r['address'],16);v=int(r['value'],16)
  if a==0x880600:commands.append(v)
  elif a==0x880700:pointers.append(v)
  elif a==0x88026c:lens.append(v)
  elif a==0x880270:rx.append(v)
  elif a==0x88080c:wm.append(v)
  elif a==0x880618:
   if not v or v&~0xffc0ffff:raise ValueError('AW2032 foreign IRQ clear')
  elif a==0x880648:
   if not v or v&~0xc000000:raise ValueError('AW2032 foreign secondary clear')
  else:raise ValueError('AW2032 unapproved bus write')
 if pointers!=list(regs) or commands!=[(1<<27)|(device<<9)|4,(2<<27)|(device<<9)]*10 or lens!=[1]*10 or rx!=[2]*10 or wm!=[1,0]*10:raise ValueError('AW2032 read-only command contract')
 # Each complete typed reply must precede the next pointer and reset assertion.
 start=text.index(f'marble-i2c-amp: transfer device={d} ')
 finish=text.index(f'marble-i2c-amp: write address={0xf101004 if device==0x34 else 0xf178004:x} value=0 phase=reset-assert device={d}')
 events=[l for l in lines if 'phase=register-read' in l and f'device={d}' in l or l.startswith('marble-aw-reg:') and f'device={d} ' in l]
 relevant='\n'.join(events)
 if any(not start<text.index(l)<finish for l in events):raise ValueError('AW2032 snapshot outside reset lease')
 for i,reg in enumerate(regs):
  pat=f'marble-i2c-amp: write address=880700 value={reg:x} phase=register-read device={d}'
  reply=next(l for l in events if l.startswith('marble-aw-reg: read ') and f'index={i} ' in l)
  if relevant.index(pat)>relevant.index(reply) or i and relevant.index(previous)>relevant.index(pat):raise ValueError('AW2032 reply ordering')
  previous=reply


def _inspect_aw2032_quiet(text,device,writes):
 d=f'{device:x}'
 def rows(prefix):return [dict(re.findall(r'(\w+)=([^\s]+)',l)) for l in text.splitlines() if l.startswith(prefix) and f'device={d} ' in l]
 reads=rows('marble-aw-quiet: read ');end=rows('marble-aw-quiet: complete ')
 initial=rows('marble-aw-reg: read ')
 if len(reads)!=2 or len(end)!=1 or len(initial)!=10:raise ValueError('AW2032 quiet evidence missing/duplicate')
 initial={int(r['reg'],16):int(r['value'],16) for r in initial}
 target=0xf000|(initial[5]&0x3f)|0x10
 for i,r in enumerate(reads):
  if any(r.get(k)!=str(v) for k,v in dict(index=i,PASS=1,commands=2,tx_words=1,rx_words=1,aborts=0,dirty=0).items()) or int(r['reg'],16)!=4+i or int(r['value'],16)!=(initial[4] if not i else target) or not 0<=int(r['elapsed_us'])<40000:raise ValueError('AW2032 quiet readback')
 r=end[0]
 if any(r.get(k)!=str(v) for k,v in dict(PASS=1,readbacks=2,commands=1,tx_words=1,rx_words=0,aborts=0,dirty=0,amplifier_register_writes=1,speaker_switch_writes=0,audio_acceptance=0).items()) or int(r['before'],16)!=initial[5] or int(r['target'],16)!=target or int(r['after'],16)!=target or int(r['sysctrl'],16)!=initial[4] or not 0<=int(r['elapsed_us'])<30000:raise ValueError('AW2032 quiet write contract')
 write=[r for r in writes if r['phase']=='quiet-write'];read=[r for r in writes if r['phase']=='quiet-read']
 expected_word=5|((target>>8)<<8)|((target&255)<<16)
 core=[(int(r['address'],16),int(r['value'],16)) for r in write if r['address'] not in ('880618','880648')]
 if core!=[(0x88026c,3),(0x880600,(1<<27)|(device<<9)),(0x88080c,1),(0x880700,expected_word),(0x88080c,0)]:raise ValueError('AW2032 quiet byte order/write-stop')
 core=[(int(r['address'],16),int(r['value'],16)) for r in read if r['address'] not in ('880618','880648')]
 expected=[]
 for reg in (4,5):expected += [(0x88026c,1),(0x880600,(1<<27)|(device<<9)|4),(0x88080c,1),(0x880700,reg),(0x88080c,0),(0x880270,2),(0x880600,(2<<27)|(device<<9))]
 if core!=expected:raise ValueError('AW2032 quiet readback sequence')
 for r in writes:
  a=int(r['address'],16);v=int(r['value'],16)
  if a==0x880618 and (not v or v&~0xffc0ffff) or a==0x880648 and (not v or v&~0xc000000):raise ValueError('AW2032 quiet foreign IRQ clear')
 lines=text.splitlines();events=[l for l in lines if l.startswith('marble-aw-quiet:') and f'device={d} ' in l or 'phase=quiet-' in l and f'device={d}' in l]
 lo=text.index(f'marble-aw-reg: complete device={d} ');hi=text.index(f'marble-i2c-amp: write address={0xf101004 if device==0x34 else 0xf178004:x} value=0 phase=reset-assert device={d}')
 if any(not lo<text.index(l)<hi for l in events):raise ValueError('AW2032 quiet operation outside reset lease')
 phases=[r['phase'] for r in writes]
 if phases!=['quiet-write']*len(write)+['quiet-read']*len(read):raise ValueError('AW2032 quiet write/read ordering')


def _inspect_aw2032_profile(text,device,writes):
 import importlib.util
 from pathlib import Path
 source=Path(__file__).with_name('marble-aw-profile.py')
 spec=importlib.util.spec_from_file_location('aw_profile_report',source);profile=importlib.util.module_from_spec(spec);spec.loader.exec_module(profile)
 board=profile.qualify();d=f'{device:x}'
 def rows(prefix):return [dict(re.findall(r'(\w+)=([^\s]+)',l)) for l in text.splitlines() if l.startswith(prefix) and f'device={d} ' in l]
 initial={int(r['reg'],16):int(r['value'],16) for r in rows('marble-aw-reg: read ')}
 calibration=profile.vcalb(initial[0x78],initial[0x7a])
 pairs=[(r['address'],profile.target(r['address'],r['value'],initial[4],initial[5])) for r in board['devices'][device-0x34]['registers']]+[(0x56,calibration)]
 words=rows('marble-aw-profile: word ');end=rows('marble-aw-profile: complete ')
 if len(words)!=45 or len(end)!=1:raise ValueError('AW2032 Music profile evidence missing/duplicate')
 expected=[]
 for i,((reg,value),r) in enumerate(zip(pairs,words)):
  if any(r.get(k)!=str(v) for k,v in dict(index=i,PASS=1,write_commands=1,read_commands=2,tx_words=2,rx_words=1,aborts=0,dirty=0).items()) or int(r['reg'],16)!=reg or int(r['target'],16)!=value or int(r['observed'],16)!=value or not 0<=int(r['elapsed_us'])<70000:raise ValueError('AW2032 Music profile word mismatch')
  packed=reg|((value>>8)<<8)|((value&255)<<16)
  expected += [('profile-write',a,v) for a,v in ((0x88026c,3),(0x880600,(1<<27)|(device<<9)),(0x88080c,1),(0x880700,packed),(0x88080c,0))]
  expected += [('profile-read',a,v) for a,v in ((0x88026c,1),(0x880600,(1<<27)|(device<<9)|4),(0x88080c,1),(0x880700,reg),(0x88080c,0),(0x880270,2),(0x880600,(2<<27)|(device<<9)))]
 actual=[]
 for r in writes:
  a=int(r['address'],16);v=int(r['value'],16)
  if a==0x880618:
   if not v or v&~0xffc0ffff:raise ValueError('AW2032 profile foreign IRQ clear')
  elif a==0x880648:
   if not v or v&~0xc000000:raise ValueError('AW2032 profile foreign secondary clear')
  else:actual.append((r['phase'],a,v))
 if actual!=expected:raise ValueError('AW2032 profile bus sequence/data mismatch')
 r=end[0]
 if any(r.get(k)!=str(v) for k,v in dict(PASS=1,words=45,amplifier_register_writes=45,power_up=0,speaker_switch_writes=0,audio_acceptance=0).items()) or int(r['efrh'],16)!=initial[0x78] or int(r['efrm1'],16)!=initial[0x7a] or int(r['vcalb'],16)!=calibration:raise ValueError('AW2032 Music profile final contract')
 lines=text.splitlines();events=[l for l in lines if l.startswith('marble-aw-profile:') and f'device={d} ' in l or 'phase=profile-' in l and f'device={d}' in l]
 lo=text.index(f'marble-aw-quiet: complete device={d} ');hi=text.index(f'marble-i2c-amp: write address={0xf101004 if device==0x34 else 0xf178004:x} value=0 phase=reset-assert device={d}')
 if any(not lo<text.index(l)<hi for l in events):raise ValueError('AW2032 profile outside reset lease')
 pos=lo
 for i,(reg,value) in enumerate(pairs):
  ptr=f'marble-i2c-amp: write address=880700 value={reg:x} phase=profile-read device={d}'
  reply=next(l for l in events if l.startswith('marble-aw-profile: word ') and f'index={i} ' in l)
  ptrpos=text.find(ptr,pos);replypos=text.find(reply,pos)
  if ptrpos<pos or replypos<=ptrpos:raise ValueError('AW2032 profile readback ordering')
  pos=replypos+len(reply)


def _inspect_aw2032_digital(text,device,writes,muted_analog=False):
 d=f'{device:x}';lines=text.splitlines()
 def rows(prefix):return [dict(re.findall(r'(\w+)=([^\s]+)',l)) for l in lines if l.startswith(prefix) and f'device={d} ' in l]
 def exact(r,**v):return all(r.get(k)==str(x) for k,x in v.items())
 end=rows('marble-aw-digital: complete ');access=rows('marble-aw-digital: access ')
 if len(end)!=1:raise ValueError('digital PLL completion missing/duplicate')
 r=end[0];polls=int(r['polls']);down=0xb443 if device==0x34 else 0x3443
 if not 1<=polls<=10 or not exact(r,PASS=1,attempted=1,writes=2,locked=1,restored=1,dirty=0,sysctrl=f'{down:x}',sysctrl2='f039',AMPPD_retained=0 if muted_analog else 1,HMUTE_retained=1,attenuation_db=90,speaker_switch_writes=0,audio_acceptance=0):raise ValueError('digital-only PLL/restore contract')
 if len(access)!=8+polls:raise ValueError('digital PLL bounded access count')
 desired=[(1,4,down),(1,5,0xf039),(0,4,down&~1),(1,4,down&~1),(1,5,0xf039)]
 for i in range(polls):
  status=int(access[5+i]['value'],16)
  if status&0xcc8a or ((status&0xccbb)==0x11)!=(i==polls-1):raise ValueError('digital PLL status/fault progression')
  desired.append((1,1,status))
 desired += [(0,4,down),(1,4,down),(1,5,0xf039)]
 if int(r['status'],16)!=desired[4+polls][2]:raise ValueError('digital PLL final status identity')
 expected=[]
 for a,(read,reg,value) in zip(access,desired):
  if not exact(a,read=read,reg=f'{reg:x}',value=f'{value:x}',PASS=1,commands=2 if read else 1,dirty=0):raise ValueError('digital PLL typed read/write')
  phase='digital-read' if read else 'digital-write'
  if read:ops=[(0x88026c,1),(0x880600,(1<<27)|(device<<9)|4),(0x88080c,1),(0x880700,reg),(0x88080c,0),(0x880270,2),(0x880600,(2<<27)|(device<<9))]
  else:ops=[(0x88026c,3),(0x880600,(1<<27)|(device<<9)),(0x88080c,1),(0x880700,reg|((value>>8)<<8)|((value&255)<<16)),(0x88080c,0)]
  expected += [(phase,a,v) for a,v in ops]
 actual=[]
 for w in writes:
  a=int(w['address'],16);v=int(w['value'],16)
  if a==0x880618:
   if not v or v&~0xffc0ffff:raise ValueError('digital PLL foreign IRQ clear')
  elif a==0x880648:
   if not v or v&~0xc000000:raise ValueError('digital PLL foreign secondary clear')
  else:actual.append((w['phase'],a,v))
 if actual!=expected:raise ValueError('digital PLL exact MMIO/command/value sequence')
 events=[l for l in lines if (l.startswith('marble-aw-digital:') or 'phase=digital-' in l) and f'device={d}' in l]
 lo=text.index(f'marble-aw-profile: complete device={d} ');hi=text.index(f'marble-i2c-amp: write address={0xf101004 if device==0x34 else 0xf178004:x} value=0 phase=reset-assert device={d}')
 if any(not lo<text.index(l)<hi for l in events):raise ValueError('digital PLL must remain inside profile/reset lease')
 if not events or not events[-1].startswith('marble-aw-digital: complete '):raise ValueError('digital PLL completion before final restore')

def _inspect_aw2032_analog(text,device,writes):
 d=f'{device:x}';lines=text.splitlines()
 def rows(prefix):return [dict(re.findall(r'(\w+)=([^\s]+)',l)) for l in lines if l.startswith(prefix) and f'device={d} ' in l]
 def exact(r,**v):return all(r.get(k)==str(x) for k,x in v.items())
 end=rows('marble-aw-analog: complete ');access=rows('marble-aw-analog: access ')
 if len(end)!=1:raise ValueError('muted analog completion missing/duplicate')
 r=end[0];polls=int(r['polls']);digital=0xb442 if device==0x34 else 0x3442
 if not 1<=polls<=10 or not 0<int(r['elapsed_us'])<100000 or not exact(r,PASS=1,attempted=1,writes=2,ready=1,restored=1,dirty=0,HMUTE_retained=1,attenuation_db=90,speaker_switch_writes=0,audio_acceptance=0):raise ValueError('muted analog bounded restore contract')
 if len(access)!=9+polls:raise ValueError('muted analog bounded access count')
 status=int(access[2]['value'],16)
 if status&0xccbb!=0x11:raise ValueError('analog requires locked PLL without faults')
 desired=[(1,4,digital),(1,5,0xf039),(1,1,status),(0,4,digital&~2),(1,4,digital&~2),(1,5,0xf039)]
 for i in range(polls):
  status=int(access[6+i]['value'],16)
  if status&0xcc8a or ((status&0xcfbb)==0x311)!=(i==polls-1):raise ValueError('muted analog status/fault progression')
  desired.append((1,1,status))
 desired += [(0,4,digital),(1,4,digital),(1,5,0xf039)]
 if int(r['status'],16)!=desired[5+polls][2]:raise ValueError('analog final status identity')
 expected=[]
 for a,(read,reg,value) in zip(access,desired):
  if not exact(a,read=read,reg=f'{reg:x}',value=f'{value:x}',PASS=1,commands=2 if read else 1,dirty=0):raise ValueError('muted analog typed read/write')
  phase='analog-read' if read else 'analog-write'
  if read:ops=[(0x88026c,1),(0x880600,(1<<27)|(device<<9)|4),(0x88080c,1),(0x880700,reg),(0x88080c,0),(0x880270,2),(0x880600,(2<<27)|(device<<9))]
  else:ops=[(0x88026c,3),(0x880600,(1<<27)|(device<<9)),(0x88080c,1),(0x880700,reg|((value>>8)<<8)|((value&255)<<16)),(0x88080c,0)]
  expected += [(phase,a,v) for a,v in ops]
 actual=[]
 for w in writes:
  a=int(w['address'],16);v=int(w['value'],16)
  if a==0x880618:
   if not v or v&~0xffc0ffff:raise ValueError('muted analog foreign IRQ clear')
  elif a==0x880648:
   if not v or v&~0xc000000:raise ValueError('muted analog foreign secondary clear')
  else:actual.append((w['phase'],a,v))
 if actual!=expected:raise ValueError('muted analog exact MMIO/command/value sequence')
 events=[l for l in lines if (l.startswith('marble-aw-analog:') or 'phase=analog-' in l) and f'device={d}' in l]
 digital_access=rows('marble-aw-digital: access ')
 if len(digital_access)<9:raise ValueError('analog lacks outer digital lifecycle')
 lock_line=next(l for l in lines if l.startswith(f'marble-aw-digital: access device={d} read=1 reg=1 ') and int(dict(re.findall(r'(\w+)=([^\s]+)',l))['value'],16)&0xccbb==0x11)
 down=0xb443 if device==0x34 else 0x3443
 restore=next(l for l in lines if l.startswith(f'marble-aw-digital: access device={d} read=0 reg=4 value={down:x} '))
 lo=text.index(lock_line);hi=text.index(restore)
 if any(not lo<text.index(l)<hi for l in events):raise ValueError('analog must remain inside locked digital window')
 if not events or not events[-1].startswith('marble-aw-analog: complete '):raise ValueError('analog completion before final restore')


def inspect_audio_hold(text):
 base=inspect(text,clock=True,cycle=True,registers=True,route=True,setup=True)
 errors=list(base['errors'])
 try:
  def row(prefix):
   lines=[l for l in text.splitlines() if l.startswith(prefix)]
   if len(lines)!=1:raise ValueError('missing/duplicate combined audio I2C evidence '+prefix)
   return dict(re.findall(r'(\w+)=([^\s]+)',lines[0]))
  expected=(('ai: begin ',dict(DSP_start=0,amps_reset=1,frequency_requests=0)),
   ('ai: work enter=',dict(enter=1,configuration_active=1,clocks_active=1,amp_commands=0)),
   ('ai: work returned=',dict(returned=1,DSP_dirty=0,amp_commands=0)),
   ('ai: end ',dict(PASS=1,called=1,DSP_passed=1,DSP_dirty=0,config_active=0,clock_active=0,dirty=0,amp_commands=0,cold_reset_required=1,audio_acceptance=0)))
  for prefix,values in expected:
   r=row(prefix)
   if any(r.get(k)!=str(v) for k,v in values.items()):raise ValueError('combined audio I2C contract '+prefix)
  end=row('ai: end ')
  if not 0<int(end['elapsed_us'])<30000000:raise ValueError('combined audio I2C time bound')
  stages=('ai: begin ','marble-i2c-setup: applied ','ai: work enter=','marble-adsp-owned: begin ',
   'marble-adsp-owned: complete PASS=1','ai: work returned=','marble-i2c-setup: write phase=restore ',
   'marble-i2c-setup: complete ','marble-i2c-lease: disable index=4 ','marble-i2c-lease: complete ','ai: end ')
  positions=[text.index(p) for p in stages]
  if positions!=sorted(positions) or len(set(positions))!=len(positions):raise ValueError('combined audio I2C acquire/DSP/restore ordering')
 except (KeyError,ValueError) as exc:errors.append(str(exc))
 return dict(passed=not errors,errors=errors,qualified_I2C_held_across_DSP=not errors,amplifier_commands=0,audio_acceptance=False,
  scope='verified I2C protocol, configuration, five unchanged-frequency clock references held across DSP; exact configuration/reference restore after bounded DSP stop; no amp commands')


def inspect_audio_window(text,amp_profile=False,prestart=False,preboot=False,qup_hold=False,runtime_deadline=False,digital_pll=False,runtime_600ms=False,digital_early_stop=False,fast_stop=False,muted_analog=False):
 base=inspect(text,clock=True,cycle=True,registers=True,route=True,setup=True,amp=amp_profile,amp_registers=amp_profile,amp_quiet=amp_profile,amp_profile=amp_profile,amp_digital=digital_pll,amp_analog=muted_analog)
 commands=(324+base.get('digital_commands',0)+base.get('analog_commands',0)) if digital_pll else 324 if amp_profile else 0
 errors=list(base['errors'])
 if digital_pll and not runtime_deadline:errors.append('digital PLL requires runtime deadline')
 if muted_analog and not fast_stop:errors.append('muted analog requires fast STOP profile')
 if prestart and not amp_profile:errors.append('prestart window requires explicit Music profile')
 if preboot and not prestart:errors.append('preboot requires explicit prestart mode')
 if qup_hold:
  if not amp_profile or prestart or preboot:errors.append('extra QUP hold requires after-START Music')
  errors+=inspect_audio_qup_hold(text)['errors']
 elif 'ah:' in text:errors.append('extra QUP lease requires explicit contract')
 try:
  def row(prefix):
   lines=[l for l in text.splitlines() if l.startswith(prefix)]
   if len(lines)!=1:raise ValueError('missing/duplicate audio I2C window evidence '+prefix)
   return dict(re.findall(r'(\w+)=([^\s]+)',lines[0]))
  if fast_stop:
   if not digital_early_stop:raise ValueError('fast STOP requires typed completion contract')
   policy=row('aud-stop-policy: ')
   values=dict(poll_us=100,timeout_us=5000000,thermal_interval_us=100000,counter_read_only=1,IRQ_masks_unchanged=1,fresh_ACK_required=1)
   if any(policy.get(k)!=str(v) for k,v in values.items()):raise ValueError('fast STOP timing/thermal/ACK contract')
   if not text.index('marble-smp2p-boot: ready ')<text.index('aud-stop-policy: ')<text.index('marble-smp2p-boot: stop-ack PASS=1')<text.index('marble-smccc: PAS_stop PASS=1'):raise ValueError('fast STOP fresh ACK before PAS')
  elif 'aud-stop-policy:' in text:raise ValueError('fast STOP requires explicit contract')
  if runtime_deadline:
   if not amp_profile or prestart or qup_hold:raise ValueError('runtime deadline requires after-START Music')
   if runtime_600ms and not digital_pll:raise ValueError('600ms budget requires digital PLL')
   rt=row('aw: runtime ');values=dict(deadline_us=600000 if runtime_600ms else 500000,poll_rounds=60 if runtime_600ms else 50,initial_service_cadence_us=100000,thermal_samples=6 if runtime_600ms else 5,includes_I2C_work=1,IRQ_masks_unchanged=1,frequency_requests=0)
   if digital_early_stop:
    if not digital_pll or runtime_600ms:raise ValueError('typed runtime requires separate digital contract')
    import importlib.util
    from pathlib import Path
    spec=importlib.util.spec_from_file_location('i2c_runtime_contract',Path(__file__).with_name('marble-audio-runtime-contract.py'))
    helper=importlib.util.module_from_spec(spec);spec.loader.exec_module(helper);budget=helper.contract(text)
    if 'aud-typed-budget:' not in text:raise ValueError('typed runtime marker missing')
    values.update(deadline_us=600000,poll_rounds=60,thermal_samples=budget['thermal_samples'])
   if any(rt.get(k)!=str(v) for k,v in values.items()):raise ValueError('actual runtime deadline contract')
   obs=row('marble-pil-boot: observed ')
   minimum=budget['minimum_us'] if digital_early_stop else 600000 if runtime_600ms else 500000
   samples=budget['thermal_samples'] if digital_early_stop else 6 if runtime_600ms else 5
   if not minimum<=int(obs['hold_us'])<750000 or obs['thermal_samples']!=str(samples) or obs['runtime_ok']!='1':raise ValueError('deadline actual runtime/thermal evidence')
   if not text.index('aw: window end ')<text.index('aw: runtime ')<text.index('marble-smccc: PAS_stop PASS=1'):raise ValueError('deadline/PCM/PAS order')
  elif 'aw: runtime ' in text:raise ValueError('runtime deadline requires explicit contract')
  expected=(('aw: begin ',dict(DSP_start=0,amps_reset=1,frequency_requests=0)),
   ('aw: window begin ',dict(stage=1 if preboot else 2 if prestart else 3,pins_active=0 if prestart else 1,amp_commands=0)),
   ('aw: lookup ',dict(PASS=1,after_START=0 if prestart else 1,amplifier_commands=0)),
   ('aw: window end ',dict(PASS=1,config_active=0,clock_active=0,amp_commands=commands)),
   ('aw: end ',dict(PASS=1,called=1,window_passed=1,DSP_passed=1,DSP_dirty=0,config_active=0,clock_active=0,dirty=0,amp_commands=commands,cold_reset_required=1,audio_acceptance=0)))
  for prefix,values in expected:
   r=row(prefix)
   if any(r.get(k)!=str(v) for k,v in values.items()):raise ValueError('audio I2C window contract '+prefix)
  t=row('aw: temperature ')
  if t.get('valid')!='1' or t.get('PASS')!='1' or t.get('limit_mc')!='50000' or not 0<=int(t['maximum_mc'])<50000:raise ValueError('audio I2C window temperature')
  if not 0<int(row('aw: end ')['elapsed_us'])<30000000:raise ValueError('audio I2C window duration')
  stages=('aw: begin ','marble-i2c: complete ','marble-adsp-owned: begin ')+(() if prestart else ('tp: acquired PASS=1',))+(
   'aw: window begin ','marble-i2c-clock: complete ','aw: lookup ','aw: temperature ','marble-i2c-lease: qualified ','marble-i2c-setup: applied ',
   'marble-i2c-setup: write phase=restore ','marble-i2c-lease: complete ','aw: window end ',
   )+(('marble-adsp-owned: phase=bandwidth ','marble-pil-boot: auth ','tr: q s=0 ','tp: acquired PASS=1') if preboot else ('tr: q s=2 ','tp: acquired PASS=1') if prestart else ())+('pd: q ','marble-adsp-owned: complete PASS=1','aw: end ')
  positions=[text.index(p) for p in stages]
  if positions!=sorted(positions) or len(set(positions))!=len(positions):raise ValueError('audio I2C window qualification/START/acquire/restore/PCM/DSP ordering')
  if amp_profile:
   amp_stages=('marble-i2c-setup: applied ','marble-aw-profile: complete device=34 PASS=1','marble-i2c-amp: complete device=34 PASS=1',
    'marble-aw-profile: complete device=35 PASS=1','marble-i2c-amp: complete device=35 PASS=1','marble-i2c-setup: write phase=restore ')
   p=[text.index(x) for x in amp_stages]
   if p!=sorted(p) or len(set(p))!=len(p):raise ValueError('both amplifiers must reset inside START configuration window')
 except (KeyError,ValueError) as exc:errors.append(str(exc))
 return dict(passed=not errors,errors=errors,qualified_I2C_window_after_START=not prestart and not errors,qualified_I2C_window_before_START=prestart and not errors,amplifier_commands=commands,amplifier_Music_profile_verified=amp_profile and not errors,amplifier_digital_PLL_verified=digital_pll and not errors,amplifier_muted_analog_verified=muted_analog and not errors,audio_acceptance=False,
  scope='qualified DSP START window; exact QUP2 restore before silent PCM; ROM Music/OTP down-muted readback and reset' if amp_profile else 'passive I2C binding before DSP, native Clock lookup and complete QUP2 lease/restore after START and before silent PCM submission; no amplifier commands')

def inspect_audio_qup_hold(text):
 errors=[]
 try:
  def rows(prefix):return [dict(re.findall(r'(\w+)=([^\s]+)',l)) for l in text.splitlines() if l.startswith(prefix)]
  def exact(r,**v):return all(r.get(k)==str(x) for k,x in v.items())
  begin=rows('ah: begin ');end=rows('ah: end ');states=rows('ah: state ');en=rows('ah: enable ');dis=rows('ah: disable ')
  if (len(begin),len(end),len(states),len(en),len(dis))!=(1,1,20,5,5):raise ValueError('extra QUP reference count evidence')
  if not exact(begin[0],PASS=1,held='1f',uncertain=0,active=1,dirty=0,after_START=1,release_after_PAS=1,frequency_requests=0,voltage_requests=0):raise ValueError('extra QUP ownership acquisition')
  if not exact(end[0],PASS=1,restored=1,table_unchanged=1,I2C_head_unchanged=1,held=0,uncertain=0,active=0,dirty=0,PAS_stopped=1,frequency_requests=0,voltage_requests=0):raise ValueError('extra QUP post-PAS restoration')
  ids=[r['id'] for r in rows('marble-i2c-clock: lookup ')]
  if len(ids)!=5:raise ValueError('extra QUP exact clock IDs')
  for j,r in enumerate(states):
   i=j%5;phase=('before','acquire','acquire','restore')[j//5]
   if not exact(r,phase=phase,index=i,id=ids[i]) or r['enabled'] not in ('0','1') or r['on'] not in ('0','1') or r['status'] not in ('0,0,0','0,0,8000000000000013'):raise ValueError('extra QUP typed state')
   if not 0<=int(r['hz'])<=0xffffffff or (r['status']!='0,0,0' and r['hz']!='0'):raise ValueError('extra QUP frequency metadata')
   if 5<=j<10 or j>=15:
    if any(r[k]!=states[i][k] for k in ('enabled','on','hz','status')):raise ValueError('extra QUP changed original state')
   if 10<=j<15 and r['enabled']!='1':raise ValueError('extra QUP reference missing')
  for r,i in zip(en,range(5)):
   if not exact(r,index=i,id=ids[i],status=0):raise ValueError('extra QUP enable')
  for r,i in zip(dis,range(4,-1,-1)):
   if not exact(r,index=i,id=ids[i],status=0):raise ValueError('extra QUP reverse disable')
  inner=rows('marble-i2c-lease: state ')
  if len(inner)!=20 or any(inner[i][k]!=states[10+i][k] for i in range(5) for k in ('id','enabled','on','hz','status')):raise ValueError('inner QUP lease not enclosed by extra refs')
  order=('aw: lookup ','ah: state phase=before index=0 ','ah: begin ','marble-i2c-lease: qualified ','aw: window end ','pd: q ',
   'marble-smccc: PAS_stop PASS=1','marble-pil-held: restored PASS=1','ah: disable index=4 ','ah: end ','marble-adsp-owned: complete PASS=1')
  positions=[text.index(p) for p in order]
  if positions!=sorted(positions) or len(set(positions))!=len(positions):raise ValueError('extra QUP START/PAS/ROM/release order')
 except (KeyError,ValueError) as e:errors.append(str(e))
 return dict(passed=not errors,errors=errors,extra_refs_held_until_PAS=not errors,frequency_requests=0,voltage_requests=0,audio_acceptance=False)


def inspect_qup_core_hold(text):
 errors=[]
 try:
  def rows(prefix):return [dict(re.findall(r'(\w+)=([^\s]+)',l)) for l in text.splitlines() if l.startswith(prefix)]
  b=rows('qk: begin ');c=rows('qk: client ');i=rows('qk: issue ');e=rows('qk: held ')
  if (len(b),len(c),len(i),len(e))!=(1,1,2,1):raise ValueError('QUP2 keepalive evidence missing/duplicate')
  def exact(row,**values):return all(row.get(k)==str(v) for k,v in values.items())
  if not exact(b[0],name='QUP2',stable=1,qualified=1,free_status=0,data='60004001',sets='2,0',zero_votes=0,sleep_votes=0):raise ValueError('QUP2 command database/minimum vote binding')
  a=int(b[0]['address'],16)
  if not a or a&3 or (a>>16)&7!=5 or not 0<int(b[0]['unit'])<=0xffffffff or not 0<int(b[0]['width'])<=65535:raise ValueError('QUP2 resource class/aux data')
  baseline=rows('qk: baseline ')
  if len(baseline)!=1 or not (exact(baseline[0],mode=1,exact_idle_zero=0,votes=0) or exact(baseline[0],mode=2,exact_idle_zero=1,votes=0)):raise ValueError('QUP2 cache baseline qualification')
  if baseline[0]['mode']=='2':
   cached=rows('qk: cached ');cv=rows('qk: cached_vote ')
   if len(cached)!=1 or len(cv)!=1 or not exact(cached[0],read_only=1,pending=0) or int(cached[0]['address'],16)!=a or int(cached[0]['header'],16)!=a:raise ValueError('QUP2 cached resource identity')
   if not exact(cv[0],depth=0,words='2,0,1,100000000,0,0,0,4,0',read_only=1) or cv[0]['pointer']!=cached[0]['pointer']:raise ValueError('QUP2 cached vote must be exact idle zero')
  if not exact(c[0],status=0,owned_empty=1):raise ValueError('QUP2 owned client creation')
  for r,which in zip(i,(2,0)):
   if not exact(r,set=which,status=0,data='60004001') or int(r['address'],16)!=a or not 0<=int(r['id'])<=0xffffffff:raise ValueError('QUP2 ACTIVE/WAKE-only request')
  if not exact(e[0],PASS=1,churn_status=0,other_resources_clients_unchanged=1,vote_acknowledged=1,owned_drained=1,retained_client=1,data='60004001',issue_calls=2,zero_votes=0,sleep_votes=0,cold_reset_required=1,CPU_GPU_frequency_requests=0,voltage_requests=0,audio_acceptance=0):raise ValueError('QUP2 completion/ownership/retained recovery contract')
  floor=rows('qk: peak ')
  if len(floor)!=1 or not exact(floor[0],logical=0,expected='20004001',retained_floor_verified=1) or int(floor[0]['address'],16)!=a:raise ValueError('QUP2 retained floor must be separate from zero ICB aggregate')
  prefixes=('marble-xo-rpmh: held PASS=1','qk: begin ','qk: baseline ','qk: client ','qk: issue set=2 ','qk: issue set=0 ','qk: held ','marble-adsp-owned: phase=bandwidth ','qk: peak ')
  p=[text.index(x) for x in prefixes]
  if p!=sorted(p) or len(set(p))!=len(p):raise ValueError('QUP2 dependency ownership order')
 except (KeyError,ValueError) as exc:errors.append(str(exc))
 return dict(passed=not errors,errors=errors,minimum_QUP2_vote_acknowledged=not errors,client_retained_until_cold_reset=True,
  sleep_votes=0,zero_votes=0,CPU_GPU_frequency_requests=0,voltage_requests=0,audio_acceptance=False)
