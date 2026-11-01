import importlib.util,unittest
from pathlib import Path
BASE=Path(__file__).parent
def load(name,path):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
old=load('amdb_gpr_fixture',BASE/'test_marble_gpr_state_report.py')
m=load('amdb_report',BASE.parent/'marble-amdb-report.py')
def fixture():
 s=old.fixture().replace('marble-gpr:','marble-amdb:').replace('4d535001','4d414d01')
 s=s.replace('opcode=1001021 bytes=24','opcode=1001007 bytes=96 module=700100e param=80013d6 graph_commands=0')
 s=s.replace('local_head=104 bytes=40','local_head=176 bytes=112').replace('local_head=112 bytes=8','local_head=184 bytes=8')
 s=s.replace('chunk=28 left=0 accumulated=28 frame_bytes=48','chunk=84 left=0 accumulated=84 frame_bytes=104')
 s=s.replace('kind=1 state=1 error=0 bytes=28','kind=1 error=0 param_error=0 module_error=0 present=1 major=2 minor=9 client_data=0 build=2026,10,30,12,0,0 bytes=84')
 s=s.replace('bytes=48 local_tail=128','bytes=104 local_tail=184').replace('SPF_state=2','module_state=2').replace('PAS_stopped=1 audio_acceptance=0\nmarble-glink-apps:', 'PAS_stopped=1 graph_commands=0 audio_acceptance=0\nmarble-glink-apps:')
 return s
class Amdb(unittest.TestCase):
 def test_mfc_requires_explicit_module_identity(self):
  text=fixture().replace('module=700100e','module=7001015')
  r=m.inspect(text,module_id='7001015');self.assertTrue(r['passed'],r)
  self.assertEqual(r['module_id'],'07001015')
  self.assertFalse(m.inspect(text)['passed'])
  self.assertFalse(m.inspect(fixture(),module_id='7001015')['passed'])
  self.assertFalse(m.inspect(text,module_id='700117c')['passed'])
 def test_metadata_and_independent_lifecycle(self):
  s=fixture();r=m.inspect(s);self.assertTrue(r['passed'],r);self.assertTrue(r['module_available']);self.assertFalse(r['audio_acceptance'])
  r=old.apps.f.held.scm.m.inspect(s,1,scm_peak=True,wire=True,raw_shutdown=True,held=True,control=True,apps=True,gpr=True,amdb=True)
  self.assertTrue(r['passed'],r);self.assertTrue(r['reset_safe'])
  self.assertFalse(old.apps.f.m.inspect(s,apps=True,gpr=True)['passed'])
 def test_absent_and_module_error_are_not_available(self):
  for s in (fixture().replace('present=1','present=0').replace('module_state=2','module_state=1'),fixture().replace('module_error=0','module_error=2')):
   r=m.inspect(s);self.assertTrue(r['passed'],r);self.assertFalse(r['module_available'])
 def test_badparam_basic_is_transport_only(self):
  s=fixture().replace('kind=1 error=0 param_error=0 module_error=0 present=1 major=2 minor=9 client_data=0 build=2026,10,30,12,0,0 bytes=84','kind=2 error=2 param_error=0 module_error=0 present=0 major=0 minor=0 client_data=0 build=0,0,0,0,0,0 bytes=32')
  s=s.replace('chunk=84 left=0 accumulated=84 frame_bytes=104','chunk=32 left=0 accumulated=32 frame_bytes=48').replace('bytes=104 local_tail=184','bytes=48 local_tail=128').replace('module_state=2','module_state=0')
  r=m.inspect(s);self.assertTrue(r['passed'],r);self.assertFalse(r['module_available']);self.assertEqual(r['basic_error'],2)
  r=m.inspect(s.replace('client_data=0','client_data=a8'));self.assertTrue(r['passed'],r);self.assertFalse(r['header_conformant']);self.assertFalse(r['metadata_exchange']);self.assertFalse(r['module_available'])
 def test_invalid_exchange(self):
  for a,b in [('bytes=96','bytes=24'),('bytes=112','bytes=40'),('token=4d414d01','token=1'),('module=700100e','module=700117c'),('graph_commands=0','graph_commands=1'),('remote_id=1','remote_id=2'),('tx_done=1','tx_done=0'),('PAS_stopped=1','PAS_stopped=0'),('build=2026,10','build=2026,13'),('present=1','present=2'),('accumulated=84','accumulated=83'),('module_state=2','module_state=0')]:
   self.assertFalse(m.inspect(fixture().replace(a,b))['passed'],a)
 def test_missing_and_duplicate(self):
  s=fixture()
  for l in s.splitlines():
   if l.startswith('marble-amdb:'):
    self.assertFalse(m.inspect(s.replace(l,''))['passed'],l);self.assertFalse(m.inspect(s+'\n'+l)['passed'],l)
