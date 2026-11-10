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
#define FIRST_PRESS_LOGS 40

typedef struct Inject {
    uint32_t left;
    uint32_t delay;
    bool first;
} Inject;

static bool g_enabled;
static KbmBind g_binds[KBM_IK_KEYS];
static KbmIkRing g_ring;
static KbmIkState g_state;
static KbmItemStats g_stats;
static Inject g_inject;
static bool g_down[KBM_IK_KEYS];
static KbmThread g_thread;
static volatile int g_stop;
static bool g_thread_on;
static uint32_t g_last_held = ~0u;
static bool g_warned_unconfigured;
static bool g_warned_spad;
static uint32_t g_press_logs;

static const char *const k_port_binds[] = {"MoveUp", "MoveDown", "MoveLeft", "MoveRight", "Walk", "A", "B", "X", "Y", "L", "R", "ZL",
    "ZR", "Plus", "Minus", "DpadUp", "DpadDown", "DpadLeft", "DpadRight", "LeftStickClick", "RightStickClick", "ItemLeft", "ItemRight"};

bool kbm_itemkeys_enabled(void) { return g_enabled; }
const KbmItemStats *kbm_itemkeys_stats(void) { return &g_stats; }

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

static void conflict_check(KbmSettingFn setting) {
    if (!setting) return;
    for (size_t i = 0; i < sizeof k_port_binds / sizeof k_port_binds[0]; i++) {
        char text[KBM_KEY_TEXT];
        if (!setting(k_port_binds[i], text, sizeof text)) continue;
        KbmBind other;
        memset(&other, 0, sizeof other);
        if (!kbm_bind_parse(&other, k_port_binds[i], text)) continue;
        for (int k = 0; k < KBM_IK_KEYS; k++) {
            for (int r = 0; r < g_binds[k].n; r++) {
                if (g_binds[k].refs[r].code && g_binds[k].refs[r].kind == 1 && kbm_bind_has_scancode(&other, g_binds[k].refs[r].code))
                    KBM_LOG(KBM_LOG_WARN, "item key %d (\"%s\") is also the port's %s bind (\"%s\"): both will act", k + 1,
                        g_cfg.ik_key_text[k], k_port_binds[i], text);
            }
        }
    }
}

bool kbm_itemkeys_init(KbmSettingFn setting) {
    g_enabled = false;
    memset(&g_stats, 0, sizeof g_stats);
    memset(g_down, 0, sizeof g_down);
    memset(&g_inject, 0, sizeof g_inject);
    kbm_ik_ring_init(&g_ring);
    kbm_ik_state_init(&g_state);
    g_last_held = ~0u;
    g_warned_unconfigured = false;
    g_warned_spad = false;
    g_press_logs = 0;
    if (!g_cfg.item_hotkeys) {
        KBM_LOG(KBM_LOG_INFO, "item hotkeys: turned off by the config (ItemHotkeys = 0)");
        return false;
    }
    if (!g_host.guest_call) {
        KBM_LOG(KBM_LOG_ERROR, "item hotkeys: unavailable, the port does not export guest_call");
        return false;
    }
    int usable = 0;
    for (int i = 0; i < KBM_IK_KEYS; i++) {
        char label[16];
        snprintf(label, sizeof label, "item%d", i + 1);
        memset(&g_binds[i], 0, sizeof g_binds[i]);
        bool ok = kbm_bind_parse(&g_binds[i], label, g_cfg.ik_key_text[i]);
        usable += ok;
        KBM_LOG(KBM_LOG_INFO, "item key %-2d = \"%s\" -> pouch slot %d: %d key(s)%s", i + 1, g_cfg.ik_key_text[i], g_cfg.ik_slot[i] + 1,
            g_binds[i].n, ok ? "" : " (unusable)");
    }
    if (!usable) {
        KBM_LOG(KBM_LOG_ERROR, "item hotkeys: none of the 12 keys could be resolved");
        return false;
    }
    conflict_check(setting);
    if (!g_cfg.ik_use_mask)
        KBM_LOG(KBM_LOG_INFO, "item hotkeys: UsePadMask is not set, so a double press selects but cannot use yet (see README, ProbeInput)");
    g_enabled = true;
    return true;
}

void kbm_itemkeys_push(int key, uint32_t t_ms) {
    if (!kbm_ik_ring_push(&g_ring, key, t_ms)) g_stats.ring_full++;
}

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
    if (!g_enabled || !g_cfg.ik_sampler_ms) {
        if (g_enabled) KBM_LOG(KBM_LOG_INFO, "item hotkeys: key sampler not started (SamplerMs = 0)");
        return true;
    }
    __atomic_store_n(&g_stop, 0, __ATOMIC_RELEASE);
    g_thread_on = kbm_thread_start(&g_thread, sampler, NULL);
    if (!g_thread_on) {
        KBM_LOG(KBM_LOG_ERROR, "item hotkeys: could not start the key sampler thread");
        g_enabled = false;
        return false;
    }
    KBM_LOG(KBM_LOG_INFO, "item hotkeys: key sampler running every %u ms", (unsigned)g_cfg.ik_sampler_ms);
    return true;
}

