/* M3 diagnostic runs from ITCM: read declared firmware IOVA, write/read
 * declared RAM scratch IOVA, record progress in DTCM, then loop. No MMIO. */
.syntax unified
.cpu cortex-m3
.thumb
.section .text
.global marble_gmu_fetch
.thumb_func
marble_gmu_fetch:
    cpsid i
    ldr r0, dtcm
    ldr r1, before
    str r1, [r0]
    ldr r2, code
    ldr r3, [r2]
    str r3, [r0, #4]
    ldr r2, scratch
    ldr r3, pattern
    str r3, [r2]
    dsb sy
    ldr r3, [r2]
    str r3, [r0, #8]
    ldr r1, done
    str r1, [r0, #12]
    dsb sy
    ldr r2, entry
    bx r2
    .balign 4
dtcm: .word 0x10004000
before: .word 0x4d475052
code: .word 0x4000
scratch: .word 0x60000000
pattern: .word 0xdec0de01
done: .word 0x4d475044
entry: .word 0x100001
