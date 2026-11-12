#include "itemkeys.h"

#include <stdio.h>
#include <string.h>

#include "config.h"
#include "guest.h"
#include "ikcore.h"
#include "platform.h"

#define MAX_EVENT_AGE_MS 600
#define MAX_PLAYER_ADDR 0xF0000000u
#define MIN_DATA_ADDR 0x00200000u

typedef struct Inject {
    uint32_t left;
    uint32_t delay;
    bool first;
} Inject;

static bool g_enabled;
static KbmBind g_binds[KBM_IK_KEYS];
static KbmIkRing g_ring;
static KbmIkState g_state;
static Inject g_inject;
static bool g_down[KBM_IK_KEYS];
static KbmThread g_thread;
static volatile int g_stop;
static bool g_thread_on;



static bool is_local(uint32_t pl) { return (kbm_ld32(pl + KBM_PL_IS_LOCAL_WORD) & 1u) == 1u; }
static bool sane_player(uint32_t a) { return a >= MIN_DATA_ADDR && a < MAX_PLAYER_ADDR && !(a & 3); }

static uint32_t gcall(KbmCpu *c, uint32_t fn, uint32_t r0, uint32_t r1, uint32_t r2) {
    uint32_t saved[16];
    for (int i = 0; i < 16; i++) saved[i] = c->r[i];
    c->r[0] = r0;
    c->r[1] = r1;
    c->r[2] = r2;
    g_host.guest_call(c, fn);
    uint32_t v = c->r[0];
    for (int i = 0; i < 16; i++) c->r[i] = saved[i];
    return v;
}

bool kbm_itemkeys_init(void) {
    g_enabled = false;
    memset(g_down, 0, sizeof g_down);
    memset(&g_inject, 0, sizeof g_inject);
    kbm_ik_ring_init(&g_ring);
    kbm_ik_state_init(&g_state);
    if (!g_cfg.item_hotkeys) return false;
    if (!g_host.guest_call) return false;
    int usable = 0;
    for (int i = 0; i < KBM_IK_KEYS; i++) {
        char label[16];
        snprintf(label, sizeof label, "item%d", i + 1);
        memset(&g_binds[i], 0, sizeof g_binds[i]);
        usable += kbm_bind_parse(&g_binds[i], label, g_cfg.ik_key_text[i]);
    }
    if (!usable) return false;
    g_enabled = true;
    return true;
}

void kbm_itemkeys_push(int key, uint32_t t_ms) { kbm_ik_ring_push(&g_ring, key, t_ms); }

static void sampler(void *unused) {
    (void)unused;
    while (!__atomic_load_n(&g_stop, __ATOMIC_ACQUIRE)) {
        int n = 0;
        const bool *ks = g_host.sdl_keyboard_state ? g_host.sdl_keyboard_state(&n) : NULL;
        uint32_t mouse = 0;
        bool need_mouse = false;
        for (int i = 0; i < KBM_IK_KEYS; i++) need_mouse |= kbm_bind_uses_mouse(&g_binds[i]);
        if (need_mouse && g_host.sdl_mouse_state) mouse = g_host.sdl_mouse_state(NULL, NULL);
        if (ks) {
            uint32_t now = (uint32_t)kbm_now_ms();
            for (int i = 0; i < KBM_IK_KEYS; i++) {
                bool d = kbm_bind_down(&g_binds[i], ks, n, mouse);
                if (d && !g_down[i]) kbm_itemkeys_push(i, now);
                g_down[i] = d;
            }
        }
        kbm_sleep_ms(g_cfg.ik_sampler_ms ? g_cfg.ik_sampler_ms : 4);
    }
}

bool kbm_itemkeys_start(void) {
    if (!g_enabled || !g_cfg.ik_sampler_ms) return true;
    __atomic_store_n(&g_stop, 0, __ATOMIC_RELEASE);
    g_thread_on = kbm_thread_start(&g_thread, sampler, NULL);
    if (!g_thread_on) {
        g_enabled = false;
        return false;
    }
    return true;
}

void kbm_itemkeys_stop(void) {
    if (!g_thread_on) return;
    __atomic_store_n(&g_stop, 1, __ATOMIC_RELEASE);
    kbm_thread_join(&g_thread, 3000);
    g_thread_on = false;
}

