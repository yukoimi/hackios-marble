/* Freestanding declarations for the unmodified public-domain LZMA SDK.
 * Definitions are supplied by the EFI wrapper; no host CRT is linked. */
#ifndef MARBLE_EFI_STRING_H
#define MARBLE_EFI_STRING_H
#include <stddef.h>
void *memcpy(void *,const void *,size_t);
void *memmove(void *,const void *,size_t);
void *memset(void *,int,size_t);
#endif
