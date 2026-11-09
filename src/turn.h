#pragma once
#include <stdint.h>

typedef struct KbmTurn {
    uint32_t response_q16;
    uint16_t min_step;
    uint16_t max_step;
} KbmTurn;

uint16_t kbm_turn_step(uint16_t actual, uint16_t target, const KbmTurn *turn);
uint16_t kbm_degrees_to_units(double degrees);