static uint32_t read_spad(void) {
    uint32_t p = kbm_ld32(KBM_GOT_SPAD);
    if (!sane_player(p)) return 0;
    uint32_t spad = kbm_ld32(p);
    return sane_player(spad) ? spad : 0;
}

void kbm_itemkeys_before_input(KbmCpu *c, uint32_t pl) {
    (void)c;
    if (!g_enabled || !g_inject.left) return;
    if (!sane_player(pl) || !is_local(pl)) return;
    uint32_t spad = read_spad();
    if (!spad) {
        g_inject.left = 0;
        return;
    }
    uint32_t held = kbm_ld32(spad + KBM_SPAD_HELD);
    if (g_inject.delay) {
        g_inject.delay--;
        return;
    }
    kbm_st32(spad + KBM_SPAD_HELD, held | g_cfg.ik_use_mask);
    if (g_inject.first) kbm_st32(spad + KBM_SPAD_NEW, kbm_ld32(spad + KBM_SPAD_NEW) | g_cfg.ik_use_mask);
    g_inject.first = false;
    g_inject.left--;
}

static bool may_pick(KbmCpu *c, uint32_t pl) {
    if (gcall(c, KBM_FN_PL_BUSY, pl, 0, 0) != 0) return false;
    uint32_t vt = kbm_ld32(pl);
    uint32_t can_select = vt >= MIN_DATA_ADDR ? kbm_ld32(vt + KBM_VF_CAN_SELECT) : 0;
    return can_select && gcall(c, can_select, pl, 1, 0) == 1;
}

typedef struct Pick {
    uint32_t slot;
    uint16_t id;
    int qty;
} Pick;

static Pick pick_slot(KbmCpu *c, uint32_t pl, int slot) {
    Pick p = {KBM_ITEM_NO_ITEM, 0, 0};
    uint32_t entry = gcall(c, KBM_FN_POUCH_ENTRY, pl, (uint32_t)slot, 0);
    if (entry < MIN_DATA_ADDR || entry >= MAX_PLAYER_ADDR) return p;
    p.id = kbm_ld16(entry);
    p.qty = (int16_t)kbm_ld16(entry + 2);
    if (p.id == 0 || p.qty <= 0) return p;
    if ((gcall(c, KBM_FN_ITEM_USABLE, 0, p.id, 0) & 0xffu) == 0) return p;
    p.slot = (uint32_t)slot;
    return p;
}

static void do_select(KbmCpu *c, uint32_t pl, int key, bool *ok_item) {
    *ok_item = false;
    int slot = g_cfg.ik_slot[key];
    if (!may_pick(c, pl)) return;
    Pick p = pick_slot(c, pl, slot);
    uint16_t cur = kbm_ld16(pl + KBM_PL_ITEM_SLOT);
    if (cur != p.slot) {
        kbm_st16(pl + KBM_PL_ITEM_SLOT, (uint16_t)p.slot);
        kbm_st32(pl + KBM_PL_ITEM_FLAGS, kbm_ld32(pl + KBM_PL_ITEM_FLAGS) | (p.slot > cur ? 2u : 4u));
    }
    if (p.slot != KBM_ITEM_NO_ITEM) *ok_item = true;
}

static void start_use(void) {
    if (!g_cfg.ik_use_mask) return;
    g_inject.left = g_cfg.ik_use_hold;
    g_inject.delay = g_cfg.ik_use_delay;
    g_inject.first = true;
}

void kbm_itemkeys_after_input(KbmCpu *c, uint32_t pl, const char *gate) {
    if (!g_enabled) return;
    KbmIkEvent e;
    const uint32_t now = (uint32_t)kbm_now_ms();
    if (gate) g_inject.left = 0;
    while (kbm_ik_ring_pop(&g_ring, &e)) {
        if ((uint32_t)(now - e.t_ms) > MAX_EVENT_AGE_MS) continue;
        if (gate) continue;
        if (e.key >= KBM_IK_KEYS) continue;
        KbmIkKind kind = kbm_ik_press(&g_state, e.key, e.t_ms, g_cfg.ik_double_ms);
        bool item = false;
        do_select(c, pl, e.key, &item);
        if (kind == KBM_IK_USE && item) start_use();
    }
}
