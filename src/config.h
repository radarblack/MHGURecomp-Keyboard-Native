#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "log.h"
#include "turn.h"

#define KBM_MAX_ACTS 32
#define KBM_KEY_TEXT 96

typedef enum KbmMode { KBM_MODE_DIRECT = 0, KBM_MODE_PASSTHROUGH } KbmMode;
typedef enum KbmSocd { KBM_SOCD_LAST = 0, KBM_SOCD_NEUTRAL } KbmSocd;
typedef enum KbmTurnMode { KBM_TURN_SMOOTH = 0, KBM_TURN_SNAP, KBM_TURN_GAME } KbmTurnMode;
typedef enum KbmSnapScope { KBM_SCOPE_UPDATE = 0, KBM_SCOPE_ALL } KbmSnapScope;

typedef struct KbmActId {
    uint16_t st;
    uint16_t no;
} KbmActId;

typedef struct KbmConfig {
    bool enabled;
    KbmLogLevel log_level;
    bool log_file;
    uint32_t stats_interval_s;
    uint32_t hook_wait_s;
    bool strict_version;
    bool require_gates;
    KbmMode mode;
    KbmSocd socd;
    bool snap_facing;
    KbmTurnMode turn_mode;
    double turn_response;
    double turn_min_deg;
    double turn_max_deg;
    KbmTurn turn;
    bool safe_area_hook;
    KbmSnapScope snap_scope;
    KbmActId snap_allow[KBM_MAX_ACTS];
    int n_allow;
    KbmActId snap_deny[KBM_MAX_ACTS];
    int n_deny;
    bool keys_from_port;
    char key_text[5][KBM_KEY_TEXT];
} KbmConfig;

extern KbmConfig g_cfg;

void kbm_config_defaults(KbmConfig *cfg, KbmLogLevel default_level);
bool kbm_config_load(KbmConfig *cfg, const char *path);
bool kbm_parse_bool(const char *text, bool *out);
int kbm_parse_actlist(const char *text, KbmActId *out, int max);
bool kbm_act_listed(const KbmActId *list, int n, uint16_t st, uint16_t no);
void kbm_config_finalize(KbmConfig *cfg);
void kbm_config_log(const KbmConfig *cfg);
