#ifdef XNU_MARBLE_AUDIO_QUP_HOLD
/* Additional balanced references, acquired only after START. The inner
 * controller lease still restores exactly; these references live until PAS.
 * No frequency, voltage, IRQ or controller requests occur in this lease. */
static MarbleI2cClockLease marble_audio_qup_hold;
static uint64_t marble_audio_qup_table[8];
static const char *marble_audio_qup_phase;
static int marble_audio_qup_state(void *ctx,unsigned i,unsigned *enabled,unsigned *on,uint32_t *hz)
{
    if(i>=5)return 1;
    marble_fw_clock *c=ctx;uint8_t e=255,n=255;uint32_t f=~0u;
    efi_status a=c->is_enabled(c,marble_i2c_clock_ids[i],&e),b=c->is_on(c,marble_i2c_clock_ids[i],&n),d=c->get_frequency(c,marble_i2c_clock_ids[i],&f);
    unsigned unavailable=d==(EFI_ERROR_BIT|19) && !f;
    print("ah: state phase=%s index=%d id=%x enabled=%d on=%d hz=%d status=%x,%x,%x\n",marble_audio_qup_phase,
        (uint64_t)i,marble_i2c_clock_ids[i],(uint64_t)e,(uint64_t)n,(uint64_t)f,a,b,d);
    *enabled=e;*on=n;*hz=unavailable?~0u:f;
    return EFI_ERROR(a) || EFI_ERROR(b) || (EFI_ERROR(d) && !unavailable);
}
static int marble_audio_qup_enable(void *ctx,unsigned i)
{
    if(i>=5)return 1;marble_fw_clock *c=ctx;efi_status s=c->enable(c,marble_i2c_clock_ids[i]);
    print("ah: enable index=%d id=%x status=%x\n",(uint64_t)i,marble_i2c_clock_ids[i],s);return EFI_ERROR(s);
}
static int marble_audio_qup_disable(void *ctx,unsigned i)
{
    if(i>=5)return 1;marble_fw_clock *c=ctx;efi_status s=c->disable(c,marble_i2c_clock_ids[i]);
    print("ah: disable index=%d id=%x status=%x\n",(uint64_t)i,marble_i2c_clock_ids[i],s);return EFI_ERROR(s);
}
static int marble_audio_qup_begin(void)
{
    if(!marble_i2c_clock_provider || marble_pil_auth_attempted!=1 || marble_tdm_run.sent!=7 ||
       marble_tdm_run.tx!=7 || marble_tdm_run.reply!=7 || !marble_tdm_pins.active || marble_audio_qup_hold.prepared)return 0;
    uint64_t table=(uint64_t)(uintptr_t)marble_i2c_clock_provider,base=table-0x1b0f8;
    if(!marble_rpmh_words(marble_npa_ram_word,table,marble_audio_qup_table,8) ||
       marble_audio_qup_table[4]!=base+0x15b8 || marble_audio_qup_table[5]!=base+0x15fc || marble_audio_qup_table[7]!=base+0x1688 ||
       !marble_code_digest(marble_npa_ram_word,base+0x1528,420,marble_i2c_clock_cycle_code) ||
       !marble_code_digest(marble_npa_ram_word,base+0x4458,148,marble_i2c_frequency_metadata_code))return 0;
    MarbleI2cClockOps o={marble_i2c_clock_provider,marble_audio_qup_state,marble_audio_qup_enable,marble_audio_qup_disable};
    marble_audio_qup_phase="before";
    int ok=marble_i2c_clocks_prepare(&o,&marble_audio_qup_hold);
    marble_audio_qup_phase="acquire";
    if(ok)ok=marble_i2c_clocks_acquire(&o,&marble_audio_qup_hold);
    print("ah: begin PASS=%d held=%x uncertain=%x active=%d dirty=%d after_START=1 release_after_PAS=1 frequency_requests=0 voltage_requests=0\n",
        (uint64_t)ok,(uint64_t)marble_audio_qup_hold.held,(uint64_t)marble_audio_qup_hold.uncertain,
        (uint64_t)marble_audio_qup_hold.active,(uint64_t)marble_audio_qup_hold.dirty);
    if(marble_pil_progress)marble_pil_progress("audio-QUP-outer-acquired",ok);
    return ok;
}
static int marble_audio_qup_end(void)
{
    if(!marble_audio_qup_hold.active || marble_pil_dsp_dirty || !marble_glink_control_state.stopped ||
       marble_pil_deferred.saved || marble_i2c_amp_dirty)return 0;
    MarbleI2cClockOps o={marble_i2c_clock_provider,marble_audio_qup_state,marble_audio_qup_enable,marble_audio_qup_disable};
    marble_audio_qup_phase="restore";
    int restored=marble_i2c_clocks_restore(&o,&marble_audio_qup_hold);uint64_t now[8],head=~0ULL;
    unsigned stable=marble_rpmh_words(marble_npa_ram_word,(uint64_t)(uintptr_t)marble_i2c_clock_provider,now,8) &&
        !memcmp(now,marble_audio_qup_table,sizeof(now)) && marble_npa_ram_word(marble_i2c_inventory_base+0xa850,&head) && !head;
    print("ah: end PASS=%d restored=%d table_unchanged=%d I2C_head_unchanged=%d held=%x uncertain=%x active=%d dirty=%d PAS_stopped=1 frequency_requests=0 voltage_requests=0\n",
        (uint64_t)(restored&&stable),(uint64_t)restored,(uint64_t)stable,(uint64_t)!head,(uint64_t)marble_audio_qup_hold.held,
        (uint64_t)marble_audio_qup_hold.uncertain,(uint64_t)marble_audio_qup_hold.active,(uint64_t)marble_audio_qup_hold.dirty);
    if(marble_pil_progress)marble_pil_progress("audio-QUP-outer-released",restored&&stable);
    return restored && stable;
}
#endif
