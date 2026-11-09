#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "config.h"
#include "turn.h"

typedef struct KbmKeys {
    bool up, down, left, right, walk;
} KbmKeys;

typedef struct KbmTracker {
    uint32_t seq;
    uint32_t stamp[4];
    bool down[4];
} KbmTracker;

typedef struct KbmMove {
    bool active;
    bool contested;
    int x;
    int y;
    uint16_t angle;
    bool walk;
} KbmMove;

void kbm_tracker_init(KbmTracker *t);
KbmMove kbm_resolve(KbmTracker *t, const KbmKeys *keys, KbmSocd socd);
uint16_t kbm_angle_for(int x, int y);
int kbm_tilt_category(int magnitude);
bool kbm_angle_reversal(uint16_t from, uint16_t to);
uint16_t kbm_angle_delta(uint16_t a, uint16_t b);
