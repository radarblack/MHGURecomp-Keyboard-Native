#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct KbmCpu {
    uint32_t r[16];
} KbmCpu;

typedef void (*KbmHostFn)(KbmCpu *c);

typedef struct KbmHost {
    bool (*fnhook_set)(uint32_t addr, uint32_t detour);
    uint32_t (*fnhook_get)(uint32_t addr);
    void (*fnhook_call_original)(KbmCpu *c, uint32_t addr);
    uint32_t (*hle_register)(const char *name, KbmHostFn fn);
    void (*guest_call)(KbmCpu *c, uint32_t addr);
    bool (*textinput_active)(void);
    bool (*chat_active)(void);
    bool (*overlay_menu_open)(void);
    const bool *(*sdl_keyboard_state)(int *numkeys);
    int (*sdl_scancode_from_name)(const char *name);
    uint32_t (*sdl_mouse_state)(float *x, float *y);
    void *(*dispatch_lookup)(uint32_t addr);
    void *(*sdl_keyboard_focus)(void);
    bool (*sdl_text_input_active)(void *window);
} KbmHost;

extern KbmHost g_host;

bool kbm_host_resolve(bool require_gates);
bool kbm_guest_verify(void);
bool kbm_guest_signatures_match(void);
