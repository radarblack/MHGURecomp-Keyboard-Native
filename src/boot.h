#pragma once
#include <stdbool.h>

typedef enum KbmBootState {
    KBM_BOOT_IDLE = 0,
    KBM_BOOT_WAITING,
    KBM_BOOT_ACTIVE,
    KBM_BOOT_FAILED,
    KBM_BOOT_STOPPED
} KbmBootState;

bool kbm_boot_start(void);
void kbm_boot_stop(void);
KbmBootState kbm_boot_state(void);
