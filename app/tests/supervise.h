#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t svc_timer_ms(void);
void svc_reboot_to_bootloader(void);

#ifdef __cplusplus
}
#endif
