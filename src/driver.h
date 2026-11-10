#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "host.h"
#include "log.h"

typedef struct KbmStats {
    uint64_t input_calls;
    uint64_t input_not_local;
    uint64_t input_suspended;
    uint64_t input_idle;
    uint64_t input_driven;
    uint64_t injected;
    uint64_t mag_from_game;
    uint64_t mag_from_learned;
    uint64_t mag_from_default;
    uint64_t reversals;
    uint64_t face_calls;
    uint64_t face_not_driving;
    uint64_t face_not_local;
    uint64_t face_skipped_scope;
    uint64_t face_skipped_act;
    uint64_t face_no_change;
    uint64_t snaps;
    uint64_t snap_reversals;
    uint64_t turn_partial;
} KbmStats;

void kbm_driver_reset(void);
void kbm_driver_before_input(KbmCpu *c, uint32_t player, bool alt);
void kbm_driver_on_input(KbmCpu *c, uint32_t player, bool alt);
void kbm_driver_on_face(uint32_t player);
void kbm_driver_log_stats(KbmLogLevel level, const char *why);
const KbmStats *kbm_driver_stats(void);
int kbm_driver_learned(int category);
