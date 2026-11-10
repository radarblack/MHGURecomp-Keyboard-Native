#pragma once
#include <stdbool.h>
#include <stdint.h>

#define KBM_IK_KEYS 12
#define KBM_IK_RING 64

typedef enum KbmIkKind { KBM_IK_NONE = 0, KBM_IK_SELECT, KBM_IK_USE } KbmIkKind;

typedef struct KbmIkState {
    int last_key;
    uint32_t last_ms;
} KbmIkState;

typedef struct KbmIkEvent {
    uint8_t key;
    uint32_t t_ms;
} KbmIkEvent;

typedef struct KbmIkRing {
    KbmIkEvent ev[KBM_IK_RING];
    volatile uint32_t head;
    volatile uint32_t tail;
} KbmIkRing;

void kbm_ik_state_init(KbmIkState *s);
KbmIkKind kbm_ik_press(KbmIkState *s, int key, uint32_t now_ms, uint32_t window_ms);
void kbm_ik_ring_init(KbmIkRing *r);
bool kbm_ik_ring_push(KbmIkRing *r, int key, uint32_t t_ms);
bool kbm_ik_ring_pop(KbmIkRing *r, KbmIkEvent *out);
bool kbm_ik_parse_slot(const char *text, int *slot_index);
