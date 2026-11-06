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

int main(void) {
    test_angles();
    test_tilt();
    test_resolve_basic();
    test_last_key_wins();
    test_neutral_and_ties();
    test_split_keys();
    test_parsers();
    test_config_file();
    CHECK_REPORT("test_core");
}
