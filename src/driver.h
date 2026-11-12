#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "host.h"

void kbm_driver_reset(void);
void kbm_driver_before_input(KbmCpu *c, uint32_t player, bool alt);
void kbm_driver_on_input(KbmCpu *c, uint32_t player, bool alt);
void kbm_driver_on_face(uint32_t player);
