/* Real polling/decoding code with a simulated Goodix volatile report register.
 * Regression: a ready report must be consumed even if IRQ is already high.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define MT_POLL_TEST 1
static uint64_t clock_ticks;
#define TEST_CNTVCT_EL0 clock_ticks
#define TEST_CNTFRQ_EL0 UINT64_C(19200000)
#define READ_SYSREG(name) TEST_##name
static uint8_t raw[90];
static unsigned ack, reads, corrupt;
static unsigned mt_get(unsigned pin) { assert(pin == 21); return 1; }
static void mt_reg_read(uint32_t addr, uint8_t *data, unsigned n) {
    assert((addr == 0x10308 && n == 26) || (addr == 0x10310 && n <= 82)); reads++;
    memcpy(data, raw + (addr - 0x10308), n);
    if(corrupt && addr == 0x10308) data[6] ^= 1;
}
static void mt_reg_write(uint32_t addr, const uint8_t *data, unsigned n) {
    assert(addr == 0x10308 && n == 1 && !*data); ack++; raw[0] = 0;
}
static void clean_dcache(uint64_t begin, uint64_t end) { (void)begin; (void)end; }
static void print(const char *format, ...) { (void)format; }
#include "../../xnuloader/marble_touch.h"
static struct mt_mailbox mailbox;
static struct mt_event *latest(void) {
    return &mailbox.events[(mailbox.produced-1)%MT_CAPACITY];
}
static void checksum(uint8_t *p, unsigned n) {
    unsigned sum = 0; for(unsigned i=0;i<n-2;i++) sum += p[i];
    p[n-2] = sum; p[n-1] = sum >> 8;
}
static void frame(unsigned down, unsigned x) {
    memset(raw, 0, sizeof(raw)); raw[0] = 0x80; raw[2] = down;
    checksum(raw,8);
    if(down) {
        raw[10]=x; raw[11]=x>>8; raw[12]=0x80; raw[13]=1; raw[14]=10;
        checksum(raw+8,10);
    }
    clock_ticks += 192000; /* 10 ms */
    mt_poll();
}
static void multi(unsigned count) {
    memset(raw, 0, sizeof(raw)); raw[0] = 0x80; raw[2] = count;
    checksum(raw, 8);
    for(unsigned i=0;i<count;i++) {
        uint8_t *p=raw+8+i*8;
        p[0]=i<<4; unsigned x=i*1000, y=i*2000;
        p[2]=x; p[3]=x>>8; p[4]=y; p[5]=y>>8; p[6]=10;
    }
    if(count) checksum(raw+8,count*8+2);
    clock_ticks+=192000; mt_poll();
}
int main(void) {
    mt_box = &mailbox; mt_ready = 1; mt_report_addr = 0x10308;
    mt_max_x = 10800; mt_max_y = 24000;
    frame(1,1000); /* DOWN, notification already gone */
    assert(mailbox.produced == 1 && mailbox.events[0].type == 1 && ack == 1);
    frame(1,2000); /* MOVE */
    assert(mailbox.produced == 2 && mailbox.events[1].type == 2 && ack == 2);
    frame(0,0); /* UP even with IRQ high */
    assert(mailbox.produced == 3 && mailbox.events[2].type == 3 && ack == 3);
    frame(0,0); /* no duplicate UP */
    assert(mailbox.produced == 3);
    frame(1,3000);
    unsigned before = reads, oldack = ack;
    corrupt = 1; frame(0,0); /* broken release: retry, never acknowledge invalid data */
    assert(reads == before+2 && ack == oldack && mt_contacts[0].down);
    corrupt = 0; clock_ticks += 192000; mt_poll();
    assert(latest()->type == 3 && !mt_contacts[0].down);
    /* Stable long holds must not be converted into an invented release. */
    frame(1,3000);
    uint64_t held=mailbox.produced;
    for(unsigned i=0;i<500;i++) { clock_ticks+=192000; mt_poll(); }
    assert(mailbox.produced==held && mt_contacts[0].down);
    frame(0,0);
    /* Both panel corners retain full 16-bit normalized coordinates. */
    frame(1,0); assert(latest()->x==0);
    frame(1,10800); assert(latest()->x==65535);
    oldack=ack; held=mailbox.produced;
    frame(1,10801); assert(ack==oldack && mailbox.produced==held);
    frame(0,0);
    /* Read the extended report and release only the contacts that disappeared. */
    held=mailbox.produced; multi(10);
    assert(mailbox.produced==held+10);
    for(unsigned i=0;i<10;i++) assert(mt_contacts[i].down);
    multi(2);
    for(unsigned i=0;i<10;i++) assert(mt_contacts[i].down==(i<2));
    multi(0);
    for(unsigned i=0;i<10;i++) assert(!mt_contacts[i].down);
    /* An otherwise checksummed frame with duplicate IDs must be atomic: no
     * partial DOWNs and no acknowledgement of the corrupt frame. */
    multi(2); held=mailbox.produced; oldack=ack;
    raw[0]=0x80; raw[16]=raw[8]; checksum(raw+8,18);
    clock_ticks+=192000; mt_poll();
    assert(mailbox.produced==held && ack==oldack);
    multi(0);
    /* Thousands of complete gestures wrap the mailbox many times. */
    held=mailbox.produced;
    for(unsigned i=0;i<2000;i++) {
        frame(1,1000); assert(latest()->type==1);
        frame(1,2000); assert(latest()->type==2);
        frame(0,0); assert(latest()->type==3 && !mt_contacts[0].down);
        assert(latest()->sequence==mailbox.produced);
    }
    assert(mailbox.produced==held+6000);
    assert(!(mailbox.heartbeat & 1));
    /* Explicit unattended input must obey lifecycle and coordinate bounds. */
    held=mailbox.produced;
    assert(!mt_test_event("down 1 2",0));
    mt_ready=0;assert(!mt_test_event("down 1 2",1));mt_ready=1;
    mt_box=NULL;assert(!mt_test_event("down 1 2",1));mt_box=&mailbox;
    const char *invalid[]={"", "up", "move 1 2", "down", "down 1", "down -1 2",
        "down 65536 0", "down 0 65536", "down 000000 1", "down 1 2x", "down  1 2", "down 1 2 ", "down 1 2 3"};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++)assert(!mt_test_event(invalid[i],1));
    assert(mailbox.produced==held);
    mt_contacts[9].down=1;assert(!mt_test_event("down 1 2",1));mt_contacts[9].down=0;
    assert(mt_test_event("down 0 65535",1));
    assert(latest()->type==1 && latest()->finger==0 && latest()->x==0 && latest()->y==65535);
    assert(!mt_test_event("down 1 2",1));
    mt_contacts[9].down=1;assert(!mt_test_event("move 1 2",1));mt_contacts[9].down=0;
    assert(mt_test_event("move 65535 0",1));
    assert(latest()->type==2 && latest()->x==65535 && latest()->y==0);
    assert(mt_test_event("up",1));
    assert(latest()->type==3 && latest()->x==65535 && latest()->y==0 && !mt_test_down);
    assert(!mt_test_event("up",1) && !mt_test_event("move 1 2",1));
    assert(mailbox.produced==held+3 && !(mailbox.heartbeat&1));
    /* Device-clock input is bounded, progresses without further host traffic,
     * handles reversed coordinates, and always releases after a late poll. */
    held=mailbox.produced;
    assert(!mt_gesture_start("0 0 65535 65535 300",0));
    const char *bad_gesture[]={"", "0 0 1 1", "0 0 1 1 49", "0 0 1 1 1001",
        "0 0 65536 1 300", "0 0 1 1 300 ", "0 0 1 1 300x", "0 0 1 1 000300"};
    for(unsigned i=0;i<sizeof(bad_gesture)/sizeof(bad_gesture[0]);i++)assert(!mt_gesture_start(bad_gesture[i],1));
    assert(mailbox.produced==held);
    assert(mt_gesture_start("65535 0 0 65535 300",1));
    assert(latest()->type==1 && latest()->x==65535 && latest()->y==0);
    assert(!mt_gesture_start("0 0 1 1 300",1) && !mt_test_event("move 1 2",1));
    clock_ticks+=19200*150;mt_poll();
    assert(latest()->type==2 && latest()->x==32768 && latest()->y==32767);
    clock_ticks+=19200*150;mt_poll();
    assert(latest()->type==2 && latest()->x==0 && latest()->y==65535 && mt_gesture.active);
    clock_ticks+=19200*20;mt_poll();
    assert(latest()->type==3 && !mt_gesture.active && !mt_test_down);
    assert(mt_gesture_start("100 200 300 400 60",1));
    held=mailbox.produced;clock_ticks+=19200*200;mt_poll();
    assert(mailbox.produced==held+2 && latest()->type==3 && latest()->x==300 && latest()->y==400);
    assert(mt_gesture_start("100 200 300 400 300",1));
    held=mailbox.produced;frame(1,1000);
    assert(!mt_gesture.active && !mt_test_down && mt_contacts[0].down);
    assert(mailbox.events[(held+1)%MT_CAPACITY].type==3); /* synthetic UP before physical DOWN */
    assert(latest()->type==1);
    frame(0,0);
    assert(mt_gesture_start("100 200 100 200 60",1));
    assert(mt_test_event("up",1) && !mt_gesture.active);
    held=mailbox.produced;clock_ticks+=19200*100;mt_poll();
    assert(mailbox.produced==held && !(mailbox.heartbeat&1));
    puts("marble device-clock gestures: bounds, interpolation, reversal, late-poll release, physical preemption and cancellation PASS");
    puts("marble synthetic input: lifecycle, capability, bounds, physical-contact conflict, exact DOWN/MOVE/UP and heartbeat PASS");
    puts("marble touch: IRQ-high release, checksum retry, true long hold, edges, 10 contacts, invalid frame and 2000-gesture ring wrap PASS");
    return 0;
}
