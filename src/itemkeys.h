#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "host.h"
#include "keys.h"

bool kbm_itemkeys_init(void);
bool kbm_itemkeys_start(void);
void kbm_itemkeys_stop(void);
void kbm_itemkeys_push(int key, uint32_t t_ms);
void kbm_itemkeys_before_input(KbmCpu *c, uint32_t player);
void kbm_itemkeys_after_input(KbmCpu *c, uint32_t player, const char *gate);
