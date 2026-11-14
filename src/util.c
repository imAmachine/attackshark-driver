// SPDX-License-Identifier: MIT
#include "attackshark.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

const char *as_strerror(int err)
{
    switch (-err) {
    case ENODEV: return "device not found";
    case EACCES: return "permission denied (install the udev rules)";
    case ETIMEDOUT: return "device did not answer (is it awake / plugged in?)";
    case EPROTO: return "unexpected reply from device";
    case ENODATA: return "device returned no data";
    case EINVAL: return "invalid argument";
    default: return strerror(-err);
    }
}

/* Shared by the CLI and the GUI: the index is the same value the vendor
 * software stores in the light-mode byte. */
static const char *const light_modes[] = {
    "off", "always-on", "breath", "neon", "wave", "ripple", "raindrop",
    "snake", "press-action", "coverage", "sine-wave", "kaleidoscope",
    "line-wave", "user-picture", "laser", "circle-wave", "dazzling",
    "rain-down", "meteor", "press-action-off", "music-follow-3",
    "screen-color", "music-follow-2", "train", "fireworks", "user-color",
};

int as_light_mode_count(void)
{
    return (int)(sizeof(light_modes) / sizeof(light_modes[0]));
}

const char *as_light_mode_name(int mode)
{
    if (mode >= 0 && (size_t)mode < sizeof(light_modes) / sizeof(light_modes[0])) {
        return light_modes[mode];
    }
    return "unknown";
}

int as_light_mode_by_name(const char *name)
{
    char *end;
    long v;
    int n = as_light_mode_count();

    if (!name) {
        return -1;
    }
    v = strtol(name, &end, 0);
    if (end != name && *end == 0) {
        return (int)v;
    }
    for (int i = 0; i < n; i++) {
        if (strcmp(name, light_modes[i]) == 0) {
            return i;
        }
    }
    return -1;
}

const char *as_mouse_model_name(unsigned pid)
{
    switch (pid) {
    case 0xfb43:
    case 0xfb44: return "R11 Ultra (2.4 GHz)";
    case 0xf5f6: return "R11 Ultra (USB)";
    case 0xf517:
    case 0xf515: return "X11 Ultra";
    default: return "Compx mouse";
    }
}
