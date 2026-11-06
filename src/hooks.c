#include "hooks.h"

#include "driver.h"
#include "guest.h"
#include "log.h"

static void detour_input(KbmCpu *c);
static void detour_face(KbmCpu *c);

KbmHook g_hooks[KBM_H_COUNT] = {
    {"keyboard_movement:player_input", KBM_FN_PL_INPUT_UPDATE, detour_input, 0, 0, false, false, false, 0},
    {"keyboard_movement:face_stick", KBM_FN_FACE_STICK, detour_face, 0, 0, false, false, false, 0},
};

static void call_next(KbmCpu *c, KbmHook *h) {
    if (h->prev) g_host.guest_call(c, h->prev);
    else g_host.fnhook_call_original(c, h->addr);
}

static void note_call(KbmHook *h, uint32_t player) {
    h->calls++;
    if (!h->alive_logged) {
        h->alive_logged = true;
        KBM_LOG(KBM_LOG_INFO, "hook %s: first call arrived (player %08x): the hook is live", h->name, player);
    }
}

static void detour_input(KbmCpu *c) {
    KbmHook *h = &g_hooks[KBM_H_INPUT];
    uint32_t player = c->r[0];
    note_call(h, player);
    call_next(c, h);
    if (!h->passthrough) kbm_driver_on_input(player);
}

static void detour_face(KbmCpu *c) {
    KbmHook *h = &g_hooks[KBM_H_FACE];
    uint32_t player = c->r[0];
    note_call(h, player);
    call_next(c, h);
    if (!h->passthrough) kbm_driver_on_face(player);
}

static bool install_one(KbmHook *h) {
    h->prev = g_host.fnhook_get(h->addr);
    if (h->prev) {
        KBM_LOG(KBM_LOG_WARN, "hook %s: %08x already has a detour (%08x), chaining after it", h->name, h->addr, h->prev);
        if (!g_host.guest_call) {
            KBM_LOG(KBM_LOG_ERROR, "hook %s: cannot chain without guest_call", h->name);
            return false;
        }
    } else {
        KBM_LOG(KBM_LOG_DEBUG, "hook %s: %08x has no detour yet", h->name, h->addr);
    }
    h->detour = g_host.hle_register(h->name, h->fn);
    if (!h->detour) {
        KBM_LOG(KBM_LOG_ERROR, "hook %s: hle_register returned no address", h->name);
        return false;
    }
    KBM_LOG(KBM_LOG_DEBUG, "hook %s: detour registered at %08x", h->name, h->detour);

    bool set = g_host.fnhook_set(h->addr, h->detour);
    uint32_t back = g_host.fnhook_get(h->addr);
    if (!set || back != h->detour) {
        KBM_LOG(KBM_LOG_ERROR, "hook %s: fnhook_set(%08x) returned %d and the table reads back %08x (wanted %08x)", h->name,
            h->addr, set, back, h->detour);
        if (back == h->detour) g_host.fnhook_set(h->addr, h->prev);
        return false;
    }
    h->installed = true;
    KBM_LOG(KBM_LOG_INFO, "hook %s: installed at %08x, detour %08x, previous %08x, readback verified", h->name, h->addr, h->detour,
        h->prev);
    return true;
}

bool kbm_hooks_install(void) {
    for (int i = 0; i < KBM_H_COUNT; i++) {
        g_hooks[i].detour = 0;
        g_hooks[i].prev = 0;
        g_hooks[i].installed = false;
        g_hooks[i].passthrough = false;
        g_hooks[i].alive_logged = false;
        g_hooks[i].calls = 0;
    }
    for (int i = 0; i < KBM_H_COUNT; i++) {
        if (!install_one(&g_hooks[i])) {
            KBM_LOG(KBM_LOG_ERROR, "hook install failed at %s: removing the ones already installed", g_hooks[i].name);
            kbm_hooks_remove();
            return false;
        }
    }
    return true;
}

static void remove_one(KbmHook *h) {
    if (!h->installed) {
        KBM_LOG(KBM_LOG_DEBUG, "hook %s: not installed, nothing to remove (calls seen: %llu)", h->name,
            (unsigned long long)h->calls);
        return;
    }
    uint32_t cur = g_host.fnhook_get(h->addr);
    if (cur == h->detour) {
        bool ok = g_host.fnhook_set(h->addr, h->prev);
        uint32_t back = g_host.fnhook_get(h->addr);
        if (ok && back == h->prev) {
            KBM_LOG(KBM_LOG_INFO, "hook %s: removed from %08x, %s restored, readback verified (calls seen: %llu)", h->name, h->addr,
                h->prev ? "previous detour" : "original function", (unsigned long long)h->calls);
        } else {
            KBM_LOG(KBM_LOG_ERROR, "hook %s: removal at %08x FAILED: set returned %d, table reads back %08x, wanted %08x", h->name,
                h->addr, ok, back, h->prev);
            h->passthrough = true;
        }
    } else {
        h->passthrough = true;
        KBM_LOG(KBM_LOG_WARN, "hook %s: %08x now points to %08x (another hook went in after ours): left in place, ours passes through",
            h->name, h->addr, cur);
    }
    h->installed = false;
}

void kbm_hooks_remove(void) {
    for (int i = KBM_H_COUNT - 1; i >= 0; i--) remove_one(&g_hooks[i]);
}

int kbm_hooks_installed_count(void) {
    int n = 0;
    for (int i = 0; i < KBM_H_COUNT; i++) n += g_hooks[i].installed;
    return n;
}
