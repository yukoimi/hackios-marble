/* One producer (EFI), one reader (Darwin). Disk tail is outside APFS.
 * All integers little endian. Slot sequence is published before produced.
 */
#ifndef MARBLE_TOUCH_MAILBOX_H
#define MARBLE_TOUCH_MAILBOX_H
#include <stdint.h>
#define MT_MAILBOX_OFFSET 0x800000u
#define MT_MAILBOX_BYTES 0x4000u
#define MT_MAILBOX_MAGIC UINT64_C(0x314843554f544d)
#define MT_CAPACITY 128u
struct mt_event {
    uint64_t sequence, ticks;
    uint32_t type, finger, x, y;
};
struct mt_mailbox {
    uint64_t magic;
    uint32_t version, event_size, capacity, ready;
    uint64_t produced, polls, reports, bad, heartbeat;
    struct mt_event events[MT_CAPACITY];
};
_Static_assert(sizeof(struct mt_event)==32,"mailbox event ABI");
_Static_assert(sizeof(struct mt_mailbox)==4160,"mailbox header ABI");
#endif
