/* SPDX-License-Identifier: MIT
 *
 * Helpers shared by the library sources but not part of the public API.
 */
#ifndef AS_INTERNAL_H
#define AS_INTERNAL_H

#include "attackshark.h"

#define AS_N(a) (sizeof(a) / sizeof((a)[0]))

/* Nodes one scan keeps; a real machine has a handful of hidraw nodes. */
#define AS_HID_MAX 32

/* 1 when the report descriptor of dev contains needle. */
int as_hid_desc_has(const struct as_hid *dev, const void *needle, size_t len);

/* Every hidraw node on the system, in readdir order. */
int as_hid_scan(struct as_hid *out, int max);

/* First node for which match() returns non-zero. Returns 1 and fills out, or
 * 0 when nothing matches. */
int as_hid_find_first(int (*match)(const struct as_hid *), struct as_hid *out);

/* Finds a node matching match() (preferring vendor id vid) and opens it.
 * Returns 0, -ENODEV when no node matches, or a negative errno. */
int as_hid_open_first(int (*match)(const struct as_hid *), unsigned vid,
                      struct as_hid *out);

/* Keyboard and mouse helpers used across the library sources only. */
int as_keyboard_cmd_raw(struct as_keyboard *kb, uint8_t cmd, const uint8_t *args,
                        size_t args_len, int bit8, uint8_t *out,
                        int timeout_ms);
int as_keyboard_precision(struct as_keyboard *kb);
/* Master switch for the whole backlight ("low power mode" in the vendor
 * software): mode 0 only changes the effect, not the power state. */
int as_keyboard_get_light_power(struct as_keyboard *kb);
int as_keyboard_get_light_power_zones(struct as_keyboard *kb, int *main_on,
                                      int *side_on);
int as_keyboard_get_sidelight(struct as_keyboard *kb, struct as_kb_light *l);
int as_mouse_handshake(struct as_mouse *m);

#endif
