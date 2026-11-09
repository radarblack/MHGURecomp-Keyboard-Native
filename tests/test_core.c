#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "config.h"
#include "keys.h"
#include "log.h"
#include "movement.h"

static KbmKeys keys(bool up, bool down, bool left, bool right, bool walk) {
    KbmKeys k = {up, down, left, right, walk};
    return k;
}

static void test_angles(void) {
    CHECK_EQ(kbm_angle_for(1, 0), 0x0000);
    CHECK_EQ(kbm_angle_for(1, 1), 0x2000);
    CHECK_EQ(kbm_angle_for(0, 1), 0x4000);
    CHECK_EQ(kbm_angle_for(-1, 1), 0x6000);
    CHECK_EQ(kbm_angle_for(-1, 0), 0x8000);
    CHECK_EQ(kbm_angle_for(-1, -1), 0xa000);
    CHECK_EQ(kbm_angle_for(0, -1), 0xc000);
    CHECK_EQ(kbm_angle_for(1, -1), 0xe000);
    CHECK_EQ(kbm_angle_delta(0x4000, 0xc000), 0x8000);
    CHECK_EQ(kbm_angle_delta(0x0000, 0xe000), 0x2000);
    CHECK(kbm_angle_reversal(0x4000, 0xc000));
    CHECK(!kbm_angle_reversal(0x4000, 0x6000));
    CHECK(kbm_angle_reversal(0x4000, 0xa000));
}

static void test_tilt(void) {
    CHECK_EQ(kbm_tilt_category(0), 0);
    CHECK_EQ(kbm_tilt_category(49), 0);
    CHECK_EQ(kbm_tilt_category(50), 1);
    CHECK_EQ(kbm_tilt_category(109), 1);
    CHECK_EQ(kbm_tilt_category(110), 2);
    CHECK_EQ(kbm_tilt_category(255), 2);
}

static void test_resolve_basic(void) {
    KbmTracker t;
    kbm_tracker_init(&t);
    KbmKeys k = keys(false, false, false, false, false);
    CHECK(!kbm_resolve(&t, &k, KBM_SOCD_LAST).active);
    k = keys(true, false, false, false, false);
    KbmMove m = kbm_resolve(&t, &k, KBM_SOCD_LAST);
    CHECK(m.active);
    CHECK_EQ(m.angle, 0x4000);
    CHECK(!m.walk);
    CHECK(!m.contested);
    k = keys(true, false, false, true, true);
    m = kbm_resolve(&t, &k, KBM_SOCD_LAST);
    CHECK_EQ(m.angle, 0x2000);
    CHECK(m.walk);
}

static void test_last_key_wins(void) {
    KbmTracker t;
    kbm_tracker_init(&t);
    KbmKeys k = keys(true, false, false, false, false);
    CHECK_EQ(kbm_resolve(&t, &k, KBM_SOCD_LAST).angle, 0x4000);
    k = keys(true, true, false, false, false);
    KbmMove m = kbm_resolve(&t, &k, KBM_SOCD_LAST);
    CHECK(m.active);
    CHECK(m.contested);
    CHECK_EQ(m.angle, 0xc000);
    k = keys(false, true, false, false, false);
    CHECK_EQ(kbm_resolve(&t, &k, KBM_SOCD_LAST).angle, 0xc000);
    k = keys(true, true, false, false, false);
    m = kbm_resolve(&t, &k, KBM_SOCD_LAST);
    CHECK_EQ(m.angle, 0x4000);
}

static void test_neutral_and_ties(void) {
    KbmTracker t;
    kbm_tracker_init(&t);
    KbmKeys k = keys(true, true, false, false, false);
    KbmMove m = kbm_resolve(&t, &k, KBM_SOCD_LAST);
    CHECK(!m.active);
    CHECK(m.contested);

    kbm_tracker_init(&t);
    k = keys(true, false, false, false, false);
    kbm_resolve(&t, &k, KBM_SOCD_NEUTRAL);
    k = keys(true, true, false, false, false);
    m = kbm_resolve(&t, &k, KBM_SOCD_NEUTRAL);
    CHECK(!m.active);

    kbm_tracker_init(&t);
    k = keys(false, false, true, false, false);
    kbm_resolve(&t, &k, KBM_SOCD_LAST);
    k = keys(true, false, true, true, false);
    m = kbm_resolve(&t, &k, KBM_SOCD_LAST);
    CHECK_EQ(m.x, 1);
    CHECK_EQ(m.angle, 0x2000);
}

