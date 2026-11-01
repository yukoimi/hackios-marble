"""Check native AOP channel closure; never equate it with audio/DSP acceptance."""
import argparse,json,re
from pathlib import Path

def inspect(text,peer_close=False,settled_close=False,load_state=False,mailbox_load_state=False,owned=False):
    def rows(prefix):
        return [dict(re.findall(r'(\w+)=([^\s]+)',s)) for s in text.splitlines() if s.startswith(prefix)]
    def one(prefix):
        r=rows(prefix);return r[0] if len(r)==1 else {}
    def exact(row,values):return all(row.get(k)==str(v) for k,v in values.items())
    load_state=load_state or mailbox_load_state
    tx_calls=2 if load_state else 0
    tx_count=2 if load_state and not mailbox_load_state else 0
    boundary=dict(raw_writes=0,TX_calls=tx_calls,NPA_calls='coordinated' if owned else 0,PAS_calls='coordinated' if owned else 0,DSP_start=0,storage_access=0)
    refused=one('marble-qmp-native: refused ')
    clean=exact(refused,dict(clean=1,attempted=0,**dict(boundary,TX_calls=0))) and not any(
        x in text for x in ('marble-qmp-native: open_before ','marble-qmp-native: open_return ',
                           'marble-qmp-native: close_return ','marble-qmp-native: complete ',
                           'marble-qmp-native: link_progress ','marble-qmp-native: remote_reconcile '))
    errors=[]
    if owned and not mailbox_load_state:errors.append('owned QMP stage requires mailbox adapter')
    if load_state:
        if not settled_close or peer_close:errors.append('load_state requires settled close')
        qualified=one('marble-qmp-load: qualified ')
        if not exact(qualified,dict(code_ranges=13 if mailbox_load_state else 10,bytes=64,zero_filled=1,on_then_off=1,DSP_start=0,PAS_calls=0,NPA_calls=0,storage_access=0)):errors.append('load_state code/message qualification missing')
        if mailbox_load_state and not exact(qualified,dict(adapter='native-mailbox',GLink_TX_calls=0)):errors.append('mailbox adapter qualification missing')
        if owned and not exact(qualified,dict(owned=1,deferred=1)):errors.append('owned post-load QMP gate missing')
        starts=rows('marble-qmp-load: tx_before ');returns=rows('marble-qmp-load: tx_return ');completions=rows('marble-qmp-load: tx_complete ')
        if len(starts)!=2 or len(returns)!=2 or len(completions)!=2:errors.append('exactly two load_state transmissions required')
        else:
            for i in range(2):
                if not exact(starts[i],dict(index=i,value='off' if i else 'on',bytes=64,flags=0,DSP_start=0)):errors.append('load_state message/ordering invalid')
                if not exact(returns[i],dict(index=i,status=0,result=0)):errors.append('load_state TX API failed')
                if not exact(completions[i],dict(index=i,PASS=1,callbacks=0 if mailbox_load_state else i+1,pending=0,invalid=0,mailbox_empty=1,physical_readback=0)):errors.append('load_state ACK ownership not proved')
                if mailbox_load_state and (not exact(starts[i],dict(adapter='native-mailbox',GLink_TX_calls=0)) or not exact(returns[i],dict(consumed=1)) or not exact(completions[i],dict(acknowledgements=i+1,native_queues_empty=1))):errors.append('native mailbox transfer/queue ACK not proved')
                try:
                    if not 0<int(completions[i]['elapsed_us'])<250000:raise ValueError('completion timeout')
                except (KeyError,ValueError):errors.append('load_state TX completion deadline missing')
        prefixes=['marble-qmp-load: qualified ','marble-qmp-native: connected ']
        for i in range(2):prefixes += [f'marble-qmp-load: tx_before index={i} ',f'marble-qmp-load: tx_return index={i} ',f'marble-qmp-load: tx_complete index={i} ']
        prefixes += ['marble-qmp-native: close_return ']
        positions=[text.find(p) for p in prefixes]
        if min(positions)<0 or positions!=sorted(positions):errors.append('load_state lifecycle order invalid')
        if owned:
            if not exact(one('marble-qmp-load: held_before '),dict(LPI_clients=2,DSP_start=0,watchdog_retained=1,ROM_loaded=1)) or not exact(one('marble-qmp-load: held_return '),dict(PASS=1,outcome=1,DSP_start=0,ROM_restored=0,watchdog_retained=1,noexec_cancel=1)) or not exact(one('marble-pil-held: post_load_gate '),dict(PASS=1,ROM_loaded=1,PAS_initialized=1,DSP_start=0)):errors.append('QMP post-load cancellation proof missing')
            stages=['marble-adsp-owned: phase=ROM-noexec ','marble-pil-held: loaded ',
                'marble-qmp-load: qualified ','marble-qmp-load: tx_complete index=0 ','marble-qmp-load: held_before ',
                'marble-qmp-load: held_return ','marble-qmp-load: tx_before index=1 ',
                'marble-qmp-native: complete ','marble-qmp-load: CPU_context ','marble-pil-held: post_load_gate ','marble-pil-held: shutdown ','marble-pil-held: restored ']
            at=[text.find(s) for s in stages]
            if min(at)<0 or at!=sorted(at):errors.append('pre-auth cancellation requires QMP off/close before PAS metadata shutdown')
            cpu=one('marble-qmp-load: CPU_context ')
            try:
                if not exact(cpu,dict(PASS=1,direct_mask_writes=0)) or int(cpu['DAIF_before'],16)!=int(cpu['DAIF_after'],16):raise ValueError('DAIF changed')
            except (KeyError,ValueError):errors.append('QMP CPU interrupt context was not preserved')
    elif rows('marble-qmp-load: tx_before '):errors.append('business TX requires explicit load_state contract')
    if peer_close and settled_close:errors.append('ambiguous closure contract')
    pre=one('marble-qmp-native: preflight ');opened=one('marble-qmp-native: open_return ')
    connection=one('marble-qmp-native: connected ');closed=one('marble-qmp-native: close_return ')
    final=one('marble-qmp-native: complete ')
    before=one('marble-qmp-native: context phase=before ');after=one('marble-qmp-native: context phase=after ')
    if clean:errors.append('qualified refusal before native open; lifecycle not tested')
    if refused and not clean:errors.append('refusal contradicts attempted lifecycle')
    if not exact(pre,dict(qualified=1,exact_abi=1,transport='qmp',remote='rpm',channel='aop_apps',open_attempted=0,TX_calls=0,DSP_start=0)):errors.append('native preflight missing')
    if not exact(one('marble-qmp-native: open_before '),dict(watchdog_seconds=30,limit_us=15000000 if owned else 2000000,TX_calls=0,DSP_start=0)):errors.append('watchdog/budget missing')
    if not exact(opened,dict(status=0,result=0)):errors.append('open failed')
    if not exact(connection,dict(PASS=1,events=1,invalid=0,RX=0,TX=0)):errors.append('connected callback/descriptor not verified')
    if not exact(closed,dict(status=0,result=0)):errors.append('close failed')
    if not exact(final,dict(PASS=1,attempted=1,active=0,dirty=0,connected=1,local_down=1,remote_down=int(peer_close or settled_close),invalid=0,RX=0,TX=tx_count,watchdog_clear='ffffffffffffffff' if owned else 0,
            peer_close_contract=int(peer_close),settled_close_contract=int(settled_close),original_restoration=int(not peer_close),**boundary)):errors.append('native lifecycle cleanup not proved')
    if owned and not exact(final,dict(watchdog_retained=1,owned=1)):errors.append('outer watchdog was not retained')
    close_state=one('marble-qmp-native: close_state ')
    if not exact(close_state,dict(descriptor=1,original_restoration=int(not peer_close),legacy_contract_pass=int(not (peer_close or settled_close)),peer_closed=int(peer_close),settled_closed=int(settled_close),
            local='ffff0000,ffff0000',remote='ffff0000,ffff0000' if peer_close else 'ffff,ffff',channels=int(not peer_close),native_handles_released=int(peer_close),local_client_released=int(peer_close or settled_close))):errors.append('descriptor/channel closure contract missing')
    try:
        calls=int(final['native_event_calls'])
        if calls<0 or calls>2000:raise ValueError('event call budget')
        if settled_close:
            settle=one('marble-qmp-native: settle ')
            if not calls or not exact(settle,dict(PASS=1,stale_handle_calls=0,raw_writes=0,TX_calls=tx_calls,DSP_start=0)) or not 1<=int(settle['rounds'])<=32:raise ValueError('transport settling')
            if not text.find('marble-qmp-native: close_return ')<text.find('marble-qmp-native: settle ')<text.find('marble-qmp-native: context phase=after '):raise ValueError('settle order')
        if calls:
            listeners=one('marble-qmp-native: listeners ');link=one('marble-qmp-native: link_progress ')
            pending=one('marble-qmp-native: context phase=link-pending ')
            if not exact(listeners,dict(PASS=1,unknown=0,terminated=1,callback_calls=0)):raise ValueError('listener qualification')
            if not exact(link,dict(PASS=1,native_event_calls=1,table_writes=0,raw_writes=0,TX_calls=0,DSP_start=0)):raise ValueError('link promotion')
            if not exact(pending,dict(low=1,link=1,channels=0)):raise ValueError('pending link owner')
            if not 0<=int(listeners['matched'])<=int(listeners['total'])<=64:raise ValueError('listeners count')
            if not text.find('marble-qmp-native: listeners ')<text.find('marble-qmp-native: link_progress ')<text.find('marble-qmp-native: open_before '):raise ValueError('promotion order')
            if pre.get('firmware_changed')!='1':raise ValueError('firmware mutation not declared')
            if before.get('channels')=='0':
                if not exact(one('marble-qmp-native: remote_reconcile '),dict(PASS=1,calls=1,observed_remote_up=1,remote_id='cd',TX_calls=0,DSP_start=0)):raise ValueError('remote channel reconciliation missing')
                if not exact(one('marble-qmp-native: context phase=remote-reconciled '),dict(low=2,link=2,channels=1,local=0,remote=2,refs=1,terminated=1)):raise ValueError('remote channel ownership')
        elif pre.get('firmware_changed')!='0':raise ValueError('unexpected firmware mutation')
        if not int(opened['handle'],16) or not 0<int(final['elapsed_us'])<(15000000 if owned else 2000000):raise ValueError('handle/time')
        for r in (before,after):
            if not exact(r,dict(low=2,link=2,terminated=1,poll_present=0)):raise ValueError('native context')
            if not int(r['transport'],16) or not int(r['core'],16):raise ValueError('context pointer')
        if before['transport']!=after['transport'] or before['core']!=after['core']:raise ValueError('provider changed')
        if not (exact(before,dict(channels=0,local=0,remote=0,refs=0)) or exact(before,dict(channels=1,local=0,remote=2,refs=1))):raise ValueError('initial owner')
        expected=dict(channels=0,local=0,remote=0,refs=0) if peer_close else dict(channels=1,local=0 if settled_close else 1,remote=2,refs=1)
        if not exact(after,expected):raise ValueError('closed channel references')
        if settled_close and after.get('client_empty')!='1':raise ValueError('client callbacks retained')
    except (KeyError,ValueError) as exc:errors.append('invalid native values: '+str(exc))
    prefixes=['marble-qmp-native: context phase=before ','marble-qmp-native: preflight ',
        'marble-qmp-native: open_before ','marble-qmp-native: open_return ','marble-qmp-native: connected ',
        'marble-qmp-native: close_return ','marble-qmp-native: context phase=after ','marble-qmp-native: close_state ','marble-qmp-native: complete ']
    positions=[text.find(p) for p in prefixes]
    if min(positions)<0 or positions!=sorted(positions):errors.append('lifecycle order missing or invalid')
    return dict(passed=not errors,clean_refusal=clean,cleanup_verified=not errors,errors=errors,
        load_state_transport_ack_verified=load_state and not errors,physical_load_state_readback=False,
        mailbox_load_state_contract=mailbox_load_state,owned_ROM_contract=owned,
        before=before,after=after,dsp_started=False,audio_acceptance=False,xo_lease_verified=False,
        peer_close_contract=peer_close,settled_close_contract=settled_close,original_restoration=False if peer_close else not errors,
        scope=('native local client released; descriptor restored after bounded settling; only a remote-owned node with no client callbacks remains; cold reset required' if settled_close else
               'native channel connected and both endpoints closed; no channel references; original remote-UP descriptor was NOT restored; cold reset required' if peer_close else
               'native channel open/close and descriptor restore; remote-owned closed reference remains; not identical heap restoration or DSP/audio proof'))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('log',type=Path);p.add_argument('--peer-close',action='store_true');p.add_argument('--settled-close',action='store_true');p.add_argument('--mailbox-load-state',action='store_true');a=p.parse_args()
    r=inspect(a.log.read_text(errors='replace'),a.peer_close,a.settled_close or a.mailbox_load_state,mailbox_load_state=a.mailbox_load_state);print(json.dumps(r,indent=2));raise SystemExit(not r['passed'])
