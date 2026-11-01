#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#define EFIAPI
static uint64_t ticker;
static uint64_t read_CNTVCT_EL0(void){return ticker+=1920;}
static uint64_t read_CNTFRQ_EL0(void){return 19200000;}
#define READ_SYSREG(x) read_##x()
static uint64_t tpl;
static uint64_t raise_tpl(uint64_t n){uint64_t old=tpl;tpl=n;return old;}
static void restore_tpl(uint64_t n){tpl=n;}
static struct {void *raise_tpl,*restore_tpl;} bs={raise_tpl,restore_tpl},*BS=&bs;
static int fault,invalid_version,failed,status_timeout,bad_pin,reads,writes,commands[2];
static int print(const char *s,...){(void)s;return 0;}
static uint64_t marble_fw_translation(uint64_t a){return fault?1:a;}
static uint32_t mt_read(uint64_t a)
{
    reads++;
    if(a==0xc400000)return invalid_version?0x50000000:0x70000000;
    if(a==0xc400004)return 32;
    if(a==0xc402008 || a==0xc40201c)return 0x01300;
    if(a==0xc402024)return 0x18d00;
    if(a>=0xc402000 && a<0xc403000)return 0;
    if(a==0xc42d008)return 1;
    if(a>=0xc42d000 && a<0xc42e000)return 0;
    unsigned ch=(a-0xc440000)/0x20,offset=a%0x20;
    assert(ch==7 || ch==9);
    if(offset==8)return status_timeout?0:failed?5:1;
    assert(offset==0x18);
    unsigned reg=(commands[ch==9]>>4)&255;
    if(reg==4)return ch==7?0x901:0x1010;
    if(reg==0x10)return ch==7?0:1;
    if(reg==0x40)return bad_pin?1:0;
    if(reg==0x42)return 0;
    if(reg==0x46)return 0x80;
    assert(0);return 0;
}
static void mt_write(uint64_t a,uint32_t v)
{
    assert(tpl==16);assert(a==0xc4400e0 || a==0xc440120);
    assert((v>>27)==1); /* observer read opcode; never a PMIC write */
    commands[a==0xc440120]=v;writes++;
}
#define MBUTTONS_TEST 1
#include "../../xnuloader/marble_buttons_fw.h"
int main(void)
{
    fault=1;assert(!marble_buttons_probe());assert(!reads && !writes);fault=0;
    invalid_version=1;assert(!marble_buttons_probe());assert(!writes);invalid_version=0;
    assert(marble_buttons_probe());assert(marble_buttons_ready && writes==7 && tpl==0);
    assert(marble_button_channels[0]==7 && marble_button_channels[1]==9);
    int before=writes;uint8_t value;
    assert(!marble_button_read(2,0,&value,1));assert(!marble_button_read(0,255,&value,2));assert(writes==before);
    failed=1;assert(!marble_buttons_probe());assert(tpl==0 && !marble_buttons_ready);failed=0;
    status_timeout=1;assert(!marble_buttons_probe());assert(tpl==0 && !marble_buttons_ready);status_timeout=0;
    bad_pin=1;assert(!marble_buttons_probe());assert(!marble_buttons_ready);
    MarbleButton b={0};
    assert(!marble_button_update(&b,1));assert(!marble_button_update(&b,1)); /* boot held is suppressed */
    assert(!marble_button_update(&b,0));assert(!marble_button_update(&b,1));
    assert(!marble_button_update(&b,0));assert(!marble_button_update(&b,1)); /* bounce */
    assert(marble_button_update(&b,1)==4);assert(!marble_button_update(&b,1));
    assert(!marble_button_update(&b,0));assert(marble_button_update(&b,0)==5);
    puts("marble buttons: PASS mapping/version gates, EE ownership, read-only opcodes, qualification and failed/timed-out reads");
}
