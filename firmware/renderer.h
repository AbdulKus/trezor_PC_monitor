#ifndef TREZOR_PC_MONITOR_RENDERER_H
#define TREZOR_PC_MONITOR_RENDERER_H

#include <stdint.h>

void renderer_init(void);
void renderer_invalidate(void);
void renderer_tick(uint32_t now_ms);
void renderer_waiting(void);
void renderer_system(void);

#endif