class Controller(unittest.TestCase):
 def simulate(self,bad=False,changed=True,cycle=False,graph=False,config=False,clock=False,tdm_run=False,pcm_graph=False,audio_dma=False,pcm_memory=False,pcm_data=False,tdm_pins=False):
  import contextlib,io,json,subprocess,sys,tempfile
  from unittest.mock import patch
  sys.path.insert(0,str(BASE.parent.resolve()))
  pcm_data=pcm_data or tdm_pins
  pcm_memory=pcm_memory or pcm_data
  audio_dma=audio_dma or pcm_memory
  pcm_graph=pcm_graph or audio_dma
  tdm_run=tdm_run or pcm_graph
  graph=graph or config
  preview=load('amdb_preview',BASE.parent/'marble-gpu-preview.py')
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp);out=root/'live';disk=root/'disk';disk.write_bytes(b'RAM');boot=root/'boot.img';boot.write_bytes(b'RAM')
   (root/'build-profile.json').write_text(json.dumps(dict(tdm_pins_only=tdm_pins,pcm_data_only=pcm_data,pcm_memory_only=pcm_memory,audio_map_probe=pcm_memory,audio_context_inventory=pcm_memory,audio_dma_inventory=audio_dma,tdm_pcm_graph_only=pcm_graph,tdm_run_only=tdm_run,audio_clock_only=clock,amdb_only=not (cycle or graph or clock or tdm_run),amdb_load_only=cycle,tdm_graph_only=graph,tdm_config_only=config,gpr_only=True)))
   state=[0];commands=[]
   def command(args,**kw):
    args=list(map(str,args));commands.append(args);result=''
    if args[0].endswith('adb.exe'):
     if args[-2:]==['reboot','bootloader']:state[0]=1
     elif args[-1]=='devices':result='YOUR_ANDROID_SERIAL\tdevice\n'
     elif args[-1]=='/proc/sys/kernel/random/boot_id':result=('11111111-1111-1111-1111-111111111111' if state[0]<3 or not changed else '22222222-2222-2222-2222-222222222222')+'\n'
     else:raise AssertionError(args)
    elif args[0]=='python':
     self.assertEqual(args[1],'tools/marble-run.py');self.assertIn('boot',args);state[0]=2
    elif args[0].endswith('fastboot.exe'):
     caps={'product':'marble','result':'idle','marble-gpu-preview':'gpu90-die50-60s-hard55-v2','marble-adsp-control-boot':'SCM-held-poll-GLINK-SMP2P-stop-v4','marble-adsp-tdm-config-cycle':'TDMcfg-noSTART1','marble-adsp-tdm-graph-cycle':'GPR-one-disconnected-TDM-open-close-v1','marble-adsp-amdb-load-cycle':'GPR-AMDB-one-TDM-load-unload-v1','marble-adsp-amdb-info':'GPR-AMDB-one-TDM-metadata-query-v1','marble-qmp-wire-preserved':'1'}
     caps['marble-tdm-pins-cycle']='CAPE-TDM4-RESTORE-v1'
     caps['marble-pcm-data-cycle']='APM-WREP-ZERO4096-v1'
     caps['marble-pcm-memory-cycle']='APM-MAP44-UNMAP28-v1'
     caps['marble-audio-map-cycle']='APPS-CB-ECATS-RESTORE2'
     caps['marble-audio-context-info']='APPS-CB-RO1'
     caps['marble-audio-dma-info']='APPS-SID1801-RO1'
     caps['marble-adsp-tdm-pcm-graph-cycle']='TDM528-PCM-noBUF1'
     caps['marble-adsp-tdm-run-cycle']='TDM432-run-v1'
     caps['marble-adsp-audio-clock-cycle']='PRM204-12288k-lease-v1'
     if args[-1]=='devices':result='xnuloader\tfastboot\n' if state[0]==2 else 'YOUR_ANDROID_SERIAL\tfastboot\n'
     elif 'getvar' in args:result=args[-1]+': '+caps[args[-1]]+'\n'
     elif 'get_staged' in args:
      path=Path(args[-1]);log=load('controller_pins_fixture',BASE/'test_marble_audio_pins_report.py').fixture() if tdm_pins else load('controller_data_fixture',BASE/'test_marble_pcm_data_report.py').fixture() if pcm_data else load('controller_memory_fixture',BASE/'test_marble_pcm_memory_report.py').fixture() if pcm_memory else load('controller_dma_fixture',BASE/'test_marble_audio_dma_report.py').fixture() if audio_dma else load('controller_tdm_run_fixture',BASE/'test_marble_tdm_run_report.py').fixture(pcm=pcm_graph) if tdm_run else load('controller_clock_fixture',BASE/'test_marble_audio_clock_report.py').fixture() if clock else load('controller_graph_fixture',BASE/'test_marble_tdm_graph_report.py').fixture(configure=config) if graph else load('controller_cycle_fixture',BASE/'test_marble_amdb_cycle_report.py').fixture() if cycle else fixture();log=log.replace('token=4d414d01','token=1').replace('token=4d4c4401','token=1').replace('token=4d475201','token=1').replace('token=4d434b01','token=1').replace('t=4d545201','t=1') if bad else log
      path.write_text('marble-efi-idle: probe PASS=1 active=0 dirty=0 failed=0 saved=0 now=0 period_us=426 waits=64 wait_us=12372 max_wait_us=426 immediate=32 late=0 max_poll_gap_us=426\n' if path.name=='idle-probe.log' else log)
     elif args[-1]=='reboot':state[0]=3
     elif 'oem' in args:self.assertIn(args[-1],('clearscreen','idle-probe','log','adsp-tdm-pcm-graph-cycle' if pcm_graph else 'adsp-tdm-run-cycle' if tdm_run else 'adsp-audio-clock-cycle' if clock else 'adsp-tdm-config-cycle' if config else 'adsp-tdm-graph-cycle' if graph else 'adsp-amdb-load-cycle' if cycle else 'adsp-amdb-info'))
     elif 'stage' in args:pass
     else:raise AssertionError(args)
    else:raise AssertionError(args)
    return subprocess.CompletedProcess(args,0,result.encode(),b'')
   argv=['preview','--out',str(out),'--boot-image',str(boot),'--ramdisk',str(disk),'--system-disk',str(disk),'--discovery-only','--adsp-tdm-pcm-graph-cycle' if pcm_graph else '--adsp-tdm-run-cycle' if tdm_run else '--adsp-audio-clock-cycle' if clock else '--adsp-tdm-config-cycle' if config else '--adsp-tdm-graph-cycle' if graph else '--adsp-amdb-load-cycle' if cycle else '--adsp-amdb-info','--adsp-package',str(BASE.parents[1]/'out/adsp-owned-package-v1-plan/dependencies.bin')]
   if audio_dma:argv.append('--audio-dma-info')
   if pcm_memory:argv+=['--audio-context-info','--audio-map-cycle','--pcm-memory-cycle']
   if pcm_data:argv.append('--pcm-data-cycle')
   if tdm_pins:argv.append('--tdm-pins-cycle')
   with patch.object(sys,'argv',argv),patch.object(preview.subprocess,'run',side_effect=command),patch.object(preview,'screen_off'),patch.object(preview,'verify_battery_preflight'),patch.object(preview,'battery_snapshot',return_value=(None,dict(temperature_c=30))),contextlib.redirect_stdout(io.StringIO()):
    try:preview.main();code=0
    except SystemExit as e:code=e.code
   r=json.loads((out/'exercise.json').read_text());self.assertEqual(state[0],3);self.assertTrue(r['android_screen_off'])
   self.assertTrue(all('flash' not in c and 'erase' not in c and not any('/data' in x for x in c) for c in commands))
   return code,r
 def test_exchange_requires_independent_reset_and_metadata(self):
  code,r=self.simulate();self.assertEqual(code,0,r);self.assertTrue(r['adsp_amdb_info']['module_available']);self.assertTrue(r['adsp_owned_boot_with_reset_passed'])
  for options in (dict(bad=True),dict(changed=False)):
   code,r=self.simulate(**options);self.assertEqual(code,1,r)
if __name__=='__main__':unittest.main()
