#ifndef TREZOR_PC_MONITOR_H
#define TREZOR_PC_MONITOR_H

#include <stdbool.h>
#include <stdint.h>

#include "protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

void monitor_init(void);
bool monitor_handle_packet(const uint8_t input[TM_PACKET_SIZE]);
void monitor_tick(uint32_t now_ms);
void monitor_button(uint8_t button, uint8_t gesture);
void monitor_show_system(void);
bool monitor_take_packet(uint8_t output[TM_PACKET_SIZE]);

const uint8_t *monitor_pack(void);
const tm_pack_header_t *monitor_pack_header(void);
const tm_metric_entry_t *monitor_metric(uint16_t channel_id);
uint32_t monitor_metric_updated(uint16_t channel_id);
uint8_t monitor_current_page(void);
bool monitor_pack_active(void);
bool monitor_system_visible(void);
bool monitor_reboot_requested(void);
void monitor_leave_system(void);

#ifdef __cplusplus
}
#endif

#endif
