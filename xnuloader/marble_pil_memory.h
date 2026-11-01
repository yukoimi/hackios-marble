/* Only for already qualified normal DDR, never register/MMIO memory.
 * Byte accesses to the noncacheable PIL carveout cost one bus transaction
 * each. Aligned 64-bit accesses preserve exact bounds and avoid that cost. */
#ifndef MARBLE_PIL_MEMORY_H
#define MARBLE_PIL_MEMORY_H
static int marble_pil_memory_equal(const void *a,const void *b,uint64_t bytes)
{
    const uint8_t *x=a,*y=b;
    while(bytes && (((uintptr_t)x|(uintptr_t)y)&7)) {if(*x++!=*y++)return 0;bytes--;}
    while(bytes>=8) {
        if(*(const volatile uint64_t *)x!=*(const volatile uint64_t *)y)return 0;
        x+=8;y+=8;bytes-=8;
    }
    while(bytes--)if(*x++!=*y++)return 0;
    return 1;
}
static void marble_pil_memory_zero(void *memory,uint64_t bytes)
{
    uint8_t *p=memory;
    while(bytes && ((uintptr_t)p&7)) {*p++=0;bytes--;}
    while(bytes>=8) {*(volatile uint64_t *)p=0;p+=8;bytes-=8;}
    while(bytes--)*p++=0;
}
static int marble_pil_memory_is_zero(const void *memory,uint64_t bytes)
{
    const uint8_t *p=memory;
    while(bytes && ((uintptr_t)p&7)) {if(*p++)return 0;bytes--;}
    while(bytes>=8) {if(*(const volatile uint64_t *)p)return 0;p+=8;bytes-=8;}
    while(bytes--)if(*p++)return 0;
    return 1;
}
#endif