void kbm_itemkeys_stop(void) {
    if (!g_thread_on) return;
    __atomic_store_n(&g_stop, 1, __ATOMIC_RELEASE);
    if (!kbm_thread_join(&g_thread, 3000)) KBM_LOG(KBM_LOG_ERROR, "item hotkeys: the key sampler thread did not stop in time");
    else KBM_LOG(KBM_LOG_INFO, "item hotkeys: key sampler stopped");
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
    if (!g_enabled || (!g_cfg.probe_input && !g_inject.left)) return;
    if (!sane_player(pl) || !is_local(pl)) return;
    uint32_t spad = read_spad();
    if (!spad) {
        if (!g_warned_spad) KBM_LOG(KBM_LOG_ERROR, "item hotkeys: the game's pad object could not be found (pointer chain at %08x)", KBM_GOT_SPAD);
        g_warned_spad = true;
        g_inject.left = 0;
        return;
    }
    uint32_t held = kbm_ld32(spad + KBM_SPAD_HELD);
    if (g_cfg.probe_input && held != g_last_held) {
        KBM_LOG(KBM_LOG_INFO, "probe: pad buttons %08x -> %08x (pressed %08x, released %08x), game's new-press word %08x",
            g_last_held == ~0u ? 0u : g_last_held, held, held & ~(g_last_held == ~0u ? 0u : g_last_held),
            (g_last_held == ~0u ? 0u : g_last_held) & ~held, kbm_ld32(spad + KBM_SPAD_NEW));
        g_last_held = held;
    }
    if (g_inject.left) {
        if (g_inject.delay) {
            g_inject.delay--;
            return;
        }
        kbm_st32(spad + KBM_SPAD_HELD, held | g_cfg.ik_use_mask);
        if (g_inject.first) kbm_st32(spad + KBM_SPAD_NEW, kbm_ld32(spad + KBM_SPAD_NEW) | g_cfg.ik_use_mask);
        g_inject.first = false;
        g_inject.left--;
        g_stats.injected_updates++;
        KBM_LOG(KBM_LOG_TRACE, "use: pad mask %08x injected (%u update(s) left)", g_cfg.ik_use_mask, (unsigned)g_inject.left);
    }
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
    const char *why;
} Pick;

static Pick pick_slot(KbmCpu *c, uint32_t pl, int slot, bool quiet_probe) {
    (void)quiet_probe;
    Pick p = {KBM_ITEM_NO_ITEM, 0, 0, NULL};
    uint32_t entry = gcall(c, KBM_FN_POUCH_ENTRY, pl, (uint32_t)slot, 0);
    if (entry < MIN_DATA_ADDR || entry >= MAX_PLAYER_ADDR) {
        p.why = "the slot is not available";
        return p;
    }
    p.id = kbm_ld16(entry);
    p.qty = (int16_t)kbm_ld16(entry + 2);
    if (p.id == 0 || p.qty <= 0) {
        p.why = "the slot is empty";
        return p;
    }
    if ((gcall(c, KBM_FN_ITEM_USABLE, 0, p.id, 0) & 0xffu) == 0) {
        p.why = "the item is not usable on the item bar";
        return p;
    }
    p.slot = (uint32_t)slot;
    return p;
}

static void dump_pouch(KbmCpu *c, uint32_t pl) {
    if (kbm_log_level() < KBM_LOG_DEBUG) return;
    for (int s = 0; s < KBM_POUCH_ITEM_SLOTS; s++) {
        uint32_t entry = gcall(c, KBM_FN_POUCH_ENTRY, pl, (uint32_t)s, 0);
        if (entry < MIN_DATA_ADDR || entry >= MAX_PLAYER_ADDR) continue;
        uint16_t id = kbm_ld16(entry);
        int qty = (int16_t)kbm_ld16(entry + 2);
        int usable = id && qty > 0 ? (int)(gcall(c, KBM_FN_ITEM_USABLE, 0, id, 0) & 0xffu) != 0 : 0;
        KBM_LOG(KBM_LOG_DEBUG, "pouch slot %2d: item %4u x%-3d %s", s + 1, id, qty, usable ? "usable" : (id && qty > 0 ? "NOT usable on the bar" : "empty"));
    }
}

