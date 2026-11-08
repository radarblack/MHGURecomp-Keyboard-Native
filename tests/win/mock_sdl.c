#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define EXPORT __declspec(dllexport)

static bool g_keys[512];
static bool g_text_active;

EXPORT const bool *SDL_GetKeyboardState(int *n) {
    if (n) *n = 512;
    return g_keys;
}

EXPORT int SDL_GetScancodeFromName(const char *name) {
    static const struct {
        const char *n;
        int c;
    } t[] = {{"W", 26}, {"S", 22}, {"A", 4}, {"D", 7}, {"Left Shift", 225}};
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++)
        if (!_stricmp(name, t[i].n)) return t[i].c;
    return 0;
}

EXPORT uint32_t SDL_GetMouseState(float *x, float *y) {
    (void)x;
    (void)y;
    return 0;
}

EXPORT void *SDL_GetKeyboardFocus(void) { return (void *)(uintptr_t)1; }
EXPORT bool SDL_TextInputActive(void *window) {
    (void)window;
    return g_text_active;
}

EXPORT void mock_set_key(int scancode, int down) { g_keys[scancode] = down != 0; }
EXPORT void mock_set_text(int on) { g_text_active = on != 0; }
