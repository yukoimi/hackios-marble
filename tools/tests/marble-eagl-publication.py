"""Generate a host C harness from the real frame_end publication path.

The GL backend is mocked: this verifies API ordering, not GPU execution.
Compile the generated C with ASan/UBSan in the Linux build VM.
"""
import argparse
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

PRELUDE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef void *GLsync;
#define GL_SYNC_GPU_COMMANDS_COMPLETE 1
#define GL_SYNC_FLUSH_COMMANDS_BIT 2
struct image { bool target, drawn, shared; unsigned mirror; int width, height; void *surface; };
struct eagl { unsigned image_count; struct image images[4]; };
static int wait_for_frames, sync_frames, missing_sync, output_timing;
static uint32_t IOSurfaceGetID(void *surface) { return (uint32_t)(uintptr_t)surface; }
static char events[64];
static unsigned used;
static void event(char c) { assert(used + 1 < sizeof(events)); events[used++] = c; events[used] = 0; }
static void clear_now(struct eagl *e) { (void)e; }
static uint64_t now_us(void) { return 1; }
static void target_to_surface(struct eagl *e, struct image *i) {
    (void)e; assert(i->drawn && i->mirror); i->drawn = false; event('B');
}
static void glFlush(void) { event('F'); }
static void glFinish(void) { event('W'); }
static GLsync glFenceSync(unsigned condition, unsigned flags) {
    assert(condition == GL_SYNC_GPU_COMMANDS_COMPLETE && flags == 0); event('S');
    return missing_sync ? NULL : (void *)(uintptr_t)1;
}
static unsigned glClientWaitSync(GLsync sync, unsigned flags, uint64_t timeout) {
    assert(sync && flags == GL_SYNC_FLUSH_COMMANDS_BIT && timeout == 0); event('P'); return 0;
}
static void glDeleteSync(GLsync sync) { assert(sync); event('D'); }
'''

TESTS = r'''
static void check(unsigned mirrors, bool shared, bool finished, int wait, int publish,
                  int fail, const char *expected) {
    struct eagl e = {0};
    for (unsigned i = 0; i < mirrors; i++)
        e.images[e.image_count++] = (struct image){true, true, true, i + 1};
    if (shared) e.images[e.image_count++] = (struct image){true, true, true, 0};
    wait_for_frames = wait; sync_frames = publish; missing_sync = fail;
    used = 0; events[0] = 0;
    frame_end(&e, finished);
    assert(strcmp(events, expected) == 0);
}
int main(void) {
    check(1, false, true, 0, 1, 0, "BFSPD"); /* post-finish copy still published */
    check(2, false, true, 0, 1, 0, "BBFSPD"); /* all copies before one flush */
    check(1, true, true, 1, 1, 0, "BFW");   /* synchronous policy covers copy */
    check(0, true, true, 0, 1, 0, "");      /* old finish suffices without a new copy */
    check(0, true, false, 0, 1, 0, "SPD");  /* ordinary async shared rendering */
    check(0, true, false, 1, 1, 0, "W");    /* ordinary synchronous rendering */
    check(1, false, true, 0, 1, 1, "BFSW"); /* failed sync falls back to finish */
    check(0, false, false, 0, 1, 0, "");    /* offscreen work needs no publication */
    puts("EAGL publication API ordering: 8 cases PASS (mock GL, not hardware)");
}
'''


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('output', type=Path)
    a = p.parse_args()
    source = (ROOT / 'userland/src/eagl.c').read_text()
    start = source.index('static void\nframe_end(struct eagl *e, bool finished)')
    end = source.index('\n\twaited = now_us();', start)
    a.output.write_text(PRELUDE + source[start:end] + '\n}\n' + TESTS)


if __name__ == '__main__':
    main()
