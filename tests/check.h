#pragma once
#include <stdio.h>

static int g_checks;
static int g_failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        g_checks++;                                                          \
        if (!(cond)) {                                                       \
            g_failures++;                                                    \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
        }                                                                    \
    } while (0)

#define CHECK_EQ(a, b)                                                                                    \
    do {                                                                                                  \
        long long va_ = (long long)(a), vb_ = (long long)(b);                                             \
        g_checks++;                                                                                       \
        if (va_ != vb_) {                                                                                 \
            g_failures++;                                                                                 \
            fprintf(stderr, "FAIL %s:%d: %s == %s (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b, va_, vb_); \
        }                                                                                                 \
    } while (0)

#define CHECK_REPORT(name)                                                  \
    do {                                                                    \
        printf("%s: %d checks, %d failures\n", name, g_checks, g_failures); \
        return g_failures ? 1 : 0;                                          \
    } while (0)
