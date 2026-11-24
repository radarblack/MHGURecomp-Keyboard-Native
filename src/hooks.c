#include "hooks.h"

#include "config.h"
#include "driver.h"
#include "guest.h"
#include "host.h"

static void detour_input(KbmCpu *c);
static void detour_input_alt(KbmCpu *c);
static void detour_face(KbmCpu *c);

KbmHook g_hooks[KBM_H_COUNT] = {
    {"keyboard_movement:player_input", KBM_FN_PL_INPUT_UPDATE, detour_input, 0, 0, true, false, false},
    {"keyboard_movement:player_input_alt", KBM_FN_PL_INPUT_UPDATE_ALT, detour_input_alt, 0, 0, true, false, false},
    {"keyboard_movement:face_stick", KBM_FN_FACE_STICK, detour_face, 0, 0, true, false, false},
};

static void call_next(KbmCpu *c, KbmHook *h) {
    if (h->prev) g_host.guest_call(c, h->prev);
    else g_host.fnhook_call_original(c, h->addr);
}

static void detour_input(KbmCpu *c) {
    KbmHook *h = &g_hooks[KBM_H_INPUT];
    uint32_t player = c->r[0];
    if (!h->passthrough) kbm_driver_before_input(c, player, false);
    call_next(c, h);
    if (!h->passthrough) kbm_driver_on_input(c, player, false);
}

static void detour_input_alt(KbmCpu *c) {
    KbmHook *h = &g_hooks[KBM_H_INPUT_ALT];
    uint32_t player = c->r[0];
    call_next(c, h);
    if (!h->passthrough) kbm_driver_on_input(c, player, true);
}

static void detour_face(KbmCpu *c) {
    KbmHook *h = &g_hooks[KBM_H_FACE];
    uint32_t player = c->r[0];
    call_next(c, h);
    if (!h->passthrough) kbm_driver_on_face(player);
}

static bool install_one(KbmHook *h) {
    h->prev = g_host.fnhook_get(h->addr);
    if (h->prev && !g_host.guest_call) {
        kbm_log("hook %s: %08x is already hooked and the port has no guest_call to chain to it", h->name, (unsigned)h->addr);
        return false;
    }
    if (h->prev) kbm_log("hook %s: chaining to an existing hook at %08x", h->name, (unsigned)h->addr);
    h->detour = g_host.hle_register(h->name, h->fn);
    if (!h->detour) {
        kbm_log("hook %s: hle_register failed", h->name);
        return false;
    }

    bool set = g_host.fnhook_set(h->addr, h->detour);
    uint32_t back = g_host.fnhook_get(h->addr);
    if (!set || back != h->detour) {
        if (back == h->detour) g_host.fnhook_set(h->addr, h->prev);
        kbm_log("hook %s: fnhook_set at %08x failed", h->name, (unsigned)h->addr);
        return false;
    }
    h->installed = true;
    kbm_log("hook %s: installed at %08x", h->name, (unsigned)h->addr);
    return true;
}

bool kbm_hooks_install(void) {
    for (int i = 0; i < KBM_H_COUNT; i++) {
        g_hooks[i].enabled = i != KBM_H_INPUT_ALT || g_cfg.safe_area_hook;
        g_hooks[i].detour = 0;
        g_hooks[i].prev = 0;
        g_hooks[i].installed = false;
        g_hooks[i].passthrough = false;
    }
    for (int i = 0; i < KBM_H_COUNT; i++) {
        if (!g_hooks[i].enabled) {
            kbm_log("hook %s: skipped (SafeAreaHook = 0)", g_hooks[i].name);
            continue;
        }
        if (!install_one(&g_hooks[i])) {
            kbm_hooks_remove();
            return false;
        }
    }
    return true;
}

static void remove_one(KbmHook *h) {
    if (!h->installed) return;
    uint32_t cur = g_host.fnhook_get(h->addr);
    if (cur == h->detour) {
        bool ok = g_host.fnhook_set(h->addr, h->prev);
        uint32_t back = g_host.fnhook_get(h->addr);
        if (!ok || back != h->prev) {
            h->passthrough = true;
            kbm_log("hook %s: could not be removed, it now passes calls straight through", h->name);
        } else {
            kbm_log("hook %s: removed", h->name);
        }
    } else {
        h->passthrough = true;
        kbm_log("hook %s: another hook was placed on top of it, it now passes calls straight through", h->name);
    }
    h->installed = false;
}

void kbm_hooks_remove(void) {
    for (int i = KBM_H_COUNT - 1; i >= 0; i--) remove_one(&g_hooks[i]);
}
