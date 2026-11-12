#include "host.h"

#include <string.h>

#include "guest.h"
#include "platform.h"

KbmHost g_host;

#ifdef _WIN32
#define SDL_LIB "SDL3.dll"
#else
#define SDL_LIB "libSDL3.so.0"
#endif

static void *find_port(const char *name) { return kbm_find_symbol(name); }

static void *find_sdl(const char *name) {
    void *p = kbm_find_symbol(name);
    return p ? p : kbm_find_symbol_in_lib(SDL_LIB, name);
}

bool kbm_host_resolve(bool require_gates) {
    memset(&g_host, 0, sizeof g_host);
    bool ok = true;

    g_host.fnhook_set = (bool (*)(uint32_t, uint32_t))find_port("fnhook_set");
    ok &= g_host.fnhook_set != NULL;
    g_host.fnhook_get = (uint32_t (*)(uint32_t))find_port("fnhook_get");
    ok &= g_host.fnhook_get != NULL;
    g_host.fnhook_call_original = (void (*)(KbmCpu *, uint32_t))find_port("fnhook_call_original");
    ok &= g_host.fnhook_call_original != NULL;
    g_host.hle_register = (uint32_t (*)(const char *, KbmHostFn))find_port("hle_register");
    ok &= g_host.hle_register != NULL;
    g_host.guest_call = (void (*)(KbmCpu *, uint32_t))find_port("guest_call");

    g_host.dispatch_lookup = (void *(*)(uint32_t))find_port("dispatch_lookup");
    g_host.textinput_active = (bool (*)(void))find_port("textinput_active");
    ok &= g_host.textinput_active != NULL || !require_gates;
    g_host.chat_active = (bool (*)(void))find_port("chat_active");
    ok &= g_host.chat_active != NULL || !require_gates;
    g_host.overlay_menu_open = (bool (*)(void))find_port("overlay_menu_open");

    g_host.sdl_keyboard_state = (const bool *(*)(int *))find_sdl("SDL_GetKeyboardState");
    ok &= g_host.sdl_keyboard_state != NULL;
    g_host.sdl_scancode_from_name = (int (*)(const char *))find_sdl("SDL_GetScancodeFromName");
    ok &= g_host.sdl_scancode_from_name != NULL;
    g_host.sdl_mouse_state = (uint32_t (*)(float *, float *))find_sdl("SDL_GetMouseState");
    g_host.sdl_keyboard_focus = (void *(*)(void))find_sdl("SDL_GetKeyboardFocus");
    g_host.sdl_text_input_active = (bool (*)(void *))find_sdl("SDL_TextInputActive");

    if (!g_host.overlay_menu_open && !(g_host.sdl_keyboard_focus && g_host.sdl_text_input_active) && require_gates) ok = false;
    return ok;
}

bool kbm_guest_verify(void) {
    bool ok = true;
    for (size_t i = 0; i < KBM_GUEST_SIG_COUNT; i++) {
        const KbmGuestSig *s = &kbm_guest_sigs[i];
        if (!kbm_readable(KBM_GPTR(s->addr), sizeof(uint32_t) * KBM_SIG_WORDS)) {
            ok = false;
            continue;
        }
        for (int w = 0; w < KBM_SIG_WORDS; w++)
            if (kbm_ld32(s->addr + 4u * (uint32_t)w) != s->words[w]) ok = false;
    }
    return ok;
}

bool kbm_guest_signatures_match(void) {
    for (size_t i = 0; i < KBM_GUEST_SIG_COUNT; i++) {
        const KbmGuestSig *s = &kbm_guest_sigs[i];
        if (!kbm_readable(KBM_GPTR(s->addr), sizeof(uint32_t) * KBM_SIG_WORDS)) return false;
        for (int w = 0; w < KBM_SIG_WORDS; w++)
            if (kbm_ld32(s->addr + 4u * (uint32_t)w) != s->words[w]) return false;
    }
    return true;
}
