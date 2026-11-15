#pragma once
#include <stdbool.h>
#include <stdint.h>

bool kbm_trace_init(const char *module_dir);
void kbm_trace_reset(void);
void kbm_trace_update(uint32_t player, bool alt);
void kbm_trace_close(void);