static void test_split_keys(void) {
    char t[6][KBM_TOKEN_MAX];
    CHECK_EQ(kbm_split_keys("W", t, 6), 1);
    CHECK(!strcmp(t[0], "W"));
    CHECK_EQ(kbm_split_keys("Left Shift, Mouse Right ,Up", t, 6), 3);
    CHECK(!strcmp(t[0], "Left Shift"));
    CHECK(!strcmp(t[1], "Mouse Right"));
    CHECK(!strcmp(t[2], "Up"));
    CHECK_EQ(kbm_split_keys("", t, 6), 0);
    CHECK_EQ(kbm_split_keys(" , ,", t, 6), 0);
    CHECK_EQ(kbm_split_keys("A,B,C", t, 2), 2);
    int b = 0;
    CHECK(kbm_parse_mouse_name("Mouse Left", &b) && b == 1);
    CHECK(kbm_parse_mouse_name("mouse 5", &b) && b == 5);
    CHECK(!kbm_parse_mouse_name("Wheel Up", &b));
}

static void test_parsers(void) {
    bool b = false;
    CHECK(kbm_parse_bool("1", &b) && b);
    CHECK(kbm_parse_bool("No", &b) && !b);
    CHECK(kbm_parse_bool(" true ", &b) && b);
    CHECK(!kbm_parse_bool("maybe", &b));
    KbmLogLevel lv;
    CHECK(kbm_log_level_parse("TRACE", &lv) && lv == KBM_LOG_TRACE);
    CHECK(kbm_log_level_parse("warning", &lv) && lv == KBM_LOG_WARN);
    CHECK(!kbm_log_level_parse("loud", &lv));
    KbmActId acts[4];
    CHECK_EQ(kbm_parse_actlist("0001:085, 1000:1001", acts, 4), 2);
    CHECK_EQ(acts[0].st, 0x0001);
    CHECK_EQ(acts[0].no, 85);
    CHECK_EQ(acts[1].st, 0x1000);
    CHECK_EQ(acts[1].no, 1001);
    CHECK_EQ(kbm_parse_actlist("", acts, 4), 0);
    CHECK_EQ(kbm_parse_actlist("zz", acts, 4), -1);
    CHECK(kbm_act_listed(acts, 2, 0x1000, 1001));
    CHECK(!kbm_act_listed(acts, 2, 0x1000, 1002));
}

static void test_config_file(void) {
    char path[] = "/tmp/kbm_cfg_XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    FILE *f = fdopen(fd, "wb");
    fputs("[General]\nEnabled = 0\nLogLevel = trace\nLogFile=0\nStatsIntervalSeconds = 3\nStrictVersion=1\n"
          "[Movement]\nMode = passthrough\nOppositeKeys = neutral\nSnapFacing = 0\nSnapScope = all\n"
          "SnapAllow = 0001:085\nSnapDeny = 1000:1001, 1000:1002\n"
          "[Keys]\nKeysFromPort = 0\nUp = Up\nWalk = Left Ctrl, Mouse 4\nBogusKey = 1\nMode2 bad line\n",
        f);
    fclose(f);
    KbmConfig c;
    kbm_config_defaults(&c, KBM_LOG_INFO);
    CHECK(c.enabled && c.mode == KBM_MODE_DIRECT && c.socd == KBM_SOCD_LAST && c.snap_facing);
    CHECK(kbm_config_load(&c, path));
    CHECK(!c.enabled);
    CHECK(c.log_level == KBM_LOG_TRACE);
    CHECK(!c.log_file);
    CHECK_EQ(c.stats_interval_s, 3);
    CHECK(c.strict_version);
    CHECK(c.mode == KBM_MODE_PASSTHROUGH);
    CHECK(c.socd == KBM_SOCD_NEUTRAL);
    CHECK(!c.snap_facing);
    CHECK(c.snap_scope == KBM_SCOPE_ALL);
    CHECK_EQ(c.n_allow, 1);
    CHECK_EQ(c.n_deny, 2);
    CHECK(!c.keys_from_port);
    CHECK(!strcmp(c.key_text[0], "Up"));
    CHECK(!strcmp(c.key_text[4], "Left Ctrl, Mouse 4"));
    remove(path);
    CHECK(!kbm_config_load(&c, "/tmp/kbm_does_not_exist.ini"));
}

