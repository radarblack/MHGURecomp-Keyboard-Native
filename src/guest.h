#pragma once
#include <stdint.h>
#include <string.h>

#define KBM_GUEST_BASE 0x10000000000ull
#define KBM_GPTR(a) ((void *)(uintptr_t)(KBM_GUEST_BASE + (uint32_t)(a)))

static inline uint16_t kbm_ld16(uint32_t a) {
    uint16_t v;
    memcpy(&v, KBM_GPTR(a), sizeof v);
    return v;
}

static inline uint32_t kbm_ld32(uint32_t a) {
    uint32_t v;
    memcpy(&v, KBM_GPTR(a), sizeof v);
    return v;
}

static inline void kbm_st16(uint32_t a, uint16_t v) { memcpy(KBM_GPTR(a), &v, sizeof v); }
static inline void kbm_st32(uint32_t a, uint32_t v) { memcpy(KBM_GPTR(a), &v, sizeof v); }

#define KBM_FN_PL_INPUT_UPDATE 0x4a79ccu
#define KBM_FN_PL_INPUT_UPDATE_ALT 0x48d1f0u
#define KBM_FN_FACE_STICK 0x47f678u
#define KBM_FN_PL_IS_LOCAL 0x47bcd0u
#define KBM_FN_PL_STICK_REVERSED 0x4f5478u
#define KBM_FN_PL_BUSY 0x47b918u
#define KBM_FN_POUCH_ENTRY 0x487fecu
#define KBM_FN_ITEM_USABLE 0x394250u

#define KBM_PL_PHASE_WORD 0x04u
#define KBM_PL_YAW_ACTUAL 0xfecu
#define KBM_PL_IS_LOCAL_WORD 0x13ccu
#define KBM_PL_STICK_ANGLE 0x140cu
#define KBM_PL_STICK_MAG 0x1410u
#define KBM_PL_YAW_TARGET 0x14a0u
#define KBM_PL_ACT_ST 0x2506u
#define KBM_PL_ACT_NO 0x250au
#define KBM_PL_REVERSED_WORD 0x28b8u

#define KBM_VF_CAN_SELECT 0x230u
#define KBM_PL_ITEM_SLOT 0x3298u
#define KBM_PL_ITEM_FLAGS 0x253cu
#define KBM_ITEM_NO_ITEM 0x28u
#define KBM_POUCH_ITEM_SLOTS 24
#define KBM_GOT_SPAD 0x1a3931cu
#define KBM_GOT_MODE 0x1a38d28u
#define KBM_MODE_BYTE 0x50u
#define KBM_GOT_STATE 0x1a38fc0u
#define KBM_STATE_WORD 0x1e0u
#define KBM_SPAD_HELD 0x6b9cu
#define KBM_SPAD_LAST 0x6ba0u
#define KBM_SPAD_NEW 0x6ba4u

#define KBM_TILT_WALK_MIN 50
#define KBM_TILT_RUN_MIN 110
#define KBM_DEFAULT_MAG_WALK 80
#define KBM_DEFAULT_MAG_RUN 150

#define KBM_SIG_WORDS 3

typedef struct KbmGuestSig {
    uint32_t addr;
    const char *name;
    uint32_t words[KBM_SIG_WORDS];
} KbmGuestSig;

static const KbmGuestSig kbm_guest_sigs[] = {
    {KBM_FN_PL_INPUT_UPDATE, "player input update", {0xe92d4ff0u, 0xe24dd014u, 0xe1a04000u}},
    {KBM_FN_PL_INPUT_UPDATE_ALT, "player input update (safe areas)", {0xe92d4bf0u, 0xe1a04000u, 0xe59f037cu}},
    {KBM_FN_FACE_STICK, "face stick", {0xe92d4010u, 0xe1a04000u, 0xe5940000u}},
    {KBM_FN_PL_IS_LOCAL, "player is local", {0xe30113ccu, 0xe7900001u, 0xe2000001u}},
    {KBM_FN_PL_STICK_REVERSED, "stick reversed", {0xe30218b8u, 0xe7901001u, 0xe3a00000u}},
    {KBM_FN_PL_BUSY, "player busy", {0xe30113ccu, 0xe7d00001u, 0xe7e001d0u}},
    {KBM_FN_POUCH_ENTRY, "item pouch entry", {0xe92d4830u, 0xe1a04002u, 0xe1a05001u}},
    {KBM_FN_ITEM_USABLE, "item usable on the bar", {0xe92d48f0u, 0xe59f00f8u, 0xe1a07002u}},
};
#define KBM_GUEST_SIG_COUNT (sizeof kbm_guest_sigs / sizeof kbm_guest_sigs[0])
