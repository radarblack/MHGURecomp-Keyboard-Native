#pragma once
#include <stdbool.h>
#include <stddef.h>

#include "config.h"
#include "movement.h"

#define KBM_BIND_KEYS 4
#define KBM_TOKEN_MAX 48

typedef enum KbmBindId { KBM_B_UP = 0, KBM_B_DOWN, KBM_B_LEFT, KBM_B_RIGHT, KBM_B_WALK, KBM_B_COUNT } KbmBindId;

typedef struct KbmKeyRef {
    int kind;
    int code;
} KbmKeyRef;

typedef struct KbmBind {
    KbmKeyRef refs[KBM_BIND_KEYS];
    int n;
} KbmBind;

typedef bool (*KbmSettingFn)(const char *key, char *out, size_t out_size);

int kbm_split_keys(const char *list, char out[][KBM_TOKEN_MAX], int max);
bool kbm_parse_mouse_name(const char *name, int *button);
bool kbm_keys_init(const KbmConfig *cfg, KbmSettingFn setting);
void kbm_keys_sample(KbmKeys *out);
const char *kbm_bind_name(KbmBindId id);
int kbm_bind_count(KbmBindId id);
