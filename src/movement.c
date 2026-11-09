#include "movement.h"

#include <string.h>

#include "guest.h"

void kbm_tracker_init(KbmTracker *t) { memset(t, 0, sizeof *t); }

uint16_t kbm_angle_for(int x, int y) {
    if (x > 0 && y == 0) return 0x0000;
    if (x > 0 && y > 0) return 0x2000;
    if (x == 0 && y > 0) return 0x4000;
    if (x < 0 && y > 0) return 0x6000;
    if (x < 0 && y == 0) return 0x8000;
    if (x < 0 && y < 0) return 0xa000;
    if (x == 0 && y < 0) return 0xc000;
    if (x > 0 && y < 0) return 0xe000;
    return 0;
}

static int resolve_axis(bool neg, bool pos, uint32_t stamp_neg, uint32_t stamp_pos, KbmSocd socd, bool *contested) {
    if (neg && pos) {
        *contested = true;
        if (socd == KBM_SOCD_NEUTRAL || stamp_neg == stamp_pos) return 0;
        return stamp_pos > stamp_neg ? 1 : -1;
    }
    if (pos) return 1;
    if (neg) return -1;
    return 0;
}

KbmMove kbm_resolve(KbmTracker *t, const KbmKeys *keys, KbmSocd socd) {
    const bool now[4] = {keys->up, keys->down, keys->left, keys->right};
    bool any_new = false;
    for (int i = 0; i < 4; i++)
        if (now[i] && !t->down[i]) any_new = true;
    if (any_new) t->seq++;
    for (int i = 0; i < 4; i++) {
        if (now[i] && !t->down[i]) t->stamp[i] = t->seq;
        t->down[i] = now[i];
    }

    KbmMove m;
    memset(&m, 0, sizeof m);
    bool contested = false;
    m.y = resolve_axis(keys->down, keys->up, t->stamp[1], t->stamp[0], socd, &contested);
    m.x = resolve_axis(keys->left, keys->right, t->stamp[2], t->stamp[3], socd, &contested);
    m.contested = contested;
    m.active = m.x != 0 || m.y != 0;
    m.walk = keys->walk;
    if (m.active) m.angle = kbm_angle_for(m.x, m.y);
    return m;
}

int kbm_tilt_category(int magnitude) {
    if (magnitude < KBM_TILT_WALK_MIN) return 0;
    if (magnitude < KBM_TILT_RUN_MIN) return 1;
    return 2;
}

uint16_t kbm_angle_delta(uint16_t a, uint16_t b) {
    uint16_t d = (uint16_t)(a - b);
    return d > 0x8000 ? (uint16_t)(0x10000 - d) : d;
}

bool kbm_angle_reversal(uint16_t from, uint16_t to) { return kbm_angle_delta(from, to) >= 0x5000; }

uint16_t kbm_degrees_to_units(double degrees) {
    double u = degrees * 65536.0 / 360.0 + 0.5;
    if (u < 0.0) return 0;
    if (u > 65535.0) return 65535;
    return (uint16_t)u;
}

uint16_t kbm_turn_step(uint16_t actual, uint16_t target, const KbmTurn *turn) {
    int32_t d = (int16_t)(uint16_t)(target - actual);
    uint32_t ad = (uint32_t)(d < 0 ? -d : d);
    if (ad == 0) return target;
    uint32_t step = (uint32_t)(((uint64_t)ad * turn->response_q16) >> 16);
    if (step < turn->min_step) step = turn->min_step;
    if (step < 1) step = 1;
    if (turn->max_step && step > turn->max_step) step = turn->max_step;
    if (step >= ad) return target;
    return (uint16_t)(d > 0 ? actual + step : actual - step);
}
