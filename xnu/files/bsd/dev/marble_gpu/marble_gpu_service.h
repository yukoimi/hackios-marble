#ifndef MARBLE_GPU_SERVICE_H
#define MARBLE_GPU_SERVICE_H
#include <stdint.h>
#include <stdbool.h>
bool marble_gpu_board(void);
bool marble_gpu_ready(void);
bool marble_gpu_parameter(uint32_t,uint64_t *);
bool marble_gpu_map(uint64_t,uint64_t,bool);
void marble_gpu_mapped(void);
void marble_gpu_prepare(void); /* gGPULock held: drain before CPU cache maintenance */
void marble_gpu_unmap(uint64_t,uint64_t);
uint32_t marble_gpu_submit(const uint64_t *,const uint32_t *,uint32_t);
bool marble_gpu_done(uint32_t);
uint32_t marble_gpu_last(void);
void marble_gpu_report(const char *);
void marble_gpu_stop_locked(void);
void redwood_gpu_marble_allocator(void);
#endif
