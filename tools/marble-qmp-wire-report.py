"""Require AP-only QMP transfers, unchanged firmware state and reset boundary."""
import argparse,json,re
from pathlib import Path
def inspect(text,messages=True,tpl=False,smccc=False,ipcc=False):
    def rows(prefix):return [dict(re.findall(r'(\w+)=([^\s]+)',s)) for s in text.splitlines() if s.startswith('marble-qmp-wire: '+prefix+' ')]
    def one(prefix):
        found=rows(prefix);return found[0] if len(found)==1 else {}
    def exact(row,**values):return all(row.get(k)==str(v) for k,v in values.items())
    errors=[];q=one('qualified');opened=one('opened');final=one('complete');context=one('context')
    clean=exact(one('refused'),clean=1,AP_writes=0,GLink_calls=0,PAS_calls=0,DSP_start=0) and not (opened or final or rows('send_before'))
    if not exact(q,native_code_ranges=13,features=0,native_low=1,native_core=1,native_channels=0,GLink_calls=0):errors.append('fresh borrowed link/driver qualification missing')
    if not exact(opened,PASS=1,native_context_unchanged=1):errors.append('channel open changed firmware state')
    starts=rows('send_before');acks=rows('send_complete')
    if not messages:
        if starts or acks or not exact(one('mode'),load_state_messages=0,owned=1,PAS_inside_wire=0):errors.append('channel isolation must send no load_state messages')
    elif len(starts)!=2 or len(acks)!=2:errors.append('two distinct messages/ACKs required')
    else:
        for i in range(2):
            if not exact(starts[i],index=i,value='off' if i else 'on',bytes=64):errors.append('wrong message order/geometry')
            if not exact(acks[i],index=i,PASS=1,published=64,ACKs=i+1,physical_readback=0,GLink_TX_calls=0):errors.append('mailbox consumption ACK missing')
    close=rows('close_ack')
    if len(close)!=2 or not exact(close[0],index=0,observed_remote='ffff0000',AP_ack='ffff0000',offset=18,remote_state_writes=0) or not exact(close[1],index=1,observed_remote='ffff',AP_ack='ffff',offset=18,remote_state_writes=0):errors.append('AP acknowledgement of observed AOP DOWN/UP missing')
    if not exact(final,PASS=1,attempted=1,dirty=0,descriptor_restored=1,native_context_unchanged=1,Clock_unchanged=1,DAIF_unchanged=1,AP_writes=38 if messages else 4,kicks=6 if messages else 4,ACKs=2 if messages else 0,thermal_ok=1,watchdog_seconds=10,watchdog_retained=1,cold_reset_required=1,GLink_calls=0,native_event_calls=0,remote_owned_writes=0,table_writes=0,PAS_calls=0,NPA_calls=0,DSP_start=0,storage_access=0,AP_payload_history_retained=1 if messages else 0,physical_load_state_readback=0):errors.append('wire/firmware preservation or reset contract missing')
    try:
        local=int(q['local_offset'],16);remote=int(q['remote_offset'],16);ls=int(q['local_bytes']);rs=int(q['remote_bytes'])
        if min(ls,rs)<68 or local<60 or remote<60 or (local|remote|ls|rs)&3 or local+ls>1024 or remote+rs>1024 or not (local+ls<=remote or remote+rs<=local):raise ValueError('overlapping/out-of-range mailbox')
        for start,ack in zip(starts,acks):
            if int(start['AP_mailbox'],16)!=0xc300000+local or not 0<int(ack['elapsed_us'])<250000:raise ValueError('mailbox ownership or ACK deadline')
        if not 0<int(final['elapsed_us'])<2500000:raise ValueError('cycle budget')
        if not exact(context,native_transport_words=38,native_core_words=19) or not int(q['Clock_flags'],16)==int(context['Clock_before'],16)==int(context['Clock_after'],16) or not int(q['DAIF'],16)==int(context['DAIF_before'],16)==int(context['DAIF_after'],16):raise ValueError('firmware context values changed')
    except (KeyError,ValueError) as exc:errors.append('invalid wire metadata: '+str(exc))
    stages=['qualified','opened']+(['send_before index=0','send_complete index=0','send_before index=1','send_complete index=1'] if messages else [])+['close_ack index=0','close_ack index=1','context','complete']
    at=[text.find('marble-qmp-wire: '+s+' ') for s in stages]
    if min(at)<0 or at!=sorted(at):errors.append('wire lifetime order missing')
    if tpl:
        def tplrows(prefix):return [dict(re.findall(r'(\w+)=([^\s]+)',s)) for s in text.splitlines() if s.startswith('marble-qmp-tpl: '+prefix+' ')]
        trips=tplrows('roundtrip');finish=tplrows('complete')
        if len(trips)!=2 or len(finish)!=1 or not exact(finish[0],PASS=1,baseline=1,wire=1,post=1,firmware_preserved=1,raises=2,restores=2,SMC=0,PAS=0,NPA=0,DSP_start=0,watchdog_retained=1,cold_reset_required=1):errors.append('TPL event boundary completion missing')
        try:
            if len(trips)!=2:raise ValueError('two roundtrips required')
            for i,row in enumerate(trips):
                if not exact(row,index=i,PASS=1,requested=8,raises=1,restores=1,direct_DAIF_writes=0,SMC=0,PAS=0) or not 0<=int(row['previous'])<=8 or not 0<int(row['elapsed_us'])<250000 or int(row['DAIF_before'],16)!=int(row['DAIF_after'],16):raise ValueError('unbalanced or changed TPL/DAIF state')
            if trips[0]['previous']!=trips[1]['previous']:raise ValueError('original TPL changed across wire')
        except (KeyError,ValueError) as exc:errors.append('invalid TPL evidence: '+str(exc))
        stages=['marble-qmp-tpl: roundtrip index=0 ','marble-qmp-wire: qualified ','marble-qmp-wire: complete ','marble-qmp-tpl: roundtrip index=1 ','marble-qmp-tpl: complete ']
        positions=[text.find(s) for s in stages]
        if min(positions)<0 or positions!=sorted(positions):errors.append('TPL checks must bracket the wire lifetime')
    if smccc:
        def smcrows(prefix):return [dict(re.findall(r'(\w+)=([^\s]+)',s)) for s in text.splitlines() if s.startswith('marble-smccc: '+prefix+' ')]
        queries=smcrows('query');finish=smcrows('complete')
        if len(queries)!=4 or len(finish)!=1 or not exact(finish[0],PASS=1,baseline=1,wire=1,post=1,firmware_preserved=1,availability_queries=4,WAITQ_replays=0,PAS_calls=0,NPA_calls=0,DSP_start=0,watchdog_retained=1,cold_reset_required=1):errors.append('four bounded availability queries required')
        try:
            if len(queries)!=4:raise ValueError('query count')
            for i,row in enumerate(queries):
                if not exact(row,index=i,PASS=1,fn='42000601' if i&1 else 'c2000601',arginfo=1,target=2000206,initial_a6=0,initial_a7=0,outcome=0,raw_a0=0,raw_a1=1,WAITQ_replays=0,PAS_calls=0,DSP_start=0) or not 1<=int(row['calls'])<=64 or not 0<int(row['elapsed_us'])<250000 or not 0<=int(row['previous_TPL'])<=8 or int(row['DAIF_before'],16)!=int(row['DAIF_after'],16):raise ValueError('register initialization, call geometry or completion')
            if len({row['previous_TPL'] for row in queries})!=1:raise ValueError('original TPL changed across wire')
        except (KeyError,ValueError) as exc:errors.append('invalid SMCCC query evidence: '+str(exc))
        stages=['marble-smccc: query index=0 ','marble-smccc: query index=1 ','marble-qmp-wire: qualified ','marble-qmp-wire: complete ','marble-smccc: query index=2 ','marble-smccc: query index=3 ','marble-smccc: complete ']
        positions=[text.find(s) for s in stages]
        if min(positions)<0 or positions!=sorted(positions):errors.append('availability queries must bracket the wire lifetime')
    if ipcc:
        def irqrows(prefix):return [dict(re.findall(r'(\w+)=([^\s]+)',s)) for s in text.splitlines() if s.startswith('marble-qmp-ipcc: '+prefix+' ')]
        qualified=irqrows('qualified');finish=irqrows('complete')
        if len(qualified)!=1 or not exact(qualified[0],APPS_client=8,source_client=0,signal=0,base='ed18000',receive_offset=10,clear_offset='1c',code_ranges=2,before='ffffffff',IRQ_enable_writes=0,GIC_writes=0,native_callbacks=0):errors.append('new QMP receive notification ownership not qualified')
        if len(finish)!=1 or not exact(finish[0],PASS=1,before='ffffffff',observed=0,after='ffffffff',owned=1,clear_writes=1,firmware_preserved=1,IRQ_enable_writes=0,GIC_writes=0,native_callbacks=0,foreign_clears=0,PAS_calls=0,DSP_start=0,watchdog_retained=1,cold_reset_required=1,original_irq_state_restored=1):errors.append('exact owned notification acknowledgement missing')
        stages=['marble-qmp-ipcc: qualified ','marble-qmp-wire: qualified ','marble-qmp-wire: complete ','marble-qmp-ipcc: complete ']
        positions=[text.find(s) for s in stages]
        if min(positions)<0 or positions!=sorted(positions):errors.append('IPCC snapshot/clear must enclose the completed wire cycle')
    return dict(passed=not errors,errors=errors,clean_refusal=clean,reset_safe=not errors,
        AP_wire_transfer_verified=not errors and messages,AP_channel_handshake_verified=not errors,firmware_context_preserved=not errors,
        TPL_roundtrip_verified=tpl and not errors,
        SMCCC_availability_verified=smccc and not errors,
        owned_IPCC_acknowledgement_verified=ipcc and not errors,
        physical_load_state_readback=False,dsp_started=False,audio_acceptance=False,
        scope='borrowed UP link, AP-owned '+('channel/mailbox' if messages else 'channel ACK handshake without load_state messages')+' and fixed native doorbell only; original descriptor/firmware heap/Clock/DAIF preserved; '+('AP payload history retained; ' if messages else 'no mailbox payload writes; ')+'cold reset required')
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('log',type=Path);a=p.parse_args();r=inspect(a.log.read_text(errors='replace'));print(json.dumps(r,indent=2));raise SystemExit(not r['passed'])
