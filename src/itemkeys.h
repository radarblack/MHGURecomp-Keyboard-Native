#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "host.h"
#include "keys.h"
#include "log.h"

typedef struct KbmItemStats {
    uint64_t presses;
    uint64_t dropped_old;
    uint64_t dropped_gate;
    uint64_t selects;
    uint64_t selected_item;
    uint64_t selected_no_item;
    uint64_t uses;
    uint64_t use_nothing;
    uint64_t use_unconfigured;
    uint64_t blocked_busy;
    uint64_t injected_updates;
    uint64_t ring_full;
} KbmItemStats;

bool kbm_itemkeys_init(KbmSettingFn setting);
bool kbm_itemkeys_start(void);
void kbm_itemkeys_stop(void);
void kbm_itemkeys_push(int key, uint32_t t_ms);
void kbm_itemkeys_before_input(KbmCpu *c, uint32_t player);
void kbm_itemkeys_after_input(KbmCpu *c, uint32_t player, const char *gate);
bool kbm_itemkeys_enabled(void);
const KbmItemStats *kbm_itemkeys_stats(void);
void kbm_itemkeys_log_stats(KbmLogLevel level, const char *why);
