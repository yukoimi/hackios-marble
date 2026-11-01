"""Bind ROM speaker configuration to static Android hardware observations.

This writes a host-side contract, never hardware settings or Android userdata.
"""
from marble_local import ANDROID_SERIAL as MARBLE_ANDROID_SERIAL
import argparse,hashlib,importlib.util,json,struct,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def sha(data):return hashlib.sha256(data).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--material',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--id-evidence',type=Path,action='append',default=[]);a=p.parse_args()
 if len(a.id_evidence) not in (0,2):p.error('physical identity requires two complete independent ID/reset evidence directories')
 rom=ROOT/'out/peripheral-material-v2-plan/material/vendor/etc/audio/sku_ukee';pairs={
  'mixer_paths_ukee_mtp.xml':'android-mixer-ukee-mtp.xml',
  'mixer_paths_overlay_static.xml':'android-mixer-overlay-static.xml',
  'mixer_paths_overlay_dynamic.xml':'android-mixer-overlay-dynamic.xml',
  'resourcemanager_ukee_mtp.xml':'android-resource-ukee-mtp.xml'}
 inputs={}
 for name,observed in pairs.items():
  data=(rom/name).read_bytes();other=(a.material/observed).read_bytes()
  if data!=other:raise ValueError('ROM/Android configuration mismatch: '+name)
  inputs[name]=dict(rom=str((rom/name).resolve()),observed=str((a.material/observed).resolve()),sha256=sha(data))
 sound=a.material/'android-dt-sound';i2c=a.material/'android-dt-i2c'
 def string(path):return path.read_bytes().rstrip(b'\0').decode('ascii')
 def cells(path):
  data=path.read_bytes()
  if not data or len(data)%4:raise ValueError('bad DT cells '+str(path))
  return list(struct.unpack('>'+str(len(data)//4)+'I',data))
 card=string(sound/'qcom,model')
 if card!='ukee-mtp-snd-card' or cells(i2c/'reg')!=[0x880000,0x4000] or string(i2c/'status') not in ('ok','okay'):raise ValueError('board/controller identity')
 amps=[]
 for index,address in enumerate((0x34,0x35)):
  node=i2c/f'aw882xx_smartpa@{address:x}'
  if string(node/'compatible')!='awinic,aw882xx_smartpa' or cells(node/'reg')!=[address] or cells(node/'sound-channel')!=[index] or cells(node/'sync-flag')!=[1]:raise ValueError('amplifier identity/channel')
  amps.append(dict(address=address,sound_channel=index,reset_gpio=cells(node/'reset-gpio'),irq_gpio=cells(node/'irq-gpio'),
   spksw_gpio=cells(node/'spksw-gpio') if (node/'spksw-gpio').is_file() else None,calibration_mode=string(node/'aw-cali-mode'),physical_chip_id_verified=False))
 tree=ET.parse(rom/'resourcemanager_ukee_mtp.xml');speakers=[n for n in tree.findall('.//out-device') if n.findtext('id')=='PAL_DEVICE_OUT_SPEAKER']
 if len(speakers)!=1:raise ValueError('ambiguous speaker route')
 node=speakers[0];route={k:node.findtext(k) for k in ('back_end_name','channels','samplerate','bit_width','snd_device_name','speaker_protection_enabled')}
 if route!=dict(back_end_name='TDM-LPAIF-RX-TERTIARY-VIRT-0',channels='2',samplerate='96000',bit_width='32',snd_device_name='speaker',speaker_protection_enabled='1'):raise ValueError('unqualified speaker format/route')
 static=ET.parse(rom/'mixer_paths_overlay_static.xml').getroot();selected=[x for x in static.findall('path') if x.get('name')=='speaker']
 if len(selected)!=1:raise ValueError('speaker overlay missing')
 controls=[dict(x.attrib) for x in selected[0]]
 if controls!=[dict(name='aw_dev_0_switch',value='Enable'),dict(name='aw_dev_1_switch',value='Enable')]:raise ValueError('unqualified speaker overlay')
 # Static overlay replaces the common WSA speaker path; it is not additive.
 acf=(a.material/'rom-aw882xx-acf.bin').read_bytes()
 if acf!=(a.material/'android-aw882xx-acf.bin').read_bytes():raise ValueError('ROM/Android ACF mismatch')
 spec=importlib.util.spec_from_file_location('board_acf',ROOT/'tools/marble-aw-acf.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);firmware=m.inspect(acf)
 if len(firmware['entries'])!=8 or {x['index'] for x in firmware['entries']}!={0,1}:raise ValueError('ACF device/profile coverage')
 result=dict(card=card,controller=dict(linux_bus=2,mmio_base=0x880000,mmio_bytes=0x4000,EFI_instance=None),
  amplifiers=amps,route=route,static_overlay_replaces_common_path=True,speaker_controls_reference_only=controls,
  firmware=dict(sha256=firmware['sha256'],candidate_chip_id=0x2032,candidate_name='AW88263S',sections=8,hardware_identity_verified=False),inputs=inputs,
  verified='static DT, ROM/Android file equality, bounded firmware integrity and configured speaker route',
  remaining=['EFI controller mapping/ownership and bounded release','physical amplifier chip identity','TDM pin/clock and DSP graph/port configuration','speaker protection/calibration lifecycle','XNU/iOS playback integration and observed audio output'],hardware_writes=0,audio_acceptance=False)
 if a.id_evidence:
  if len({x.resolve() for x in a.id_evidence})!=2:raise ValueError('duplicate physical ID evidence')
  spec=importlib.util.spec_from_file_location('board_id_report',ROOT/'tools/marble-i2c-inventory-report.py');audit=importlib.util.module_from_spec(spec);spec.loader.exec_module(audit)
  runs=[]
  for directory in a.id_evidence:
   raw=(directory/'i2c-inventory.log').read_bytes();exercise_raw=(directory/'exercise.json').read_bytes();exercise=json.loads(exercise_raw)
   checked=audit.inspect(raw.decode(),clock=True,cycle=True,registers=True,route=True,setup=True,amp=True)
   if not checked['passed'] or not checked['chip_id_verified'] or not exercise.get('i2c_inventory_with_reset_passed') or not exercise.get('android_screen_off') or exercise.get('error') or exercise.get('i2c_command_error'):raise ValueError('unqualified physical ID/reset evidence: '+str(directory))
   before=exercise['android_boot_id_before_sha256'];after=exercise['android_boot_id_after_sha256']
   if len(before)!=64 or len(after)!=64 or before==after or any(c not in '0123456789abcdef' for c in before+after):raise ValueError('missing independent cold reset')
   screen=json.loads((directory/'android-screen-off.json').read_text())
   if screen.get('confirmed') is not True or screen.get('screen_state')!='OFF' or screen.get('actual_state')!='OFF':raise ValueError('screen-off proof missing')
   temperatures=[]
   for phase in ('before','after'):
    battery=json.loads((directory/('battery-'+phase+'.json')).read_text());value=battery.get('temperature_decic')
    if battery.get('confirmed') is not True or battery.get('serial')!=MARBLE_ANDROID_SERIAL or type(value) is not int or not 0<=value<380 or exercise.get('battery_'+phase+'_c')!=value/10:raise ValueError('battery endpoint proof mismatch')
    temperatures.append(value/10)
   runs.append(dict(directory=str(directory.resolve()),log_sha256=sha(raw),exercise_sha256=sha(exercise_raw),boot_before=before,boot_after=after,battery_before_c=temperatures[0],battery_after_c=temperatures[1],logical_quiesce=True,raw_IRQ_bitwise_restoration=False,mandatory_cold_reset_verified=True))
  if runs[0]['boot_after']==runs[1]['boot_after'] or runs[0]['boot_before']==runs[1]['boot_before']:raise ValueError('same hardware session supplied twice')
  for amplifier in result['amplifiers']:amplifier['physical_chip_id_verified']=True;amplifier['physical_chip_id']=0x2032
  result['firmware']['hardware_identity_verified']=True
  result['hardware_verification']=dict(runs=runs,scope='two real AW2032 IDs; bounded configuration/reset/clock release plus mandatory cold reset; no amplifier register data writes or sound',auditor_sha256=sha((ROOT/'tools/marble-i2c-inventory-report.py').read_bytes()))
  result['host_tool_hardware_writes']=result.pop('hardware_writes');result['observed_hardware_write_scope']='controller configuration, fixed ID pointer commands, pinmux and amplifier reset only'
  result['remaining']=result['remaining'][2:]
 a.out.write_text(json.dumps(result,indent=2)+'\n');print('Bound ROM speaker route and two amplifier channels; physical IDs '+('verified twice' if a.id_evidence else 'unverified')+'; audio output remains unverified')
if __name__=='__main__':main()
