// SPDX-License-Identifier: MIT
#define _GNU_SOURCE
#include "internal.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

const char *as_strerror(int err)
{
    switch (-err) {
    case ENODEV: return "device not found";
    case EACCES:
        return "permission denied: the hidraw node is root-owned, install the "
               "udev rules";
    case ETIMEDOUT: return "device did not answer (is it awake / plugged in?)";
    case EPROTO: return "unexpected reply from device";
    case ENODATA: return "device returned no data";
    case EREMOTEIO:
        return "the device refused the command: it answered with status 1 "
               "(understood but not supported)";
    case EIO: return "input/output error: the operation could not be completed";
    case EINVAL: return "invalid argument";
    case ENOTSUP:
        return "settings are unavailable over Bluetooth: the vendor settings "
               "channel exists only on the 2.4 GHz receiver or a USB cable";
    default: return strerror(-err);
    }
}

/* Vendor collections: usage page 0xFF02 on Compx mice and 0xFFFF/usage 2 on
 * Royuan HE keyboards. Bluetooth HID links carry neither. */
static const unsigned char mouse_vendor[] = { 0x06, 0x02, 0xff };
static const unsigned char keyboard_vendor[] = { 0x06, 0xff, 0xff, 0x09, 0x02 };

int as_hid_desc_has(const struct as_hid *dev, const void *needle, size_t len)
{
    if (!dev || dev->descriptor_len < len) {
        return 0;
    }
    return memmem(dev->descriptor, dev->descriptor_len, needle, len) != NULL;
}

int as_hid_is_bluetooth(const struct as_hid *dev)
{
    return dev && dev->bus == AS_BUS_BLUETOOTH;
}

int as_hid_is_mouse(const struct as_hid *dev)
{
    return dev && dev->vid == 0x3554;
}

int as_hid_is_keyboard(const struct as_hid *dev)
{
    return dev && dev->vid == 0x3151;
}

int as_hid_has_vendor(const struct as_hid *dev)
{
    return as_hid_desc_has(dev, mouse_vendor, sizeof(mouse_vendor)) ||
           as_hid_desc_has(dev, keyboard_vendor, sizeof(keyboard_vendor));
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
    return (int)AS_N(light_modes);
}

const char *as_light_mode_name(int mode)
{
    return mode >= 0 && (size_t)mode < AS_N(light_modes) ? light_modes[mode]
                                                        : "unknown";
}

int as_light_mode_by_name(const char *name)
{
    char *end;
    long v;

    if (!name) {
        return -1;
    }
    v = strtol(name, &end, 0);
    if (end != name && *end == 0) {
        return (int)v;
    }
    for (size_t i = 0; i < AS_N(light_modes); i++) {
        if (strcmp(name, light_modes[i]) == 0) {
            return (int)i;
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
    case 0xf533: return "R11 Ultra (Bluetooth)";
    case 0xf517:
    case 0xf515: return "X11 Ultra";
    default: return "Compx mouse";
    }
}
