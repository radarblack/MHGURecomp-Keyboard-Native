#include "host.h"

#include <stdio.h>
#include <string.h>

#include "guest.h"
#include "log.h"
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

static bool report(const char *name, void *p, bool required, const char *why) {
    if (p) {
        KBM_LOG(KBM_LOG_DEBUG, "symbol %-24s found at %p", name, p);
        return true;
    }
    KBM_LOG(required ? KBM_LOG_ERROR : KBM_LOG_WARN, "symbol %-24s MISSING (%s)%s%s", name, required ? "required" : "optional",
        why ? ": " : "", why ? why : "");
    return false;
}

bool kbm_host_resolve(bool require_gates) {
    memset(&g_host, 0, sizeof g_host);
    bool ok = true;

    g_host.fnhook_set = (bool (*)(uint32_t, uint32_t))find_port("fnhook_set");
    ok &= report("fnhook_set", (void *)g_host.fnhook_set, true, "the port's function hook table");
    g_host.fnhook_get = (uint32_t (*)(uint32_t))find_port("fnhook_get");
    ok &= report("fnhook_get", (void *)g_host.fnhook_get, true, NULL);
    g_host.fnhook_call_original = (void (*)(KbmCpu *, uint32_t))find_port("fnhook_call_original");
    ok &= report("fnhook_call_original", (void *)g_host.fnhook_call_original, true, NULL);
    g_host.hle_register = (uint32_t (*)(const char *, KbmHostFn))find_port("hle_register");
    ok &= report("hle_register", (void *)g_host.hle_register, true, NULL);
    g_host.guest_call = (void (*)(KbmCpu *, uint32_t))find_port("guest_call");
    ok &= report("guest_call", (void *)g_host.guest_call, false, "only needed by later features");

    g_host.textinput_active = (bool (*)(void))find_port("textinput_active");
    ok &= report("textinput_active", (void *)g_host.textinput_active, require_gates, "typing gate");
    g_host.chat_active = (bool (*)(void))find_port("chat_active");
    ok &= report("chat_active", (void *)g_host.chat_active, require_gates, "chat gate");
    g_host.overlay_menu_open = (bool (*)(void))find_port("overlay_menu_open");
    report("overlay_menu_open", (void *)g_host.overlay_menu_open, false, "not exported by every build of the port");

    g_host.sdl_keyboard_state = (const bool *(*)(int *))find_sdl("SDL_GetKeyboardState");
    ok &= report("SDL_GetKeyboardState", (void *)g_host.sdl_keyboard_state, true, "keyboard state");
    g_host.sdl_scancode_from_name = (int (*)(const char *))find_sdl("SDL_GetScancodeFromName");
    ok &= report("SDL_GetScancodeFromName", (void *)g_host.sdl_scancode_from_name, true, "key names from the port's ini");
    g_host.sdl_mouse_state = (uint32_t (*)(float *, float *))find_sdl("SDL_GetMouseState");
    report("SDL_GetMouseState", (void *)g_host.sdl_mouse_state, false, "mouse buttons cannot be used for movement");
    g_host.sdl_keyboard_focus = (void *(*)(void))find_sdl("SDL_GetKeyboardFocus");
    report("SDL_GetKeyboardFocus", (void *)g_host.sdl_keyboard_focus, false, "part of the menu gate fallback");
    g_host.sdl_text_input_active = (bool (*)(void *))find_sdl("SDL_TextInputActive");
    report("SDL_TextInputActive", (void *)g_host.sdl_text_input_active, false, "part of the menu gate fallback");

    if (g_host.overlay_menu_open) {
        KBM_LOG(KBM_LOG_INFO, "menu gate: the port's overlay_menu_open()");
    } else if (g_host.sdl_keyboard_focus && g_host.sdl_text_input_active) {
        KBM_LOG(KBM_LOG_INFO,
            "menu gate: overlay_menu_open is not exported by this build, using SDL's text-input state instead (the port turns it on "
            "whenever Forge's menu or the game's text entry is open)");
    } else if (require_gates) {
        KBM_LOG(KBM_LOG_ERROR, "menu gate: no way to tell that Forge's menu is open (set RequireGates = 0 to run without it)");
        ok = false;
    } else {
        KBM_LOG(KBM_LOG_WARN, "menu gate: unavailable, movement keys will also act while Forge's menu is open");
    }
    return ok;
}

bool kbm_host_menu_gate_available(void) {
    return g_host.overlay_menu_open || (g_host.sdl_keyboard_focus && g_host.sdl_text_input_active);
}

bool kbm_guest_verify(void) {
    bool ok = true;
    for (size_t i = 0; i < KBM_GUEST_SIG_COUNT; i++) {
        const KbmGuestSig *s = &kbm_guest_sigs[i];
        if (!kbm_readable(KBM_GPTR(s->addr), sizeof(uint32_t) * KBM_SIG_WORDS)) {
            KBM_LOG(KBM_LOG_ERROR, "guest %08x (%s): memory not readable at the expected guest base", s->addr, s->name);
            ok = false;
            continue;
        }
        bool same = true;
        uint32_t got[KBM_SIG_WORDS];
        for (int w = 0; w < KBM_SIG_WORDS; w++) {
            got[w] = kbm_ld32(s->addr + 4u * (uint32_t)w);
            same &= got[w] == s->words[w];
        }
        if (same) {
            KBM_LOG(KBM_LOG_INFO, "guest %08x (%s): signature ok", s->addr, s->name);
        } else {
            KBM_LOG(KBM_LOG_ERROR, "guest %08x (%s): signature MISMATCH, expected %08x %08x %08x, found %08x %08x %08x", s->addr,
                s->name, s->words[0], s->words[1], s->words[2], got[0], got[1], got[2]);
            ok = false;
        }
    }
    return ok;
}
