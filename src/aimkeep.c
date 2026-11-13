#include "aimkeep.h"

#include "config.h"
#include "guest.h"
#include "host.h"
#include "hooks.h"
#include "platform.h"

static void detour_kinect(KbmCpu *c);

static KbmHook g_ak_hook = {"betterkeyboard:kinect_synth", KBM_FN_KINECT_SYNTH, detour_kinect, 0, 0, true, false, false};

#define KBM_AK_SIG_WORDS 3

typedef struct AkSig {
    uint32_t addr;
    uint32_t words[KBM_AK_SIG_WORDS];
} AkSig;

static const AkSig k_ak_sigs[] = {
    {KBM_FN_KINECT_SYNTH, {0xe92d48f0u, 0xe24dd008u, 0xe1a04000u}},
    {KBM_FN_ROLE_MASK, {0xe59f001cu, 0xe304146eu, 0xe79f0000u}},
};

static bool ak_sigs_match(void) {
    for (size_t i = 0; i < sizeof k_ak_sigs / sizeof k_ak_sigs[0]; i++) {
        const AkSig *s = &k_ak_sigs[i];
        if (!kbm_readable(KBM_GPTR(s->addr), sizeof(uint32_t) * KBM_AK_SIG_WORDS)) return false;
        for (int w = 0; w < KBM_AK_SIG_WORDS; w++)
            if (kbm_ld32(s->addr + 4u * (uint32_t)w) != s->words[w]) return false;
    }
    return true;
}

static bool sane_pl(uint32_t a) { return a >= 0x00200000u && a < 0xF0000000u && !(a & 3u); }

static bool is_local(uint32_t pl) { return (kbm_ld32(pl + KBM_PL_IS_LOCAL_WORD) & 1u) == 1u; }

static uint32_t ak_call(KbmCpu *c, uint32_t fn, uint32_t r0, uint32_t r1, uint32_t r2) {
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

static bool vfn_ready(uint32_t pl, uint32_t slot, uint32_t *fn) {
    uint32_t vt = kbm_ld32(pl);
    if (!sane_pl(vt)) return false;
    *fn = kbm_ld32(vt + slot);
    return *fn != 0;
}

static bool aim_drawn(KbmCpu *c, uint32_t pl) {
    uint32_t fn;
    if (!vfn_ready(pl, KBM_VF_DRAWN, &fn)) return false;
    return ak_call(c, fn, pl, 0, 0) == 1;
}

static bool aim_state_clear(KbmCpu *c, uint32_t pl) {
    uint32_t fn;
    if (!vfn_ready(pl, KBM_VF_STATE_QUERY, &fn)) return false;
    return ak_call(c, fn, pl, KBM_AK_STATE_BIT, 0) == 0;
}

static bool aim_r_held(KbmCpu *c, uint32_t pl) {
    uint32_t mask = ak_call(c, KBM_FN_ROLE_MASK, 0, 0, 0);
    if (!mask || mask > 0xffffu) return false;
    return (kbm_ld16(pl + KBM_PL_HELD_WORD) & (uint16_t)mask) != 0;
}

static bool aim_mode_hold(uint32_t pl) {
    uint32_t t = kbm_ld32(KBM_AK_GOT_AIMMODE);
    if (!sane_pl(t)) return false;
    uint32_t obj = kbm_ld32(t);
    if (!sane_pl(obj)) return false;
    return kbm_ld8(obj + KBM_AK_AIMMODE_BYTE) != 1;
}

static bool aim_walk_shape(uint32_t pl) {
    if (kbm_ld16(pl + KBM_PL_ACT_ST) != 1) return false;
    uint16_t no = kbm_ld16(pl + KBM_PL_ACT_NO);
    return no == KBM_AK_WALK_A || no == KBM_AK_WALK_B;
}

static bool aim_req_set(uint32_t pl, unsigned act) {
    return (kbm_ld32(pl + KBM_PL_ACT_REQ + 4u * (act >> 5)) >> (act & 31u)) & 1u;
}

static void aim_req_assert(uint32_t pl, unsigned act) {
    uint32_t a = pl + KBM_PL_ACT_REQ + 4u * (act >> 5);
    kbm_st32(a, kbm_ld32(a) | (1u << (act & 31u)));
}

static void detour_kinect(KbmCpu *c) {
    KbmHook *h = &g_ak_hook;
    uint32_t pl = c->r[0];
    bool pre_aim = false;
    if (g_cfg.aim_while_moving && sane_pl(pl) && is_local(pl)) pre_aim = kbm_ld8(pl + KBM_PL_AIM_TOGGLE) != 0;
    if (h->prev) g_host.guest_call(c, h->prev);
    else g_host.fnhook_call_original(c, h->addr);
    if (!g_cfg.aim_while_moving || !sane_pl(pl) || !is_local(pl)) return;
    if (aim_mode_hold(pl)) {
        if (aim_r_held(c, pl) && aim_state_clear(c, pl) && aim_drawn(c, pl) && aim_walk_shape(pl) &&
            !aim_req_set(pl, KBM_AK_ACT_AIM))
            aim_req_assert(pl, KBM_AK_ACT_AIM);
    } else if (pre_aim) {
        if (kbm_ld8(pl + KBM_PL_AIM_TOGGLE) == 0 && aim_drawn(c, pl) &&
            kbm_ld32(pl + KBM_PL_AIM_TIMER) != KBM_AK_TIMER_SET && aim_walk_shape(pl)) {
            kbm_st8(pl + KBM_PL_AIM_TOGGLE, 1);
            aim_req_assert(pl, KBM_AK_ACT_KINECT);
            aim_req_assert(pl, KBM_AK_ACT_AIM);
        }
    }
}

bool kbm_aimkeep_install(void) {
    if (!g_cfg.aim_while_moving) return true;
    if (!g_host.guest_call) return false;
    if (!ak_sigs_match()) return false;
    KbmHook *h = &g_ak_hook;
    h->prev = g_host.fnhook_get(h->addr);
    h->detour = g_host.hle_register(h->name, h->fn);
    if (!h->detour) return false;
    bool set = g_host.fnhook_set(h->addr, h->detour);
    uint32_t back = g_host.fnhook_get(h->addr);
    if (!set || back != h->detour) {
        if (back == h->detour) g_host.fnhook_set(h->addr, h->prev);
        return false;
    }
    h->installed = true;
    return true;
}

void kbm_aimkeep_remove(void) {
    KbmHook *h = &g_ak_hook;
    if (!h->installed) return;
    uint32_t cur = g_host.fnhook_get(h->addr);
    if (cur == h->detour) {
        bool ok = g_host.fnhook_set(h->addr, h->prev);
        uint32_t back = g_host.fnhook_get(h->addr);
        if (!ok || back != h->prev) h->passthrough = true;
    } else {
        h->passthrough = true;
    }
    h->installed = false;
}
