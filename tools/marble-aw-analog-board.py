"""Bind the muted AMPPD test to the already pinned AW2032 OEM source."""
import argparse, hashlib, importlib.util, json, re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def inspect():
    s=importlib.util.spec_from_file_location('analog_digital_board',ROOT/'tools/marble-aw-digital-board.py')
    digital=importlib.util.module_from_spec(s);s.loader.exec_module(digital)
    baseline=digital.inspect()
    base=ROOT/'out/audio-route-v103-plan/audio-kernel/asoc/codecs/aw882xx'
    h=(base/'inc/aw882xx_pid_2032_reg.h').read_text()
    dev=(base/'src/aw882xx_device.c').read_text()
    for name,bit in dict(AMPPD=1,HMUTE=4,BSTS=9,SWS=8).items():
        if not re.search(r'#define\s+AW_PID_2032_'+name+r'_START_BIT\s+\('+str(bit)+r'\)',h):
            raise ValueError('OEM analog bit '+name)
    start=dev.index('int aw882xx_device_start(')
    if not dev.index('aw_dev_syspll_check(aw_dev)',start)<dev.index('/*amppd on*/',start)<dev.index('aw_dev_sysst_check(aw_dev)',start):
        raise ValueError('OEM PLL/AMPPD/status order')
    headers={name:hashlib.sha256((ROOT/'xnuloader'/name).read_bytes()).hexdigest()
             for name in ('marble_aw2032_analog.h','marble_aw2032_analog_fifo.h')}
    return dict(passed=True,OEM_sources=baseline['OEM_sources'],headers=headers,
                devices=[dict(address=d['address'],sysctrl_digital=d['sysctrl_digital'],
                              sysctrl_analog=d['sysctrl_digital']&~2,sysctrl2=d['sysctrl2'])
                         for d in baseline['devices']],polls_maximum=10,poll_delay_us=1000,
                HMUTE_retained=True,attenuation_db=90,speaker_switch_writes=0,
                audio_acceptance=False,scope='offline source binding only; no audible playback')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    a.out.write_text(json.dumps(inspect(),indent=2)+'\n')
    print('PASS offline pinned AW2032 muted analog source/ROM binding')
