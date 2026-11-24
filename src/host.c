#include "host.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "guest.h"
#include "platform.h"

KbmHost g_host;
static void (*g_log_fn)(const char *fmt, ...);

void kbm_log_set(void (*fn)(const char *fmt, ...)) { g_log_fn = fn; }

void kbm_log(const char *fmt, ...) {
    if (!g_log_fn) return;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    g_log_fn("[BetterKeyboard] %s\n", buf);
}

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

static bool need(const void *p, const char *name, bool required) {
    if (!p && required) kbm_log("missing required symbol: %s", name);
    else if (!p) kbm_log("optional symbol not found: %s", name);
    return p != NULL || !required;
}

bool kbm_host_resolve(bool require_gates) {
    memset(&g_host, 0, sizeof g_host);
    bool ok = true;

    g_host.fnhook_set = (bool (*)(uint32_t, uint32_t))find_port("fnhook_set");
    ok &= need(g_host.fnhook_set, "fnhook_set", true);
    g_host.fnhook_get = (uint32_t (*)(uint32_t))find_port("fnhook_get");
    ok &= need(g_host.fnhook_get, "fnhook_get", true);
    g_host.fnhook_call_original = (void (*)(KbmCpu *, uint32_t))find_port("fnhook_call_original");
    ok &= need(g_host.fnhook_call_original, "fnhook_call_original", true);
    g_host.hle_register = (uint32_t (*)(const char *, KbmHostFn))find_port("hle_register");
    ok &= need(g_host.hle_register, "hle_register", true);
    g_host.guest_call = (void (*)(KbmCpu *, uint32_t))find_port("guest_call");
    need(g_host.guest_call, "guest_call", false);

    g_host.dispatch_lookup = (void *(*)(uint32_t))find_port("dispatch_lookup");
    need(g_host.dispatch_lookup, "dispatch_lookup", false);
    g_host.textinput_active = (bool (*)(void))find_port("textinput_active");
    ok &= need(g_host.textinput_active, "textinput_active", require_gates);
    g_host.chat_active = (bool (*)(void))find_port("chat_active");
    ok &= need(g_host.chat_active, "chat_active", require_gates);
    g_host.overlay_menu_open = (bool (*)(void))find_port("overlay_menu_open");
    need(g_host.overlay_menu_open, "overlay_menu_open", false);

    g_host.sdl_keyboard_state = (const bool *(*)(int *))find_sdl("SDL_GetKeyboardState");
    ok &= need(g_host.sdl_keyboard_state, "SDL_GetKeyboardState", true);
    g_host.sdl_scancode_from_name = (int (*)(const char *))find_sdl("SDL_GetScancodeFromName");
    ok &= need(g_host.sdl_scancode_from_name, "SDL_GetScancodeFromName", true);
    g_host.sdl_mouse_state = (uint32_t (*)(float *, float *))find_sdl("SDL_GetMouseState");
    need(g_host.sdl_mouse_state, "SDL_GetMouseState", false);
    g_host.sdl_keyboard_focus = (void *(*)(void))find_sdl("SDL_GetKeyboardFocus");
    need(g_host.sdl_keyboard_focus, "SDL_GetKeyboardFocus", false);
    g_host.sdl_text_input_active = (bool (*)(void *))find_sdl("SDL_TextInputActive");
    need(g_host.sdl_text_input_active, "SDL_TextInputActive", false);

    if (!g_host.overlay_menu_open && !(g_host.sdl_keyboard_focus && g_host.sdl_text_input_active) && require_gates) {
        kbm_log("no menu gate available: neither overlay_menu_open nor the SDL text-input fallback");
        ok = false;
    }
    return ok;
}

bool kbm_guest_verify(void) {
    bool ok = true;
    for (size_t i = 0; i < KBM_GUEST_SIG_COUNT; i++) {
        const KbmGuestSig *s = &kbm_guest_sigs[i];
        if (!kbm_readable(KBM_GPTR(s->addr), sizeof(uint32_t) * KBM_SIG_WORDS)) {
            kbm_log("signature check: %s at %08x is not readable", s->name, (unsigned)s->addr);
            ok = false;
            continue;
        }
        bool match = true;
        for (int w = 0; w < KBM_SIG_WORDS; w++)
            if (kbm_ld32(s->addr + 4u * (uint32_t)w) != s->words[w]) match = false;
        if (!match) {
            kbm_log("signature check: %s at %08x does not match", s->name, (unsigned)s->addr);
            ok = false;
        }
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
