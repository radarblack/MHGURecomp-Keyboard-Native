#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "host.h"

typedef enum KbmHookId { KBM_H_INPUT = 0, KBM_H_INPUT_ALT, KBM_H_FACE, KBM_H_COUNT } KbmHookId;

typedef struct KbmHook {
    const char *name;
    uint32_t addr;
    KbmHostFn fn;
    uint32_t detour;
    uint32_t prev;
    bool enabled;
    bool installed;
    bool passthrough;
    bool alive_logged;
    uint64_t calls;
} KbmHook;

extern KbmHook g_hooks[KBM_H_COUNT];

bool kbm_hooks_install(void);
void kbm_hooks_remove(void);
int kbm_hooks_installed_count(void);
