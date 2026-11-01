"""Composed parser fixtures only; never used as physical acceptance evidence."""
import importlib.util,unittest
from pathlib import Path
HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('gate_report',HERE.parent/'marble-audio-owner-report.py')
audit=importlib.util.module_from_spec(spec);spec.loader.exec_module(audit)
def fixture():
 lines=(HERE/'fixtures/marble-audio-owner-usb-v234.log').read_text().splitlines()
 configured=next(x for x in lines if 'aud-owner-fw: phase=configured-real-START-proof ' in x)
 pages=[x for x in lines if 'marble-audio-owner-PCM: period=' in x]
 lines=[x for x in lines if x!=configured and x not in pages and not any('marble-aw-'+k+': complete ' in x for k in ('analog','digital'))]
 pos=next(i for i,x in enumerate(lines) if 'aw: window end ' in x)
 new=[f'aud-board-held: phase=begin device={d} PASS=1 status=311 HMUTE=1 attenuation_db=90 audible=0' for d in ('34','35')]
 new += [configured,'aud-board-held: phase=DATA-granted devices=2 HMUTE=1 attenuation_db=90 STOP_allowed=0 audible=0']+pages
 new += ['marble-audio-board-gate: phase=drained submitted=8 completed=8 pending=0 STOP_sent=0 ABI=MAUDIO-BOARD-QUIET-v1 audible=0',
         'aud-board-held: phase=drained PASS=1 submitted=8 completed=8 STOP_allowed=0 audible=0']
 new += [f'aud-board-held: phase=finish device={d} PASS=1 powered_down=1 reset_restored=1 bus_clean=1 aux_restored=1 held_us=52000 HMUTE=1 audible=0' for d in ('35','34')]
 lines[pos:pos]=new
 pos=next(i for i,x in enumerate(lines) if 'aw: window end ' in x)+1
 lines[pos:pos]=['aud-board-held: phase=quiet-ACK flags=7 submitted=8 completed=8 STOP_allowed=1 config_active=0 clock_active=0 audible=0',
                 'marble-audio-board-gate: phase=quiet-ACK submitted=8 completed=8 flags=7 STOP_allowed=1 audible=0']
 return '\n'.join(lines)
class BoardGateReport(unittest.TestCase):
 args=dict(board=True,periods=8,pcm=True,usb_poll_lease=True,board_held=True)
 def test_two_real_hardware_logs_and_rejection_of_missing_quiet(self):
  for name in ('marble-audio-board-held-v237.log','marble-audio-board-held-v237b.log'):
   text=(HERE/'fixtures'/name).read_bytes().split(b'\0PMOC')[0].decode(errors='replace')
   result=audit.inspect(text,**self.args)
   self.assertTrue(result['passed'],result['errors']);self.assertFalse(result['audio_acceptance'])
   self.assertFalse(audit.inspect(text.replace('phase=quiet-ACK flags=7','phase=quiet-ACK flags=3'),**self.args)['passed'])
 def test_composed_complete_and_explicit_optin(self):
  text=fixture();r=audit.inspect(text,**self.args)
  self.assertTrue(r['passed'],r['errors']);self.assertTrue(r['board_held']);self.assertFalse(r['audio_acceptance'])
  self.assertFalse(audit.inspect(text,board=True,periods=8,pcm=True,usb_poll_lease=True)['passed'])
 def test_legacy_hardware_log_cannot_pass_new_profile(self):
  text=(HERE/'fixtures/marble-audio-owner-usb-v234.log').read_text()
  self.assertFalse(audit.inspect(text,**self.args)['passed'])
 def test_incomplete_power_reset_bus_deadline_and_gate(self):
  text=fixture()
  for old,new in [('flags=7','flags=3'),('pending=0 STOP_sent=0','pending=1 STOP_sent=0'),('powered_down=1','powered_down=0'),
                  ('reset_restored=1 bus_clean=1','reset_restored=0 bus_clean=1'),('aux_restored=1 held_us=52000','aux_restored=0 held_us=52000'),
                  ('held_us=52000','held_us=600000'),('status=311','status=11'),('completed=8 pending=0','completed=7 pending=0')]:
   self.assertFalse(audit.inspect(text.replace(old,new),**self.args)['passed'],(old,new))
 def test_finish_and_ACK_order(self):
  lines=fixture().splitlines()
  for a,b in [('aud-board-held: phase=finish device=35 ','aud-board-held: phase=finish device=34 '),
              ('aud-board-held: phase=drained ','aud-board-held: phase=DATA-granted '),
              ('marble-audio-owner-PCM: period=7 ','marble-audio-board-gate: phase=drained ')]:
   bad=lines.copy();i=next(i for i,x in enumerate(bad) if a in x);j=next(i for i,x in enumerate(bad) if b in x)
   bad[i],bad[j]=bad[j],bad[i]
   self.assertFalse(audit.inspect('\n'.join(bad),**self.args)['passed'],(a,b))
if __name__=='__main__':unittest.main()
