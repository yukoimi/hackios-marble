import importlib.util,re,unittest
from pathlib import Path
BASE=Path(__file__).parent
def load(name,path):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
analog=load('repeat_analog_fixture',BASE/'test_marble_audio_analog_report.py')
data=load('repeat_data_report',BASE.parent/'marble-pcm-data-report.py')
owned=load('repeat_owned_report',BASE.parent/'marble-adsp-owned-report.py')
run=load('repeat_run_report',BASE.parent/'marble-tdm-run-report.py')
mapping=load('repeat_map_report',BASE.parent/'marble-audio-map-report.py')
runtime=load('repeat_runtime_contract',BASE.parent/'marble-audio-runtime-contract.py')

def fixture(requests=2):
 text=analog.fixture();begin=text.index('marble-glink-control: tx command=9 param1=1 param2=8 ')
 end=text.index('pd: end ');packet=text[begin:end]
 assert 'pd: q t=4d504401' in packet
 ret=lambda i:f'pr: retired index={i} token={0x4d504401+i:x} id=8 faults_ok=1 zero=1\n'
 text=text[:end]+ret(0)+''.join(packet.replace('t=4d504401',f't={0x4d504401+i:x}')+ret(i) for i in range(1,requests))+text[end:]
 end=text.index('\n',text.index('pd: end '))+1
 text=text[:end]+f'pr: end PASS=1 submitted={requests} completed={requests} unique_tokens={requests} page_bytes=4096 reuse_after_DONE_and_TX=1 zero=1 audio_acceptance=0\n'+text[end:]
 text=text.replace('PCM_requests=1',f'PCM_requests={requests}').replace('GPR_packets=8',f'GPR_packets={7+requests}')
 head=tail=ntx=nrx=0;out=[]
 for line in text.splitlines():
  if line.startswith('marble-glink-control: tx '):
   head=(head+(int(re.search(r'bytes=(\d+)',line)[1]) if 'bytes=' in line else 8))&16383;ntx+=1
   line=re.sub(r'local_head=\d+',f'local_head={head}',line)
  if line.startswith('marble-glink-control: rx '):
   tail=(tail+int(re.search(r'bytes=(\d+)',line)[1]))&16383;nrx+=1
   line=re.sub(r'local_tail=\d+',f'local_tail={tail}',line)
  out.append(line)
 text='\n'.join(out)
 text=re.sub(r'RX_messages=\d+',f'RX_messages={nrx}',text)
 return re.sub(r'TX_messages=\d+',f'TX_messages={ntx}',text)

