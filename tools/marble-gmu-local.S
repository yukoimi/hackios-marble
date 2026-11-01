/* Cortex-M3 diagnostic: ITCM vector reset PC=0x41, stack from real firmware.
 * Only writes a local DTCM marker, disables interrupts and loops. No AXI/DDR,
 * MMIO, HFI, votes, GPU or power management code. Host stops after 100ms. */
.syntax unified
.cpu cortex-m3
.thumb
.section .text
.global marble_gmu_local
.thumb_func
marble_gmu_local:
    cpsid i
    ldr r0, dtcm
    ldr r1, marker
    str r1, [r0]
    dsb sy
1:  b 1b
    .balign 4
dtcm: .word 0x10004000
marker: .word 0x4d475055
