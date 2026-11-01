/* SPDX-License-Identifier: 0BSD
 * ARM64 BL/ADRP filter from XZ Utils v5.8.1 src/liblzma/simple/arm64.c.
 * Authors: Lasse Collin, Jia Tan, Igor Pavlov. Only IO names/signature adapted.
 * This is a reversible representation transform; SHA256 is verified AFTER
 * inverse conversion and before LoadImage. Never execute transformed bytes.
 */
#ifndef MARBLE_ARM64_BCJ_H
#define MARBLE_ARM64_BCJ_H
#include <stdint.h>
#include <stddef.h>
static uint32_t marble_bcj_read32(const uint8_t *p)
{return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void marble_bcj_write32(uint8_t *p,uint32_t v)
{for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static size_t marble_arm64_bcj(uint32_t now_pos,unsigned is_encoder,uint8_t *buffer,size_t size)
{
	size &= ~(size_t)3;

	size_t i;

	// Clang 14.0.6 on x86-64 makes this four times bigger and 40 % slower
	// with auto-vectorization that is enabled by default with -O2.
	// Such vectorization bloat happens with -O2 when targeting ARM64 too
	// but performance hasn't been tested.
#ifdef __clang__
#	pragma clang loop vectorize(disable)
#endif
	for (i = 0; i < size; i += 4) {
		uint32_t pc = (uint32_t)(now_pos + i);
		uint32_t instr = marble_bcj_read32(buffer + i);

		if ((instr >> 26) == 0x25) {
			// BL instruction:
			// The full 26-bit immediate is converted.
			// The range is +/-128 MiB.
			//
			// Using the full range helps quite a lot with
			// big executables. Smaller range would reduce false
			// positives in non-code sections of the input though
			// so this is a compromise that slightly favors big
			// files. With the full range, only six bits of the 32
			// need to match to trigger a conversion.
			const uint32_t src = instr;
			instr = 0x94000000;

			pc >>= 2;
			if (!is_encoder)
				pc = 0U - pc;

			instr |= (src + pc) & 0x03FFFFFF;
			marble_bcj_write32(buffer + i, instr);

		} else if ((instr & 0x9F000000) == 0x90000000) {
			// ADRP instruction:
			// Only values in the range +/-512 MiB are converted.
			//
			// Using less than the full +/-4 GiB range reduces
			// false positives on non-code sections of the input
			// while being excellent for executables up to 512 MiB.
			// The positive effect of ADRP conversion is smaller
			// than that of BL but it also doesn't hurt so much in
			// non-code sections of input because, with +/-512 MiB
			// range, nine bits of 32 need to match to trigger a
			// conversion (two 10-bit match choices = 9 bits).
			const uint32_t src = ((instr >> 29) & 3)
					| ((instr >> 3) & 0x001FFFFC);

			// With the addition only one branch is needed to
			// check the +/- range. This is usually false when
			// processing ARM64 code so branch prediction will
			// handle it well in terms of performance.
			//
			//if ((src & 0x001E0000) != 0
			// && (src & 0x001E0000) != 0x001E0000)
			if ((src + 0x00020000) & 0x001C0000)
				continue;

			instr &= 0x9000001F;

			pc >>= 12;
			if (!is_encoder)
				pc = 0U - pc;

			const uint32_t dest = src + pc;
			instr |= (dest & 3) << 29;
			instr |= (dest & 0x0003FFFC) << 3;
			instr |= (0U - (dest & 0x00020000)) & 0x00E00000;
			marble_bcj_write32(buffer + i, instr);
		}
	}

	return i;
}
#endif
