import importlib.util,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parents[1]))
spec=importlib.util.spec_from_file_location('blocking_audio_runner',Path(__file__).parents[1]/'marble-run.py')
runner=importlib.util.module_from_spec(spec);spec.loader.exec_module(runner)
class BlockingLog(unittest.TestCase):
 def test_actual_early_failure_is_parsed_for_log_fetch(self):
  reply='(bootloader) audio-run-result=no-such-core 1 0\r\nOKAY [  3.902s]\r\n'
  self.assertEqual(runner.blocking_owner_completion(reply),'no-such-core 1 0')
  self.assertEqual(runner.blocking_owner_completion(reply.replace('no-such-core 1 0','returned 0 0')),'returned 0 0')
  for bad in ('',reply+reply,reply.replace('no-such-core 1 0','running 0 0')):
   with self.assertRaises(RuntimeError):runner.blocking_owner_completion(bad)
if __name__=='__main__':unittest.main()
