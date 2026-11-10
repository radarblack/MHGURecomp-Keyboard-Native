#include "ikcore.h"

#include <ctype.h>
#include <stdlib.h>

void kbm_ik_state_init(KbmIkState *s) {
    s->last_key = -1;
    s->last_ms = 0;
}

KbmIkKind kbm_ik_press(KbmIkState *s, int key, uint32_t now_ms, uint32_t window_ms) {
    if (window_ms && s->last_key == key && (uint32_t)(now_ms - s->last_ms) <= window_ms) {
        s->last_key = -1;
        return KBM_IK_USE;
    }
    s->last_key = key;
    s->last_ms = now_ms;
    return KBM_IK_SELECT;
}

void kbm_ik_ring_init(KbmIkRing *r) {
    r->head = 0;
    r->tail = 0;
}

bool kbm_ik_ring_push(KbmIkRing *r, int key, uint32_t t_ms) {
    uint32_t head = __atomic_load_n(&r->head, __ATOMIC_RELAXED);
    uint32_t next = (head + 1) % KBM_IK_RING;
    if (next == __atomic_load_n(&r->tail, __ATOMIC_ACQUIRE)) return false;
    r->ev[head].key = (uint8_t)key;
    r->ev[head].t_ms = t_ms;
    __atomic_store_n(&r->head, next, __ATOMIC_RELEASE);
    return true;
}

bool kbm_ik_ring_pop(KbmIkRing *r, KbmIkEvent *out) {
    uint32_t tail = __atomic_load_n(&r->tail, __ATOMIC_RELAXED);
    if (tail == __atomic_load_n(&r->head, __ATOMIC_ACQUIRE)) return false;
    *out = r->ev[tail];
    __atomic_store_n(&r->tail, (tail + 1) % KBM_IK_RING, __ATOMIC_RELEASE);
    return true;
}

bool kbm_ik_parse_slot(const char *text, int *slot_index) {
    while (*text && isspace((unsigned char)*text)) text++;
    char *end = NULL;
    long v = strtol(text, &end, 10);
    if (end == text || v < 1 || v > 24) return false;
    while (*end && isspace((unsigned char)*end)) end++;
    if (*end) return false;
    *slot_index = (int)v - 1;
    return true;
}
