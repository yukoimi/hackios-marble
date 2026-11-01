/* Encode the known-size, no-EOS LZMA stream required by EDK2's guided section. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "LzmaEnc.h"
#include "Alloc.h"

int main(int argc, char **argv)
{
	if (argc != 3) return 2;
	FILE *input = fopen(argv[1], "rb");
	if (!input || fseek(input, 0, SEEK_END)) return 3;
	long size = ftell(input);
	if (size <= 0 || size > 64 * 1024 * 1024 || fseek(input, 0, SEEK_SET)) return 4;
	Byte *source = malloc((size_t)size);
	SizeT capacity = (SizeT)size + (SizeT)size / 3 + 65536;
	Byte *dest = malloc(capacity + 13);
	if (!source || !dest || fread(source, 1, (size_t)size, input) != (size_t)size) return 5;
	fclose(input);
	CLzmaEncProps props;
	LzmaEncProps_Init(&props);
	props.dictSize = 0x1000000;
	props.numThreads = 1;
	SizeT props_size = 5;
	SRes status = LzmaEncode(dest + 13, &capacity, source, (SizeT)size, &props,
	    dest, &props_size, 0, NULL, &g_Alloc, &g_Alloc);
	if (status != SZ_OK || props_size != 5) return 6;
	for (int i = 0; i < 8; i++) dest[5 + i] = (Byte)((uint64_t)size >> (8 * i));
	FILE *output = fopen(argv[2], "wb");
	if (!output || fwrite(dest, 1, capacity + 13, output) != capacity + 13) return 7;
	if (fclose(output)) return 8;
	free(source); free(dest);
	return 0;
}