class RepeatReport(unittest.TestCase):
 def test_unrelated_gpr_prefix_is_not_pcm_repeat(self):
  self.assertEqual(runtime.contract('marble-gpr: complete PASS=1')['hold_limit_ms'],500)

 def test_two_requests_and_owned_lifecycle(self):
  text=fixture();r=data.inspect(text,requests=2)
  self.assertTrue(r['passed'],r);self.assertEqual(r['PCM_bytes'],8192)
  self.assertFalse(r['continuous_playback']);self.assertFalse(r['audio_acceptance'])
  self.assertFalse(data.inspect(text)['passed'])
  kw=dict(scm_peak=True,wire=True,raw_shutdown=True,held=True,control=True,apps=True,gpr=True,tdm_run=True,tdm_pcm_graph=True,audio_dma=True,pcm_memory=True,pcm_data=2,tdm_pins=True)
  r=owned.inspect(text,1,**kw);self.assertTrue(r['passed'],r)
  kw['pcm_data']=True;self.assertFalse(owned.inspect(text,1,**kw)['passed'])
  r=run.inspect(text,pcm_graph=True,pcm_memory=True,pcm_data=2,tdm_pins=True);self.assertTrue(r['passed'],r)
  r=mapping.inspect(text,held=True,pcm_data=2);self.assertTrue(r['passed'],r)
  self.assertEqual(runtime.contract(text)['hold_limit_ms'],600)
 def test_eight_requests_require_explicit_count_and_complete_lifecycle(self):
  text=fixture(8);r=data.inspect(text,requests=8)
  self.assertTrue(r['passed'],r);self.assertEqual(r['PCM_bytes'],32768)
  self.assertFalse(data.inspect(text,requests=2)['passed'])
  self.assertFalse(data.inspect(text,requests=9)['passed'])
  kw=dict(scm_peak=True,wire=True,raw_shutdown=True,held=True,control=True,apps=True,gpr=True,tdm_run=True,tdm_pcm_graph=True,audio_dma=True,pcm_memory=True,pcm_data=8,tdm_pins=True)
  r=owned.inspect(text,1,**kw);self.assertTrue(r['passed'],r)
  for i in range(8):
   token=f'{0x4d504401+i:x}'
   with self.subTest(index=i):
    self.assertFalse(data.inspect(text.replace('token='+token,'token=deadbeef'),requests=8)['passed'])
  self.assertFalse(data.inspect(text.replace('unique_tokens=8','unique_tokens=2'),requests=8)['passed'])
 def test_mfc_format_is_explicit_and_preserves_4k_mapping(self):
  text=fixture().replace('n=528','n=608').replace('bytes=544','bytes=624')
  text=text.replace('bytes=4096 frames=512 rate=96000 bits=32','bytes=1024 frames=256 rate=48000 bits=16')
  text=text.replace('modules=2 links=1','modules=3 links=2')
  text+='\nmfc: path source_rate=48000 source_bits=16 output_rate=96000 output_bits=32 channels=2 module=7001015 instance=6003 mapped_bytes=4096 PCM_bytes=1024 conversion_verified=0 audio_acceptance=0\n'
  head=0;lines=[]
  for line in text.splitlines():
   if line.startswith('marble-glink-control: tx '):
    head=(head+(int(re.search(r'bytes=(\d+)',line)[1]) if 'bytes=' in line else 8))&16383
    line=re.sub(r'local_head=\d+',f'local_head={head}',line)
   lines.append(line)
  text='\n'.join(lines)
  r=data.inspect(text,requests=2,mfc=True);self.assertTrue(r['passed'],r)
  self.assertEqual(r['PCM_bytes'],2048);self.assertEqual(r['physical_buffer_bytes'],4096)
  self.assertFalse(r['conversion_verified']);self.assertFalse(data.inspect(text,requests=2)['passed'])
  kw=dict(scm_peak=True,wire=True,raw_shutdown=True,held=True,control=True,apps=True,gpr=True,tdm_run=True,tdm_pcm_graph=True,audio_dma=True,pcm_memory=True,pcm_data=2,tdm_pins=True,mfc=True)
  r=owned.inspect(text,1,**kw);self.assertTrue(r['passed'],r)
  r=run.inspect(text,pcm_graph=True,pcm_memory=True,pcm_data=2,tdm_pins=True,mfc=True);self.assertTrue(r['passed'],r)
  for a,b in [('source_rate=48000','source_rate=96000'),('PCM_bytes=1024','PCM_bytes=4096'),('module=7001015','module=700100e'),('conversion_verified=0','conversion_verified=1')]:
   self.assertFalse(data.inspect(text.replace(a,b),requests=2,mfc=True)['passed'])
 def test_changed_identity_reuse_fault_and_geometry_rejected(self):
  text=fixture()
  for a,b in [('t=4d504402','t=4d504401'),('completed=2','completed=1'),('reuse_after_DONE_and_TX=1','reuse_after_DONE_and_TX=0'),
              ('pd: d c=14','pd: d c=6'),('PCM_requests=2','PCM_requests=1'),('faults_ok=1','faults_ok=0'),
              ('token=4d504402','token=4d504401'),('frames=512','frames=1024'),('page_bytes=4096','page_bytes=8192'),
              ('submitted=2','submitted=3'),('zero=1','zero=0')]:
   with self.subTest(a=a):self.assertFalse(data.inspect(text.replace(a,b),requests=2)['passed'])
  ret='pr: retired index=0 token=4d504401 id=8 faults_ok=1 zero=1\n'
  bad=text.replace(ret,'');pos=bad.index('pd: end ');bad=bad[:pos]+ret+bad[pos:]
  self.assertFalse(data.inspect(bad,requests=2)['passed'])
 def test_missing_or_duplicate_typed_records_rejected(self):
  text=fixture()
  for line in text.splitlines():
   if line.startswith(('pd:','pr:')):
    with self.subTest(line=line):
     self.assertFalse(data.inspect(text.replace(line+'\n','',1),requests=2)['passed'])
     self.assertFalse(data.inspect(text+'\n'+line,requests=2)['passed'])

if __name__=='__main__':unittest.main()