static void do_select(KbmCpu *c, uint32_t pl, int key, KbmIkKind kind, bool *ok_item) {
    *ok_item = false;
    int slot = g_cfg.ik_slot[key];
    if (!may_pick(c, pl)) {
        g_stats.blocked_busy++;
        KBM_LOG(KBM_LOG_DEBUG, "item key %d: ignored, the hunter cannot pick an item right now", key + 1);
        return;
    }
    dump_pouch(c, pl);
    Pick p = pick_slot(c, pl, slot, false);
    uint16_t cur = kbm_ld16(pl + KBM_PL_ITEM_SLOT);
    if (cur != p.slot) {
        kbm_st16(pl + KBM_PL_ITEM_SLOT, (uint16_t)p.slot);
        kbm_st32(pl + KBM_PL_ITEM_FLAGS, kbm_ld32(pl + KBM_PL_ITEM_FLAGS) | (p.slot > cur ? 2u : 4u));
    }
    g_stats.selects++;
    KbmLogLevel lv = ++g_press_logs <= FIRST_PRESS_LOGS ? KBM_LOG_INFO : KBM_LOG_DEBUG;
    if (p.slot == KBM_ITEM_NO_ITEM) {
        g_stats.selected_no_item++;
        KBM_LOG(lv, "item key %d (pouch slot %d): %s -> bar set to No Item (was %u) [%s]", key + 1, slot + 1, p.why, cur,
            kind == KBM_IK_USE ? "double press" : "press");
    } else {
        g_stats.selected_item++;
        *ok_item = true;
        KBM_LOG(lv, "item key %d (pouch slot %d): item %u x%d -> bar selected slot %u (was %u) [%s]", key + 1, slot + 1, p.id, p.qty,
            (unsigned)p.slot, cur, kind == KBM_IK_USE ? "double press" : "press");
    }
}

static void start_use(int key) {
    if (!g_cfg.ik_use_mask) {
        g_stats.use_unconfigured++;
        if (!g_warned_unconfigured) {
            g_warned_unconfigured = true;
            KBM_LOG(KBM_LOG_WARN,
                "item key %d: double press asked to use the item, but UsePadMask is not set. Set ProbeInput = 1, press the in-game "
                "use button once, and put the 'pressed' value from the log into UsePadMask",
                key + 1);
        }
        return;
    }
    g_inject.left = g_cfg.ik_use_hold;
    g_inject.delay = g_cfg.ik_use_delay;
    g_inject.first = true;
    g_stats.uses++;
    KBM_LOG(KBM_LOG_INFO, "item key %d: use requested, pad mask %08x will be pressed for %u update(s)", key + 1, g_cfg.ik_use_mask,
        (unsigned)g_cfg.ik_use_hold);
}

void kbm_itemkeys_after_input(KbmCpu *c, uint32_t pl, const char *gate) {
    if (!g_enabled) return;
    KbmIkEvent e;
    const uint32_t now = (uint32_t)kbm_now_ms();
    if (gate) g_inject.left = 0;
    while (kbm_ik_ring_pop(&g_ring, &e)) {
        g_stats.presses++;
        if ((uint32_t)(now - e.t_ms) > MAX_EVENT_AGE_MS) {
            g_stats.dropped_old++;
            continue;
        }
        if (gate) {
            g_stats.dropped_gate++;
            continue;
        }
        if (e.key >= KBM_IK_KEYS) continue;
        KbmIkKind kind = kbm_ik_press(&g_state, e.key, e.t_ms, g_cfg.ik_double_ms);
        bool item = false;
        do_select(c, pl, e.key, kind, &item);
        if (kind == KBM_IK_USE) {
            if (item) start_use(e.key);
            else {
                g_stats.use_nothing++;
                KBM_LOG(KBM_LOG_INFO, "item key %d: double press, but there is nothing to use in that slot", e.key + 1);
            }
        }
    }
}

void kbm_itemkeys_log_stats(KbmLogLevel level, const char *why) {
    const KbmItemStats *s = &g_stats;
    KBM_LOG(level,
        "stats (%s): item keys presses=%llu dropped_old=%llu dropped_gate=%llu ring_full=%llu selects=%llu item=%llu no_item=%llu busy=%llu uses=%llu "
        "use_nothing=%llu use_unconfigured=%llu injected_updates=%llu",
        why, (unsigned long long)s->presses, (unsigned long long)s->dropped_old, (unsigned long long)s->dropped_gate,
        (unsigned long long)s->ring_full, (unsigned long long)s->selects, (unsigned long long)s->selected_item,
        (unsigned long long)s->selected_no_item, (unsigned long long)s->blocked_busy, (unsigned long long)s->uses,
        (unsigned long long)s->use_nothing, (unsigned long long)s->use_unconfigured, (unsigned long long)s->injected_updates);
}
