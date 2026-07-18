#include <stdint.h>

static uint32_t test_timer_ms;
static int reboot_requested;

void renderer_init(void) {}
void renderer_invalidate(void) {}
void renderer_tick(uint32_t now_ms) { (void)now_ms; }
void renderer_waiting(void) {}
void renderer_system(void) {}

uint32_t svc_timer_ms(void) { return test_timer_ms; }
void svc_reboot_to_bootloader(void) { reboot_requested = 1; }

void test_set_timer_ms(uint32_t value) { test_timer_ms = value; }
int test_reboot_requested(void) { return reboot_requested; }
