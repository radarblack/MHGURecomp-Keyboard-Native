#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MHGU_ENGINE_MODULE_API 1

#ifdef __cplusplus
#define MHGU_ENGINE_MODULE_LINK extern "C"
#else
#define MHGU_ENGINE_MODULE_LINK
#endif
#ifdef _WIN32
#define MHGU_ENGINE_MODULE_EXPORT MHGU_ENGINE_MODULE_LINK __declspec(dllexport)
#else
#define MHGU_ENGINE_MODULE_EXPORT MHGU_ENGINE_MODULE_LINK __attribute__((visibility("default")))
#endif

typedef struct MhguHost {
    uint32_t api;
    void (*log)(const char *fmt, ...);
    bool (*setting)(const char *key, char *out, size_t out_size);
    const char *module_dir;
    const char *port_version;
} MhguHost;

typedef struct MhguEngineModule {
    const char *name;
    void (*shutdown)(void);
} MhguEngineModule;

typedef bool (*MhguEngineModuleEntry)(const MhguHost *host, MhguEngineModule *out);
#define MHGU_ENGINE_MODULE_API_NAME "mhgu_engine_module_api"
#define MHGU_ENGINE_MODULE_ENTRY_NAME "mhgu_engine_module_entry"