static void test_turn_step(void) {
    KbmTurn t = {32768u, kbm_degrees_to_units(10.0), kbm_degrees_to_units(50.0)};
    CHECK_EQ(kbm_degrees_to_units(180.0), 0x8000);
    CHECK_EQ(kbm_degrees_to_units(0.0), 0);
    CHECK_EQ(kbm_degrees_to_units(90.0), 0x4000);
    CHECK_EQ(kbm_turn_step(0x1234, 0x1234, &t), 0x1234);
    CHECK_EQ(kbm_turn_step(0x0100, 0x0100 + 100, &t), 0x0100 + 100);
    uint16_t first = kbm_turn_step(0, 0x8000, &t);
    CHECK_EQ(kbm_angle_delta(first, 0), t.max_step);
    CHECK(first != 0x8000);
    KbmTurn snap = {65536u, 0, 0};
    CHECK_EQ(kbm_turn_step(0x1000, 0x9000, &snap), 0x9000);

    const uint16_t starts[] = {0x0000, 0x4000, 0x8000, 0xc000, 0xff00, 0x0123, 0x7fff, 0x8001};
    const uint16_t targets[] = {0x0000, 0x2000, 0x8000, 0xe000, 0x0100, 0xabcd};
    for (size_t i = 0; i < sizeof starts / sizeof starts[0]; i++) {
        for (size_t j = 0; j < sizeof targets / sizeof targets[0]; j++) {
            uint16_t a = starts[i];
            uint16_t prev = kbm_angle_delta(a, targets[j]);
            int steps = 0;
            while (a != targets[j] && steps < 40) {
                a = kbm_turn_step(a, targets[j], &t);
                uint16_t now = kbm_angle_delta(a, targets[j]);
                CHECK(now <= prev);
                prev = now;
                steps++;
            }
            CHECK_EQ(a, targets[j]);
            CHECK(steps <= 8);
        }
    }
    KbmTurn slow = {kbm_degrees_to_units(0) + 6554u, 0, 0};
    uint16_t a = 0;
    int n = 0;
    while (a != 0x4000 && n < 200) {
        a = kbm_turn_step(a, 0x4000, &slow);
        n++;
    }
    CHECK_EQ(a, 0x4000);
}

static void test_turn_config(void) {
    char path[] = "/tmp/kbm_cfg_XXXXXX";
    int fd = mkstemp(path);
    FILE *f = fdopen(fd, "wb");
    fputs("[Movement]\nTurnMode = snap\nTurnResponse = 0.8\nTurnMinStepDegrees = 20\nTurnMaxStepDegrees = 15\nSafeAreaHook = 0\n", f);
    fclose(f);
    KbmConfig c;
    kbm_config_defaults(&c, KBM_LOG_INFO);
    CHECK(c.turn_mode == KBM_TURN_SMOOTH);
    CHECK(c.safe_area_hook);
    CHECK_EQ(c.turn.min_step, kbm_degrees_to_units(12.0));
    CHECK_EQ(c.turn.max_step, kbm_degrees_to_units(60.0));
    CHECK_EQ(c.turn.response_q16, (long long)(0.6 * 65536.0));
    CHECK(kbm_config_load(&c, path));
    CHECK(c.turn_mode == KBM_TURN_SNAP);
    CHECK(!c.safe_area_hook);
    CHECK_EQ(c.turn.response_q16, (long long)(0.8 * 65536.0));
    CHECK_EQ(c.turn.min_step, kbm_degrees_to_units(20.0));
    CHECK_EQ(c.turn.max_step, kbm_degrees_to_units(20.0));
    remove(path);

    strcpy(path, "/tmp/kbm_cfg_XXXXXX");
    fd = mkstemp(path);
    f = fdopen(fd, "wb");
    fputs("TurnResponse = 5\nTurnMode = wobble\nTurnMaxStepDegrees = 0\n", f);
    fclose(f);
    kbm_config_defaults(&c, KBM_LOG_INFO);
    CHECK(kbm_config_load(&c, path));
    CHECK_EQ(c.turn.response_q16, (long long)(0.6 * 65536.0));
    CHECK(c.turn_mode == KBM_TURN_SMOOTH);
    CHECK_EQ(c.turn.max_step, 0);
    remove(path);
}

int main(void) {
    test_angles();
    test_tilt();
    test_resolve_basic();
    test_last_key_wins();
    test_neutral_and_ties();
    test_split_keys();
    test_parsers();
    test_config_file();
    test_turn_step();
    test_turn_config();
    CHECK_REPORT("test_core");
}
