/* Backboardd input scheduling and timing probe, built without an Apple SDK.
 * These declarations use the public Darwin arm64 ABI. No private address patch.
 * DYLD interposing affects this process only; system files remain untouched.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

extern int pthread_setname_np(const char *);
extern int open(const char *, int, ...);
extern long read(int, void *, size_t);
extern long write(int, const void *, size_t);
extern int snprintf(char *, size_t, const char *, ...);
extern int strcmp(const char *, const char *);
extern void *dlsym(void *, const char *);
extern void *dispatch_queue_create(const char *, void *);
extern unsigned char IOHIDVirtualServiceClientDispatchEvent(void *, void *);
extern uint64_t mach_absolute_time(void);
struct timebase { uint32_t numer, denom; };
extern int mach_timebase_info(struct timebase *);

#define RTLD_DEFAULT ((void *)-2)
#define QOS_USER_INTERACTIVE 0x21u
static int input_fd = -1;
static unsigned traces;
static struct timebase tb;
static struct sample { uint64_t time; uint32_t type, finger, x, y; } sample;
static uint64_t read_at, max_queue_us, max_dispatch_us;
static unsigned pending;

static uint64_t us(uint64_t delta) {
    if (!tb.denom) mach_timebase_info(&tb);
    return tb.denom ? delta / tb.denom * tb.numer / 1000 : 0;
}
static void say(const char *line) {
    size_t n = 0; while (line[n]) n++;
    write(2, line, n);
}
static int input_name(const char *name) {
    int result = pthread_setname_np(name);
    if (name && !strcmp(name, "redwood input")) {
        int (*set_qos)(unsigned, int) = dlsym(RTLD_DEFAULT, "pthread_set_qos_class_self_np");
        unsigned (*get_qos)(int *) = dlsym(RTLD_DEFAULT, "qos_class_self");
        int status = set_qos ? set_qos(QOS_USER_INTERACTIVE, 0) : -1;
        char line[160];
        snprintf(line, sizeof(line), "marble-input: HID reader QoS set=%d class=%u\n",
                 status, get_qos ? get_qos(0) : 0);
        say(line);
    }
    return result;
}
static void *input_queue(const char *label, void *attr) {
    if (label && !strcmp(label, "redwood.input")) {
        void *(*with_qos)(void *, unsigned, int) = dlsym(RTLD_DEFAULT, "dispatch_queue_attr_make_with_qos_class");
        if (with_qos) { attr = with_qos(attr, QOS_USER_INTERACTIVE, 0); say("marble-input: HID dispatch queue QoS interactive\n"); }
    }
    return dispatch_queue_create(label, attr);
}
static int input_open(const char *path, int flags, ...) {
    int mode = 0;
    if (flags & 0x200) {
        va_list args; va_start(args, flags); mode = va_arg(args, int); va_end(args);
    }
    int fd = open(path, flags, mode);
    if (fd >= 0 && path && !strcmp(path, "/dev/redwood-touch") && !(flags & 3)) {
        input_fd = fd; say("marble-input: HID queue timing enabled\n");
    }
    return fd;
}
static long input_read(int fd, void *buffer, size_t count) {
    long n = read(fd, buffer, count);
    if (fd == input_fd && n == sizeof(sample)) {
        sample = *(struct sample *)buffer; read_at = mach_absolute_time(); pending = 1;
        uint64_t delay = read_at >= sample.time ? us(read_at - sample.time) : 0;
        if (delay > max_queue_us) max_queue_us = delay;
    }
    return n;
}
static unsigned char input_dispatch(void *service, void *event) {
    uint64_t start = mach_absolute_time();
    unsigned char result = IOHIDVirtualServiceClientDispatchEvent(service, event);
    if (pending) {
        uint64_t end = mach_absolute_time(), dispatch_us = us(end-start);
        if (dispatch_us > max_dispatch_us) max_dispatch_us = dispatch_us;
        if ((sample.type == 1 || sample.type == 3) && traces++ < 256) {
            char line[256];
            snprintf(line, sizeof(line), "marble-input: HID type=%u finger=%u queue_us=%llu read_to_send_us=%llu dispatch_us=%llu max_queue_us=%llu max_dispatch_us=%llu ok=%u\n",
                     sample.type, sample.finger, read_at >= sample.time ? us(read_at-sample.time) : 0,
                     us(start-read_at), dispatch_us, max_queue_us, max_dispatch_us, (unsigned)result);
            say(line);
        }
        pending = 0;
    }
    return result;
}

#define INTERPOSE(replacement, original) \
    __attribute__((used, section("__DATA,__interpose"))) \
    static const struct { const void *replacement; const void *original; } \
    interpose_##original = { (const void *)replacement, (const void *)original }
INTERPOSE(input_name, pthread_setname_np);
INTERPOSE(input_queue, dispatch_queue_create);
INTERPOSE(input_open, open);
INTERPOSE(input_read, read);
INTERPOSE(input_dispatch, IOHIDVirtualServiceClientDispatchEvent);
